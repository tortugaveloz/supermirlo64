/* Audio for the SM64 port: the sound thread's per-vblank work, the RSP audio
 * task and the AI.
 *
 * On the N64, thread4_sound wakes every vblank, runs the sequence player and
 * builds an ABI command list (create_next_audio_frame_task), hands it to the
 * RSP, and queues the PCM made two frames earlier to the AI
 * (osAiSetNextBuffer). It pre-empts the game thread to do so.
 *
 * Here:
 *   - audio_hal_tick() is that per-vblank body. On the Pocket it runs from a
 *     60 Hz machine-timer interrupt (audio_hal_start), which pre-empts the
 *     game exactly as the N64's thread did; the host sim calls it twice per
 *     game frame (display_and_vsync waits two vblanks).
 *   - The RSP task and the AI are Mirlo's audio core, running
 *     audio/abi_main.c, reached through its mailbox (audio/abi_mailbox.h).
 *     The host sim runs the same interpreter (audio/abi.c) in place and
 *     writes the AI's output to a WAV.
 */
#include <ultra64.h>
#include <stdint.h>
#include <string.h>

#include "types.h"
#include "game/main.h"
#include "audio/external.h"

#include "audio_hw.h"
#include "abi_mailbox.h"
#include "audio_hal.h"

struct audio_hal_stats g_audio_stats;

#ifdef SIM_AUDIO_HOST
/* ======================================================= host sim ======== */
#include <stdio.h>
#include <stdlib.h>
#include "abi.h"

static FILE    *s_wav;
static uint32_t s_wav_frames;
static uint32_t s_freq = 32000;
static uint32_t s_q_left[2];       /* bytes left: [0] current, [1] next */
static int      s_q_n;
static uint32_t s_tick_frac;       /* sub-byte remainder of the per-tick drain */

static void wav_header(uint32_t frames)
{
    uint32_t data = frames * 4, rate = s_freq;
    uint8_t h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0 };
    #define PUT32(o, v) do { h[o] = (uint8_t)(v); h[o+1] = (uint8_t)((v) >> 8); h[o+2] = (uint8_t)((v) >> 16); h[o+3] = (uint8_t)((v) >> 24); } while (0)
    PUT32(4, 36 + data);
    PUT32(24, rate);
    PUT32(28, rate * 4);
    h[32] = 4; h[34] = 16;
    memcpy(h + 36, "data", 4);
    PUT32(40, data);
    fseek(s_wav, 0, SEEK_SET);
    fwrite(h, 1, 44, s_wav);
    fseek(s_wav, 0, SEEK_END);
}
static void wav_close(void)
{
    if (!s_wav) return;
    wav_header(s_wav_frames);
    fclose(s_wav);
    fprintf(stderr, "[audio] %s: %u frames at %u Hz (%.2f s), %u ticks, %u tasks, %u AI buffers, %u dropped\n",
            getenv("SIM_AUDIO_WAV"), s_wav_frames, s_freq, s_wav_frames / (double)s_freq,
            g_audio_stats.ticks, g_audio_stats.tasks, g_audio_stats.ai_buffers, g_audio_stats.ai_dropped);
}

s32 osAiSetFrequency(u32 frequency) { s_freq = frequency; return (s32)frequency; }

s32 osAiSetNextBuffer(void *buf, u32 size)
{
    if (s_q_n >= 2) { g_audio_stats.ai_dropped++; return -1; }
    s_q_left[s_q_n++] = size;
    g_audio_stats.ai_buffers++;
    if (s_wav) { fwrite(buf, 1, size & ~3u, s_wav); s_wav_frames += size / 4; }
    return 0;
}

u32 osAiGetLength(void) { return s_q_n ? s_q_left[0] : 0; }

/* One vblank of playback: freq/60 frames leave the AI. */
static void ai_drain_one_tick(void)
{
    uint32_t bytes = (s_freq * 4 + s_tick_frac) / 60;
    s_tick_frac = (s_freq * 4 + s_tick_frac) % 60;
    while (bytes && s_q_n) {
        uint32_t take = bytes < s_q_left[0] ? bytes : s_q_left[0];
        s_q_left[0] -= take; bytes -= take;
        if (!s_q_left[0]) { s_q_left[0] = s_q_left[1]; s_q_n--; }
    }
}

_Static_assert(sizeof(Acmd) == 8, "sim/build.sh must shadow PR/abi.h with 32-bit Acmd words");
static void run_task(struct SPTask *t)
{
    /* 8-byte packets, as on the target (sim/build.sh shadows PR/abi.h and
     * links non-PIE for this); data_size = commands * sizeof(u64). */
    const uint32_t *w = (const uint32_t *)t->task.t.data_ptr;
    uint32_t n = t->task.t.data_size / sizeof(u64);
    for (uint32_t i = 0; i < n; i++) abi_exec(w[2 * i], w[2 * i + 1]);
    g_audio_stats.tasks++;
    g_audio_stats.last_cmds = n;
}

void audio_hal_init(void)
{
    const char *path = getenv("SIM_AUDIO_WAV");
    if (path && (s_wav = fopen(path, "wb"))) { wav_header(0); atexit(wav_close); }
    abi_reset();
    audio_init();
    sound_init();
}

void audio_hal_tick(void)
{
    g_audio_stats.ticks++;
    ai_drain_one_tick();
    if (gResetTimer < 25) {
        struct SPTask *t = create_next_audio_frame_task();
        if (t) run_task(t);
    }
}

#else
/* ======================================================= Pocket ========== */
#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>
#include <system.h>
#include "audio_load.h"
#include "mirlo_game.h"
#include AUDIO_FW_HEADER   /* build/audio/audio_fw.h, from the Makefile */

extern int printf(const char *, ...);

s32 osAiSetFrequency(u32 frequency)
{
    audio_set_mbox(AMB_AI_FREQ, frequency);
    return (s32)frequency;
}

/* The N64's two-slot AI: the audio core's current buffer plus MBOX3. */
s32 osAiSetNextBuffer(void *buf, u32 size)
{
    if (audio_mbox(AMB_AI_NEXT) != 0) { g_audio_stats.ai_dropped++; return -1; }
    audio_set_mbox(AMB_AI_LEN, size);
    audio_set_mbox(AMB_AI_NEXT, (uint32_t)(uintptr_t)buf);
    g_audio_stats.ai_buffers++;
    return 0;
}

u32 osAiGetLength(void) { return audio_mbox(AMB_AI_REMAIN); }

static inline uint32_t cyc(void) { return audio_r32(AUDIO_CYCLES); }

/* A queue of two (audio_hw.h): the core takes a task out of the mailbox when
 * it starts it, so a task can wait there while the previous one still runs.
 * The engine builds tick N+2's command list in the buffer tick N used
 * (gAudioCmdBuffers[2]) and queues tick N's PCM to the AI at tick N+2, so
 * before building a new list the mailbox must be free: then task N+1 has been
 * taken, i.e. task N is done. Returns 0 if it is not free in time -- the tick
 * is skipped (the engine does not advance: a gap, not corrupted audio). */
static int task_slot_free(void)
{
    uint32_t t0 = cyc();
    while (audio_mbox(AMB_TASK) != 0) {
        if (cyc() - t0 > CONFIG_CLOCK_FREQUENCY / 125) return 0;   /* 8 ms */
    }
    return 1;
}

static void run_task(struct SPTask *t)
{
    g_audio_stats.core_cycles = audio_mbox(AMB_STATS);
    if (g_audio_stats.core_cycles > g_audio_stats.core_cycles_max) g_audio_stats.core_cycles_max = g_audio_stats.core_cycles;
    /* The command list and the sample DMA buffers were written through the
     * game CPU's write-through D-cache: drain it before the core reads SDRAM. */
    flush_cpu_dcache();
    audio_set_mbox(AMB_TASK_N, t->task.t.data_size / 8u);
    audio_set_mbox(AMB_TASK, (uint32_t)(uintptr_t)t->task.t.data_ptr);
    g_audio_stats.tasks++;
    g_audio_stats.last_cmds = t->task.t.data_size / 8u;
}

void audio_hal_tick(void)
{
    uint32_t t0 = cyc();
    g_audio_stats.ticks++;
    if (gResetTimer < 25) {
        if (!task_slot_free()) {
            g_audio_stats.tasks_dropped++;           /* the tick is skipped */
        } else {
            struct SPTask *t = create_next_audio_frame_task();
            if (t) run_task(t);
        }
    }
    uint32_t dt = cyc() - t0;
    g_audio_stats.cpu_cycles = dt;
    if (dt > g_audio_stats.cpu_cycles_max) g_audio_stats.cpu_cycles_max = dt;
}

/* ---- sound data */
extern u8 gSoundDataADSR[], SOUND_DATA_END[];

/* The sound data travels inside the game file as its SND0 block (see
 * hal/mirlo_game.c and MIRLO's tools/make_mirlo_game.py). */
static int load_sound_data(void)
{
    uint32_t base = (uint32_t)(uintptr_t)gSoundDataADSR;
    uint32_t want = (uint32_t)(uintptr_t)SOUND_DATA_END - base;
    uint32_t off = 0, size = 0;
    if (!mirlo_find_block("SND0", &off, &size)) { printf("audio: no SND0 block in the game file\n"); return 0; }
    printf("audio: sound data: %lu bytes at %lu (firmware expects %lu) -> 0x%08lx\n",
           (unsigned long)size, (unsigned long)off, (unsigned long)want, (unsigned long)base);
    if (size != want) return 0;
    if (!mirlo_read(off, size, base)) { printf("audio: sound load TIMEOUT\n"); return 0; }
    return 1;
}

/* ---- 60 Hz tick from the CLINT machine timer (MIRLO's lang/c/game/log.c
 * trap handler calls g_mtimer_hook) ---- */
#define CLINT_REG(off) (*(volatile uint32_t *)(uintptr_t)(CLINT_BASE + (off)))
static uint64_t s_next_tick;
static const uint32_t TICK = CONFIG_CLOCK_FREQUENCY / 60u;

static uint64_t mtime(void)
{
    uint32_t hi, lo;
    do { hi = CLINT_REG(0xBFFC); lo = CLINT_REG(0xBFF8); } while (hi != CLINT_REG(0xBFFC));
    return ((uint64_t)hi << 32) | lo;
}
static void arm(uint64_t t)
{
    CLINT_REG(0x4004) = 0xFFFFFFFFu;
    CLINT_REG(0x4000) = (uint32_t)t;
    CLINT_REG(0x4004) = (uint32_t)(t >> 32);
}
extern void (*volatile g_mtimer_hook)(void);
void frame_isr_poll(void) __attribute__((weak));
void frame_isr_poll(void) { }
static void timer_tick(void)
{
    s_next_tick += TICK;
    uint64_t now = mtime();
    if (now > s_next_tick + TICK) {           /* fell behind (long critical section): resync */
        g_audio_stats.ticks_late++;
        s_next_tick = now + TICK;
    }
    arm(s_next_tick);
    audio_hal_tick();
    frame_isr_poll();   /* flip to frames that finished while the game submits none (a level load) */
}

static int s_enabled;

void audio_hal_init(void)
{
    if (!load_sound_data()) {
        printf("audio: no sound data -- running silent\n");
        sound_init();                   /* the bank lists still need their terminators */
        return;
    }
    int bad = audio_load(audio_imem_words, AUDIO_IMEM_NWORDS, audio_dmem_words, AUDIO_DMEM_NWORDS);
    audio_set_mbox(AMB_AI_FREQ, 32000);
    audio_start();
    uint32_t t0 = cyc();
    while (audio_mbox(AMB_STATE) != AUDIO_ST_ABI && cyc() - t0 < CONFIG_CLOCK_FREQUENCY / 10) { }
    printf("audio: core firmware %u+%u words (%d bad), state 0x%08lx\n", AUDIO_IMEM_NWORDS, AUDIO_DMEM_NWORDS, bad,
           (unsigned long)audio_mbox(AMB_STATE));
    if (audio_mbox(AMB_STATE) != AUDIO_ST_ABI) { sound_init(); return; }
    audio_init();
    sound_init();
    s_enabled = 1;
    printf("audio: engine initialised\n");
}

void audio_hal_start(void)
{
    if (!s_enabled) return;
    g_mtimer_hook = timer_tick;
    s_next_tick = mtime() + TICK;
    arm(s_next_tick);
    __asm__ volatile("csrs mie, %0" :: "r"(1u << 7));       /* MTIE */
    __asm__ volatile("csrs mstatus, %0" :: "r"(1u << 3));   /* MIE */
}
#endif
