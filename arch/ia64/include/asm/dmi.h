#ifndef _ASM_DMI_H
#define _ASM_DMI_H 1

#include <asm/io.h>

/* Use normal IO mappings for DMI */
#define dmi_ioremap ioremap
#define dmi_iounmap(x,l) iounmap(x)
#define dmi_alloc(l) kmalloc(l, GFP_ATOMIC)
#include <linux/slab.h>
#include <asm/io.h>

/* Use normal IO mappings for DMI */
#define dmi_early_remap		ioremap
#define dmi_early_unmap(x, l)	iounmap(x)
#define dmi_remap		ioremap
#define dmi_unmap		iounmap
#define dmi_alloc(l)		kzalloc(l, GFP_ATOMIC)

#endif
