/* Custom boot level-script that skips the Goddard "SM64" splash + rotating
 * Mario-head title and drops straight into the Cool, Cool Mountain (CCM)
 * attract demo (US demo table index 2, assets/demos/ccm.bin).
 *
 * Replaces the decomp's levels/entry.c (which is NOT compiled for this
 * firmware). The engine still walks its own level_main_scripts_entry ->
 * script_exec_level_table -> script_exec_ccm -> level_ccm_entry; we only
 * pre-seed the demo globals and carry LEVEL_CCM in sRegister.
 *
 * Flow, once thread5_game_loop reaches the level script:
 *   setup_game_memory() has already built gDemoInputsBuf (game_init.c).
 *   CALL port_start_ccm_demo -> loads ccm.bin, sets gCurrDemoInput != NULL,
 *     gCurrLevelNum = LEVEL_CCM, returns LEVEL_CCM into sRegister.
 *   EXECUTE level_main_scripts_entry -> common model loads, then
 *     CALL lvl_init_from_save_file (sRegister -> gCurrLevelNum, stays CCM),
 *     loop { EXECUTE level_main_menu_entry_2 (self-skips: lvl_set_current_level
 *            returns 0 while a demo is active), JUMP_LINK script_exec_level_table
 *            (GET gCurrLevelNum == LEVEL_CCM -> script_exec_ccm ->
 *             EXECUTE level_ccm_entry) }.
 *   level_ccm_entry spawns Mario at (-1512,2560,-2305) yaw 140 area 1, builds
 *     the camera, and (gCurrDemoInput != NULL) sets Mario ACT_IDLE, no music.
 *   run_demo_inputs() (already wired via read_controller_inputs) feeds
 *     gControllerPads[0] from ccm.bin every frame.
 */
#include <stdio.h>
#include <ultra64.h>
#include "sm64.h"
#include "types.h"
#include "level_table.h"

#include "game/memory.h"
#include "game/game_init.h"
#include "game/area.h"
#include "levels/scripts.h"

#include "level_commands.h"
#include "make_const_nonconst.h"

/* Runs once, in place of src/menu/title_screen.c's run_level_id_or_demo()
 * attract-timeout body. `arg0` is the CALL macro's literal arg (0); the
 * second parameter is the level-script sRegister. Returns LEVEL_CCM so
 * sRegister carries it into level_main_scripts_entry. */
/* Neutralise the pad for the frame the demo starts in (same race as port_boot_flat.c).
 *
 * The engine reads the controller at the top of each frame and only applies a
 * demo's inputs (run_demo_inputs) if one is active at that moment. This boot
 * script activates the demo LATER in the same frame -- and Mario is created and
 * updated in that same frame -- so his very first update runs on the RAW pad.
 * On the host the pad is always zero, so nothing shows. On the Pocket with the
 * dock's controller the raw registers read joy=0x80008000, i.e. both sticks'
 * Y axis at -128: "stick fully back" and "C-down". Measured on the console:
 * Mario walks at frame 3, decelerates at frame 4 and drifts 44 units in +z,
 * and the camera follows him -- the whole "camera differs from the host by a
 * fixed offset" investigation was this.
 *
 * Zero everything the first update will read: the engine's Controller fields
 * and the OSContPad they were copied from. */
static void neutralise_pad(void)
{
    struct Controller *c = &gControllers[0];
    c->rawStickX = c->rawStickY = 0;
    c->stickX = c->stickY = c->stickMag = 0.0f;
    c->buttonDown = c->buttonPressed = 0;
    if (c->controllerData != NULL) {
        c->controllerData->stick_x = 0;
        c->controllerData->stick_y = 0;
        c->controllerData->button  = 0;
    }
}

#ifndef PORT_FIRST_DEMO
#define PORT_FIRST_DEMO 2
#endif
#ifdef GEOM_HOST_TEST
#include <stdlib.h>
#endif

s32 port_start_ccm_demo(UNUSED s16 arg0, UNUSED s32 sRegister)
{
    printf("port_start_ccm_demo: level script reached, seeding CCM demo\n");
    /* US demo table: 0 bitdw, 1 wf, 2 ccm, 3 bbh, 4 jrb, 5 hmc, 6 pss.
     * PORT_FIRST_DEMO (SIM_FIRST_DEMO on the host) starts the chain elsewhere. */
    gDemoInputListID = PORT_FIRST_DEMO;
#ifdef GEOM_HOST_TEST
    if (getenv("SIM_FIRST_DEMO")) gDemoInputListID = (u16) atoi(getenv("SIM_FIRST_DEMO"));
#endif
    load_patchable_table(&gDemoInputsBuf, gDemoInputListID);
    gCurrDemoInput   = ((struct DemoInput *) gDemoInputsBuf.bufTarget) + 1; /* skip 4-byte level-id header */
    gCurrSaveFileNum = 1;
    gCurrActNum      = 1;
#ifdef GEOM_HOST_TEST
    if (getenv("SIM_ACT")) gCurrActNum = (s16) atoi(getenv("SIM_ACT"));
#endif
    gCurrLevelNum    = (s8) ((struct DemoInput *) gDemoInputsBuf.bufTarget)->timer;
    neutralise_pad();
    {
        const unsigned char *h = (const unsigned char *) gDemoInputsBuf.bufTarget;
        const unsigned char *d = (const unsigned char *) gCurrDemoInput;
        printf("  ccm.bin header: %02x %02x %02x %02x  (level id %u), gCurrDemoInput=%p\n",
               h[0], h[1], h[2], h[3], h[0], (void *) gCurrDemoInput);
        printf("  demo stream[0..15]: %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
               d[0],d[1],d[2],d[3], d[4],d[5],d[6],d[7], d[8],d[9],d[10],d[11], d[12],d[13],d[14],d[15]);
    }
    return gCurrLevelNum;
}

/* Attract-demo chain: when a demo ends, start the next one in the US demo
 * table (0 bitdw, 1 wf, 2 ccm, 3 bbh, 4 jrb, 5 hmc, 6 pss, then round again)
 * instead of going back to the title screen -- which needs Goddard, and this
 * port has no Goddard.
 *
 * Called with every lvl_init_or_update() result (--wrap=lvl_init_or_update:
 * level scripts reach it through a function pointer, so --wrap sees it). At
 * the end of a demo the engine's delayed warp makes it return -2 (-8 after
 * PSS, the last one) while gCurrDemoInput is still set; returning the next
 * demo's level number instead turns that into an ordinary level change:
 * level_main_scripts_entry's loop continues, level_main_menu_entry_2 skips
 * itself because a demo is active, and script_exec_level_table enters the
 * level -- exactly the path src/menu/title_screen.c's run_level_id_or_demo()
 * starts a demo on. */
s32 port_demo_chain(s32 r)
{
    if (gCurrDemoInput == NULL || (r != -2 && r != -8)) return r;
    u16 next = gDemoInputListID + 1;
    if (next >= gDemoInputsBuf.dmaTable->count) next = 0;
    gDemoInputListID = next;
    load_patchable_table(&gDemoInputsBuf, gDemoInputListID);
    gCurrDemoInput   = ((struct DemoInput *) gDemoInputsBuf.bufTarget) + 1;
    gCurrSaveFileNum = 1;
    gCurrActNum      = 1;
#ifdef GEOM_HOST_TEST
    if (getenv("SIM_ACT")) gCurrActNum = (s16) atoi(getenv("SIM_ACT"));
#endif
    s32 level = (s8) ((struct DemoInput *) gDemoInputsBuf.bufTarget)->timer;
#ifdef GEOM_HOST_TEST
    printf("DEMO: next demo %u -> level %d\n", (unsigned) next, (int) level);
#else
    extern void log_printf(const char *, ...);   /* never printf in the frame loop */
    log_printf("DEMO: next demo %u -> level %d\n", (unsigned) next, (int) level);
#endif
    return level;
}

s32 __real_lvl_init_or_update(s16 initOrUpdate, s32 unused);
s32 __wrap_lvl_init_or_update(s16 initOrUpdate, s32 unused)
{
    return port_demo_chain(__real_lvl_init_or_update(initOrUpdate, unused));
}

const LevelScript level_script_entry[] = {
    INIT_LEVEL(),
    SLEEP(/*frames*/ 2),
    BLACKOUT(/*active*/ FALSE),
    CALL(/*arg*/ 0, /*func*/ port_start_ccm_demo),
    EXECUTE(/*seg*/ 0x15, /*script*/ NULL, /*scriptEnd*/ NULL, /*entry*/ level_main_scripts_entry),
    JUMP(/*target*/ level_script_entry),
};
