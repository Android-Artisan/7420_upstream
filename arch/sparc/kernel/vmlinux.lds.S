/* ld script to make SparcLinux kernel */

#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>

/* ld script for sparc32/sparc64 kernel */

#include <asm-generic/vmlinux.lds.h>

#include <asm/page.h>
#include <asm/thread_info.h>

#ifdef CONFIG_SPARC32
#define INITIAL_ADDRESS  0x10000 + SIZEOF_HEADERS
#define TEXTSTART	0xf0004000

#define SMP_CACHE_BYTES_SHIFT 5

#else
#define SMP_CACHE_BYTES_SHIFT 6
#define INITIAL_ADDRESS 0x4000
#define TEXTSTART      0x0000000000404000

#endif

#define SMP_CACHE_BYTES (1 << SMP_CACHE_BYTES_SHIFT)

#ifdef CONFIG_SPARC32
OUTPUT_FORMAT("elf32-sparc", "elf32-sparc", "elf32-sparc")
OUTPUT_ARCH(sparc)
ENTRY(_start)
jiffies = jiffies_64 + 4;
SECTIONS
{
	. = 0x10000 + SIZEOF_HEADERS;
	.text 0xf0004000 :
	{
		_text = .;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		*(.gnu.warning)
	} = 0
	_etext = .;
	PROVIDE (etext = .);
	RODATA
	.data :	{
		DATA_DATA
		CONSTRUCTORS
	}
	.data1 : {
		*(.data1)
	}
	_edata = .;
	PROVIDE (edata = .);
#else
/* sparc64 */
OUTPUT_FORMAT("elf64-sparc", "elf64-sparc", "elf64-sparc")
OUTPUT_ARCH(sparc:v9a)
ENTRY(_start)
jiffies = jiffies_64;
#endif

#ifdef CONFIG_SPARC64
ASSERT((swapper_tsb == 0x0000000000408000), "Error: sparc64 early assembler too large")
#endif

SECTIONS
{
#ifdef CONFIG_SPARC64
	swapper_pg_dir = 0x0000000000402000;
#endif
	. = INITIAL_ADDRESS;
	.text TEXTSTART :
	{
		_text = .;
		HEAD_TEXT
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		IRQENTRY_TEXT
		*(.gnu.warning)
	} = 0
	_etext = .;

	RO_DATA(PAGE_SIZE)

	/* Start of data section */
	_sdata = .;

	.data1 : {
		*(.data1)
	}
	RW_DATA_SECTION(SMP_CACHE_BYTES, 0, THREAD_SIZE)

	/* End of data section */
	_edata = .;

	.fixup : {
		__start___fixup = .;
		*(.fixup)
		__stop___fixup = .;
	}
	__ex_table : {
		__start___ex_table = .;
		*(__ex_table)
		__stop___ex_table = .;
	}

	NOTES

	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	.init.text : {
		_sinittext = .;
		INIT_TEXT
		_einittext = .;
	}
	__init_text_end = .;
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

#ifdef CONFIG_BLK_DEV_INITRD
	. = ALIGN(PAGE_SIZE);
	.init.ramfs : {
	__initramfs_start = .;
		*(.init.ramfs)
	__initramfs_end = .;
	}
#endif

	PERCPU(PAGE_SIZE)
	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	. = ALIGN(32);
	.data.cacheline_aligned : {
		*(.data.cacheline_aligned)
	}
	. = ALIGN(32);
	.data.read_mostly : {
		*(.data.read_mostly)
	}

	__bss_start = .;
	.sbss : {
		*(.sbss)
		*(.scommon) }
	.bss : {
		*(.dynbss)
		*(.bss)
		*(COMMON)
	}
	_end = . ;
	PROVIDE (end = .);
	/DISCARD/ : {
		EXIT_TEXT
		EXIT_DATA
		*(.exitcall.exit)
	}

	STABS_DEBUG
	DWARF_DEBUG
	EXCEPTION_TABLE(16)
	NOTES

	. = ALIGN(PAGE_SIZE);
	__init_begin = ALIGN(PAGE_SIZE);
	INIT_TEXT_SECTION(PAGE_SIZE)
	__init_text_end = .;
	INIT_DATA_SECTION(16)

	. = ALIGN(4);
	.tsb_ldquad_phys_patch : {
		__tsb_ldquad_phys_patch = .;
		*(.tsb_ldquad_phys_patch)
		__tsb_ldquad_phys_patch_end = .;
	}

	.tsb_phys_patch : {
		__tsb_phys_patch = .;
		*(.tsb_phys_patch)
		__tsb_phys_patch_end = .;
	}

	.cpuid_patch : {
		__cpuid_patch = .;
		*(.cpuid_patch)
		__cpuid_patch_end = .;
	}

	.sun4v_1insn_patch : {
		__sun4v_1insn_patch = .;
		*(.sun4v_1insn_patch)
		__sun4v_1insn_patch_end = .;
	}
	.sun4v_2insn_patch : {
		__sun4v_2insn_patch = .;
		*(.sun4v_2insn_patch)
		__sun4v_2insn_patch_end = .;
	}
	.leon_1insn_patch : {
		__leon_1insn_patch = .;
		*(.leon_1insn_patch)
		__leon_1insn_patch_end = .;
	}
	.swapper_tsb_phys_patch : {
		__swapper_tsb_phys_patch = .;
		*(.swapper_tsb_phys_patch)
		__swapper_tsb_phys_patch_end = .;
	}
	.swapper_4m_tsb_phys_patch : {
		__swapper_4m_tsb_phys_patch = .;
		*(.swapper_4m_tsb_phys_patch)
		__swapper_4m_tsb_phys_patch_end = .;
	}
	.popc_3insn_patch : {
		__popc_3insn_patch = .;
		*(.popc_3insn_patch)
		__popc_3insn_patch_end = .;
	}
	.popc_6insn_patch : {
		__popc_6insn_patch = .;
		*(.popc_6insn_patch)
		__popc_6insn_patch_end = .;
	}
	.pause_3insn_patch : {
		__pause_3insn_patch = .;
		*(.pause_3insn_patch)
		__pause_3insn_patch_end = .;
	}
	.sun_m7_2insn_patch : {
		__sun_m7_2insn_patch = .;
		*(.sun_m7_2insn_patch)
		__sun_m7_2insn_patch_end = .;
	}
	PERCPU_SECTION(SMP_CACHE_BYTES)

	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	BSS_SECTION(0, 0, 0)
	_end = . ;

	STABS_DEBUG
	DWARF_DEBUG

	DISCARDS
}
