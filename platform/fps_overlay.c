/* On-screen frame counter: "FPS nn" at the bottom left of every frame --
 * title, Goddard's head, the intro and the levels -- drawn last, just before
 * the master display list is closed (end_master_display_list is wrapped:
 * -Wl,--wrap=end_master_display_list in the Makefile; render_hud, which it
 * used to follow, only runs in levels). nn is the game's frames per
 * second over the last half second (frame.c's frame_fps). Shown while the
 * "Show FPS" core setting is on: bit 24 of interact slot 1 (interact.json
 * id 102, 0x10000104). */
#include <generated/csr.h>
#include <ultra64.h>
#include "sm64.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/segment2.h"
#include "f3d_emit.h"

extern u32 frame_fps;

void __real_end_master_display_list(void);
void __wrap_end_master_display_list(void)
{
    if (!((apf_interact_interact1_read() >> 24) & 1u)) {
        __real_end_master_display_list();
        return;
    }

    u32 fps = frame_fps > 99u ? 99u : frame_fps;
    u8 str[8];
    int i = 0;
    str[i++] = 0x0A + ('F' - 'A');   /* the dialog font: 0..9, then A..Z */
    str[i++] = 0x0A + ('P' - 'A');
    str[i++] = 0x0A + ('S' - 'A');
    str[i++] = DIALOG_CHAR_SPACE;
    if (fps >= 10u) str[i++] = (u8)(fps / 10u);
    str[i++] = (u8)(fps % 10u);
    str[i] = DIALOG_CHAR_TERMINATOR;

    /* the full-screen viewport: the title never sets one */
    gDma0p(gDisplayListHead++, G_SPNOOP, F3D_TAG_FULL_VIEWPORT, 0);
    create_dl_ortho_matrix();   /* outside levels nothing set the 2D projection */
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    print_generic_string(22, 12, str);
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    __real_end_master_display_list();
}
