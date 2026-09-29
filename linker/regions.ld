/* Super Mirlo 64's SDRAM map.
 *
 *   main_ram   0x40000000 .. 0x40c00000  (12 MiB) -- .text + .rodata + .data +
 *                                                     .bss + stack (~10 MiB)
 *   framebuffers, depth  0x40c00000 ..    (MIRLO's lang/c/game/frame.c)
 *   GDL ring   0x41000000 .. 0x41200000  (frame.c GDL_RING_BASE)
 *   geometry core: texture staging 0x41200000, configuration 0x41300000,
 *              decoded-texture cache 0x41400000 .. 0x41608000
 *   sound data 0x41700000 ..             (the game file's SND0 block,
 *                                         tools/build_sound_le.sh SOUND_BASE)
 *   DL cache   0x42000000 .. 0x42400000  (4 MiB) -- f3d_emit's translations (hal/exec_dl_wrap.c)
 *   pool       0x42c00000 .. 0x43fe0000  (20 MiB) -- SM64 main_pool (platform/game_main.c)
 *   save       0x43ff0000 .. 0x43ff0200  (hal/os_hal.c)
 *
 * One contiguous image from 0x40000000: the program part of the game file,
 * which the Pocket loads at launch. MIRLO's stock lang/linker/regions.ld
 * (main_ram = 64 MiB) overlaps the framebuffers and can't bound the image.
 */
MEMORY {
	plic              : ORIGIN = 0xf0c00000, LENGTH = 0x00400000
	clint             : ORIGIN = 0xf0010000, LENGTH = 0x00010000
	rom               : ORIGIN = 0x00000000, LENGTH = 0x00020000
	sram              : ORIGIN = 0x10000000, LENGTH = 0x00008000
	main_ram          : ORIGIN = 0x40000000, LENGTH = 0x00c00000
	video_framebuffer : ORIGIN = 0x40c00000, LENGTH = 0x00800000
	geom_rom          : ORIGIN = 0x20000000, LENGTH = 0x00008000
	geom_ram          : ORIGIN = 0x20008000, LENGTH = 0x00004000
	example_slave     : ORIGIN = 0x80000000, LENGTH = 0x00100000
	csr               : ORIGIN = 0xf0000000, LENGTH = 0x00010000
}
