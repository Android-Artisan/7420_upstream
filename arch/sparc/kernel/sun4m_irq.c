/*  sun4m_irq.c
 *  arch/sparc/kernel/sun4m_irq.c:
/*
 * sun4m irq support
 *
 *  djhr: Hacked out of irq.c into a CPU dependent version.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@yahoo.com)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/cacheflush.h>

#include "irq.h"

/* On the sun4m, just like the timers, we have both per-cpu and master
 * interrupt registers.
 */

/* These registers are used for sending/receiving irqs from/to
 * different cpu's.
 */
struct sun4m_intreg_percpu {
	unsigned int tbt;        /* Interrupts still pending for this cpu. */

	/* These next two registers are WRITE-ONLY and are only
	 * "on bit" sensitive, "off bits" written have NO affect.
	 */
	unsigned int clear;  /* Clear this cpus irqs here. */
	unsigned int set;    /* Set this cpus irqs here. */
	unsigned char space[PAGE_SIZE - 12];
};

/*
 * djhr
 * Actually the clear and set fields in this struct are misleading..
 * according to the SLAVIO manual (and the same applies for the SEC)
 * the clear field clears bits in the mask which will ENABLE that IRQ
 * the set field sets bits in the mask to DISABLE the IRQ.
 *
 * Also the undirected_xx address in the SLAVIO is defined as
 * RESERVED and write only..
 *
 * DAVEM_NOTE: The SLAVIO only specifies behavior on uniprocessor
 *             sun4m machines, for MP the layout makes more sense.
 */
struct sun4m_intregs {
	struct sun4m_intreg_percpu cpu_intregs[SUN4M_NCPUS];
	unsigned int tbt;                /* IRQ's that are still pending. */
	unsigned int irqs;               /* Master IRQ bits. */

	/* Again, like the above, two these registers are WRITE-ONLY. */
	unsigned int clear;              /* Clear master IRQ's by setting bits here. */
	unsigned int set;                /* Set master IRQ's by setting bits here. */

	/* This register is both READ and WRITE. */
	unsigned int undirected_target;  /* Which cpu gets undirected irqs. */
};

static unsigned long dummy;

struct sun4m_intregs *sun4m_interrupts;
unsigned long *irq_rcvreg = &dummy;

/* Dave Redman (djhr@tadpole.co.uk)
 * The sun4m interrupt registers.
 */
#define SUN4M_INT_ENABLE  	0x80000000
#define SUN4M_INT_E14     	0x00000080
#define SUN4M_INT_E10     	0x00080000

#define SUN4M_HARD_INT(x)	(0x000000001 << (x))
#define SUN4M_SOFT_INT(x)	(0x000010000 << (x))

#define	SUN4M_INT_MASKALL	0x80000000	  /* mask all interrupts */
#define	SUN4M_INT_MODULE_ERR	0x40000000	  /* module error */
#define	SUN4M_INT_M2S_WRITE	0x20000000	  /* write buffer error */
#define	SUN4M_INT_ECC		0x10000000	  /* ecc memory error */
#include <linux/slab.h>

#include <asm/timer.h>
#include <asm/traps.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#include "irq.h"
#include "kernel.h"

/* Sample sun4m IRQ layout:
 *
 * 0x22 - Power
 * 0x24 - ESP SCSI
 * 0x26 - Lance ethernet
 * 0x2b - Floppy
 * 0x2c - Zilog uart
 * 0x32 - SBUS level 0
 * 0x33 - Parallel port, SBUS level 1
 * 0x35 - SBUS level 2
 * 0x37 - SBUS level 3
 * 0x39 - Audio, Graphics card, SBUS level 4
 * 0x3b - SBUS level 5
 * 0x3d - SBUS level 6
 *
 * Each interrupt source has a mask bit in the interrupt registers.
 * When the mask bit is set, this blocks interrupt deliver.  So you
 * clear the bit to enable the interrupt.
 *
 * Interrupts numbered less than 0x10 are software triggered interrupts
 * and unused by Linux.
 *
 * Interrupt level assignment on sun4m:
 *
 *	level		source
 * ------------------------------------------------------------
 *	  1		softint-1
 *	  2		softint-2, VME/SBUS level 1
 *	  3		softint-3, VME/SBUS level 2
 *	  4		softint-4, onboard SCSI
 *	  5		softint-5, VME/SBUS level 3
 *	  6		softint-6, onboard ETHERNET
 *	  7		softint-7, VME/SBUS level 4
 *	  8		softint-8, onboard VIDEO
 *	  9		softint-9, VME/SBUS level 5, Module Interrupt
 *	 10		softint-10, system counter/timer
 *	 11		softint-11, VME/SBUS level 6, Floppy
 *	 12		softint-12, Keyboard/Mouse, Serial
 *	 13		softint-13, VME/SBUS level 7, ISDN Audio
 *	 14		softint-14, per-processor counter/timer
 *	 15		softint-15, Asynchronous Errors (broadcast)
 *
 * Each interrupt source is masked distinctly in the sun4m interrupt
 * registers.  The PIL level alone is therefore ambiguous, since multiple
 * interrupt sources map to a single PIL.
 *
 * This ambiguity is resolved in the 'intr' property for device nodes
 * in the OF device tree.  Each 'intr' property entry is composed of
 * two 32-bit words.  The first word is the IRQ priority value, which
 * is what we're intersted in.  The second word is the IRQ vector, which
 * is unused.
 *
 * The low 4 bits of the IRQ priority indicate the PIL, and the upper
 * 4 bits indicate onboard vs. SBUS leveled vs. VME leveled.  0x20
 * means onboard, 0x30 means SBUS leveled, and 0x40 means VME leveled.
 *
 * For example, an 'intr' IRQ priority value of 0x24 is onboard SCSI
 * whereas a value of 0x33 is SBUS level 2.  Here are some sample
 * 'intr' property IRQ priority values from ss4, ss5, ss10, ss20, and
 * Tadpole S3 GX systems.
 *
 * esp:		0x24	onboard ESP SCSI
 * le:		0x26	onboard Lance ETHERNET
 * p9100:	0x32	SBUS level 1 P9100 video
 * bpp:		0x33	SBUS level 2 BPP parallel port device
 * DBRI:	0x39	SBUS level 5 DBRI ISDN audio
 * SUNW,leo:	0x39	SBUS level 5 LEO video
 * pcmcia:	0x3b	SBUS level 6 PCMCIA controller
 * uctrl:	0x3b	SBUS level 6 UCTRL device
 * modem:	0x3d	SBUS level 7 MODEM
 * zs:		0x2c	onboard keyboard/mouse/serial
 * floppy:	0x2b	onboard Floppy
 * power:	0x22	onboard power device (XXX unknown mask bit XXX)
 */


/* Code in entry.S needs to get at these register mappings.  */
struct sun4m_irq_percpu __iomem *sun4m_irq_percpu[SUN4M_NCPUS];
struct sun4m_irq_global __iomem *sun4m_irq_global;

struct sun4m_handler_data {
	bool    percpu;
	long    mask;
};

/* Dave Redman (djhr@tadpole.co.uk)
 * The sun4m interrupt registers.
 */
#define SUN4M_INT_ENABLE	0x80000000
#define SUN4M_INT_E14		0x00000080
#define SUN4M_INT_E10		0x00080000

#define	SUN4M_INT_MASKALL	0x80000000	  /* mask all interrupts */
#define	SUN4M_INT_MODULE_ERR	0x40000000	  /* module error */
#define	SUN4M_INT_M2S_WRITE_ERR	0x20000000	  /* write buffer error */
#define	SUN4M_INT_ECC_ERR	0x10000000	  /* ecc memory error */
#define	SUN4M_INT_VME_ERR	0x08000000	  /* vme async error */
#define	SUN4M_INT_FLOPPY	0x00400000	  /* floppy disk */
#define	SUN4M_INT_MODULE	0x00200000	  /* module interrupt */
#define	SUN4M_INT_VIDEO		0x00100000	  /* onboard video */
#define	SUN4M_INT_REALTIME	0x00080000	  /* system timer */
#define	SUN4M_INT_SCSI		0x00040000	  /* onboard scsi */
#define	SUN4M_INT_AUDIO		0x00020000	  /* audio/isdn */
#define	SUN4M_INT_ETHERNET	0x00010000	  /* onboard ethernet */
#define	SUN4M_INT_SERIAL	0x00008000	  /* serial ports */
#define	SUN4M_INT_KBDMS		0x00004000	  /* keyboard/mouse */
#define	SUN4M_INT_SBUSBITS	0x00003F80	  /* sbus int bits */
#define	SUN4M_INT_VMEBITS	0x0000007F	  /* vme int bits */

#define	SUN4M_INT_ERROR		(SUN4M_INT_MODULE_ERR |    \
				 SUN4M_INT_M2S_WRITE_ERR | \
				 SUN4M_INT_ECC_ERR |       \
				 SUN4M_INT_VME_ERR)

#define SUN4M_INT_SBUS(x)	(1 << (x+7))
#define SUN4M_INT_VME(x)	(1 << (x))

/* These tables only apply for interrupts greater than 15..
 * 
 * any intr value below 0x10 is considered to be a soft-int
 * this may be useful or it may not.. but that's how I've done it.
 * and it won't clash with what OBP is telling us about devices.
 *
 * take an encoded intr value and lookup if it's valid
 * then get the mask bits that match from irq_mask
 *
 * P3: Translation from irq 0x0d to mask 0x2000 is for MrCoffee.
 */
static unsigned char irq_xlate[32] = {
    /*  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  a,  b,  c,  d,  e,  f */
	0,  0,  0,  0,  1,  0,  2,  0,  3,  0,  4,  5,  6, 14,  0,  7,
	0,  0,  8,  9,  0, 10,  0, 11,  0, 12,  0, 13,  0, 14,  0,  0
};

static unsigned long irq_mask[] = {
	0,						  /* illegal index */
	SUN4M_INT_SCSI,				  	  /*  1 irq 4 */
	SUN4M_INT_ETHERNET,				  /*  2 irq 6 */
	SUN4M_INT_VIDEO,				  /*  3 irq 8 */
	SUN4M_INT_REALTIME,				  /*  4 irq 10 */
	SUN4M_INT_FLOPPY,				  /*  5 irq 11 */
	(SUN4M_INT_SERIAL | SUN4M_INT_KBDMS),	  	  /*  6 irq 12 */
	SUN4M_INT_MODULE_ERR,			  	  /*  7 irq 15 */
	SUN4M_INT_SBUS(0),				  /*  8 irq 2 */
	SUN4M_INT_SBUS(1),				  /*  9 irq 3 */
	SUN4M_INT_SBUS(2),				  /* 10 irq 5 */
	SUN4M_INT_SBUS(3),				  /* 11 irq 7 */
	SUN4M_INT_SBUS(4),				  /* 12 irq 9 */
	SUN4M_INT_SBUS(5),				  /* 13 irq 11 */
	SUN4M_INT_SBUS(6)				  /* 14 irq 13 */
};

static int sun4m_pil_map[] = { 0, 2, 3, 5, 7, 9, 11, 13 };

static unsigned int sun4m_sbint_to_irq(struct sbus_dev *sdev,
				       unsigned int sbint)
{
	if (sbint >= sizeof(sun4m_pil_map)) {
		printk(KERN_ERR "%s: bogus SBINT %d\n", sdev->prom_name, sbint);
		BUG();
	}
	return sun4m_pil_map[sbint] | 0x30;
}

static unsigned long sun4m_get_irqmask(unsigned int irq)
{
	unsigned long mask;
    
	if (irq > 0x20) {
		/* OBIO/SBUS interrupts */
		irq &= 0x1f;
		mask = irq_mask[irq_xlate[irq]];
		if (!mask)
			printk("sun4m_get_irqmask: IRQ%d has no valid mask!\n",irq);
	} else {
		/* Soft Interrupts will come here.
		 * Currently there is no way to trigger them but I'm sure
		 * something could be cooked up.
		 */
		irq &= 0xf;
		mask = SUN4M_SOFT_INT(irq);
	}
	return mask;
}

static void sun4m_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	mask = sun4m_get_irqmask(irq_nr);
	local_irq_save(flags);
	if (irq_nr > 15)
		sun4m_interrupts->set = mask;
	else
		sun4m_interrupts->cpu_intregs[cpu].set = mask;
	local_irq_restore(flags);    
}

static void sun4m_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	/* Dreadful floppy hack. When we use 0x2b instead of
         * 0x0b the system blows (it starts to whistle!).
         * So we continue to use 0x0b. Fixme ASAP. --P3
         */
        if (irq_nr != 0x0b) {
		mask = sun4m_get_irqmask(irq_nr);
		local_irq_save(flags);
		if (irq_nr > 15)
			sun4m_interrupts->clear = mask;
		else
			sun4m_interrupts->cpu_intregs[cpu].clear = mask;
		local_irq_restore(flags);    
	} else {
		local_irq_save(flags);
		sun4m_interrupts->clear = SUN4M_INT_FLOPPY;
/* Interrupt levels used by OBP */
#define	OBP_INT_LEVEL_SOFT	0x10
#define	OBP_INT_LEVEL_ONBOARD	0x20
#define	OBP_INT_LEVEL_SBUS	0x30
#define	OBP_INT_LEVEL_VME	0x40

#define SUN4M_TIMER_IRQ         (OBP_INT_LEVEL_ONBOARD | 10)
#define SUN4M_PROFILE_IRQ       (OBP_INT_LEVEL_ONBOARD | 14)

static unsigned long sun4m_imask[0x50] = {
	/* 0x00 - SMP */
	0,  SUN4M_SOFT_INT(1),
	SUN4M_SOFT_INT(2),  SUN4M_SOFT_INT(3),
	SUN4M_SOFT_INT(4),  SUN4M_SOFT_INT(5),
	SUN4M_SOFT_INT(6),  SUN4M_SOFT_INT(7),
	SUN4M_SOFT_INT(8),  SUN4M_SOFT_INT(9),
	SUN4M_SOFT_INT(10), SUN4M_SOFT_INT(11),
	SUN4M_SOFT_INT(12), SUN4M_SOFT_INT(13),
	SUN4M_SOFT_INT(14), SUN4M_SOFT_INT(15),
	/* 0x10 - soft */
	0,  SUN4M_SOFT_INT(1),
	SUN4M_SOFT_INT(2),  SUN4M_SOFT_INT(3),
	SUN4M_SOFT_INT(4),  SUN4M_SOFT_INT(5),
	SUN4M_SOFT_INT(6),  SUN4M_SOFT_INT(7),
	SUN4M_SOFT_INT(8),  SUN4M_SOFT_INT(9),
	SUN4M_SOFT_INT(10), SUN4M_SOFT_INT(11),
	SUN4M_SOFT_INT(12), SUN4M_SOFT_INT(13),
	SUN4M_SOFT_INT(14), SUN4M_SOFT_INT(15),
	/* 0x20 - onboard */
	0, 0, 0, 0,
	SUN4M_INT_SCSI,  0, SUN4M_INT_ETHERNET, 0,
	SUN4M_INT_VIDEO, SUN4M_INT_MODULE,
	SUN4M_INT_REALTIME, SUN4M_INT_FLOPPY,
	(SUN4M_INT_SERIAL | SUN4M_INT_KBDMS),
	SUN4M_INT_AUDIO, SUN4M_INT_E14, SUN4M_INT_MODULE_ERR,
	/* 0x30 - sbus */
	0, 0, SUN4M_INT_SBUS(0), SUN4M_INT_SBUS(1),
	0, SUN4M_INT_SBUS(2), 0, SUN4M_INT_SBUS(3),
	0, SUN4M_INT_SBUS(4), 0, SUN4M_INT_SBUS(5),
	0, SUN4M_INT_SBUS(6), 0, 0,
	/* 0x40 - vme */
	0, 0, SUN4M_INT_VME(0), SUN4M_INT_VME(1),
	0, SUN4M_INT_VME(2), 0, SUN4M_INT_VME(3),
	0, SUN4M_INT_VME(4), 0, SUN4M_INT_VME(5),
	0, SUN4M_INT_VME(6), 0, 0
};

static void sun4m_mask_irq(struct irq_data *data)
{
	struct sun4m_handler_data *handler_data;
	int cpu = smp_processor_id();

	handler_data = irq_data_get_irq_handler_data(data);
	if (handler_data->mask) {
		unsigned long flags;

		local_irq_save(flags);
		if (handler_data->percpu) {
			sbus_writel(handler_data->mask, &sun4m_irq_percpu[cpu]->set);
		} else {
			sbus_writel(handler_data->mask, &sun4m_irq_global->mask_set);
		}
		local_irq_restore(flags);
	}
}

static unsigned long cpu_pil_to_imask[16] = {
/*0*/	0x00000000,
/*1*/	0x00000000,
/*2*/	SUN4M_INT_SBUS(0) | SUN4M_INT_VME(0),
/*3*/	SUN4M_INT_SBUS(1) | SUN4M_INT_VME(1),
/*4*/	SUN4M_INT_SCSI,
/*5*/	SUN4M_INT_SBUS(2) | SUN4M_INT_VME(2),
/*6*/	SUN4M_INT_ETHERNET,
/*7*/	SUN4M_INT_SBUS(3) | SUN4M_INT_VME(3),
/*8*/	SUN4M_INT_VIDEO,
/*9*/	SUN4M_INT_SBUS(4) | SUN4M_INT_VME(4) | SUN4M_INT_MODULE_ERR,
/*10*/	SUN4M_INT_REALTIME,
/*11*/	SUN4M_INT_SBUS(5) | SUN4M_INT_VME(5) | SUN4M_INT_FLOPPY,
/*12*/	SUN4M_INT_SERIAL | SUN4M_INT_KBDMS,
/*13*/	SUN4M_INT_AUDIO,
/*14*/	SUN4M_INT_E14,
/*15*/	0x00000000
};

/* We assume the caller has disabled local interrupts when these are called,
 * or else very bizarre behavior will result.
 */
static void sun4m_disable_pil_irq(unsigned int pil)
{
	sun4m_interrupts->set = cpu_pil_to_imask[pil];
}

static void sun4m_enable_pil_irq(unsigned int pil)
{
	sun4m_interrupts->clear = cpu_pil_to_imask[pil];
}

#ifdef CONFIG_SMP
static void sun4m_send_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].set = mask;
}

static void sun4m_clear_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].clear = mask;
}

static void sun4m_set_udt(int cpu)
{
	sun4m_interrupts->undirected_target = cpu;
}
#endif

#define OBIO_INTR	0x20
#define TIMER_IRQ  	(OBIO_INTR | 10)
#define PROFILE_IRQ	(OBIO_INTR | 14)

static struct sun4m_timer_regs *sun4m_timers;
unsigned int lvl14_resolution = (((1000000/HZ) + 1) << 10);

static void sun4m_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4m_timers->l10_timer_limit;
}

static void sun4m_clear_profile_irq(int cpu)
{
	volatile unsigned int clear;
    
	clear = sun4m_timers->cpu_timers[cpu].l14_timer_limit;
static void sun4m_unmask_irq(struct irq_data *data)
{
	struct sun4m_handler_data *handler_data;
	int cpu = smp_processor_id();

	handler_data = irq_data_get_irq_handler_data(data);
	if (handler_data->mask) {
		unsigned long flags;

		local_irq_save(flags);
		if (handler_data->percpu) {
			sbus_writel(handler_data->mask, &sun4m_irq_percpu[cpu]->clear);
		} else {
			sbus_writel(handler_data->mask, &sun4m_irq_global->mask_clear);
		}
		local_irq_restore(flags);
	}
}

static unsigned int sun4m_startup_irq(struct irq_data *data)
{
	irq_link(data->irq);
	sun4m_unmask_irq(data);
	return 0;
}

static void sun4m_shutdown_irq(struct irq_data *data)
{
	sun4m_mask_irq(data);
	irq_unlink(data->irq);
}

static struct irq_chip sun4m_irq = {
	.name		= "sun4m",
	.irq_startup	= sun4m_startup_irq,
	.irq_shutdown	= sun4m_shutdown_irq,
	.irq_mask	= sun4m_mask_irq,
	.irq_unmask	= sun4m_unmask_irq,
};


static unsigned int sun4m_build_device_irq(struct platform_device *op,
					   unsigned int real_irq)
{
	struct sun4m_handler_data *handler_data;
	unsigned int irq;
	unsigned int pil;

	if (real_irq >= OBP_INT_LEVEL_VME) {
		prom_printf("Bogus sun4m IRQ %u\n", real_irq);
		prom_halt();
	}
	pil = (real_irq & 0xf);
	irq = irq_alloc(real_irq, pil);

	if (irq == 0)
		goto out;

	handler_data = irq_get_handler_data(irq);
	if (unlikely(handler_data))
		goto out;

	handler_data = kzalloc(sizeof(struct sun4m_handler_data), GFP_ATOMIC);
	if (unlikely(!handler_data)) {
		prom_printf("IRQ: kzalloc(sun4m_handler_data) failed.\n");
		prom_halt();
	}

	handler_data->mask = sun4m_imask[real_irq];
	handler_data->percpu = real_irq < OBP_INT_LEVEL_ONBOARD;
	irq_set_chip_and_handler_name(irq, &sun4m_irq,
	                              handle_level_irq, "level");
	irq_set_handler_data(irq, handler_data);

out:
	return irq;
}

struct sun4m_timer_percpu {
	u32		l14_limit;
	u32		l14_count;
	u32		l14_limit_noclear;
	u32		user_timer_start_stop;
};

static struct sun4m_timer_percpu __iomem *timers_percpu[SUN4M_NCPUS];

struct sun4m_timer_global {
	u32		l10_limit;
	u32		l10_count;
	u32		l10_limit_noclear;
	u32		reserved;
	u32		timer_config;
};

static struct sun4m_timer_global __iomem *timers_global;

static void sun4m_clear_clock_irq(void)
{
	sbus_readl(&timers_global->l10_limit);
}

void sun4m_nmi(struct pt_regs *regs)
{
	unsigned long afsr, afar, si;

	printk(KERN_ERR "Aieee: sun4m NMI received!\n");
	/* XXX HyperSparc hack XXX */
	__asm__ __volatile__("mov 0x500, %%g1\n\t"
			     "lda [%%g1] 0x4, %0\n\t"
			     "mov 0x600, %%g1\n\t"
			     "lda [%%g1] 0x4, %1\n\t" :
			     "=r" (afsr), "=r" (afar));
	printk(KERN_ERR "afsr=%08lx afar=%08lx\n", afsr, afar);
	si = sbus_readl(&sun4m_irq_global->pending);
	printk(KERN_ERR "si=%08lx\n", si);
	if (si & SUN4M_INT_MODULE_ERR)
		printk(KERN_ERR "Module async error\n");
	if (si & SUN4M_INT_M2S_WRITE_ERR)
		printk(KERN_ERR "MBus/SBus async error\n");
	if (si & SUN4M_INT_ECC_ERR)
		printk(KERN_ERR "ECC memory error\n");
	if (si & SUN4M_INT_VME_ERR)
		printk(KERN_ERR "VME async error\n");
	printk(KERN_ERR "you lose buddy boy...\n");
	show_regs(regs);
	prom_halt();
}

void sun4m_unmask_profile_irq(void)
{
	unsigned long flags;

	local_irq_save(flags);
	sbus_writel(sun4m_imask[SUN4M_PROFILE_IRQ], &sun4m_irq_global->mask_clear);
	local_irq_restore(flags);
}

void sun4m_clear_profile_irq(int cpu)
{
	sbus_readl(&timers_percpu[cpu]->l14_limit);
}

static void sun4m_load_profile_irq(int cpu, unsigned int limit)
{
	sun4m_timers->cpu_timers[cpu].l14_timer_limit = limit;
}

static void __init sun4m_init_timers(irq_handler_t counter_fn)
{
	int reg_count, irq, cpu;
	struct linux_prom_registers cnt_regs[PROMREG_MAX];
	int obio_node, cnt_node;
	struct resource r;

	cnt_node = 0;
	if((obio_node =
	    prom_searchsiblings (prom_getchild(prom_root_node), "obio")) == 0 ||
	   (obio_node = prom_getchild (obio_node)) == 0 ||
	   (cnt_node = prom_searchsiblings (obio_node, "counter")) == 0) {
		prom_printf("Cannot find /obio/counter node\n");
		prom_halt();
	}
	reg_count = prom_getproperty(cnt_node, "reg",
				     (void *) cnt_regs, sizeof(cnt_regs));
	reg_count = (reg_count/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to the timer registers. */
	prom_apply_obio_ranges(cnt_regs, reg_count);
    
	cnt_regs[4].phys_addr = cnt_regs[reg_count-1].phys_addr;
	cnt_regs[4].reg_size = cnt_regs[reg_count-1].reg_size;
	cnt_regs[4].which_io = cnt_regs[reg_count-1].which_io;
	for(obio_node = 1; obio_node < 4; obio_node++) {
		cnt_regs[obio_node].phys_addr =
			cnt_regs[obio_node-1].phys_addr + PAGE_SIZE;
		cnt_regs[obio_node].reg_size = cnt_regs[obio_node-1].reg_size;
		cnt_regs[obio_node].which_io = cnt_regs[obio_node-1].which_io;
	}

	memset((char*)&r, 0, sizeof(struct resource));
	/* Map the per-cpu Counter registers. */
	r.flags = cnt_regs[0].which_io;
	r.start = cnt_regs[0].phys_addr;
	sun4m_timers = (struct sun4m_timer_regs *) sbus_ioremap(&r, 0,
	    PAGE_SIZE*SUN4M_NCPUS, "sun4m_cpu_cnt");
	/* Map the system Counter register. */
	/* XXX Here we expect consequent calls to yeld adjusent maps. */
	r.flags = cnt_regs[4].which_io;
	r.start = cnt_regs[4].phys_addr;
	sbus_ioremap(&r, 0, cnt_regs[4].reg_size, "sun4m_sys_cnt");

	sun4m_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4m_timers->l10_cur_count;
	master_l10_limit = &sun4m_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
   
	if (!cpu_find_by_instance(1, NULL, NULL)) {
		for(cpu = 0; cpu < 4; cpu++)
			sun4m_timers->cpu_timers[cpu].l14_timer_limit = 0;
		sun4m_interrupts->set = SUN4M_INT_E14;
	} else {
		sun4m_timers->cpu_timers[0].l14_timer_limit = 0;
	}
#ifdef CONFIG_SMP
	{
		unsigned long flags;
		extern unsigned long lvl14_save[4];
	unsigned int value = limit ? timer_value(limit) : 0;
	sbus_writel(value, &timers_percpu[cpu]->l14_limit);
}

static void __init sun4m_init_timers(void)
{
	struct device_node *dp = of_find_node_by_name(NULL, "counter");
	int i, err, len, num_cpu_timers;
	unsigned int irq;
	const u32 *addr;

	if (!dp) {
		printk(KERN_ERR "sun4m_init_timers: No 'counter' node.\n");
		return;
	}

	addr = of_get_property(dp, "address", &len);
	of_node_put(dp);
	if (!addr) {
		printk(KERN_ERR "sun4m_init_timers: No 'address' prop.\n");
		return;
	}

	num_cpu_timers = (len / sizeof(u32)) - 1;
	for (i = 0; i < num_cpu_timers; i++) {
		timers_percpu[i] = (void __iomem *)
			(unsigned long) addr[i];
	}
	timers_global = (void __iomem *)
		(unsigned long) addr[num_cpu_timers];

	/* Every per-cpu timer works in timer mode */
	sbus_writel(0x00000000, &timers_global->timer_config);

#ifdef CONFIG_SMP
	sparc_config.cs_period = SBUS_CLOCK_RATE * 2;  /* 2 seconds */
	sparc_config.features |= FEAT_L14_ONESHOT;
#else
	sparc_config.cs_period = SBUS_CLOCK_RATE / HZ; /* 1/HZ sec  */
	sparc_config.features |= FEAT_L10_CLOCKEVENT;
#endif
	sparc_config.features |= FEAT_L10_CLOCKSOURCE;
	sbus_writel(timer_value(sparc_config.cs_period),
	            &timers_global->l10_limit);

	master_l10_counter = &timers_global->l10_count;

	irq = sun4m_build_device_irq(NULL, SUN4M_TIMER_IRQ);

	err = request_irq(irq, timer_interrupt, IRQF_TIMER, "timer", NULL);
	if (err) {
		printk(KERN_ERR "sun4m_init_timers: Register IRQ error %d.\n",
			err);
		return;
	}

	for (i = 0; i < num_cpu_timers; i++)
		sbus_writel(0, &timers_percpu[i]->l14_limit);
	if (num_cpu_timers == 4)
		sbus_writel(SUN4M_INT_E14, &sun4m_irq_global->mask_set);

#ifdef CONFIG_SMP
	{
		unsigned long flags;
		struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];

		/* For SMP we use the level 14 ticker, however the bootup code
		 * has copied the firmware's level 14 vector into the boot cpu's
		 * trap table, we must fix this now or we get squashed.
		 */
		local_irq_save(flags);
		trap_table->inst_one = lvl14_save[0];
		trap_table->inst_two = lvl14_save[1];
		trap_table->inst_three = lvl14_save[2];
		trap_table->inst_four = lvl14_save[3];
		local_flush_cache_all();
		local_ops->cache_all();
		local_irq_restore(flags);
	}
#endif
}

void __init sun4m_init_IRQ(void)
{
	int ie_node,i;
	struct linux_prom_registers int_regs[PROMREG_MAX];
	int num_regs;
	struct resource r;
	int mid;
    
	local_irq_disable();
	if((ie_node = prom_searchsiblings(prom_getchild(prom_root_node), "obio")) == 0 ||
	   (ie_node = prom_getchild (ie_node)) == 0 ||
	   (ie_node = prom_searchsiblings (ie_node, "interrupt")) == 0) {
		prom_printf("Cannot find /obio/interrupt node\n");
		prom_halt();
	}
	num_regs = prom_getproperty(ie_node, "reg", (char *) int_regs,
				    sizeof(int_regs));
	num_regs = (num_regs/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to these registers. */
	prom_apply_obio_ranges(int_regs, num_regs);
    
	int_regs[4].phys_addr = int_regs[num_regs-1].phys_addr;
	int_regs[4].reg_size = int_regs[num_regs-1].reg_size;
	int_regs[4].which_io = int_regs[num_regs-1].which_io;
	for(ie_node = 1; ie_node < 4; ie_node++) {
		int_regs[ie_node].phys_addr = int_regs[ie_node-1].phys_addr + PAGE_SIZE;
		int_regs[ie_node].reg_size = int_regs[ie_node-1].reg_size;
		int_regs[ie_node].which_io = int_regs[ie_node-1].which_io;
	}

	memset((char *)&r, 0, sizeof(struct resource));
	/* Map the interrupt registers for all possible cpus. */
	r.flags = int_regs[0].which_io;
	r.start = int_regs[0].phys_addr;
	sun4m_interrupts = (struct sun4m_intregs *) sbus_ioremap(&r, 0,
	    PAGE_SIZE*SUN4M_NCPUS, "interrupts_percpu");

	/* Map the system interrupt control registers. */
	r.flags = int_regs[4].which_io;
	r.start = int_regs[4].phys_addr;
	sbus_ioremap(&r, 0, int_regs[4].reg_size, "interrupts_system");

	sun4m_interrupts->set = ~SUN4M_INT_MASKALL;
	for (i = 0; !cpu_find_by_instance(i, NULL, &mid); i++)
		sun4m_interrupts->cpu_intregs[mid].clear = ~0x17fff;

	if (!cpu_find_by_instance(1, NULL, NULL)) {
		/* system wide interrupts go to cpu 0, this should always
		 * be safe because it is guaranteed to be fitted or OBP doesn't
		 * come up
		 *
		 * Not sure, but writing here on SLAVIO systems may puke
		 * so I don't do it unless there is more than 1 cpu.
		 */
		irq_rcvreg = (unsigned long *)
				&sun4m_interrupts->undirected_target;
		sun4m_interrupts->undirected_target = 0;
	}
	BTFIXUPSET_CALL(sbint_to_irq, sun4m_sbint_to_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_irq, sun4m_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4m_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, sun4m_enable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, sun4m_disable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4m_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, sun4m_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4m_load_profile_irq, BTFIXUPCALL_NORM);
	sparc_init_timers = sun4m_init_timers;
#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4m_send_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4m_clear_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(set_irq_udt, sun4m_set_udt, BTFIXUPCALL_NORM);
#endif
	struct device_node *dp = of_find_node_by_name(NULL, "interrupt");
	int len, i, mid, num_cpu_iregs;
	const u32 *addr;

	if (!dp) {
		printk(KERN_ERR "sun4m_init_IRQ: No 'interrupt' node.\n");
		return;
	}

	addr = of_get_property(dp, "address", &len);
	of_node_put(dp);
	if (!addr) {
		printk(KERN_ERR "sun4m_init_IRQ: No 'address' prop.\n");
		return;
	}

	num_cpu_iregs = (len / sizeof(u32)) - 1;
	for (i = 0; i < num_cpu_iregs; i++) {
		sun4m_irq_percpu[i] = (void __iomem *)
			(unsigned long) addr[i];
	}
	sun4m_irq_global = (void __iomem *)
		(unsigned long) addr[num_cpu_iregs];

	local_irq_disable();

	sbus_writel(~SUN4M_INT_MASKALL, &sun4m_irq_global->mask_set);
	for (i = 0; !cpu_find_by_instance(i, NULL, &mid); i++)
		sbus_writel(~0x17fff, &sun4m_irq_percpu[mid]->clear);

	if (num_cpu_iregs == 4)
		sbus_writel(0, &sun4m_irq_global->interrupt_target);

	sparc_config.init_timers      = sun4m_init_timers;
	sparc_config.build_device_irq = sun4m_build_device_irq;
	sparc_config.clock_rate       = SBUS_CLOCK_RATE;
	sparc_config.clear_clock_irq  = sun4m_clear_clock_irq;
	sparc_config.load_profile_irq = sun4m_load_profile_irq;


	/* Cannot enable interrupts until OBP ticker is disabled. */
}
