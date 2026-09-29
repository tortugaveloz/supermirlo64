/* Boot level-script for the whole game: levels/entry.c's own flow.
 *
 * "SUPER MARIO 64" logo (levels/intro level_intro_splash_screen) -> title
 * screen (level_intro_mario_head_regular: Start goes to the file select, a
 * timeout runs the attract demos as on the N64) -> file select -> the castle.
 *
 * levels/entry.c itself is not compiled: its EXECUTE names the intro
 * segment's ROM range, which NO_SEGMENTED_MEMORY builds do not have. The
 * script below is the same with those NULL.
 */
#include <ultra64.h>
#include "sm64.h"
#include "types.h"

#include "levels/intro/header.h"

#include "level_commands.h"
#include "make_const_nonconst.h"

const LevelScript level_script_entry[] = {
    INIT_LEVEL(),
    SLEEP(/*frames*/ 2),
    BLACKOUT(/*active*/ FALSE),
    SET_REG(/*value*/ 0),
    EXECUTE(/*seg*/ 0x14, /*script*/ NULL, /*scriptEnd*/ NULL, /*entry*/ level_intro_splash_screen),
    JUMP(/*target*/ level_script_entry),
};
