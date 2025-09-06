#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/efi.h>
#include <acpi/reboot.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <linux/dmi.h>
#include <linux/sched.h>
#include <linux/tboot.h>
#include <linux/delay.h>
#include <acpi/reboot.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/desc.h>
#include <asm/hpet.h>
#include <asm/pgtable.h>
#include <asm/proto.h>
#include <asm/reboot_fixups.h>
#include <asm/reboot.h>

#ifdef CONFIG_X86_32
# include <linux/dmi.h>
# include <linux/ctype.h>
# include <linux/mc146818rtc.h>
#else
# include <asm/iommu.h>
#endif
#include <asm/pci_x86.h>
#include <asm/virtext.h>
#include <asm/cpu.h>
#include <asm/nmi.h>
#include <asm/smp.h>

#include <linux/ctype.h>
#include <linux/mc146818rtc.h>
#include <asm/realmode.h>
#include <asm/x86_init.h>
#include <asm/efi.h>

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static const struct desc_ptr no_idt = {};
static int reboot_mode;
enum reboot_type reboot_type = BOOT_KBD;
int reboot_force;

#if defined(CONFIG_X86_32) && defined(CONFIG_SMP)
static int reboot_cpu = -1;
#endif

/* reboot=b[ios] | s[mp] | t[riple] | k[bd] | e[fi] [, [w]arm | [c]old]
   warm   Don't set the cold reboot flag
   cold   Set the cold reboot flag
   bios   Reboot by jumping through the BIOS (only for X86_32)
   smp    Reboot by executing reset on BSP or other CPU (only for X86_32)
   triple Force a triple fault (init)
   kbd    Use the keyboard controller. cold reset (default)
   acpi   Use the RESET_REG in the FADT
   efi    Use efi reset_system runtime service
   force  Avoid anything that could hang.
 */
static int __init reboot_setup(char *str)
{
	for (;;) {
		switch (*str) {
		case 'w':
			reboot_mode = 0x1234;
			break;

		case 'c':
			reboot_mode = 0;
			break;

#ifdef CONFIG_X86_32
#ifdef CONFIG_SMP
		case 's':
			if (isdigit(*(str+1))) {
				reboot_cpu = (int) (*(str+1) - '0');
				if (isdigit(*(str+2)))
					reboot_cpu = reboot_cpu*10 + (int)(*(str+2) - '0');
			}
				/* we will leave sorting out the final value
				   when we are ready to reboot, since we might not
				   have set up boot_cpu_id or smp_num_cpu */
			break;
#endif /* CONFIG_SMP */

		case 'b':
#endif
		case 'a':
		case 'k':
		case 't':
		case 'e':
			reboot_type = *str;
			break;

		case 'f':
			reboot_force = 1;
			break;
		}

		str = strchr(str, ',');
		if (str)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);


#ifdef CONFIG_X86_32

/*
 * This is set if we need to go through the 'emergency' path.
 * When machine_emergency_restart() is called, we may be on
 * an inconsistent state and won't be able to do a clean cleanup
 */
static int reboot_emergency;

/* This is set by the PCI code if either type 1 or type 2 PCI is detected */
bool port_cf9_safe = false;

/*
 * Reboot options and system auto-detection code provided by
 * Dell Inc. so their systems "just work". :-)
 */

/*
 * Some machines require the "reboot=b"  commandline option,
 * Some machines require the "reboot=b" or "reboot=k"  commandline options,
 * this quirk makes that automatic.
 */
static int __init set_bios_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_BIOS) {
		reboot_type = BOOT_BIOS;
		printk(KERN_INFO "%s series board detected. Selecting BIOS-method for reboots.\n", d->ident);
		pr_info("%s series board detected. Selecting %s-method for reboots.\n",
			d->ident, "BIOS");
	}
	return 0;
}

static struct dmi_system_id __initdata reboot_dmi_table[] = {
void __noreturn machine_real_restart(unsigned int type)
{
	local_irq_disable();

	/*
	 * Write zero to CMOS register number 0x0f, which the BIOS POST
	 * routine will recognize as telling it to do a proper reboot.  (Well
	 * that's what this book in front of me says -- it may only apply to
	 * the Phoenix BIOS though, it's not clear).  At the same time,
	 * disable NMIs by setting the top bit in the CMOS address register,
	 * as we're about to do peculiar things to the CPU.  I'm not sure if
	 * `outb_p' is needed instead of just `outb'.  Use it to be on the
	 * safe side.  (Yes, CMOS_WRITE does outb_p's. -  Paul G.)
	 */
	spin_lock(&rtc_lock);
	CMOS_WRITE(0x00, 0x8f);
	spin_unlock(&rtc_lock);

	/*
	 * Switch back to the initial page table.
	 */
#ifdef CONFIG_X86_32
	load_cr3(initial_page_table);
#else
	write_cr3(real_mode_header->trampoline_pgd);

	/* Exiting long mode will fail if CR4.PCIDE is set. */
	if (static_cpu_has(X86_FEATURE_PCID))
		cr4_clear_bits(X86_CR4_PCIDE);
#endif

	/* Jump to the identity-mapped low memory code */
#ifdef CONFIG_X86_32
	asm volatile("jmpl *%0" : :
		     "rm" (real_mode_header->machine_real_restart_asm),
		     "a" (type));
#else
	asm volatile("ljmpl *%0" : :
		     "m" (real_mode_header->machine_real_restart_asm),
		     "D" (type));
#endif
	unreachable();
}
#ifdef CONFIG_APM_MODULE
EXPORT_SYMBOL(machine_real_restart);
#endif

/*
 * Some Apple MacBook and MacBookPro's needs reboot=p to be able to reboot
 */
static int __init set_pci_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_CF9_FORCE) {
		reboot_type = BOOT_CF9_FORCE;
		pr_info("%s series board detected. Selecting %s-method for reboots.\n",
			d->ident, "PCI");
	}
	return 0;
}

static int __init set_kbd_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_KBD) {
		reboot_type = BOOT_KBD;
		pr_info("%s series board detected. Selecting %s-method for reboot.\n",
			d->ident, "KBD");
	}
	return 0;
}

/*
 * This is a single dmi_table handling all reboot quirks.
 */
static struct dmi_system_id __initdata reboot_dmi_table[] = {

	/* Acer */
	{	/* Handle reboot issue on Acer Aspire one */
		.callback = set_kbd_reboot,
		.ident = "Acer Aspire One A110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AOA110"),
		},
	},

	/* Apple */
	{	/* Handle problems with rebooting on Apple MacBook5 */
		.callback = set_pci_reboot,
		.ident = "Apple MacBook5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBook5"),
		},
	},
	{	/* Handle problems with rebooting on Apple MacBook6,1 */
		.callback = set_pci_reboot,
		.ident = "Apple MacBook6,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBook6,1"),
		},
	},
	{	/* Handle problems with rebooting on Apple MacBookPro5 */
		.callback = set_pci_reboot,
		.ident = "Apple MacBookPro5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro5"),
		},
	},
	{	/* Handle problems with rebooting on Apple Macmini3,1 */
		.callback = set_pci_reboot,
		.ident = "Apple Macmini3,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini3,1"),
		},
	},
	{	/* Handle problems with rebooting on the iMac9,1. */
		.callback = set_pci_reboot,
		.ident = "Apple iMac9,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "iMac9,1"),
		},
	},
	{	/* Handle problems with rebooting on the iMac10,1. */
		.callback = set_pci_reboot,
		.ident = "Apple iMac10,1",
		.matches = {
		    DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		    DMI_MATCH(DMI_PRODUCT_NAME, "iMac10,1"),
		},
	},

	/* ASRock */
	{	/* Handle problems with rebooting on ASRock Q1900DC-ITX */
		.callback = set_pci_reboot,
		.ident = "ASRock Q1900DC-ITX",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASRock"),
			DMI_MATCH(DMI_BOARD_NAME, "Q1900DC-ITX"),
		},
	},

	/* ASUS */
	{	/* Handle problems with rebooting on ASUS P4S800 */
		.callback = set_bios_reboot,
		.ident = "ASUS P4S800",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "P4S800"),
		},
	},

	/* Certec */
	{       /* Handle problems with rebooting on Certec BPC600 */
		.callback = set_pci_reboot,
		.ident = "Certec BPC600",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Certec"),
			DMI_MATCH(DMI_PRODUCT_NAME, "BPC600"),
		},
	},

	/* Dell */
	{	/* Handle problems with rebooting on Dell DXP061 */
		.callback = set_bios_reboot,
		.ident = "Dell DXP061",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell DXP061"),
		},
	},
	{	/* Handle problems with rebooting on Dell E520's */
		.callback = set_bios_reboot,
		.ident = "Dell E520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell DM061"),
		},
	},
	{	/* Handle problems with rebooting on Dell 1300's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 1300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1300/"),
	{	/* Handle problems with rebooting on the Latitude E5410. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E5410",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E5410"),
		},
	},
	{	/* Handle problems with rebooting on the Latitude E5420. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E5420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E5420"),
		},
	},
	{	/* Handle problems with rebooting on the Latitude E6320. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E6320",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E6320"),
		},
	},
	{	/* Handle problems with rebooting on the Latitude E6420. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E6420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E6420"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 330 with 0KP561 */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 330",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 330"),
			DMI_MATCH(DMI_BOARD_NAME, "0KP561"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 360 with 0T656F */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 360"),
			DMI_MATCH(DMI_BOARD_NAME, "0T656F"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 745's SFF */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 745's DFF */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0MM599"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 745 with 0KW626 */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0KW626"),
		},
	},
	{	/* Handle problems with rebooting on Dell OptiPlex 760 with 0G919G */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 760",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 760"),
			DMI_MATCH(DMI_BOARD_NAME, "0G919G"),
		},
	},
	{	/* Handle problems with rebooting on the OptiPlex 990. */
		.callback = set_pci_reboot,
		.ident = "Dell OptiPlex 990 BIOS A0x",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 990"),
			DMI_MATCH(DMI_BIOS_VERSION, "A0"),
		},
	},
	{	/* Handle problems with rebooting on Dell 300's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 300/"),
		},
	},
	{       /* Handle problems with rebooting on Dell Optiplex 745's SFF*/
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
		},
	},
	{       /* Handle problems with rebooting on Dell Optiplex 745's DFF*/
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0MM599"),
		},
	},
	{       /* Handle problems with rebooting on Dell Optiplex 745 with 0KW626 */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0KW626"),
	{	/* Handle problems with rebooting on Dell 1300's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 1300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1300/"),
		},
	},
	{	/* Handle problems with rebooting on Dell 2400's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 2400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2400"),
		},
	},
	{	/* Handle problems with rebooting on the Dell PowerEdge C6100. */
		.callback = set_pci_reboot,
		.ident = "Dell PowerEdge C6100",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell"),
			DMI_MATCH(DMI_PRODUCT_NAME, "C6100"),
		},
	},
	{	/* Handle problems with rebooting on the Precision M6600. */
		.callback = set_pci_reboot,
		.ident = "Dell Precision M6600",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision M6600"),
		},
	},
	{	/* Handle problems with rebooting on Dell T5400's */
		.callback = set_bios_reboot,
		.ident = "Dell Precision T5400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision WorkStation T5400"),
		},
	},
	{	/* Handle problems with rebooting on Dell T7400's */
		.callback = set_bios_reboot,
		.ident = "Dell Precision T7400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision WorkStation T7400"),
		},
	},
	{	/* Handle problems with rebooting on Dell XPS710 */
		.callback = set_bios_reboot,
		.ident = "Dell XPS710",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell XPS710"),
		},
	},

	/* Hewlett-Packard */
	{	/* Handle problems with rebooting on HP laptops */
		.callback = set_bios_reboot,
		.ident = "HP Compaq Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq"),
		},
	},

	{	/* PCIe Wifi card isn't detected after reboot otherwise */
		.callback = set_pci_reboot,
		.ident = "Zotac ZBOX CI327 nano",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "NA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZBOX-CI327NANO-GS-01"),
		},
	},

	/* Sony */
	{	/* Handle problems with rebooting on Sony VGN-Z540N */
		.callback = set_bios_reboot,
		.ident = "Sony VGN-Z540N",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-Z540N"),
		},
	},

	{ }
};

static int __init reboot_init(void)
{
	dmi_check_system(reboot_dmi_table);
	int rv;

	/*
	 * Only do the DMI check if reboot_type hasn't been overridden
	 * on the command line
	 */
	if (!reboot_default)
		return 0;

	/*
	 * The DMI quirks table takes precedence. If no quirks entry
	 * matches and the ACPI Hardware Reduced bit is set, force EFI
	 * reboot.
	 */
	rv = dmi_check_system(reboot_dmi_table);

	if (!rv && efi_reboot_required())
		reboot_type = BOOT_EFI;

	return 0;
}
core_initcall(reboot_init);

/* The following code and data reboots the machine by switching to real
   mode and jumping to the BIOS reset entry point, as if the CPU has
   really been reset.  The previous version asked the keyboard
   controller to pulse the CPU reset line, which is more thorough, but
   doesn't work with at least one type of 486 motherboard.  It is easy
   to stop this code working; hence the copious comments. */
static const unsigned long long
real_mode_gdt_entries [3] =
{
	0x0000000000000000ULL,	/* Null descriptor */
	0x00009b000000ffffULL,	/* 16-bit real-mode 64k code at 0x00000000 */
	0x000093000100ffffULL	/* 16-bit real-mode 64k data at 0x00000100 */
};

static const struct desc_ptr
real_mode_gdt = { sizeof (real_mode_gdt_entries) - 1, (long)real_mode_gdt_entries },
real_mode_idt = { 0x3ff, 0 };

/* This is 16-bit protected mode code to disable paging and the cache,
   switch to real mode and jump to the BIOS reset code.

   The instruction that switches to real mode by writing to CR0 must be
   followed immediately by a far jump instruction, which set CS to a
   valid value for real mode, and flushes the prefetch queue to avoid
   running instructions that have already been decoded in protected
   mode.

   Clears all the flags except ET, especially PG (paging), PE
   (protected-mode enable) and TS (task switch for coprocessor state
   save).  Flushes the TLB after paging has been disabled.  Sets CD and
   NW, to disable the cache on a 486, and invalidates the cache.  This
   is more like the state of a 486 after reset.  I don't know if
   something else should be done for other chips.

   More could be done here to set up the registers as if a CPU reset had
   occurred; hopefully real BIOSs don't assume much. */
static const unsigned char real_mode_switch [] =
{
	0x66, 0x0f, 0x20, 0xc0,			/*    movl  %cr0,%eax        */
	0x66, 0x83, 0xe0, 0x11,			/*    andl  $0x00000011,%eax */
	0x66, 0x0d, 0x00, 0x00, 0x00, 0x60,	/*    orl   $0x60000000,%eax */
	0x66, 0x0f, 0x22, 0xc0,			/*    movl  %eax,%cr0        */
	0x66, 0x0f, 0x22, 0xd8,			/*    movl  %eax,%cr3        */
	0x66, 0x0f, 0x20, 0xc3,			/*    movl  %cr0,%ebx        */
	0x66, 0x81, 0xe3, 0x00, 0x00, 0x00, 0x60,	/*    andl  $0x60000000,%ebx */
	0x74, 0x02,				/*    jz    f                */
	0x0f, 0x09,				/*    wbinvd                 */
	0x24, 0x10,				/* f: andb  $0x10,al         */
	0x66, 0x0f, 0x22, 0xc0			/*    movl  %eax,%cr0        */
};
static const unsigned char jump_to_bios [] =
{
	0xea, 0x00, 0x00, 0xff, 0xff		/*    ljmp  $0xffff,$0x0000  */
};

/*
 * Switch to real mode and then execute the code
 * specified by the code and length parameters.
 * We assume that length will aways be less that 100!
 */
void machine_real_restart(const unsigned char *code, int length)
{
	local_irq_disable();

	/* Write zero to CMOS register number 0x0f, which the BIOS POST
	   routine will recognize as telling it to do a proper reboot.  (Well
	   that's what this book in front of me says -- it may only apply to
	   the Phoenix BIOS though, it's not clear).  At the same time,
	   disable NMIs by setting the top bit in the CMOS address register,
	   as we're about to do peculiar things to the CPU.  I'm not sure if
	   `outb_p' is needed instead of just `outb'.  Use it to be on the
	   safe side.  (Yes, CMOS_WRITE does outb_p's. -  Paul G.)
	 */
	spin_lock(&rtc_lock);
	CMOS_WRITE(0x00, 0x8f);
	spin_unlock(&rtc_lock);

	/* Remap the kernel at virtual address zero, as well as offset zero
	   from the kernel segment.  This assumes the kernel segment starts at
	   virtual address PAGE_OFFSET. */
	memcpy(swapper_pg_dir, swapper_pg_dir + KERNEL_PGD_BOUNDARY,
		sizeof(swapper_pg_dir [0]) * KERNEL_PGD_PTRS);

	/*
	 * Use `swapper_pg_dir' as our page directory.
	 */
	load_cr3(swapper_pg_dir);

	/* Write 0x1234 to absolute memory location 0x472.  The BIOS reads
	   this on booting to tell it to "Bypass memory test (also warm
	   boot)".  This seems like a fairly standard thing that gets set by
	   REBOOT.COM programs, and the previous reset routine did this
	   too. */
	*((unsigned short *)0x472) = reboot_mode;

	/* For the switch to real mode, copy some code to low memory.  It has
	   to be in the first 64k because it is running in 16-bit mode, and it
	   has to have the same physical and virtual address, because it turns
	   off paging.  Copy it near the end of the first page, out of the way
	   of BIOS variables. */
	memcpy((void *)(0x1000 - sizeof(real_mode_switch) - 100),
		real_mode_switch, sizeof (real_mode_switch));
	memcpy((void *)(0x1000 - 100), code, length);

	/* Set up the IDT for real mode. */
	load_idt(&real_mode_idt);

	/* Set up a GDT from which we can load segment descriptors for real
	   mode.  The GDT is not used in real mode; it is just needed here to
	   prepare the descriptors. */
	load_gdt(&real_mode_gdt);

	/* Load the data segment registers, and thus the descriptors ready for
	   real mode.  The base address of each segment is 0x100, 16 times the
	   selector value being loaded here.  This is so that the segment
	   registers don't have to be reloaded after switching to real mode:
	   the values are consistent for real mode operation already. */
	__asm__ __volatile__ ("movl $0x0010,%%eax\n"
				"\tmovl %%eax,%%ds\n"
				"\tmovl %%eax,%%es\n"
				"\tmovl %%eax,%%fs\n"
				"\tmovl %%eax,%%gs\n"
				"\tmovl %%eax,%%ss" : : : "eax");

	/* Jump to the 16-bit code that we copied earlier.  It disables paging
	   and the cache, switches to real mode, and jumps to the BIOS reset
	   entry point. */
	__asm__ __volatile__ ("ljmp $0x0008,%0"
				:
				: "i" ((void *)(0x1000 - sizeof (real_mode_switch) - 100)));
}
#ifdef CONFIG_APM_MODULE
EXPORT_SYMBOL(machine_real_restart);
#endif

#endif /* CONFIG_X86_32 */

static inline void kb_wait(void)
{
	int i;

	for (i = 0; i < 0x10000; i++) {
		if ((inb(0x64) & 0x02) == 0)
			break;
		udelay(2);
	}
}

static void vmxoff_nmi(int cpu, struct pt_regs *regs)
{
	cpu_emergency_vmxoff();
}

/* Use NMIs as IPIs to tell all CPUs to disable virtualization */
static void emergency_vmx_disable_all(void)
{
	/* Just make sure we won't change CPUs while doing this */
	local_irq_disable();

	/*
	 * Disable VMX on all CPUs before rebooting, otherwise we risk hanging
	 * the machine, because the CPU blocks INIT when it's in VMX root.
	 *
	 * We can't take any locks and we may be on an inconsistent state, so
	 * use NMIs as IPIs to tell the other CPUs to exit VMX root and halt.
	 *
	 * Do the NMI shootdown even if VMX if off on _this_ CPU, as that
	 * doesn't prevent a different CPU from being in VMX root operation.
	 */
	if (cpu_has_vmx()) {
		/* Safely force _this_ CPU out of VMX root operation. */
		__cpu_emergency_vmxoff();

		/* Halt and exit VMX root operation on the other CPUs. */
		nmi_shootdown_cpus(vmxoff_nmi);

	}
}


void __attribute__((weak)) mach_reboot_fixups(void)
{
}

static void native_machine_emergency_restart(void)
{
	int i;

	/* Tell the BIOS if we want cold or warm reboot */
	*((unsigned short *)__va(0x472)) = reboot_mode;
/*
 * To the best of our knowledge Windows compatible x86 hardware expects
 * the following on reboot:
 *
 * 1) If the FADT has the ACPI reboot register flag set, try it
 * 2) If still alive, write to the keyboard controller
 * 3) If still alive, write to the ACPI reboot register again
 * 4) If still alive, write to the keyboard controller again
 * 5) If still alive, call the EFI runtime service to reboot
 * 6) If no EFI runtime service, call the BIOS to do a reboot
 *
 * We default to following the same pattern. We also have
 * two other reboot methods: 'triple fault' and 'PCI', which
 * can be triggered via the reboot= kernel boot option or
 * via quirks.
 *
 * This means that this function can never return, it can misbehave
 * by not rebooting properly and hanging.
 */
static void native_machine_emergency_restart(void)
{
	int i;
	int attempt = 0;
	int orig_reboot_type = reboot_type;
	unsigned short mode;

	if (reboot_emergency)
		emergency_vmx_disable_all();

	tboot_shutdown(TB_SHUTDOWN_REBOOT);

	/* Tell the BIOS if we want cold or warm reboot */
	mode = reboot_mode == REBOOT_WARM ? 0x1234 : 0;
	*((unsigned short *)__va(0x472)) = mode;

	/*
	 * If an EFI capsule has been registered with the firmware then
	 * override the reboot= parameter.
	 */
	if (efi_capsule_pending(NULL)) {
		pr_info("EFI capsule is pending, forcing EFI reboot.\n");
		reboot_type = BOOT_EFI;
	}

	for (;;) {
		/* Could also try the reset bit in the Hammer NB */
		switch (reboot_type) {
		case BOOT_KBD:
			mach_reboot_fixups(); /* for board specific fixups */

			for (i = 0; i < 10; i++) {
				kb_wait();
				udelay(50);
				outb(0xfe, 0x64); /* pulse reset low */
				udelay(50);
			}

		case BOOT_TRIPLE:
			load_idt(&no_idt);
			__asm__ __volatile__("int3");

			reboot_type = BOOT_KBD;
			break;

#ifdef CONFIG_X86_32
		case BOOT_BIOS:
			machine_real_restart(jump_to_bios, sizeof(jump_to_bios));

			reboot_type = BOOT_KBD;
			break;
#endif

		case BOOT_ACPI:
			acpi_reboot();
			reboot_type = BOOT_KBD;
			break;


		case BOOT_EFI:
			if (efi_enabled)
				efi.reset_system(reboot_mode ? EFI_RESET_WARM : EFI_RESET_COLD,
						 EFI_SUCCESS, 0, NULL);

		case BOOT_KBD:
			mach_reboot_fixups(); /* For board specific fixups */

			for (i = 0; i < 10; i++) {
				kb_wait();
				udelay(50);
				outb(0xfe, 0x64); /* Pulse reset low */
				udelay(50);
			}
			if (attempt == 0 && orig_reboot_type == BOOT_ACPI) {
				attempt = 1;
				reboot_type = BOOT_ACPI;
			} else {
				reboot_type = BOOT_EFI;
			}
			break;

		case BOOT_EFI:
			efi_reboot(reboot_mode, NULL);
			reboot_type = BOOT_BIOS;
			break;

		case BOOT_BIOS:
			machine_real_restart(MRR_BIOS);

			/* We're probably dead after this, but... */
			reboot_type = BOOT_CF9_SAFE;
			break;

		case BOOT_CF9_FORCE:
			port_cf9_safe = true;
			/* Fall through */

		case BOOT_CF9_SAFE:
			if (port_cf9_safe) {
				u8 reboot_code = reboot_mode == REBOOT_WARM ?  0x06 : 0x0E;
				u8 cf9 = inb(0xcf9) & ~reboot_code;
				outb(cf9|2, 0xcf9); /* Request hard reset */
				udelay(50);
				/* Actually do the reset */
				outb(cf9|reboot_code, 0xcf9);
				udelay(50);
			}
			reboot_type = BOOT_TRIPLE;
			break;

		case BOOT_TRIPLE:
			load_idt(&no_idt);
			__asm__ __volatile__("int3");

			/* We're probably dead after this, but... */
			reboot_type = BOOT_KBD;
			break;
		}
	}
}

void native_machine_shutdown(void)
{
	/* Stop the cpus and apics */
#ifdef CONFIG_SMP

	/* The boot cpu is always logical cpu 0 */
	int reboot_cpu_id = 0;

#ifdef CONFIG_X86_32
	/* See if there has been given a command line override */
	if ((reboot_cpu != -1) && (reboot_cpu < NR_CPUS) &&
		cpu_online(reboot_cpu))
		reboot_cpu_id = reboot_cpu;
#endif

	/* Make certain the cpu I'm about to reboot on is online */
	if (!cpu_online(reboot_cpu_id))
		reboot_cpu_id = smp_processor_id();

	/* Make certain I only run on the appropriate processor */
	set_cpus_allowed_ptr(current, &cpumask_of_cpu(reboot_cpu_id));

	/* O.K Now that I'm on the appropriate processor,
	 * stop all of the others.
	 */
	smp_send_stop();
#ifdef CONFIG_X86_IO_APIC
	/*
	 * Disabling IO APIC before local APIC is a workaround for
	 * erratum AVR31 in "Intel Atom Processor C2000 Product Family
	 * Specification Update". In this situation, interrupts that target
	 * a Logical Processor whose Local APIC is either in the process of
	 * being hardware disabled or software disabled are neither delivered
	 * nor discarded. When this erratum occurs, the processor may hang.
	 *
	 * Even without the erratum, it still makes sense to quiet IO APIC
	 * before disabling Local APIC.
	 */
	disable_IO_APIC();
#endif

#ifdef CONFIG_SMP
	/*
	 * Stop all of the others. Also disable the local irq to
	 * not receive the per-cpu timer interrupt which may trigger
	 * scheduler's load balance.
	 */
	local_irq_disable();
	stop_other_cpus();
#endif

	lapic_shutdown();

#ifdef CONFIG_X86_IO_APIC
	disable_IO_APIC();
#endif

#ifdef CONFIG_HPET_TIMER
	hpet_disable();
#endif

#ifdef CONFIG_X86_64
	pci_iommu_shutdown();
#endif
}

static void native_machine_restart(char *__unused)
{
	printk("machine restart\n");

	if (!reboot_force)
		machine_shutdown();
	machine_emergency_restart();
	x86_platform.iommu_shutdown();
#endif
}

static void __machine_emergency_restart(int emergency)
{
	reboot_emergency = emergency;
	machine_ops.emergency_restart();
}

static void native_machine_restart(char *__unused)
{
	pr_notice("machine restart\n");

	if (!reboot_force)
		machine_shutdown();
	__machine_emergency_restart(0);
}

static void native_machine_halt(void)
{
	/* Stop other cpus and apics */
	machine_shutdown();

	tboot_shutdown(TB_SHUTDOWN_HALT);

	stop_this_cpu(NULL);
}

static void native_machine_power_off(void)
{
	if (pm_power_off) {
		if (!reboot_force)
			machine_shutdown();
		pm_power_off();
	}
	/* A fallback in case there is no PM info available */
	tboot_shutdown(TB_SHUTDOWN_HALT);
}

struct machine_ops machine_ops = {
	.power_off = native_machine_power_off,
	.shutdown = native_machine_shutdown,
	.emergency_restart = native_machine_emergency_restart,
	.restart = native_machine_restart,
	.halt = native_machine_halt,
#ifdef CONFIG_KEXEC
#ifdef CONFIG_KEXEC_CORE
	.crash_shutdown = native_machine_crash_shutdown,
#endif
};

void machine_power_off(void)
{
	machine_ops.power_off();
}

void machine_shutdown(void)
{
	machine_ops.shutdown();
}

void machine_emergency_restart(void)
{
	machine_ops.emergency_restart();
	__machine_emergency_restart(1);
}

void machine_restart(char *cmd)
{
	machine_ops.restart(cmd);
}

void machine_halt(void)
{
	machine_ops.halt();
}

#ifdef CONFIG_KEXEC
#ifdef CONFIG_KEXEC_CORE
void machine_crash_shutdown(struct pt_regs *regs)
{
	machine_ops.crash_shutdown(regs);
}
#endif


#if defined(CONFIG_SMP)

/* This keeps a track of which one is crashing cpu. */
static int crashing_cpu;
static nmi_shootdown_cb shootdown_callback;

static atomic_t waiting_for_crash_ipi;
static int crash_ipi_issued;

static int crash_nmi_callback(unsigned int val, struct pt_regs *regs)
{
	int cpu;

	cpu = raw_smp_processor_id();

	/*
	 * Don't do anything if this handler is invoked on crashing cpu.
	 * Otherwise, system will completely hang. Crashing cpu can get
	 * an NMI if system was initially booted with nmi_watchdog parameter.
	 */
	if (cpu == crashing_cpu)
		return NMI_HANDLED;
	local_irq_disable();

	shootdown_callback(cpu, regs);

	atomic_dec(&waiting_for_crash_ipi);
	/* Assume hlt works */
	halt();
	for (;;)
		cpu_relax();

	return NMI_HANDLED;
}

static void smp_send_nmi_allbutself(void)
{
	apic->send_IPI_allbutself(NMI_VECTOR);
}

/*
 * Halt all other CPUs, calling the specified function on each of them
 *
 * This function can be used to halt all other CPUs on crash
 * or emergency reboot time. The function passed as parameter
 * will be called inside a NMI handler on all CPUs.
 */
void nmi_shootdown_cpus(nmi_shootdown_cb callback)
{
	unsigned long msecs;
	local_irq_disable();

	/* Make a note of crashing cpu. Will be used in NMI callback. */
	crashing_cpu = safe_smp_processor_id();

	shootdown_callback = callback;

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	/* Would it be better to replace the trap vector here? */
	if (register_nmi_handler(NMI_LOCAL, crash_nmi_callback,
				 NMI_FLAG_FIRST, "crash"))
		return;		/* Return what? */
	/*
	 * Ensure the new callback function is set before sending
	 * out the NMI
	 */
	wmb();

	smp_send_nmi_allbutself();

	/* Kick CPUs looping in NMI context. */
	WRITE_ONCE(crash_ipi_issued, 1);

	msecs = 1000; /* Wait at most a second for the other cpus to stop */
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && msecs) {
		mdelay(1);
		msecs--;
	}

	/* Leave the nmi callback set */
}

/* Override the weak function in kernel/panic.c */
void nmi_panic_self_stop(struct pt_regs *regs)
{
	while (1) {
		/*
		 * Wait for the crash dumping IPI to be issued, and then
		 * call its callback directly.
		 */
		if (READ_ONCE(crash_ipi_issued))
			crash_nmi_callback(0, regs); /* Don't return */

		cpu_relax();
	}
}

#else /* !CONFIG_SMP */
void nmi_shootdown_cpus(nmi_shootdown_cb callback)
{
	/* No other CPUs to shoot down */
}
#endif
