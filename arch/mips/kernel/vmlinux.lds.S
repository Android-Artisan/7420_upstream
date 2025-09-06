#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#define PAGE_SIZE _PAGE_SIZE

/*
 * Put .bss..swapper_pg_dir as the first thing in .bss. This will
 * ensure that it has .bss alignment (64K).
 */
#define BSS_FIRST_SECTIONS *(.bss..swapper_pg_dir)

#include <asm-generic/vmlinux.lds.h>

#undef mips
#define mips mips
OUTPUT_ARCH(mips)
ENTRY(kernel_entry)
PHDRS {
	text PT_LOAD FLAGS(7);	/* RWX */
	note PT_NOTE FLAGS(4);	/* R__ */
}
jiffies = JIFFIES;
#ifndef CONFIG_CAVIUM_OCTEON_SOC
	note PT_NOTE FLAGS(4);	/* R__ */
#endif /* CAVIUM_OCTEON_SOC */
}

#ifdef CONFIG_32BIT
	#ifdef CONFIG_CPU_LITTLE_ENDIAN
		jiffies	 = jiffies_64;
	#else
		jiffies	 = jiffies_64 + 4;
	#endif
#else
	jiffies	 = jiffies_64;
#endif

SECTIONS
{
#ifdef CONFIG_BOOT_ELF64
	/* Read-only sections, merged into text segment: */
	/* . = 0xc000000000000000; */

	/* This is the value for an Origin kernel, taken from an IRIX kernel.  */
	/* . = 0xc00000000001c000; */

	/* Set the vaddr for the text segment to a value
	 *   >= 0xa800 0000 0001 9000 if no symmon is going to configured
	 *   >= 0xa800 0000 0030 0000 otherwise
	 */

	/* . = 0xa800000000300000; */
	. = 0xffffffff80300000;
#endif
	. = LOADADDR;
	. = VMLINUX_LOAD_ADDRESS;
	/* read-only */
	_text = .;	/* Text and read-only data */
	.text : {
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		*(.text.*)
		*(.fixup)
		*(.gnu.warning)
	} :text = 0
	_etext = .;	/* End of text section */

	/* Exception table */
	. = ALIGN(16);
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}
	EXCEPTION_TABLE(16)

	/* Exception table for data bus errors */
	__dbe_table : {
		__start___dbe_table = .;
		*(__dbe_table)
		__stop___dbe_table = .;
	}

	NOTES :text :note
	.dummy : { *(.dummy) } :text

#ifdef CONFIG_CAVIUM_OCTEON_SOC
#define NOTES_HEADER
#else /* CONFIG_CAVIUM_OCTEON_SOC */
#define NOTES_HEADER :note
#endif /* CONFIG_CAVIUM_OCTEON_SOC */
	NOTES :text NOTES_HEADER
	.dummy : { *(.dummy) } :text

	_sdata = .;			/* Start of data section */
	RODATA

	/* writeable */
	.data : {	/* Data */
		. = . + DATAOFFSET;		/* for CONFIG_MAPPED_KERNEL */
		/*
		 * This ALIGN is needed as a workaround for a bug a
		 * gcc bug upto 4.1 which limits the maximum alignment
		 * to at most 32kB and results in the following
		 * warning:
		 *
		 *  CC      arch/mips/kernel/init_task.o
		 * arch/mips/kernel/init_task.c:30: warning: alignment
		 * of ‘init_thread_union’ is greater than maximum
		 * object file alignment.  Using 32768
		 */
		. = ALIGN(_PAGE_SIZE);
		*(.data.init_task)


		INIT_TASK_DATA(THREAD_SIZE)
		NOSAVE_DATA
		PAGE_ALIGNED_DATA(PAGE_SIZE)
		CACHELINE_ALIGNED_DATA(1 << CONFIG_MIPS_L1_CACHE_SHIFT)
		READ_MOSTLY_DATA(1 << CONFIG_MIPS_L1_CACHE_SHIFT)
		DATA_DATA
		CONSTRUCTORS
	}
	_gp = . + 0x8000;
	.lit8 : {
		*(.lit8)
	}
	.lit4 : {
		*(.lit4)
	}
	/* We want the small data sections together, so single-instruction offsets
	   can access them all, and initialized data all before uninitialized, so
	   we can shorten the on-disk segment size.  */
	.sdata : {
		*(.sdata)
	}

	. = ALIGN(_PAGE_SIZE);
	.data_nosave : {
		__nosave_begin = .;
		*(.data.nosave)
	}
	. = ALIGN(_PAGE_SIZE);
	__nosave_end = .;

	. = ALIGN(32);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}
	_edata =  .;			/* End of data section */

	/* will be freed after init */
	. = ALIGN(_PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	.init.text : {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}
	.init.data : {
		INIT_DATA
	}
	. = ALIGN(16);
	.init.setup : {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}

	.initcall.init : {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
	}

	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}
	SECURITY_INIT

	_edata =  .;			/* End of data section */

	/* will be freed after init */
	. = ALIGN(PAGE_SIZE);		/* Init code and data */
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)

	. = ALIGN(4);
	.mips.machines.init : AT(ADDR(.mips.machines.init) - LOAD_OFFSET) {
		__mips_machines_start = .;
		*(.mips.machines.init)
		__mips_machines_end = .;
	}

	/* .exit.text is discarded at runtime, not link time, to deal with
	 * references from .rodata
	 */
	.exit.text : {
		EXIT_TEXT
	}
	.exit.data : {
		EXIT_DATA
	}
#if defined(CONFIG_BLK_DEV_INITRD)
	. = ALIGN(_PAGE_SIZE);
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif
	PERCPU(_PAGE_SIZE)
	. = ALIGN(_PAGE_SIZE);
	__init_end = .;
	/* freed after init ends here */

	__bss_start = .;	/* BSS */
	.sbss  : {
		*(.sbss)
		*(.scommon)
	}
	.bss : {
		*(.bss)
		*(COMMON)
	}
	__bss_stop = .;

	_end = . ;

	/* Sections to be discarded */
	/DISCARD/ : {
		*(.exitcall.exit)

		/* ABI crap starts here */
		*(.MIPS.options)
		*(.options)
		*(.pdr)
		*(.reginfo)
	}

#ifdef CONFIG_SMP
	PERCPU_SECTION(1 << CONFIG_MIPS_L1_CACHE_SHIFT)
#endif
#ifdef CONFIG_MIPS_RAW_APPENDED_DTB
	__appended_dtb = .;
	/* leave space for appended DTB */
	. += 0x100000;
#elif defined(CONFIG_MIPS_ELF_APPENDED_DTB)
	.appended_dtb : AT(ADDR(.appended_dtb) - LOAD_OFFSET) {
		*(.appended_dtb)
		KEEP(*(.appended_dtb))
	}
#endif
	/*
	 * Align to 64K in attempt to eliminate holes before the
	 * .bss..swapper_pg_dir section at the start of .bss.  This
	 * also satisfies PAGE_SIZE alignment as the largest page size
	 * allowed is 64K.
	 */
	. = ALIGN(0x10000);
	__init_end = .;
	/* freed after init ends here */

	/*
	 * Force .bss to 64K alignment so that .bss..swapper_pg_dir
	 * gets that alignment.	 .sbss should be empty, so there will be
	 * no holes after __init_end. */
	BSS_SECTION(0, 0x10000, 8)

	_end = . ;

	/* These mark the ABI of the kernel for debuggers.  */
	.mdebug.abi32 : {
		KEEP(*(.mdebug.abi32))
	}
	.mdebug.abi64 : {
		KEEP(*(.mdebug.abi64))
	}

	/* This is the MIPS specific mdebug section.  */
	.mdebug : {
		*(.mdebug)
	}

	STABS_DEBUG
	DWARF_DEBUG

	/* These must appear regardless of  .  */
	.gptab.sdata : {
		*(.gptab.data)
		*(.gptab.sdata)
	}
	.gptab.sbss : {
		*(.gptab.bss)
		*(.gptab.sbss)
	}

	/* Sections to be discarded */
	DISCARDS
	/DISCARD/ : {
		/* ABI crap starts here */
		*(.MIPS.abiflags)
		*(.MIPS.options)
		*(.options)
		*(.pdr)
		*(.reginfo)
		*(.eh_frame)
	}
}
