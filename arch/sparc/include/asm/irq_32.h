/* irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

#include <linux/interrupt.h>

#define NR_IRQS    16

#define irq_canonicalize(irq)	(irq)

/* Allocated number of logical irq numbers.
 * sun4d boxes (ss2000e) should be OK with ~32.
 * Be on the safe side and make room for 64
 */
#define NR_IRQS    64

#include <linux/interrupt.h>

#define irq_canonicalize(irq)	(irq)

void __init init_IRQ(void);
void __init sun4d_init_sbi_irq(void);

#define NO_IRQ		0xffffffff

#endif
