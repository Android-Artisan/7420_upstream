/* MN10300 Main kernel linker script
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#define __VMLINUX_LDS__
#include <asm-generic/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>

OUTPUT_FORMAT("elf32-am33lin", "elf32-am33lin", "elf32-am33lin")
OUTPUT_ARCH(mn10300)
ENTRY(_start)
jiffies = jiffies_64;
#ifndef CONFIG_MN10300_CURRENT_IN_E2
current = __current;
#endif
SECTIONS
{
  . = CONFIG_KERNEL_TEXT_ADDRESS;
  /* read-only */
  _stext = .;
  _text = .;			/* Text and read-only data */
  .text : {
	*(
	.text.head
	.text
	)
	HEAD_TEXT
	TEXT_TEXT
	SCHED_TEXT
	LOCK_TEXT
	KPROBES_TEXT
	*(.fixup)
	*(.gnu.warning)
	} = 0xcb

  _etext = .;			/* End of text section */

  . = ALIGN(16);		/* Exception table */
  __start___ex_table = .;
  __ex_table : { *(__ex_table) }
  __stop___ex_table = .;

  BUG_TABLE

  RODATA

  /* writeable */
  .data : {			/* Data */
	DATA_DATA
	CONSTRUCTORS
	}

  . = ALIGN(4096);
  __nosave_begin = .;
  .data_nosave : { *(.data.nosave) }
  . = ALIGN(4096);
  __nosave_end = .;

  . = ALIGN(4096);
  .data.page_aligned : { *(.data.idt) }

  . = ALIGN(32);
  .data.cacheline_aligned : { *(.data.cacheline_aligned) }

  /* rarely changed data like cpu maps */
  . = ALIGN(32);
  .data.read_mostly : AT(ADDR(.data.read_mostly)) {
	*(.data.read_mostly)
	_edata = .;		/* End of data section */
  }

  . = ALIGN(THREAD_SIZE);	/* init_task */
  .data.init_task : { *(.data.init_task) }

  /* might get freed after init */
  . = ALIGN(4096);
  EXCEPTION_TABLE(16)
  BUG_TABLE

  RO_DATA(PAGE_SIZE)

  /* writeable */
  _sdata = .;     /* Start of rw data section */
  RW_DATA_SECTION(32, PAGE_SIZE, THREAD_SIZE)
  _edata = .;

  /* might get freed after init */
  . = ALIGN(PAGE_SIZE);
  .smp_locks : AT(ADDR(.smp_locks) - LOAD_OFFSET) {
  	__smp_locks = .;
	*(.smp_locks)
	__smp_locks_end = .;
  }

  /* will be freed after init */
  . = ALIGN(4096);		/* Init code and data */
  __init_begin = .;
  .init.text : {
	_sinittext = .;
	*(.init.text)
	_einittext = .;
  }
  .init.data : { *(.init.data) }
  . = ALIGN(16);
  __setup_start = .;
  .setup.init : { KEEP(*(.init.setup)) }
  __setup_end = .;

  __initcall_start = .;
  .initcall.init : {
	INITCALLS
  }
  __initcall_end = .;
  __con_initcall_start = .;
  .con_initcall.init : { *(.con_initcall.init) }
  __con_initcall_end = .;

  SECURITY_INIT
  . = ALIGN(PAGE_SIZE);		/* Init code and data */
  __init_begin = .;
  INIT_TEXT_SECTION(PAGE_SIZE)
  INIT_DATA_SECTION(16)
  . = ALIGN(4);
  __alt_instructions = .;
  .altinstructions : { *(.altinstructions) }
  __alt_instructions_end = .;
 .altinstr_replacement : { *(.altinstr_replacement) }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : { *(.exit.text) }
  .exit.data : { *(.exit.data) }

#ifdef CONFIG_BLK_DEV_INITRD
  . = ALIGN(4096);
  __initramfs_start = .;
  .init.ramfs : { *(.init.ramfs) }
  __initramfs_end = .;
#endif

  . = ALIGN(32);
  __per_cpu_start = .;
  .data.percpu  : { *(.data.percpu) }
  __per_cpu_end = .;
  . = ALIGN(4096);
  __init_end = .;
  /* freed after init ends here */

  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss.page_aligned)
	*(.bss)
  }
  . = ALIGN(4);
  __bss_stop = .;
	.exit.text : { EXIT_TEXT; }
	.exit.data : { EXIT_DATA; }

  PERCPU_SECTION(32)
  . = ALIGN(PAGE_SIZE);
  __init_end = .;
  /* freed after init ends here */

  BSS_SECTION(0, PAGE_SIZE, 4)

  _end = . ;

  /* This is where the kernel creates the early boot page tables */
  . = ALIGN(4096);
  pg0 = .;

  /* Sections to be discarded */
  /DISCARD/ : {
	*(.exitcall.exit)
	}

  STABS_DEBUG

  DWARF_DEBUG
  . = ALIGN(PAGE_SIZE);
  pg0 = .;

  STABS_DEBUG

  DWARF_DEBUG

  /* Sections to be discarded */
  DISCARDS
}
