/* Host simulator: the whole game on the workstation.
 *
 * Runs the real SM64 engine (the decomp, host gcc) with the port's HAL
 * collapsed to a synchronous loop, takes every per-frame graphics task where
 * hal/sptask_dualcore.c does on the Pocket (osSpTaskStartGo: task->t.data_ptr
 * is the F3DEX2 master display list), and pushes it through the same code:
 *
 *     Gfx --f3d_emit--> GDL --geom_pipeline--> MRDP commands
 *         --MRDP's bit-exact C model--> RGB565 framebuffer in a simulated
 *         SDRAM --> PPM
 *
 * Environment:
 *   SIM_DUMP_FRAME=N     the frame to write to SIM_OUT, then exit (default 30)
 *   SIM_OUT=path.ppm     (default out.ppm)
 *   SIM_DUMP_EVERY=N     also write a PPM every N frames, to
 *   SIM_DUMP_PREFIX=p    p_<frame>.ppm (default "frame")
 *   SIM_PAD=...          controller input (sim_pad_key() below)
 *   SIM_WARP, SIM_ACT, SIM_HUD, SIM_HEALTH, SIM_TRANS, SIM_FPS: sim_hud.c
 *   SIM_FIRST_DEMO=n     the attract demo to start with (SIM_BOOT=demo builds)
 *   SIM_MRDP_CAPTURE=f   the dump frame's MRDP commands, for MIRLO's sim/mrdp
 *   SIM_MRDP_PIXEL=x,y,f every write to that pixel in frame f (a model
 *                        built with SIM_EXTRA_DEFS=-DMRDP_DEBUG_PIXEL)
 *   SIM_DUMP_GDL_RAW=f   the dump frame's GDL, for MIRLO's sim/geom_full
 *                        (with SIM_DLC_OFF=1, so it is self-contained)
 *   SIM_DLC_OFF=1        no display-list translation cache
 *   SIM_FOLD_OFF=1       no 2D translation folding (f3d_emit.c mv_load)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "f3d_emit.h"
#include "geom_gdl.h"
#include "geom_pipeline.h"
#include "mrdp.h"
#include "mrdp_frame.h"

/* ---- MRDP: the geom core's command words go through the model into a
 * simulated SDRAM; frames are read back from its RGB565 colour image ---- */
#define SIM_SDRAM_BASE 0x40000000u
#define SIM_SDRAM_SIZE (64u << 20)
#define SIM_FB 0x40C00000u               /* frame.c's first colour buffer */
#define SIM_ZB 0x40D01000u               /* its depth buffer */
uint8_t *g_geom_host_sdram;
static mrdp_t s_mrdp;
static uint16_t sim_rd16(void *c, uint32_t a) { (void)c; return *(uint16_t *)(g_geom_host_sdram + (a - SIM_SDRAM_BASE)); }
static void sim_wr16(void *c, uint32_t a, uint16_t v) { (void)c; *(uint16_t *)(g_geom_host_sdram + (a - SIM_SDRAM_BASE)) = v; }
/* SIM_MRDP_CAPTURE: {1, n, n command words} and {2, addr, bytes, data}
 * (SDRAM the geom core wrote: decoded textures), u32 little-endian */
static FILE *s_mrdp_cap;
void geom_test_mrdp(const uint32_t *w, unsigned n)
{
    if (s_mrdp_cap) { uint32_t h[2] = { 1, n }; fwrite(h, 4, 2, s_mrdp_cap); fwrite(w, 4, n, s_mrdp_cap); }
    for (unsigned i = 0; i < n; i++) mrdp_push(&s_mrdp, w[i]);
}
void geom_test_mrdp_mem(uint32_t addr, uint32_t bytes)
{
    if (!s_mrdp_cap) return;
    uint32_t n4 = (bytes + 3u) & ~3u;
    uint32_t h[3] = { 2, addr, n4 };
    fwrite(h, 4, 3, s_mrdp_cap);
    fwrite(g_geom_host_sdram + (addr - SIM_SDRAM_BASE), 1, n4, s_mrdp_cap);
}
/* frame.c's per-frame clears, as the same MRDP commands */
static void sim_mrdp_clear(uint32_t rgb888)
{
    uint32_t w[MRDP_FRAME_CLEAR_WORDS];
    geom_test_mrdp(w, mrdp_frame_clear(w, SIM_FB, SIM_ZB, GEOM_FB_HRES, GEOM_FB_VRES, rgb888));
}
static void sim_mrdp_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6\n%d %d\n255\n", GEOM_FB_HRES, GEOM_FB_VRES);
    for (int y = 0; y < GEOM_FB_VRES; y++)
        for (int x = 0; x < GEOM_FB_HRES; x++) {
            uint16_t v = sim_rd16(NULL, SIM_FB + (uint32_t)(y * GEOM_FB_HRES + x) * 2u);
            unsigned r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
            unsigned char c[3] = { (unsigned char)(r5 << 3 | r5 >> 2), (unsigned char)(g6 << 2 | g6 >> 4),
                                   (unsigned char)(b5 << 3 | b5 >> 2) };
            fwrite(c, 1, 3, fp);
        }
    fclose(fp);
}

/* ---- SM64 main pool: host memory (include/segments.h calls these) ---- */
#define POOL_BYTES (24u * 1024u * 1024u)
static unsigned char *s_pool;
void *port_pool_start(void) { return s_pool; }
void *port_pool_end(void)   { return s_pool + POOL_BYTES; }

/* ---- config ---------------------------------------------------------- */
static int   s_dump_frame = 30;
static const char *s_out = "out.ppm";
static int   s_dump_every;
static const char *s_dump_prefix = "frame";

/* ---- the graphics task (== hal/sptask_dualcore.c on the Pocket) ------- */
static uint32_t s_gdl[1u << 20];   /* 4 MiB GDL scratch */
static unsigned s_gfx_frame;

void osSpTaskLoad(OSTask *task) { (void)task; }
void osSpTaskYield(void) {}
OSYieldResult osSpTaskYielded(OSTask *task) { (void)task; return 0; }

#ifdef SIM_AUDIO_HOST
#include "audio_hal.h"
#include <signal.h>
#include <sys/time.h>
/* The engine spins on the sound thread in places (sound_reset ->
 * audio_reset_session -> wait_for_audio_frames): on the N64 and on the
 * Pocket a pre-empting tick makes progress there. The sim's ticks are tied to
 * game frames, so give it the same pre-emption as a fallback: a 20 ms timer
 * that ticks once whenever no frame-driven tick happened since the last one. */
static volatile uint32_t s_ticks_seen;
static void audio_alarm(int sig)
{
    (void)sig;
    if (g_audio_stats.ticks == s_ticks_seen) audio_hal_tick();
    s_ticks_seen = g_audio_stats.ticks;
}
static void audio_alarm_start(void)
{
    struct itimerval it = { { 0, 20000 }, { 0, 20000 } };
    signal(SIGALRM, audio_alarm);
    setitimer(ITIMER_REAL, &it, NULL);
}
#endif

/* SIM_PAD="from-to:key,..." holds APF_INPUT cont1_key `key` (hex, MIRLO's
 * docs/control.md bitmap: 0x10 A, 0x8000 Start, 0x1 D-pad up...) during
 * gfx frames from..to, e.g. SIM_PAD="150-153:8000,300-303:10". */
uint32_t sim_pad_key(void)
{
    static int parsed, n;
    static unsigned lo[64], hi[64], key[64];
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("SIM_PAD");
        while (e && *e && n < 64) {
            if (sscanf(e, "%u-%u:%x", &lo[n], &hi[n], &key[n]) == 3) n++;
            e = strchr(e, ',');
            if (e) e++;
        }
    }
    uint32_t k = 0;
    for (int i = 0; i < n; i++) if (s_gfx_frame >= lo[i] && s_gfx_frame <= hi[i]) k |= key[i];
    return k;
}

void osSpTaskStartGo(OSTask *task) {
    if (task->t.type != M_GFXTASK) return;   /* audio tasks go to hal/audio_hal.c */
#ifdef SIM_AUDIO_HOST
    /* the sound thread's two vblanks per 30 Hz game frame (display_and_vsync) */
    audio_hal_tick();
    audio_hal_tick();
#endif
    { extern void sim_hud_apply(void); sim_hud_apply(); }
    { extern void sim_trans_apply(unsigned); sim_trans_apply(s_gfx_frame); }
    { extern void sim_warp_apply(unsigned); sim_warp_apply(s_gfx_frame); }
    unsigned f = s_gfx_frame++;

    gdl_cur_t c;
    gdl_begin(&c, s_gdl, sizeof(s_gdl) / sizeof(s_gdl[0]));
    f3d_emit_reset();
    f3d_emit_display_list((const f3d_word_t *) task->t.data_ptr, &c);
    gdl_end(&c);
    uint32_t used = gdl_used(&c, s_gdl);

    if (f == (unsigned)s_dump_frame && getenv("SIM_MRDP_CAPTURE")) s_mrdp_cap = fopen(getenv("SIM_MRDP_CAPTURE"), "wb");
    {
        const char *e = getenv("SIM_MRDP_PIXEL");
        int px, py, pf;
        s_mrdp.dbg_x = s_mrdp.dbg_y = -1;
        if (e && sscanf(e, "%d,%d,%d", &px, &py, &pf) == 3 && (unsigned)pf == f) { s_mrdp.dbg_x = px; s_mrdp.dbg_y = py; }
    }
    /* SM64's background colour, as frame.c applies it on the Pocket */
    sim_mrdp_clear(f3d_clear_valid ? (uint32_t)f3d_clear_rgb : 0xCCD1D8u);

    if (f == (unsigned)s_dump_frame && getenv("SIM_DUMP_GDL_RAW")) {
        FILE *rf = fopen(getenv("SIM_DUMP_GDL_RAW"), "wb");
        if (rf) {
            fwrite(s_gdl, 4, used, rf);
            fclose(rf);
            printf("[sim] dumped raw GDL: %u words\n", used);
        }
    }

    geom_reset();
    geom_run_display_list(s_gdl);

    if (f < 4 || f == (unsigned)s_dump_frame)
        printf("[sim] gfx task #%u: GDL words=%u\n", f, used);

    if (s_dump_every && f > 0 && f % (unsigned)s_dump_every == 0) {
        char path[256];
        snprintf(path, sizeof path, "%s_%06u.ppm", s_dump_prefix, f);
        sim_mrdp_ppm(path);
        printf("[sim] frame %u -> %s\n", f, path);
        fflush(stdout);
    }
    if (f == (unsigned)s_dump_frame) {
        if (s_mrdp_cap) {
            uint32_t sync[4] = { 1, 2, MRDP_OP_SYNC_FULL << 24, 0 };
            fwrite(sync, 4, 4, s_mrdp_cap);
            fclose(s_mrdp_cap);
            s_mrdp_cap = NULL;
            printf("[sim] MRDP capture written\n");
        }
        sim_mrdp_ppm(s_out);
        printf("[sim] MRDP: pixels %llu unknown ops %u\n", (unsigned long long)s_mrdp.pixels_drawn,
               s_mrdp.unknown_ops);
        printf("[sim] wrote %s (%dx%d) at frame %u\n", s_out, GEOM_FB_HRES, GEOM_FB_VRES, f);
        fflush(stdout);
        exit(0);
    }
}

/* ---- entry --------------------------------------------------------------- */
extern void main_func(void);   /* src/game/main.c */
extern void sound_init(void);  /* src/audio/external.c -- sSoundBanks terminators */

/* the translation cache's counters, printed when the run ends */
extern unsigned long f3d_dlc_hits, f3d_dlc_misses, f3d_dlc_never, f3d_dlc_toomany, f3d_dlc_switches;
static void dlc_stats_at_exit(void)
{
    fprintf(stderr, "[sim] DLC hits=%lu misses=%lu never=%lu toomany=%lu switches=%lu\n",
            f3d_dlc_hits, f3d_dlc_misses, f3d_dlc_never, f3d_dlc_toomany, f3d_dlc_switches);
}
int main(void) {
    atexit(dlc_stats_at_exit);
    const char *e;
    if ((e = getenv("SIM_DUMP_FRAME"))) s_dump_frame = atoi(e);
    if (getenv("SIM_DLC_OFF")) { extern int f3d_dlc_off; f3d_dlc_off = 1; }
    if (getenv("SIM_FOLD_OFF")) { extern int f3d_fold_off; f3d_fold_off = 1; }
    if ((e = getenv("SIM_OUT")))        s_out = e;
    if ((e = getenv("SIM_DUMP_EVERY"))) s_dump_every = atoi(e);
    if ((e = getenv("SIM_DUMP_PREFIX"))) s_dump_prefix = e;

    s_pool = malloc(POOL_BYTES);
    if (!s_pool) { fprintf(stderr, "pool alloc failed\n"); return 1; }
    g_geom_host_sdram = calloc(SIM_SDRAM_SIZE, 1);
    mrdp_init(&s_mrdp, NULL, sim_rd16, sim_wr16);

    printf("=== Super Mirlo 64 host simulator ===\n");
#ifdef SIM_AUDIO_HOST
    audio_hal_init();     /* audio_init() + sound_init(), WAV output */
    audio_alarm_start();
#else
    sound_init();
#endif
    main_func();          /* never returns; osSpTaskStartGo exit()s at the dump frame */
    return 0;
}
