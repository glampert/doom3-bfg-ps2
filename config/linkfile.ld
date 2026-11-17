/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# Linkfile script for ee-ld
*/

/*
 * Doom 3 BFG PS2: ps2sdk's ee/startup/linkfile (329e2bb) with the boot data added, and .rel.dyn
 * placed (below). Every EE image links with it. Keep the rest in step with the SDK's when the
 * pinned SDK moves.
 *
 * .bootdata holds what only the boot reads: the IOP storage drivers (src/ps2/system/
 * iop_modules.S), which must be in EE memory when the IOP resets, since the drive they read is out
 * of reach until they run. It sits at the first page past _end, where the heap's arena begins:
 * crt0 clears .bss only up to _end, and the heap holds the range as its first block until the IOP
 * boot releases it (src/ps2/system/heap.cpp). Its own segment keeps .bss out of the file. An image
 * without boot data has an empty segment and no held block.
 */

ENTRY(__start);

PHDRS {
  text PT_LOAD;
  bootdata PT_LOAD;
}

SECTIONS {
	.text 0x00100000: {
		_ftext = . ;
		*(.text)
		*(.text.*)
		*(.gnu.linkonce.t*)
		KEEP(*(.init))
		KEEP(*(.fini))
		QUAD(0)
	} :text

	PROVIDE(_etext = .);
	PROVIDE(etext = .);

	/* Global/static constructors and deconstructors. */
	.ctors ALIGN(16): {
		KEEP(*crtbegin*.o(.ctors))
		KEEP(*(EXCLUDE_FILE(*crtend*.o) .ctors))
		KEEP(*(SORT(.ctors.*)))
		KEEP(*(.ctors))
	}
	.dtors ALIGN(16): {
		KEEP(*crtbegin*.o(.dtors))
		KEEP(*(EXCLUDE_FILE(*crtend*.o) .dtors))
		KEEP(*(SORT(.dtors.*)))
		KEEP(*(.dtors))
	}

	.reginfo : { *(.reginfo) }

	.data ALIGN(128): {
		_fdata = . ;
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d*)
		SORT(CONSTRUCTORS)
	}

	/* Static data.  */
	.rodata ALIGN(128): {
		*(.rodata)
		*(.rodata.*)
		*(.gnu.linkonce.r*)
	}

	.rdata ALIGN(128): { *(.rdata) }
	.gcc_except_table ALIGN(128): { *(.gcc_except_table) }

	/* Placeholder (R_MIPS_NONE) relocations some objects carry. As an orphan, ld would put them
	   after .bootdata, the last read-only section, past the boot data and inside the arena. */
	.rel.dyn : { *(.rel.dyn) }

	_gp = ALIGN(128) + 0x7ff0;
	.lit4 ALIGN(128): { *(.lit4) }
	.lit8 ALIGN(128): { *(.lit8) }

	.sdata ALIGN(128): {
		*(.sdata)
		*(.sdata.*)
		*(.gnu.linkonce.s*)
	}

	_edata = .;
	PROVIDE(edata = .);

	/* Uninitialized data.  */
	.sbss ALIGN(128) : {
		_fbss = . ;
		*(.sbss)
		*(.sbss.*)
		*(.gnu.linkonce.sb*)
		*(.scommon)
	}

	.bss ALIGN(128) : {
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b*)
		*(COMMON)
	}
	_end_bss = .;

	_end = . ;
	PROVIDE(end = .);

	/* Boot data: see the note at the top. */
	.bootdata ALIGN(4096) : {
		_boot_data_start = .;
		KEEP(*(.bootdata))
		_boot_data_end = .;
	} :bootdata

	.spad 0x70000000: {
		*(.spad)
	} :text

	/* Symbols needed by crt0.s.  */
	PROVIDE(_heap_size = -1);
	PROVIDE(_stack = -1);
	PROVIDE(_stack_size = 128 * 1024);
}
