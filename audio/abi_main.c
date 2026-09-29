/* Audio core firmware: the N64's audio RSP task and its AI, for SM64.
 *
 *   - Tasks: the game CPU hands over an ABI command list (built by
 *     src/audio/synthesis.c) through the mailbox; abi.c executes it. The PCM
 *     it produces lands in SDRAM (aSaveBuffer), like the RSP's did.
 *   - AI: the game queues finished PCM buffers (osAiSetNextBuffer); this
 *     plays them into the APF audio FIFO, resampled from the AI rate (32 kHz
 *     for SM64 US) to the Pocket's fixed 48 kHz, by linear interpolation.
 *
 * Protocol: abi_mailbox.h (AMB_*). The FIFO is kept shallow
 * (FIFO_TARGET) and counted in AI_REMAIN, so the audio engine's own buffer
 * sizing -- which reads osAiGetLength() -- sees what is really queued; it is
 * topped up between commands, so a long task cannot starve it.
 */
#include <stdint.h>
#include "audio_hw.h"
#include "abi_mailbox.h"
#include "abi.h"
#include "generated_csr_addrs.h"

#define FIFO_TARGET 512u            /* 48 kHz samples kept in the APF FIFO (~10.7 ms) */
#ifdef ABI_PROFILE
extern uint32_t abi_ai_diag[12];    /* AI health counters, see ai_service() */
#endif

static inline void     w32(uint32_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint32_t r32(uint32_t a)             { return *(volatile uint32_t *)a; }
static inline uint32_t mbox(int i)                 { return r32(AUDIO_MBOX(i)); }
static inline void     set_mbox(int i, uint32_t v) { w32(AUDIO_MBOX(i), v); }
static inline uint32_t cycles(void)                { return r32(AUDIO_CYCLES); }

/* ---- AI ------------------------------------------------------------------ */
static uint32_t s_cur_addr, s_cur_left;     /* current buffer: next frame, bytes left */
static uint32_t s_freq = 32000, s_step;     /* source frames per output sample, Q16 */
static uint32_t s_phase;                    /* Q16 position between s_prev and s_next */
static int32_t  s_prev_l, s_prev_r, s_next_l, s_next_r;
static int      s_have_frame;

/* (freq << 16) / 48000 by restoring division: this core has no divider,
 * and freq << 16 fits in 32 bits (freq < 65536). */
static uint32_t step_for(uint32_t freq)
{
    uint32_t num = freq << 16, q = 0, r = 0;
    for (int b = 31; b >= 0; b--) {
        r = (r << 1) | ((num >> b) & 1u);
        if (r >= 48000u) { r -= 48000u; q |= 1u << b; }
    }
    return q;
}

/* Next source frame (L, R) from the current buffer, switching to the queued
 * one when it runs out. 0 when the AI has nothing left. */
static int fetch_frame(void)
{
    if (s_cur_left < 4) {
        uint32_t next = mbox(AMB_AI_NEXT);
        if (!next) { s_cur_left = 0; return 0; }
        s_cur_left = mbox(AMB_AI_LEN) & ~3u;
        s_cur_addr = next;
        set_mbox(AMB_AI_NEXT, 0);
        if (s_cur_left < 4) return 0;
    }
    uint32_t w = *(volatile uint32_t *)s_cur_addr;      /* [15:0] left, [31:16] right */
#ifdef ABI_PROFILE
    abi_ai_diag[9]++;
#endif
    s_cur_addr += 4;
    s_cur_left -= 4;
    s_prev_l = s_next_l; s_prev_r = s_next_r;
    s_next_l = (int16_t)w; s_next_r = (int16_t)(w >> 16);
    return 1;
}

#ifdef ABI_PROFILE
/* AI health, read out of DMEM by the game (hal/audio_hal.c): [0] calls,
 * [1] entries with the APF FIFO nearly empty (< 64) during a task, [2] same
 * while idle, [3] times the AI ran out of source frames, [4] entries that
 * found it still out with the FIFO nearly empty (= an underrun because the
 * engine had queued nothing), [5] longest gap
 * between calls (cycles), [6] gaps longer than 5 ms, [7] lowest fill seen.
 * [8] samples pushed into the FIFO, [9] source frames consumed, [10] the
 * FIFO fill and [11] the cycle counter as of the last call: the game turns
 * them into rates (FIFO drain = pushes - fill change, per cycle).
 * The game zeroes [5] and sets [7] to 0xFFFFFFFF when it reports. */
uint32_t abi_ai_diag[12] = {0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFu, 0, 0, 0, 0};
static uint32_t s_diag_last, s_in_task;
#define AI_DIAG_GAP (CONFIG_CLOCK_FREQUENCY / 200u)
#endif
static void ai_service(void)
{
    uint32_t f = mbox(AMB_AI_FREQ);
    if (f == 0) f = 32000;
    if (f != s_freq) { s_freq = f; s_step = step_for(f); }

    uint32_t fill = r32(CSR_APF_AUDIO_BUFFER_FILL_ADDR);
#ifdef ABI_PROFILE
    {
        uint32_t now = cycles(), gap = now - s_diag_last;
        s_diag_last = now;
        abi_ai_diag[0]++;
        if (fill < 64u) abi_ai_diag[s_in_task ? 1 : 2]++;
        if (gap > abi_ai_diag[5]) abi_ai_diag[5] = gap;
        if (gap > AI_DIAG_GAP) abi_ai_diag[6]++;
        if (fill < abi_ai_diag[7]) abi_ai_diag[7] = fill;
        abi_ai_diag[10] = fill;
        abi_ai_diag[11] = now;
    }
#endif
    while (fill < FIFO_TARGET) {
        if (!s_have_frame) {                    /* prime two frames */
            if (!fetch_frame() || !fetch_frame()) {
#ifdef ABI_PROFILE
                if (fill < 64u) abi_ai_diag[4]++;
#endif
                break;
            }
            s_have_frame = 1;
            s_phase = 0;
        }
        s_phase += s_step;
        int starved = 0;
        while (s_phase >= 0x10000u) {
            s_phase -= 0x10000u;
            if (!fetch_frame()) { starved = 1; break; }
        }
        if (starved) {
            s_have_frame = 0;
#ifdef ABI_PROFILE
            abi_ai_diag[3]++;
#endif
            break;
        }
        /* Q15 phase: a 17-bit difference times a 16-bit phase would overflow */
        int32_t ph = (int32_t)(s_phase >> 1);
        int32_t l = s_prev_l + (((s_next_l - s_prev_l) * ph) >> 15);
        int32_t r = s_prev_r + (((s_next_r - s_prev_r) * ph) >> 15);
        w32(CSR_APF_AUDIO_OUT_ADDR, ((uint32_t)(uint16_t)l << 16) | (uint16_t)r);
        fill++;
#ifdef ABI_PROFILE
        abi_ai_diag[8]++;
#endif
    }
    /* what the AI still holds, in AI-rate bytes: the current buffer plus the
     * FIFO's content converted back to the source rate */
    set_mbox(AMB_AI_REMAIN, s_cur_left + (((fill * s_step) >> 16) << 2));
}

/* ---- tasks --------------------------------------------------------------- */
#ifdef ABI_PROFILE
uint32_t abi_prof_cycles[16], abi_prof_calls[16];   /* read out by sim/audio tb (PROF=1) */
#endif
static void run_task(uint32_t list, uint32_t n)
{
    uint32_t t0 = cycles();
    abi_op_count[0] = 0;
    for (uint32_t i = 0; i < n; i++, list += 8) {
        uint32_t w0 = r32(list), w1 = r32(list + 4);
#ifdef ABI_PROFILE
        uint32_t c0 = cycles();
        abi_exec(w0, w1);
        abi_prof_cycles[(w0 >> 24) & 15] += cycles() - c0;
        abi_prof_calls[(w0 >> 24) & 15]++;
#else
        abi_exec(w0, w1);
#endif
        if ((i & 7) == 7) ai_service();
    }
#ifdef ABI_STATE_FLUSH_EACH_TASK
    abi_state_flush();   /* test builds: DRAM as the RSP would leave it */
#endif
    set_mbox(AMB_STATS, cycles() - t0);
}

int main(void)
{
    abi_reset();
    s_step = step_for(s_freq);
    set_mbox(AMB_AI_REMAIN, 0);
    w32(CSR_APF_AUDIO_BUFFER_FLUSH_ADDR, 1);
    w32(CSR_APF_AUDIO_PLAYBACK_EN_ADDR, 1);
    set_mbox(AMB_STATE, AUDIO_ST_ABI);
    for (;;) {
        ai_service();
        uint32_t list = mbox(AMB_TASK);
        if (list) {
            uint32_t n = mbox(AMB_TASK_N);
            set_mbox(AMB_STATE, AUDIO_ST_ABI_BUSY);
            set_mbox(AMB_TASK, 0);          /* taken: the game may queue the next one */
#ifdef ABI_PROFILE
            s_in_task = 1;
            run_task(list, n);
            s_in_task = 0;
#else
            run_task(list, n);
#endif
            set_mbox(AMB_STATE, AUDIO_ST_ABI);
        }
    }
}
