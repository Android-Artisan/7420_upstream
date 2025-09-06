/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_HW_IRQ_H
#define _ASM_POWERPC_HW_IRQ_H

#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

extern void timer_interrupt(struct pt_regs *);
#ifdef CONFIG_PPC64

/*
 * PACA flags in paca->irq_happened.
 *
 * This bits are set when interrupts occur while soft-disabled
 * and allow a proper replay. Additionally, PACA_IRQ_HARD_DIS
 * is set whenever we manually hard disable.
 */
#define PACA_IRQ_HARD_DIS	0x01
#define PACA_IRQ_DBELL		0x02
#define PACA_IRQ_EE		0x04
#define PACA_IRQ_DEC		0x08 /* Or FIT */
#define PACA_IRQ_EE_EDGE	0x10 /* BookE only */
#define PACA_IRQ_HMI		0x20

#endif /* CONFIG_PPC64 */

#ifndef __ASSEMBLY__

extern void __replay_interrupt(unsigned int vector);

extern void timer_interrupt(struct pt_regs *);
extern void performance_monitor_exception(struct pt_regs *regs);
extern void WatchdogException(struct pt_regs *regs);
extern void unknown_exception(struct pt_regs *regs);

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline unsigned long local_get_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (flags)
	: "i" (offsetof(struct paca_struct, soft_enabled)));
static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile(
		"lbz %0,%1(13)"
		: "=r" (flags)
		: "i" (offsetof(struct paca_struct, soft_enabled)));

	return flags;
}

static inline unsigned long raw_local_irq_disable(void)
{
	unsigned long flags, zero;

	__asm__ __volatile__("li %1,0; lbz %0,%2(13); stb %1,%2(13)"
	: "=r" (flags), "=&r" (zero)
	: "i" (offsetof(struct paca_struct, soft_enabled))
	: "memory");
static inline unsigned long arch_local_irq_disable(void)
{
	unsigned long flags, zero;

	asm volatile(
		"li %1,0; lbz %0,%2(13); stb %1,%2(13)"
		: "=r" (flags), "=&r" (zero)
		: "i" (offsetof(struct paca_struct, soft_enabled))
		: "memory");

	return flags;
}

extern void raw_local_irq_restore(unsigned long);
extern void iseries_handle_interrupts(void);

#define raw_local_irq_enable()		raw_local_irq_restore(1)
#define raw_local_save_flags(flags)	((flags) = local_get_flags())
#define raw_local_irq_save(flags)	((flags) = raw_local_irq_disable())

#define raw_irqs_disabled()		(local_get_flags() == 0)
#define raw_irqs_disabled_flags(flags)	((flags) == 0)

#define __hard_irq_enable()	__mtmsrd(mfmsr() | MSR_EE, 1)
#define __hard_irq_disable()	__mtmsrd(mfmsr() & ~MSR_EE, 1)

#define  hard_irq_disable()			\
	do {					\
		__hard_irq_disable();		\
		get_paca()->soft_enabled = 0;	\
		get_paca()->hard_enabled = 0;	\
	} while(0)

static inline int irqs_disabled_flags(unsigned long flags)
extern void arch_local_irq_restore(unsigned long);

static inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(1);
}

static inline unsigned long arch_local_irq_save(void)
{
	return arch_local_irq_disable();
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return flags == 0;
}

#else

#if defined(CONFIG_BOOKE)
#define SET_MSR_EE(x)	mtmsr(x)
#define local_irq_restore(flags)	__asm__ __volatile__("wrtee %0" : : "r" (flags) : "memory")
#else
#define SET_MSR_EE(x)	mtmsr(x)
#define local_irq_restore(flags)	mtmsr(flags)
#endif

static inline void local_irq_disable(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	SET_MSR_EE(msr & ~MSR_EE);
#endif
}

static inline void local_irq_enable(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 1": : :"memory");
#else
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#ifdef CONFIG_PPC_BOOK3E
#define __hard_irq_enable()	asm volatile("wrteei 1" : : : "memory")
#define __hard_irq_disable()	asm volatile("wrteei 0" : : : "memory")
#else
#define __hard_irq_enable()	__mtmsrd(local_paca->kernel_msr | MSR_EE, 1)
#define __hard_irq_disable()	__mtmsrd(local_paca->kernel_msr, 1)
#endif

#define hard_irq_disable()	do {			\
	u8 _was_enabled;				\
	__hard_irq_disable();				\
	_was_enabled = local_paca->soft_enabled;	\
	local_paca->soft_enabled = 0;			\
	local_paca->irq_happened |= PACA_IRQ_HARD_DIS;	\
	if (_was_enabled)				\
		trace_hardirqs_off();			\
} while(0)

static inline bool lazy_irq_pending(void)
{
	return !!(get_paca()->irq_happened & ~PACA_IRQ_HARD_DIS);
}

/*
 * This is called by asynchronous interrupts to conditionally
 * re-enable hard interrupts when soft-disabled after having
 * cleared the source of the interrupt
 */
static inline void may_hard_irq_enable(void)
{
	get_paca()->irq_happened &= ~PACA_IRQ_HARD_DIS;
	if (!(get_paca()->irq_happened & PACA_IRQ_EE))
		__hard_irq_enable();
}

static inline bool arch_irq_disabled_regs(struct pt_regs *regs)
{
	return !regs->softe;
}

extern bool prep_irq_for_idle(void);

#else /* CONFIG_PPC64 */

#define SET_MSR_EE(x)	mtmsr(x)

static inline unsigned long arch_local_save_flags(void)
{
	return mfmsr();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
#if defined(CONFIG_BOOKE)
	asm volatile("wrtee %0" : : "r" (flags) : "memory");
#else
	mtmsr(flags);
#endif
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
#ifdef CONFIG_BOOKE
	asm volatile("wrteei 0" : : : "memory");
#else
	SET_MSR_EE(flags & ~MSR_EE);
#endif
	return flags;
}

static inline void arch_local_irq_disable(void)
{
#ifdef CONFIG_BOOKE
	asm volatile("wrteei 0" : : : "memory");
#else
	arch_local_irq_save();
#endif
}

static inline void arch_local_irq_enable(void)
{
#ifdef CONFIG_BOOKE
	asm volatile("wrteei 1" : : : "memory");
#else
	unsigned long msr = mfmsr();
	SET_MSR_EE(msr | MSR_EE);
#endif
}

static inline void local_irq_save_ptr(unsigned long *flags)
{
	unsigned long msr;
	msr = mfmsr();
	*flags = msr;
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	SET_MSR_EE(msr & ~MSR_EE);
#endif
	__asm__ __volatile__("": : :"memory");
}

#define local_save_flags(flags)	((flags) = mfmsr())
#define local_irq_save(flags)	local_irq_save_ptr(&flags)
#define irqs_disabled()		((mfmsr() & MSR_EE) == 0)

#define hard_irq_enable()	local_irq_enable()
#define hard_irq_disable()	local_irq_disable()

static inline int irqs_disabled_flags(unsigned long flags)
static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & MSR_EE) == 0;
}

#endif /* CONFIG_PPC64 */

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#define hard_irq_disable()		arch_local_irq_disable()

static inline bool arch_irq_disabled_regs(struct pt_regs *regs)
{
	return !(regs->msr & MSR_EE);
}

static inline void may_hard_irq_enable(void) { }

#endif /* CONFIG_PPC64 */

#define ARCH_IRQ_INIT_FLAGS	IRQ_NOREQUEST

/*
 * interrupt-retrigger: should we handle this via lost interrupts and IPIs
 * or should we not care like we do now ? --BenH.
 */
struct hw_interrupt_type;

struct irq_chip;

#endif  /* __ASSEMBLY__ */
#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */
