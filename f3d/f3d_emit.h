/* Walk a real F3DEX2 `Gfx` display list and translate it into a flat GDL
 * for the geometry core (MIRLO's lang/c/geom/geom_gdl.h), instead of doing
 * the vertex math on the game CPU. The opcodes and field packings are
 * F3DEX2's (gbi.h under F3DEX_GBI_2).
 *
 * Decomp-header-free: a Gfx command is just two 32-bit words here, Vtx is a
 * raw 16-byte blob, Mtx is 16 s16 + 16 u16. So this compiles for host tests
 * as well as the target.
 */
#ifndef GAME_F3D_EMIT_H
#define GAME_F3D_EMIT_H

#include <stdint.h>
#include "gdl_build.h"

typedef struct { uint32_t w0; uintptr_t w1; } f3d_word_t;  /* w1 holds a pointer; uintptr_t == uint32_t on the rv32 target, matching Gfx */

/* Reset the translator's tracked state (matrix target, geometry mode, tex). */
void f3d_emit_reset(void);

/* Translate the Gfx list at `dl` into GDL written through `out`. Nested lists
 * (G_DL / G_ENDDL) are flattened here on the game CPU. Does NOT emit a
 * trailing GDL_END -- the caller does that after any extra commands. */
void f3d_emit_display_list(const f3d_word_t *dl, gdl_cur_t *out);
/* After f3d_emit_display_list(): the display list painted a background
 * colour (a full-screen fill before any drawing) -- the caller's clear of
 * this frame should use it. 0xRRGGBB. */
extern int      f3d_clear_valid;
extern uint32_t f3d_clear_rgb;

/* w1 of a G_SPNOOP that sets SM64's full-screen viewport: for the port's own
 * overlays (the FPS counter), which may draw on a screen that never set one
 * (the title). */
#define F3D_TAG_FULL_VIEWPORT 0x4D4B3244u   /* "MK2D" */

#endif /* GAME_F3D_EMIT_H */
