/*
 * sleep.c - x86-specific ACPI sleep support.
 *
 *  Copyright (C) 2001-2003 Patrick Mochel
 *  Copyright (C) 2001-2003 Pavel Machek <pavel@suse.cz>
 *  Copyright (C) 2001-2003 Pavel Machek <pavel@ucw.cz>
 */

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/dmi.h>
#include <linux/cpumask.h>
#include <asm/segment.h>

#include "realmode/wakeup.h"
#include "sleep.h"

unsigned long acpi_wakeup_address;
unsigned long acpi_realmode_flags;

/* address in low memory of the wakeup routine. */
static unsigned long acpi_realmode;

#if defined(CONFIG_SMP) && defined(CONFIG_64BIT)
static char temp_stack[10240];
#endif

/**
 * acpi_save_state_mem - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 *
 * Note that this is too late to change acpi_wakeup_address.
 */
int acpi_save_state_mem(void)
{
	struct wakeup_header *header;

	if (!acpi_realmode) {
		printk(KERN_ERR "Could not allocate memory during boot, "
		       "S3 disabled\n");
		return -ENOMEM;
	}
	memcpy((void *)acpi_realmode, &wakeup_code_start, WAKEUP_SIZE);

	header = (struct wakeup_header *)(acpi_realmode + HEADER_OFFSET);
	if (header->signature != 0x51ee1111) {
#include <linux/memblock.h>
#include <linux/dmi.h>
#include <linux/cpumask.h>
#include <asm/segment.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/realmode.h>

#include <linux/ftrace.h>
#include "../../realmode/rm/wakeup.h"
#include "sleep.h"

unsigned long acpi_realmode_flags;

#if defined(CONFIG_SMP) && defined(CONFIG_64BIT)
static char temp_stack[4096];
#endif

/**
 * x86_acpi_enter_sleep_state - enter sleep state
 * @state: Sleep state to enter.
 *
 * Wrapper around acpi_enter_sleep_state() to be called by assmebly.
 */
acpi_status asmlinkage __visible x86_acpi_enter_sleep_state(u8 state)
{
	return acpi_enter_sleep_state(state);
}

/**
 * x86_acpi_suspend_lowlevel - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int x86_acpi_suspend_lowlevel(void)
{
	struct wakeup_header *header =
		(struct wakeup_header *) __va(real_mode_header->wakeup_header);

	if (header->signature != WAKEUP_HEADER_SIGNATURE) {
		printk(KERN_ERR "wakeup header does not match\n");
		return -EINVAL;
	}

	header->video_mode = saved_video_mode;

	header->wakeup_jmp_seg = acpi_wakeup_address >> 4;

	/*
	 * Set up the wakeup GDT.  We set these up as Big Real Mode,
	 * that is, with limits set to 4 GB.  At least the Lenovo
	 * Thinkpad X61 is known to need this for the video BIOS
	 * initialization quirk to work; this is likely to also
	 * be the case for other laptops or integrated video devices.
	 */

	/* GDT[0]: GDT self-pointer */
	header->wakeup_gdt[0] =
		(u64)(sizeof(header->wakeup_gdt) - 1) +
		((u64)(acpi_wakeup_address +
			((char *)&header->wakeup_gdt - (char *)acpi_realmode))
				<< 16);
	/* GDT[1]: big real mode-like code segment */
	header->wakeup_gdt[1] =
		GDT_ENTRY(0x809b, acpi_wakeup_address, 0xfffff);
	/* GDT[2]: big real mode-like data segment */
	header->wakeup_gdt[2] =
		GDT_ENTRY(0x8093, acpi_wakeup_address, 0xfffff);

#ifndef CONFIG_64BIT
	store_gdt((struct desc_ptr *)&header->pmode_gdt);

	header->pmode_efer_low = nx_enabled;
	if (header->pmode_efer_low & 1) {
		/* This is strange, why not save efer, always? */
		rdmsr(MSR_EFER, header->pmode_efer_low,
			header->pmode_efer_high);
	}
#endif /* !CONFIG_64BIT */

	header->pmode_cr0 = read_cr0();
	header->pmode_cr4 = read_cr4_safe();
	header->pmode_behavior = 0;

#ifndef CONFIG_64BIT
	native_store_gdt((struct desc_ptr *)&header->pmode_gdt);

	/*
	 * We have to check that we can write back the value, and not
	 * just read it.  At least on 90 nm Pentium M (Family 6, Model
	 * 13), reading an invalid MSR is not guaranteed to trap, see
	 * Erratum X4 in "Intel Pentium M Processor on 90 nm Process
	 * with 2-MB L2 Cache and Intel® Processor A100 and A110 on 90
	 * nm process with 512-KB L2 Cache Specification Update".
	 */
	if (!rdmsr_safe(MSR_EFER,
			&header->pmode_efer_low,
			&header->pmode_efer_high) &&
	    !wrmsr_safe(MSR_EFER,
			header->pmode_efer_low,
			header->pmode_efer_high))
		header->pmode_behavior |= (1 << WAKEUP_BEHAVIOR_RESTORE_EFER);
#endif /* !CONFIG_64BIT */

	header->pmode_cr0 = read_cr0();
	if (__this_cpu_read(cpu_info.cpuid_level) >= 0) {
		header->pmode_cr4 = __read_cr4();
		header->pmode_behavior |= (1 << WAKEUP_BEHAVIOR_RESTORE_CR4);
	}
	if (!rdmsr_safe(MSR_IA32_MISC_ENABLE,
			&header->pmode_misc_en_low,
			&header->pmode_misc_en_high) &&
	    !wrmsr_safe(MSR_IA32_MISC_ENABLE,
			header->pmode_misc_en_low,
			header->pmode_misc_en_high))
		header->pmode_behavior |=
			(1 << WAKEUP_BEHAVIOR_RESTORE_MISC_ENABLE);
	header->realmode_flags = acpi_realmode_flags;
	header->real_magic = 0x12345678;

#ifndef CONFIG_64BIT
	header->pmode_entry = (u32)&wakeup_pmode_return;
	header->pmode_cr3 = (u32)(swsusp_pg_dir - __PAGE_OFFSET);
	saved_magic = 0x12345678;
#else /* CONFIG_64BIT */
	header->trampoline_segment = setup_trampoline() >> 4;
#ifdef CONFIG_SMP
	stack_start.sp = temp_stack + 4096;
#endif
	initial_code = (unsigned long)wakeup_long64;
	saved_magic = 0x123456789abcdef0;
#endif /* CONFIG_64BIT */

	return 0;
}

/*
 * acpi_restore_state - undo effects of acpi_save_state_mem
 */
void acpi_restore_state_mem(void)
{
}


/**
 * acpi_reserve_bootmem - do _very_ early ACPI initialisation
 *
 * We allocate a page from the first 1MB of memory for the wakeup
 * routine for when we come back from a sleep state. The
 * runtime allocator allows specification of <16MB pages, but not
 * <1MB pages.
 */
void __init acpi_reserve_bootmem(void)
{
	if ((&wakeup_code_end - &wakeup_code_start) > WAKEUP_SIZE) {
		printk(KERN_ERR
		       "ACPI: Wakeup code way too big, S3 disabled.\n");
		return;
	}

	acpi_realmode = (unsigned long)alloc_bootmem_low(WAKEUP_SIZE);

	if (!acpi_realmode) {
		printk(KERN_ERR "ACPI: Cannot allocate lowmem, S3 disabled.\n");
		return;
	}

	acpi_wakeup_address = virt_to_phys((void *)acpi_realmode);
}


	header->pmode_cr3 = (u32)__pa_symbol(initial_page_table);
	saved_magic = 0x12345678;
#else /* CONFIG_64BIT */
#ifdef CONFIG_SMP
	stack_start = (unsigned long)temp_stack + sizeof(temp_stack);
	early_gdt_descr.address =
			(unsigned long)get_cpu_gdt_table(smp_processor_id());
	initial_gs = per_cpu_offset(smp_processor_id());
#endif
	initial_code = (unsigned long)wakeup_long64;
       saved_magic = 0x123456789abcdef0L;
#endif /* CONFIG_64BIT */

	/*
	 * Pause/unpause graph tracing around do_suspend_lowlevel as it has
	 * inconsistent call/return info after it jumps to the wakeup vector.
	 */
	pause_graph_tracing();
	do_suspend_lowlevel();
	unpause_graph_tracing();
	return 0;
}

static int __init acpi_sleep_setup(char *str)
{
	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "s3_bios", 7) == 0)
			acpi_realmode_flags |= 1;
		if (strncmp(str, "s3_mode", 7) == 0)
			acpi_realmode_flags |= 2;
		if (strncmp(str, "s3_beep", 7) == 0)
			acpi_realmode_flags |= 4;
#ifdef CONFIG_HIBERNATION
		if (strncmp(str, "s4_nohwsig", 10) == 0)
			acpi_no_s4_hw_signature();
#endif
		if (strncmp(str, "nonvs", 5) == 0)
			acpi_nvs_nosave();
		if (strncmp(str, "nonvs_s3", 8) == 0)
			acpi_nvs_nosave_s3();
		if (strncmp(str, "old_ordering", 12) == 0)
			acpi_old_suspend_ordering();
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi_sleep=", acpi_sleep_setup);
