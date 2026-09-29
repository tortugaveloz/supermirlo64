/* The game file's data blocks (tools/make_mirlo_game.py): the file the
 * Pocket loaded into slot 0 may end in a MIRLODATA footer and a table of
 * tagged blocks; the BIOS loads only the program, a game reads its blocks. */
#ifndef MIRLO_GAME_H
#define MIRLO_GAME_H
#include <stdint.h>

/* Finds the block tagged `tag` (4 chars, e.g. "SND0") in the game file:
 * 1 with its file offset and size, 0 if the file has no such block. */
int mirlo_find_block(const char *tag, uint32_t *offset, uint32_t *size);

/* Reads len bytes at `offset` of the game file (slot 0) to SDRAM address
 * `addr` over the APF bridge, D-cache flushed after: 1 on success. */
int mirlo_read(uint32_t offset, uint32_t len, uint32_t addr);

/* The resolution the game file's header asks for: 268 (default, every file
 * without the field) or 320 (tools/make_mirlo_game.py --video 320). */
unsigned mirlo_video_hres(void);

#endif
