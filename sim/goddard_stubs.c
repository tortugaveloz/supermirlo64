/* The Goddard subsystem (src/goddard/*) renders only the rotating 3D "Mario
 * head" on the title screen. The simulator leaves it out unless built with
 * SIM_GODDARD=1 (the title then shows its backdrop without the head). These
 * no-op stubs satisfy the handful of Goddard entry points referenced from
 * src/game / src/engine:
 *   src/game/mario_misc.c   : gd_copy_p1_contpad, gd_vblank, gd_sfx_to_play
 *   src/engine/level_script.c: gd_add_to_heap, gdm_init, gdm_setup, gdm_maketestdl
 *     (all inside level_cmd_load_mario_head -> LOAD_MARIO_HEAD, intro-only)
 */
#include <PR/ultratypes.h>
#include <PR/os_cont.h>
#include <PR/gbi.h>

void gd_add_to_heap(void *addr, u32 size) { (void) addr; (void) size; }
Gfx *gdm_gettestdl(s32 id) { (void) id; return (Gfx *) 0; }
void gdm_init(void *blockpool, u32 size) { (void) blockpool; (void) size; }
void gdm_setup(void) { }
void gdm_maketestdl(s32 id) { (void) id; }
void gd_vblank(void) { }
void gd_copy_p1_contpad(OSContPad *p1cont) { (void) p1cont; }
/* 0 = no sound. The title screen passes this straight to play_menu_sounds(),
 * a bit mask: -1 played every menu sound on every frame (noise). */
s32  gd_sfx_to_play(void) { return 0; }
