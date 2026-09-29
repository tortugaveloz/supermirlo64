#ifndef SEGMENTS_H
#define SEGMENTS_H

#include "config.h"

/* Port override of the decomp's include/segments.h.
 *
 * The decomp file hard-codes N64 RDRAM virtual addresses (SEG_POOL_START =
 * 0x8005C000, ...). Nothing is mapped there on Mirlo. Only two of
 * these are used at run time under NO_SEGMENTED_MEMORY:
 *   - src/game/main.c alloc_pool():  SEG_POOL_START / SEG_POOL_END -> main_pool_init()
 *   - src/game/memory.c load_engine_code_segment(): SEG_ENGINE / SEG_FRAMEBUFFERS
 *       (whole function is #ifndef NO_SEGMENTED_MEMORY -> compiled out)
 *
 * So the main pool is the only thing that must point at real memory. It is
 * computed at run time (platform/game_main.c) from the linker-provided _end
 * (top of the linked image + BSS) up to _fstack (top of main_ram, minus a
 * stack margin) -- i.e. it uses ALL remaining SDRAM in the port's 44 MiB
 * main_ram region.
 */

extern void *port_pool_start(void);
extern void *port_pool_end(void);

#define SEG_POOL_START  port_pool_start()
#define SEG_POOL_END    port_pool_end()
#define SEG_POOL_SIZE   ((u32)((char *)port_pool_end() - (char *)port_pool_start()))

/* Referenced only by compiled-out code; keep them defined for cpp. */
#define SEG_START         0x41400000
#define SEG_BUFFERS       SEG_POOL_START
#define SEG_ENGINE        0x41400000
#define SEG_FRAMEBUFFERS  0x40c00000
#define RDRAM_END         0x44000000
#define SEG_GODDARD_POOL_OFFSET 0x52000
#define SEG_GODDARD       0x41400000

#endif /* SEGMENTS_H */
