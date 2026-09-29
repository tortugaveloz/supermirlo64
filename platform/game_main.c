/* main(): Super Mirlo 64's entry point on the game CPU.
 *
 * init_asm.S (MIRLO's lang/linker) sets the stack, zeroes .bss and calls
 * main(). This sets up the video and the frame pipeline (MIRLO's
 * lang/c/game/frame.c), the SM64 main pool in SDRAM and the sound, then hands
 * off to the decomp's own entry point main_func() (src/game/main.c), which
 * never returns. Every frame's graphics task then flows through
 * hal/sptask_dualcore.c -> f3d/f3d_emit.c -> the geometry core -> MRDP.
 */
#include <stdint.h>
#include <stdio.h>

#include "frame.h"
#include "audio_hal.h"
#include "mirlo_game.h"

extern void main_func(void);      /* src/game/main.c (no decomp header declares it) */

/* SM64's alloc_pool() reads SEG_POOL_START / SEG_POOL_END from
 * include/segments.h, which calls these: a fixed window in SDRAM, above the
 * framebuffers, the display-list cache and the sound data (linker/regions.ld). */
#define PORT_POOL_START  0x42c00000u
#define PORT_POOL_END    0x43fe0000u   /* 20 MiB; the save sits above it at 0x43ff0000 */

void *port_pool_start(void) { return (void *) PORT_POOL_START; }
void *port_pool_end(void)   { return (void *) PORT_POOL_END; }

int main(void)
{
    printf("\n=== Super Mirlo 64 ===\n");

    frame_set_video(mirlo_video_hres());   /* 320 x 240: the game file's header */
    frame_init();
    /* pipelined: the geometry core walks frame k+1 while MRDP draws frame k */
    frame_set_overlap(1);

    /* Sound: loads the sound data (the game file's SND0 block) and the audio
     * core's firmware, then audio_init() + sound_init(), as thread4_sound
     * would. Without them it still runs sound_init() -- the sound banks'
     * list terminators, which the engine walks even when silent -- and the
     * game runs without sound. */
    audio_hal_init();
    audio_hal_start();     /* 60 Hz audio tick from the machine timer */

#if !defined(PORT_EEPROM_RAM)
    {   /* the save as the Pocket left it (hal/os_hal.c): all 0xFF = no .sav yet */
        extern uint32_t port_save_slot_init(void);
        port_save_slot_init();
    }
#endif
    main_func();           /* runs the game forever */

    for (;;) { }
    return 0;
}
