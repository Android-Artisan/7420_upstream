#ifndef __ARCH_ASM_MACH_OMAP2_CM_H
#define __ARCH_ASM_MACH_OMAP2_CM_H

/*
 * OMAP2/3 Clock Management (CM) register definitions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2008 Nokia Corporation
/*
 * OMAP2+ Clock Management prototypes
 *
 * Copyright (C) 2007-2009, 2012 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "prcm-common.h"

#ifndef __ASSEMBLER__
#define OMAP_CM_REGADDR(module, reg)					\
	(void __iomem *)IO_ADDRESS(OMAP2_CM_BASE + (module) + (reg))
#else
#define OMAP2420_CM_REGADDR(module, reg)				\
			IO_ADDRESS(OMAP2420_CM_BASE + (module) + (reg))
#define OMAP2430_CM_REGADDR(module, reg)				\
			IO_ADDRESS(OMAP2430_CM_BASE + (module) + (reg))
#define OMAP34XX_CM_REGADDR(module, reg)				\
			IO_ADDRESS(OMAP3430_CM_BASE + (module) + (reg))
#endif

/*
 * Architecture-specific global CM registers
 * Use cm_{read,write}_reg() with these registers.
 * These registers appear once per CM module.
 */

#define OMAP3430_CM_REVISION		OMAP_CM_REGADDR(OCP_MOD, 0x0000)
#define OMAP3430_CM_SYSCONFIG		OMAP_CM_REGADDR(OCP_MOD, 0x0010)
#define OMAP3430_CM_POLCTRL		OMAP_CM_REGADDR(OCP_MOD, 0x009c)

#define OMAP3430_CM_CLKOUT_CTRL		OMAP_CM_REGADDR(OMAP3430_CCR_MOD, 0x0070)

/*
 * Module specific CM registers from CM_BASE + domain offset
 * Use cm_{read,write}_mod_reg() with these registers.
 * These register offsets generally appear in more than one PRCM submodule.
 */

/* Common between 24xx and 34xx */

#define CM_FCLKEN					0x0000
#define CM_FCLKEN1					CM_FCLKEN
#define CM_CLKEN					CM_FCLKEN
#define CM_ICLKEN					0x0010
#define CM_ICLKEN1					CM_ICLKEN
#define CM_ICLKEN2					0x0014
#define CM_ICLKEN3					0x0018
#define CM_IDLEST					0x0020
#define CM_IDLEST1					CM_IDLEST
#define CM_IDLEST2					0x0024
#define CM_AUTOIDLE					0x0030
#define CM_AUTOIDLE1					CM_AUTOIDLE
#define CM_AUTOIDLE2					0x0034
#define CM_AUTOIDLE3					0x0038
#define CM_CLKSEL					0x0040
#define CM_CLKSEL1					CM_CLKSEL
#define CM_CLKSEL2					0x0044
#define CM_CLKSTCTRL					0x0048


/* Architecture-specific registers */

#define OMAP24XX_CM_FCLKEN2				0x0004
#define OMAP24XX_CM_ICLKEN4				0x001c
#define OMAP24XX_CM_AUTOIDLE4				0x003c

#define OMAP2430_CM_IDLEST3				0x0028

#define OMAP3430_CM_CLKEN_PLL				0x0004
#define OMAP3430ES2_CM_CLKEN2				0x0004
#define OMAP3430ES2_CM_FCLKEN3				0x0008
#define OMAP3430_CM_IDLEST_PLL				CM_IDLEST2
#define OMAP3430_CM_AUTOIDLE_PLL			CM_AUTOIDLE2
#define OMAP3430ES2_CM_AUTOIDLE2_PLL			CM_AUTOIDLE2
#define OMAP3430_CM_CLKSEL1				CM_CLKSEL
#define OMAP3430_CM_CLKSEL1_PLL				CM_CLKSEL
#define OMAP3430_CM_CLKSEL2_PLL				CM_CLKSEL2
#define OMAP3430_CM_SLEEPDEP				CM_CLKSEL2
#define OMAP3430_CM_CLKSEL3				CM_CLKSTCTRL
#define OMAP3430_CM_CLKSTST				0x004c
#define OMAP3430ES2_CM_CLKSEL4				0x004c
#define OMAP3430ES2_CM_CLKSEL5				0x0050
#define OMAP3430_CM_CLKSEL2_EMU				0x0050
#define OMAP3430_CM_CLKSEL3_EMU				0x0054


/* Clock management domain register get/set */

#ifndef __ASSEMBLER__

extern u32 cm_read_mod_reg(s16 module, u16 idx);
extern void cm_write_mod_reg(u32 val, s16 module, u16 idx);
extern u32 cm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx);

static inline u32 cm_set_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return cm_rmw_mod_reg_bits(bits, bits, module, idx);
}

static inline u32 cm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return cm_rmw_mod_reg_bits(bits, 0x0, module, idx);
}

#endif

/* CM register bits shared between 24XX and 3430 */

/* CM_CLKSEL_GFX */
#define OMAP_CLKSEL_GFX_SHIFT				0
#define OMAP_CLKSEL_GFX_MASK				(0x7 << 0)

/* CM_ICLKEN_GFX */
#define OMAP_EN_GFX_SHIFT				0
#define OMAP_EN_GFX					(1 << 0)

/* CM_IDLEST_GFX */
#define OMAP_ST_GFX					(1 << 0)

#ifndef __ARCH_ASM_MACH_OMAP2_CM_H
#define __ARCH_ASM_MACH_OMAP2_CM_H

/*
 * MAX_MODULE_READY_TIME: max duration in microseconds to wait for the
 * PRCM to request that a module exit the inactive state in the case of
 * OMAP2 & 3.
 * In the case of OMAP4 this is the max duration in microseconds for the
 * module to reach the functionnal state from an inactive state.
 */
#define MAX_MODULE_READY_TIME		2000

# ifndef __ASSEMBLER__
extern void __iomem *cm_base;
extern void __iomem *cm2_base;
extern void omap2_set_globals_cm(void __iomem *cm, void __iomem *cm2);
# endif

/*
 * MAX_MODULE_DISABLE_TIME: max duration in microseconds to wait for
 * the PRCM to request that a module enter the inactive state in the
 * case of OMAP2 & 3.  In the case of OMAP4 this is the max duration
 * in microseconds for the module to reach the inactive state from
 * a functional state.
 * XXX FSUSB on OMAP4430 takes ~4ms to idle after reset during
 * kernel init.
 */
#define MAX_MODULE_DISABLE_TIME		5000

# ifndef __ASSEMBLER__

/**
 * struct cm_ll_data - fn ptrs to per-SoC CM function implementations
 * @split_idlest_reg: ptr to the SoC CM-specific split_idlest_reg impl
 * @wait_module_ready: ptr to the SoC CM-specific wait_module_ready impl
 * @wait_module_idle: ptr to the SoC CM-specific wait_module_idle impl
 * @module_enable: ptr to the SoC CM-specific module_enable impl
 * @module_disable: ptr to the SoC CM-specific module_disable impl
 */
struct cm_ll_data {
	int (*split_idlest_reg)(void __iomem *idlest_reg, s16 *prcm_inst,
				u8 *idlest_reg_id);
	int (*wait_module_ready)(u8 part, s16 prcm_mod, u16 idlest_reg,
				 u8 idlest_shift);
	int (*wait_module_idle)(u8 part, s16 prcm_mod, u16 idlest_reg,
				u8 idlest_shift);
	void (*module_enable)(u8 mode, u8 part, u16 inst, u16 clkctrl_offs);
	void (*module_disable)(u8 part, u16 inst, u16 clkctrl_offs);
};

extern int cm_split_idlest_reg(void __iomem *idlest_reg, s16 *prcm_inst,
			       u8 *idlest_reg_id);
int omap_cm_wait_module_ready(u8 part, s16 prcm_mod, u16 idlest_reg,
			      u8 idlest_shift);
int omap_cm_wait_module_idle(u8 part, s16 prcm_mod, u16 idlest_reg,
			     u8 idlest_shift);
int omap_cm_module_enable(u8 mode, u8 part, u16 inst, u16 clkctrl_offs);
int omap_cm_module_disable(u8 part, u16 inst, u16 clkctrl_offs);
extern int cm_register(struct cm_ll_data *cld);
extern int cm_unregister(struct cm_ll_data *cld);
int omap_cm_init(void);
int omap2_cm_base_init(void);

# endif

#endif
