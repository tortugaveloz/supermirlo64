/* SIM_HUD="lives,coins,stars": holds the HUD's numbers (and Mario's own, which
 * update_hud_values() copies into it) at those values every frame, to check
 * the widest HUD fits the screen. */
#include <ultra64.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "sm64.h"
#include "types.h"
#include "game/level_update.h"
#include "game/area.h"
#include "game/save_file.h"

void sim_hud_apply(void)
{
    static int parsed, on, lives, coins, stars;
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("SIM_HUD");
        on = e && sscanf(e, "%d,%d,%d", &lives, &coins, &stars) == 3;
    }
    if (!on || !gMarioState) return;
    gMarioState->numLives = (s8)lives; gMarioState->numCoins = (s16)coins; gMarioState->numStars = (s16)stars;
    gHudDisplay.lives = (s16)lives; gHudDisplay.coins = (s16)coins; gHudDisplay.stars = (s16)stars;
    /* SIM_HEALTH=n (0x100 a wedge): below full the power meter shows in play */
    { static int h = -2; if (h == -2) { const char *e = getenv("SIM_HEALTH"); h = e ? (int)strtol(e, 0, 0) : -1; }
      if (h >= 0) gMarioState->health = (s16)h; }
}

/* SIM_TRANS="frame:type:time": starts play_transition(type, time) (area.h's
 * WARP_TRANSITION_*, e.g. 0x11 into Mario's head) at that gfx frame. */
void sim_trans_apply(unsigned frame)
{
    static int parsed, on; static unsigned at; static int type, time;
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("SIM_TRANS");
        on = e && sscanf(e, "%u:%i:%i", &at, &type, &time) == 3;
    }
    if (on && frame == at) play_transition((s16)type, (s16)time, 0, 0, 0);
}

uint32_t sim_interact(int i)
{
    static int parsed; static unsigned r[4];
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("SIM_INTERACT");
        if (e) sscanf(e, "%x,%x,%x,%x", &r[0], &r[1], &r[2], &r[3]);
    }
    return r[i & 3];
}

/* SIM_HISCORE=1: the course-complete screen's rainbow "HI SCORE", drawn over
 * the HUD every frame with the game's own routine (hud.c's render_hud is
 * wrapped: -Wl,--wrap=render_hud in build.sh). */
void print_hud_course_complete_string(s8 str);
void render_course_complete_lvl_info_and_hud_str(void);
void __real_render_hud(void);
void __wrap_render_hud(void)
{
    __real_render_hud();
    static int on = -1;
    if (on < 0) on = getenv("SIM_HISCORE") != NULL;
    if (on) print_hud_course_complete_string(0);
    /* SIM_DIALOG=n: opens dialog n (once) -- the text boxes at the screen's left */
    { static int dlg = -2; if (dlg == -2) { const char *e = getenv("SIM_DIALOG"); dlg = e ? atoi(e) : -1; }
      if (dlg >= 0) { void create_dialog_box(s16 dialog); create_dialog_box((s16)dlg); dlg = -1; } }
    /* SIM_PAUSE=1: the pause menu (render_menus_and_dialogs draws it right
     * after the HUD) */
    static int pz = -1;
    if (pz < 0) pz = getenv("SIM_PAUSE") != NULL;
    if (pz) {
        extern s16 gMenuMode; gMenuMode = 1;   /* MENU_MODE_RENDER_PAUSE_SCREEN */
        /* SIM_PAUSE=castle: the castle's pause box (PAUSE, course list, stars) */
        { const char *e = getenv("SIM_PAUSE"); extern s8 gMenuState;
          if (e && e[0] == 'c') gMenuState = 2;   /* MENU_STATE_PAUSE_SCREEN_CASTLE */ }
        /* the power meter (health below full) and a MY SCORE row: 2 stars and
         * 12 coins in this course */
        extern struct SaveBuffer gSaveBuffer;
        if (gMarioState) gMarioState->health = 0x480;
        gSaveBuffer.files[gCurrSaveFileNum - 1][0].courseStars[gCurrCourseNum - 1] = 0x03;
        gSaveBuffer.files[gCurrSaveFileNum - 1][0].courseCoinScores[gCurrCourseNum - 1] = 12;
    }
    /* SIM_CCOMPLETE=course:star[:coins]: the course-complete info (course number,
     * star glyph + act name, coins) as after that star */
    static int cc = -1, ccourse, cstar, ccoins = -1;
    if (cc < 0) { const char *e = getenv("SIM_CCOMPLETE"); cc = e && sscanf(e, "%d:%d:%d", &ccourse, &cstar, &ccoins) >= 2; }
    if (cc) {
        extern u8 gLastCompletedCourseNum, gLastCompletedStarNum; extern u16 gMenuTextAlpha;
        gLastCompletedCourseNum = (u8)ccourse; gLastCompletedStarNum = (u8)cstar; gMenuTextAlpha = 255;
        if (ccoins >= 0) { extern s32 gCourseCompleteCoins; gCourseCompleteCoins = ccoins; gHudDisplay.coins = (s16)ccoins; }
        render_course_complete_lvl_info_and_hud_str();
    }
}

/* SIM_FPS=nn: the on-screen counter as platform/fps_overlay.c draws it, on
 * every screen (end_master_display_list is wrapped in build.sh). */
void __real_end_master_display_list(void);
void __wrap_end_master_display_list(void)
{
    /* SIM_FPS=nn: the on-screen counter as platform/fps_overlay.c draws it */
    { const char *e = getenv("SIM_FPS");
      if (e) { unsigned fps = (unsigned)atoi(e) % 100u; u8 str[8]; int i = 0;
               str[i++] = 0x0A + ('F' - 'A'); str[i++] = 0x0A + ('P' - 'A'); str[i++] = 0x0A + ('S' - 'A');
               str[i++] = 0x9E; if (fps >= 10u) str[i++] = (u8)(fps / 10u); str[i++] = (u8)(fps % 10u); str[i] = 0xFF;
               extern Gfx dl_ia_text_begin[], dl_ia_text_end[]; extern Gfx *gDisplayListHead;
               void print_generic_string(s16 x, s16 y, const u8 *str);
               gDma0p(gDisplayListHead++, G_SPNOOP, 0x4D4B3244 /* F3D_TAG_FULL_VIEWPORT */, 0);
               void create_dl_ortho_matrix(void); create_dl_ortho_matrix();
               gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
               gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
               print_generic_string(22, 12, str);
               gSPDisplayList(gDisplayListHead++, dl_ia_text_end); } }
    __real_end_master_display_list();
}

/* SIM_WARP="frame:x,y,z,yaw": at that gfx frame puts Mario at (x,y,z) facing
 * yaw (N64 angle units) and from then on feeds neutral demo input, so a
 * demo's level can be looked at anywhere (e.g. an object the demo never
 * reaches). SIM_ACT picks the act the demo loads (port_boot.c). */
#include "engine/math_util.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "game/game_init.h"
void sim_warp_apply(unsigned frame)
{
    static int parsed, on; static unsigned at; static float x, y, z; static int yaw;
    static struct DemoInput neutral = { 255, 0, 0, 0 };
    if (!parsed) {
        parsed = 1;
        const char *e = getenv("SIM_WARP");
        on = e && sscanf(e, "%u:%f,%f,%f,%i", &at, &x, &y, &z, &yaw) == 5;
    }
    if (!on || frame < at || !gMarioState || !gMarioObject) return;
    neutral.timer = 255;
    gCurrDemoInput = &neutral;
    if (frame != at) return;
    vec3f_set(gMarioState->pos, x, y, z);
    vec3f_set(gMarioState->vel, 0, 0, 0);
    gMarioState->forwardVel = 0;
    gMarioState->faceAngle[1] = (s16)yaw;
    gMarioObject->oPosX = x; gMarioObject->oPosY = y; gMarioObject->oPosZ = z;
    set_mario_action(gMarioState, ACT_FREEFALL, 0);
}
