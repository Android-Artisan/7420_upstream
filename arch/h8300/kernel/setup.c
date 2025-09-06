/*
 *  linux/arch/h8300/kernel/setup.c
 *
 *  Copyleft  ()) 2000       James D. Schettine {james@telos-systems.com}
 *  Copyright (C) 1999,2000  Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 1998,1999  D. Jeff Dionne <jeff@lineo.ca>
 *  Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 *  Copyright (C) 1995       Hamish Macdonald
 *  Copyright (C) 2000       Lineo Inc. (www.lineo.com) 
 *  Copyright (C) 2001 	     Lineo, Inc. <www.lineo.com>
 *
 *  H8/300 porting Yoshinori Sato <ysato@users.sourceforge.jp>
 *  Copyright (C) 2001-2014 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

/*
 * This file handles the architecture-dependent parts of system setup
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>
#include <linux/memblock.h>
#include <linux/screen_info.h>
#include <linux/clocksource.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

#if defined(__H8300H__)
#define CPU "H8/300H"
#include <asm/regs306x.h>
#endif

#if defined(__H8300S__)
#define CPU "H8S"
#include <asm/regs267x.h>
#endif

#define STUBSIZE 0xc000;

unsigned long rom_length;
unsigned long memory_start;
unsigned long memory_end;

char __initdata command_line[COMMAND_LINE_SIZE];

extern int _stext, _etext, _sdata, _edata, _sbss, _ebss, _end;
extern int _ramstart, _ramend;
extern char _target_name[];
extern void h8300_gpio_init(void);

#if (defined(CONFIG_H8300H_SIM) || defined(CONFIG_H8S_SIM)) \
    && defined(CONFIG_GDB_MAGICPRINT)
/* printk with gdb service */
static void gdb_console_output(struct console *c, const char *msg, unsigned len)
{
	for (; len > 0; len--) {
		asm("mov.w %0,r2\n\t"
                    "jsr @0xc4"::"r"(*msg++):"er2");
	}
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init gdb_console_setup(struct console *co, char *options)
{
	return 0;
}

static const struct console gdb_console = {
	.name		= "gdb_con",
	.write		= gdb_console_output,
	.device		= NULL,
	.setup		= gdb_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};
#endif

void __init setup_arch(char **cmdline_p)
{
	int bootmap_size;

	memory_start = (unsigned long) &_ramstart;

	/* allow for ROMFS on the end of the kernel */
	if (memcmp((void *)memory_start, "-rom1fs-", 8) == 0) {
#if defined(CONFIG_BLK_DEV_INITRD)
		initrd_start = memory_start;
		initrd_end = memory_start += be32_to_cpu(((unsigned long *) (memory_start))[2]);
#else
		memory_start += be32_to_cpu(((unsigned long *) memory_start)[2]);
#endif
	}
	memory_start = PAGE_ALIGN(memory_start);
#if !defined(CONFIG_BLKDEV_RESERVE)
	memory_end = (unsigned long) &_ramend; /* by now the stack is part of the init task */
#if defined(CONFIG_GDB_DEBUG)
	memory_end -= STUBSIZE;
#endif
#else
	if ((memory_end < CONFIG_BLKDEV_RESERVE_ADDRESS) && 
	    (memory_end > CONFIG_BLKDEV_RESERVE_ADDRESS))
	    /* overlap userarea */
	    memory_end = CONFIG_BLKDEV_RESERVE_ADDRESS; 
#endif

	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) 0; 

#if (defined(CONFIG_H8300H_SIM) || defined(CONFIG_H8S_SIM)) && defined(CONFIG_GDB_MAGICPRINT)
	register_console((struct console *)&gdb_console);
#endif

	printk(KERN_INFO "\r\n\nuClinux " CPU "\n");
	printk(KERN_INFO "Target Hardware: %s\n",_target_name);
	printk(KERN_INFO "Flat model support (C) 1998,1999 Kenneth Albanowski, D. Jeff Dionne\n");
	printk(KERN_INFO "H8/300 series support by Yoshinori Sato <ysato@users.sourceforge.jp>\n");

#ifdef DEBUG
	printk(KERN_DEBUG "KERNEL -> TEXT=0x%06x-0x%06x DATA=0x%06x-0x%06x "
		"BSS=0x%06x-0x%06x\n", (int) &_stext, (int) &_etext,
		(int) &_sdata, (int) &_edata,
		(int) &_sbss, (int) &_ebss);
	printk(KERN_DEBUG "KERNEL -> ROMFS=0x%06x-0x%06x MEM=0x%06x-0x%06x "
		"STACK=0x%06x-0x%06x\n",
	       (int) &_ebss, (int) memory_start,
		(int) memory_start, (int) memory_end,
		(int) memory_end, (int) &_ramend);
#endif

#ifdef CONFIG_DEFAULT_CMDLINE
	/* set from default command line */
	if (*command_line == '\0')
		strcpy(command_line,CONFIG_KERNEL_COMMAND);
#endif
	/* Keep a copy of command line */
	*cmdline_p = &command_line[0];
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	boot_command_line[COMMAND_LINE_SIZE-1] = 0;

#ifdef DEBUG
	if (strlen(*cmdline_p)) 
		printk(KERN_DEBUG "Command line: '%s'\n", *cmdline_p);
#endif
#include <asm/sections.h>
#include <asm/page.h>

#if defined(CONFIG_CPU_H8300H)
#define CPU "H8/300H"
#elif defined(CONFIG_CPU_H8S)
#define CPU "H8S"
#else
#define CPU "Unknown"
#endif

unsigned long memory_start;
unsigned long memory_end;
EXPORT_SYMBOL(memory_end);
static unsigned long freq;
extern char __dtb_start[];

#ifdef CONFIG_VT
struct screen_info screen_info;
#endif

char __initdata command_line[COMMAND_LINE_SIZE];

void sim_console_register(void);

void __init h8300_fdt_init(void *fdt, char *bootargs)
{
	if (!fdt)
		fdt = __dtb_start;
	else
		strcpy(command_line, bootargs);

	early_init_dt_scan(fdt);
	memblock_allow_resize();
}

static void __init bootmem_init(void)
{
	int bootmap_size;
	unsigned long ram_start_pfn;
	unsigned long free_ram_start_pfn;
	unsigned long ram_end_pfn;
	struct memblock_region *region;

	memory_end = memory_start = 0;

	/* Find main memory where is the kernel */
	for_each_memblock(memory, region) {
		memory_start = region->base;
		memory_end = region->base + region->size;
	}

	if (!memory_end)
		panic("No memory!");

	ram_start_pfn = PFN_UP(memory_start);
	/* free_ram_start_pfn is first page after kernel */
	free_ram_start_pfn = PFN_UP(__pa(_end));
	ram_end_pfn = PFN_DOWN(memblock_end_of_DRAM());

	max_pfn = ram_end_pfn;

	/*
	 * give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map at the start of memory
	 */
	bootmap_size = init_bootmem_node(
			NODE_DATA(0),
			memory_start >> PAGE_SHIFT, /* map goes here */
			PAGE_OFFSET >> PAGE_SHIFT,	/* 0 on coldfire */
			memory_end >> PAGE_SHIFT);
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 free_ram_start_pfn,
					 0,
					 ram_end_pfn);
	/*
	 * free the usable memory,  we have to make sure we do not free
	 * the bootmem bitmap so we then reserve it after freeing it :-)
	 */
	free_bootmem(memory_start, memory_end - memory_start);
	reserve_bootmem(memory_start, bootmap_size, BOOTMEM_DEFAULT);
	free_bootmem(PFN_PHYS(free_ram_start_pfn),
		     (ram_end_pfn - free_ram_start_pfn) << PAGE_SHIFT);
	reserve_bootmem(PFN_PHYS(free_ram_start_pfn), bootmap_size,
			BOOTMEM_DEFAULT);

	for_each_memblock(reserved, region) {
		reserve_bootmem(region->base, region->size, BOOTMEM_DEFAULT);
	}
}

void __init setup_arch(char **cmdline_p)
{
	unflatten_and_copy_device_tree();

	init_mm.start_code = (unsigned long) _stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) 0;

	pr_notice("\r\n\nuClinux " CPU "\n");
	pr_notice("Flat model support (C) 1998,1999 Kenneth Albanowski, D. Jeff Dionne\n");

	if (*command_line)
		strcpy(boot_command_line, command_line);
	*cmdline_p = boot_command_line;

	parse_early_param();

	bootmem_init();
#if defined(CONFIG_H8300H_SIM) || defined(CONFIG_H8S_SIM)
	sim_console_register();
#endif

	early_platform_driver_probe("earlyprintk", 1, 0);
	/*
	 * get kmalloc into gear
	 */
	paging_init();
	h8300_gpio_init();
#if defined(CONFIG_H8300_AKI3068NET) && defined(CONFIG_IDE)
	{
#define AREABIT(addr) (1 << (((addr) >> 21) & 7))
		/* setup BSC */
		volatile unsigned char *abwcr = (volatile unsigned char *)ABWCR;
		volatile unsigned char *cscr = (volatile unsigned char *)CSCR;
		*abwcr &= ~(AREABIT(CONFIG_H8300_IDE_BASE) | AREABIT(CONFIG_H8300_IDE_ALT));
		*cscr  |= (AREABIT(CONFIG_H8300_IDE_BASE) | AREABIT(CONFIG_H8300_IDE_ALT)) | 0x0f;
	}
#endif
#ifdef DEBUG
	printk(KERN_DEBUG "Done setup_arch\n");
#endif
}

/*
 *	Get CPU information for use by the procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
    char *cpu;
    int mode;
    u_long clockfreq;

    cpu = CPU;
    mode = *(volatile unsigned char *)MDCR & 0x07;

    clockfreq = CONFIG_CPU_CLOCK;

    seq_printf(m,  "CPU:\t\t%s (mode:%d)\n"
		   "Clock:\t\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
	           cpu,mode,
		   clockfreq/1000,clockfreq%1000,
		   (loops_per_jiffy*HZ)/500000,((loops_per_jiffy*HZ)/5000)%100,
		   (loops_per_jiffy*HZ));

    return 0;
	char *cpu;

	cpu = CPU;

	seq_printf(m,  "CPU:\t\t%s\n"
		   "Clock:\t\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu,
		   freq/1000, freq%1000,
		   (loops_per_jiffy*HZ)/500000,
		   ((loops_per_jiffy*HZ)/5000)%100,
		   (loops_per_jiffy*HZ));

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? ((void *) 0x12345678) : NULL;
	return *pos < num_possible_cpus() ?
		((void *) 0x12345678) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

static int __init device_probe(void)
{
	of_platform_populate(NULL, NULL, NULL, NULL);

	return 0;
}

device_initcall(device_probe);

#if defined(CONFIG_CPU_H8300H)
#define get_wait(base, addr) ({		\
	int baddr;			\
	baddr = ((addr) / 0x200000 * 2);			     \
	w *= (ctrl_inw((unsigned long)(base) + 2) & (3 << baddr)) + 1;	\
	})
#endif
#if defined(CONFIG_CPU_H8S)
#define get_wait(base, addr) ({		\
	int baddr;			\
	baddr = ((addr) / 0x200000 * 16);			     \
	w *= (ctrl_inl((unsigned long)(base) + 2) & (7 << baddr)) + 1;	\
	})
#endif

static __init int access_timing(void)
{
	struct device_node *bsc;
	void __iomem *base;
	unsigned long addr = (unsigned long)&__delay;
	int bit = 1 << (addr / 0x200000);
	int w;

	bsc = of_find_compatible_node(NULL, NULL, "renesas,h8300-bsc");
	base = of_iomap(bsc, 0);
	w = (ctrl_inb((unsigned long)base + 0) & bit)?2:1;
	if (ctrl_inb((unsigned long)base + 1) & bit)
		w *= get_wait(base, addr);
	else
		w *= 2;
	return w * 3 / 2;
}

void __init calibrate_delay(void)
{
	struct device_node *cpu;
	int freq;

	cpu = of_find_compatible_node(NULL, NULL, "renesas,h8300");
	of_property_read_s32(cpu, "clock-frequency", &freq);
	loops_per_jiffy = freq / HZ / (access_timing() * 2);
	pr_cont("%lu.%02lu BogoMIPS (lpj=%lu)\n",
		loops_per_jiffy / (500000 / HZ),
		(loops_per_jiffy / (5000 / HZ)) % 100, loops_per_jiffy);
}


void __init time_init(void)
{
	of_clk_init(NULL);
	clocksource_probe();
}
