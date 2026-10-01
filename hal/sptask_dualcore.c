/* The graphics task: the RSP's side of the frame, on Mirlo.
 *
 * The engine builds its per-frame F3DEX2 master display list exactly as on
 * the N64 (src/game/rendering_graph_node.c) and hands it to the "RSP" through
 * exec_display_list() -> osSpTaskStartGo() (hal/exec_dl_wrap.c). Here
 * f3d/f3d_emit.c translates it into a GDL, Mirlo's display-list format; the
 * geometry core walks the GDL (transform, lighting, clipping, triangle setup)
 * and feeds MRDP; MIRLO's lang/c/game/frame.c owns the framebuffers, the
 * pipelining and the vblank flip.
 *
 * Audio tasks never come here: hal/audio_hal.c takes them to the audio core.
 */
#include <stdint.h>
#include <ultra64.h>

#include "frame.h"
#include "f3d_emit.h"

void osSpTaskLoad(OSTask *task)
{
    (void) task; /* nothing to preload -- osSpTaskStartGo runs it directly */
}

void osSpTaskStartGo(OSTask *task)
{
    if (task->t.type != M_GFXTASK) {
        return;
    }
    gdl_cur_t c;
    frame_begin(&c);                         /* a GDL slot; waits for one if all are in flight */
#ifdef PORT_GEOM_F3D
    /* The geom core translates the list itself, as the N64's RSP did
     * (MIRLO's GDL_F3D): the static lists -- the image's read-only data --
     * are cached in the same arena the game CPU's translator would use.
     * The game builds the next frame in the other gfx pool meanwhile;
     * frame_submit() hands this one over only once the geom core is done
     * with the last, so this pool is never rewritten under it. */
    {
        extern const char _frodata[], _erodata[];
        extern void *f3d_dlc_arena(uint32_t *bytes);
        uint32_t ab; void *arena = f3d_dlc_arena(&ab);
        gdl_f3d(&c, (const void *) task->t.data_ptr, frame_clear_word(), _frodata, _erodata, arena, ab);
        gdl_end(&c);
        frame_submit(&c);
        return;
    }
#endif
    f3d_emit_reset();
    uint64_t t_emit = frame_uptime_cycles();
    f3d_emit_display_list((const f3d_word_t *) task->t.data_ptr, &c);
    if (f3d_clear_valid) frame_set_clear_rgb(f3d_clear_rgb);   /* SM64's background colour */
    frame_emit_cyc += frame_uptime_cycles() - t_emit;
    gdl_end(&c);
    frame_submit(&c);                        /* geometry core + MRDP, flip on vblank */
}

void osSpTaskYield(void)
{
}

OSYieldResult osSpTaskYielded(OSTask *task)
{
    (void) task;
    return 0;
}
