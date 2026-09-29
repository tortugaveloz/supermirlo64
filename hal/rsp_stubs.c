/* The graphics and audio SP tasks are intercepted synchronously in
 * sptask_dualcore.c and audio_hal.c -- the real F3DEX2 / audio RSP microcode
 * blobs are never executed. These dummy arrays only exist to satisfy the
 * ucode pointer/size fields the task-structure builders still fill in:
 *   src/game/game_init.c  create_gfx_task_structure()   (rspF3D*)
 *   src/audio/external.c  create_next_audio_frame_task() (rspAspMain*)
 */
#include <PR/ultratypes.h>

u8 rspF3DStart[1];
u8 rspF3DDataStart[1];
u8 rspF3DBootStart[1];
u8 rspF3DBootEnd[1];

u8 rspAspMainStart[1];
u8 rspAspMainDataStart[1];
u8 rspAspMainDataEnd[1];
