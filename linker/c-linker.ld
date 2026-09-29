/* Super Mirlo 64's linker script.
 *
 * Layout inside a bounded 12 MiB main_ram (linker/regions.ld):
 *
 *   0x40000000  .text, .data / .sdata
 *   ---- gap ----
 *   0x40280000  .rodata        (~7.9 MiB: the game's models, levels, textures)
 *   0x40a?????  .bss           zeroed by init_asm.S each boot
 *   ...
 *   0x40bffff8  _fstack
 *
 * One contiguous image (0x40000000 .. end of .rodata, ~10 MiB with the gap):
 * the program part of the game file, which the Pocket loads at launch.
 */
INCLUDE output_format.ld
ENTRY(_start)

__DYNAMIC = 0;

INCLUDE regions.ld

SECTIONS
{
	.text 0x40000000 :
	{
		_ftext = .;
		*init_asm*(.text)
		KEEP(*init_asm*(.text))
		KEEP(*(.text.isr))
		*(.text .stub .text.* .gnu.linkonce.t.*)
		_etext = .;
	} > main_ram

	.data :
	{
		. = ALIGN(8);
		_fdata = .;
		*(.data .data.* .gnu.linkonce.d.*)
		*(.data1)
		_gp = ALIGN(16);
		*(.sdata .sdata.* .gnu.linkonce.s.*)
		. = ALIGN(8);
		_edata = .;
	} > main_ram

	/* Read-only asset data past the memtest zone so a core reset leaves it
	 * intact -- only .text/.data need re-flashing over JTAG. */
	.rodata 0x40280000 :
	{
		. = ALIGN(8);
		_frodata = .;
		*(.rodata .rodata.* .gnu.linkonce.r.*)
		*(.rodata1)
		*(.got .got.*)
		*(.toc .toc.*)
		. = ALIGN(8);
		_erodata = .;
	} > main_ram

	.bss :
	{
		. = ALIGN(8);
		_fbss = .;
		*(.dynsbss)
		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
		*(.dynbss)
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(8);
		_ebss = .;
		_end = .;
	} > main_ram
}

PROVIDE(_fstack = ORIGIN(main_ram) + LENGTH(main_ram) - 8);

PROVIDE(_fdata_rom = LOADADDR(.data));
PROVIDE(_edata_rom = LOADADDR(.data) + SIZEOF(.data));
PROVIDE(_emain_ram = ORIGIN(main_ram) + LENGTH(main_ram));

PROVIDE(__heap_start = _end);
PROVIDE(__heap_end = _fstack - (DEFINED(__stack_size) ? __stack_size : 0x8000));
PROVIDE(__heap_size = __heap_end - __heap_start);
