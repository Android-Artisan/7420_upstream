#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm/page.h>
#include <asm/setup.h>

OUTPUT_FORMAT("elf64-alpha")
OUTPUT_ARCH(alpha)
ENTRY(__start)
PHDRS { kernel PT_LOAD; note PT_NOTE; }
jiffies = jiffies_64;
SECTIONS
{
#ifdef CONFIG_ALPHA_LEGACY_START_ADDRESS
	. = 0xfffffc0000310000;
#else
	. = 0xfffffc0001010000;
#endif

	_text = .;	/* Text and read-only data */
	.text : {
	*(.text.head)
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		*(.fixup)
		*(.gnu.warning)
	} :kernel
	swapper_pg_dir = SWAPPER_PGD;
	_etext = .;	/* End of text section */

	NOTES :kernel :note
	.dummy : {
		*(.dummy)
	} :kernel

	RODATA

	/* Exception table */
	. = ALIGN(16);
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	/* Will be freed after init */
	. = ALIGN(PAGE_SIZE);
	/* Init code and data */
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

	. = ALIGN(8);
	.initcall.init : {
		__initcall_start = .;
		INITCALLS
		__initcall_end = .;
	}

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
	}
#endif

	. = ALIGN(8);
	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	. = ALIGN(8);
	SECURITY_INIT

	PERCPU(PAGE_SIZE)

	. = ALIGN(2 * PAGE_SIZE);
	__init_end = .;
	/* Freed after init ends here */

	/* Note 2 page alignment above.  */
	.data.init_thread : {
		*(.data.init_thread)
	}

	. = ALIGN(PAGE_SIZE);
	.data.page_aligned : {
		*(.data.page_aligned)
	}

	. = ALIGN(64);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}

	_data = .;
	/* Data */
	.data : {
		DATA_DATA
		CONSTRUCTORS
	}
	EXCEPTION_TABLE(16)

	/* Will be freed after init */
	__init_begin = ALIGN(PAGE_SIZE);
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
	PERCPU_SECTION(L1_CACHE_BYTES)
	/* Align to THREAD_SIZE rather than PAGE_SIZE here so any padding page
	   needed for the THREAD_SIZE aligned init_task gets freed after init */
	. = ALIGN(THREAD_SIZE);
	__init_end = .;
	/* Freed after init ends here */

	_sdata = .;	/* Start of rw data section */
	_data = .;
	RW_DATA_SECTION(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)

	.got : {
		*(.got)
	}
	.sdata : {
		*(.sdata)
	}
	_edata = .;	/* End of data section */

	__bss_start = .;
	.sbss : {
		*(.sbss)
		*(.scommon)
	}
	.bss : {
		*(.bss)
		*(COMMON)
	}
	__bss_stop = .;
	_end = .;

	/* Sections to be discarded */
	/DISCARD/ : {
		EXIT_TEXT
		EXIT_DATA
		*(.exitcall.exit)
	}

	BSS_SECTION(0, 0, 0)
	_end = .;

	.mdebug 0 : {
		*(.mdebug)
	}
	.note 0 : {
		*(.note)
	}

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
