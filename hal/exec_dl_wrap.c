/* The graphics task, run inline, and the display-list translation cache's
 * platform side.
 *
 * On the N64, exec_display_list() (src/game/main.c) posts
 * MESG_START_GFX_SPTASK to gIntrMesgQueue and thread3_main's interrupt loop
 * turns that into osSpTaskLoad/osSpTaskStartGo. This HAL runs the game's
 * threads as one (osStartThread is synchronous), so thread3_main never reaches
 * that loop. Linked with -Wl,--wrap=exec_display_list: every call lands here
 * instead, runs the task (hal/sptask_dualcore.c) and posts the completion
 * message display_and_vsync() waits for on gGfxVblankQueue.
 */
#include <ultra64.h>
#include "sm64.h"
#include "types.h"

void osSpTaskStartGo(OSTask *task);   /* hal/sptask_dualcore.c */

void __wrap_exec_display_list(struct SPTask *spTask)
{
    if (spTask == NULL) {
        return;
    }
    spTask->state = SPTASK_STATE_RUNNING;
    osSpTaskStartGo(&spTask->task);
    spTask->state = SPTASK_STATE_FINISHED;
    if (spTask->msgqueue != NULL) {
        osSendMesg(spTask->msgqueue, spTask->msg, OS_MESG_NOBLOCK);  /* -> gGfxVblankQueue */
    }
}

/* f3d/f3d_emit.c's translation cache for static display lists: what is
 * static (the image's read-only data: SM64's model lists, vertices,
 * textures, lights) and the arena it keeps the translations in. */
#define PORT_DLCACHE_BYTES 0x00400000u   /* 4 MiB, two halves (f3d_emit.c) */
#ifdef F3D_HOST
#include <stdlib.h>
#include <sys/mman.h>
extern char etext[], __data_start[];   /* .rodata and .data.rel.ro lie between */
int f3d_dlc_static(const void *p) { return (const char *)p >= etext && (const char *)p < __data_start; }
void *f3d_dlc_arena(u32 *bytes)
{
    /* below 4 GiB: GDL_DL carries a 32-bit address, as on the target */
    static void *s_arena;
    const char *e = getenv("SIM_DLCACHE");
    if (e && *e == '0') { *bytes = 0; return NULL; }
    if (!s_arena) {
        s_arena = mmap(NULL, PORT_DLCACHE_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (s_arena == MAP_FAILED) s_arena = NULL;
    }
    *bytes = s_arena ? PORT_DLCACHE_BYTES : 0u;
    return s_arena;
}
#else
extern const char _frodata[], _erodata[];
int f3d_dlc_static(const void *p) { return (const char *)p >= _frodata && (const char *)p < _erodata; }
/* 0x42000000..0x42400000: free SDRAM between the sound data and the SM64
 * main pool (linker/regions.ld) */
void *f3d_dlc_arena(u32 *bytes) { *bytes = PORT_DLCACHE_BYTES; return (void *)0x42000000u; }
#endif
