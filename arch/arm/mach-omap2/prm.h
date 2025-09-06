#ifndef __ARCH_ARM_MACH_OMAP2_PRM_H
#define __ARCH_ARM_MACH_OMAP2_PRM_H

/*
 * OMAP2/3 Power/Reset Management (PRM) register definitions
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
/*
 * OMAP2/3/4 Power/Reset Management (PRM) bitfield definitions
 *
 * Copyright (C) 2007-2009, 2012 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "prcm-common.h"

#ifndef __ASSEMBLER__
#define OMAP_PRM_REGADDR(module, reg)					\
	(void __iomem *)IO_ADDRESS(OMAP2_PRM_BASE + (module) + (reg))
#else
#define OMAP2420_PRM_REGADDR(module, reg)				\
			IO_ADDRESS(OMAP2420_PRM_BASE + (module) + (reg))
#define OMAP2430_PRM_REGADDR(module, reg)				\
			IO_ADDRESS(OMAP2430_PRM_BASE + (module) + (reg))
#define OMAP34XX_PRM_REGADDR(module, reg)				\
			IO_ADDRESS(OMAP3430_PRM_BASE + (module) + (reg))
#endif

/*
 * Architecture-specific global PRM registers
 * Use __raw_{read,write}l() with these registers.
 *
 * With a few exceptions, these are the register names beginning with
 * PRCM_* on 24xx, and PRM_* on 34xx.  (The exceptions are the
 * IRQSTATUS and IRQENABLE bits.)
 *
 */

/* Global 24xx registers in GR_MOD (Same as OCP_MOD for 24xx) */
#define OMAP24XX_PRCM_VOLTCTRL_OFFSET		0x0050
#define OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET	0x0080

/* 242x GR_MOD registers, use these only for assembly code */
#define OMAP242X_PRCM_VOLTCTRL		OMAP2420_PRM_REGADDR(OMAP24XX_GR_MOD,	\
						OMAP24XX_PRCM_VOLTCTRL_OFFSET)
#define OMAP242X_PRCM_CLKCFG_CTRL	OMAP2420_PRM_REGADDR(OMAP24XX_GR_MOD,	\
						OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET)

/* 243x GR_MOD registers, use these only for assembly code */
#define OMAP243X_PRCM_VOLTCTRL		OMAP2430_PRM_REGADDR(OMAP24XX_GR_MOD,	\
						OMAP24XX_PRCM_VOLTCTRL_OFFSET)
#define OMAP243X_PRCM_CLKCFG_CTRL	OMAP2430_PRM_REGADDR(OMAP24XX_GR_MOD,	\
						OMAP24XX_PRCM_CLKCFG_CTRL_OFFSET)

/* These will disappear */
#define OMAP24XX_PRCM_REVISION		OMAP_PRM_REGADDR(OCP_MOD, 0x0000)
#define OMAP24XX_PRCM_SYSCONFIG		OMAP_PRM_REGADDR(OCP_MOD, 0x0010)

#define OMAP24XX_PRCM_IRQSTATUS_MPU	OMAP_PRM_REGADDR(OCP_MOD, 0x0018)
#define OMAP24XX_PRCM_IRQENABLE_MPU	OMAP_PRM_REGADDR(OCP_MOD, 0x001c)

#define OMAP24XX_PRCM_VOLTST		OMAP_PRM_REGADDR(OCP_MOD, 0x0054)
#define OMAP24XX_PRCM_CLKSRC_CTRL	OMAP_PRM_REGADDR(OCP_MOD, 0x0060)
#define OMAP24XX_PRCM_CLKOUT_CTRL	OMAP_PRM_REGADDR(OCP_MOD, 0x0070)
#define OMAP24XX_PRCM_CLKEMUL_CTRL	OMAP_PRM_REGADDR(OCP_MOD, 0x0078)
#define OMAP24XX_PRCM_CLKCFG_CTRL	OMAP_PRM_REGADDR(OCP_MOD, 0x0080)
#define OMAP24XX_PRCM_CLKCFG_STATUS	OMAP_PRM_REGADDR(OCP_MOD, 0x0084)
#define OMAP24XX_PRCM_VOLTSETUP		OMAP_PRM_REGADDR(OCP_MOD, 0x0090)
#define OMAP24XX_PRCM_CLKSSETUP		OMAP_PRM_REGADDR(OCP_MOD, 0x0094)
#define OMAP24XX_PRCM_POLCTRL		OMAP_PRM_REGADDR(OCP_MOD, 0x0098)

#define OMAP3430_PRM_REVISION		OMAP_PRM_REGADDR(OCP_MOD, 0x0004)
#define OMAP3430_PRM_SYSCONFIG		OMAP_PRM_REGADDR(OCP_MOD, 0x0014)

#define OMAP3430_PRM_IRQSTATUS_MPU	OMAP_PRM_REGADDR(OCP_MOD, 0x0018)
#define OMAP3430_PRM_IRQENABLE_MPU	OMAP_PRM_REGADDR(OCP_MOD, 0x001c)


#define OMAP3430_PRM_VC_SMPS_SA		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0020)
#define OMAP3430_PRM_VC_SMPS_VOL_RA	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0024)
#define OMAP3430_PRM_VC_SMPS_CMD_RA	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0028)
#define OMAP3430_PRM_VC_CMD_VAL_0	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x002c)
#define OMAP3430_PRM_VC_CMD_VAL_1	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0030)
#define OMAP3430_PRM_VC_CH_CONF		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0034)
#define OMAP3430_PRM_VC_I2C_CFG		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0038)
#define OMAP3430_PRM_VC_BYPASS_VAL	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x003c)
#define OMAP3430_PRM_RSTCTRL		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0050)
#define OMAP3430_PRM_RSTTIME		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0054)
#define OMAP3430_PRM_RSTST		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0058)
#define OMAP3430_PRM_VOLTCTRL		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0060)
#define OMAP3430_PRM_SRAM_PCHARGE	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0064)
#define OMAP3430_PRM_CLKSRC_CTRL	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0070)
#define OMAP3430_PRM_VOLTSETUP1		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0090)
#define OMAP3430_PRM_VOLTOFFSET		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0094)
#define OMAP3430_PRM_CLKSETUP		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x0098)
#define OMAP3430_PRM_POLCTRL		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x009c)
#define OMAP3430_PRM_VOLTSETUP2		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00a0)
#define OMAP3430_PRM_VP1_CONFIG		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00b0)
#define OMAP3430_PRM_VP1_VSTEPMIN	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00b4)
#define OMAP3430_PRM_VP1_VSTEPMAX	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00b8)
#define OMAP3430_PRM_VP1_VLIMITTO	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00bc)
#define OMAP3430_PRM_VP1_VOLTAGE	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00c0)
#define OMAP3430_PRM_VP1_STATUS		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00c4)
#define OMAP3430_PRM_VP2_CONFIG		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00d0)
#define OMAP3430_PRM_VP2_VSTEPMIN	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00d4)
#define OMAP3430_PRM_VP2_VSTEPMAX	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00d8)
#define OMAP3430_PRM_VP2_VLIMITTO	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00dc)
#define OMAP3430_PRM_VP2_VOLTAGE	OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00e0)
#define OMAP3430_PRM_VP2_STATUS		OMAP_PRM_REGADDR(OMAP3430_GR_MOD, 0x00e4)

#define OMAP3430_PRM_CLKSEL		OMAP_PRM_REGADDR(OMAP3430_CCR_MOD, 0x0040)
#define OMAP3430_PRM_CLKOUT_CTRL	OMAP_PRM_REGADDR(OMAP3430_CCR_MOD, 0x0070)

/*
 * Module specific PRM registers from PRM_BASE + domain offset
 *
 * Use prm_{read,write}_mod_reg() with these registers.
 *
 * With a few exceptions, these are the register names beginning with
 * {PM,RM}_* on both architectures.  (The exceptions are the IRQSTATUS
 * and IRQENABLE bits.)
 *
 */

/* Registers appearing on both 24xx and 34xx */

#define RM_RSTCTRL					0x0050
#define RM_RSTTIME					0x0054
#define RM_RSTST					0x0058

#define PM_WKEN						0x00a0
#define PM_WKEN1					PM_WKEN
#define PM_WKST						0x00b0
#define PM_WKST1					PM_WKST
#define PM_WKDEP					0x00c8
#define PM_EVGENCTRL					0x00d4
#define PM_EVGENONTIM					0x00d8
#define PM_EVGENOFFTIM					0x00dc
#define PM_PWSTCTRL					0x00e0
#define PM_PWSTST					0x00e4

#define OMAP3430_PM_MPUGRPSEL				0x00a4
#define OMAP3430_PM_MPUGRPSEL1				OMAP3430_PM_MPUGRPSEL

#define OMAP3430_PM_IVAGRPSEL				0x00a8
#define OMAP3430_PM_IVAGRPSEL1				OMAP3430_PM_IVAGRPSEL

#define OMAP3430_PM_PREPWSTST				0x00e8

#define OMAP3430_PRM_IRQSTATUS_IVA2			0x00f8
#define OMAP3430_PRM_IRQENABLE_IVA2			0x00fc


/* Architecture-specific registers */

#define OMAP24XX_PM_WKEN2				0x00a4
#define OMAP24XX_PM_WKST2				0x00b4

#define OMAP24XX_PRCM_IRQSTATUS_DSP			0x00f0	/* IVA mod */
#define OMAP24XX_PRCM_IRQENABLE_DSP			0x00f4	/* IVA mod */
#define OMAP24XX_PRCM_IRQSTATUS_IVA			0x00f8
#define OMAP24XX_PRCM_IRQENABLE_IVA			0x00fc

#ifndef __ASSEMBLER__

/* Power/reset management domain register get/set */
extern u32 prm_read_mod_reg(s16 module, u16 idx);
extern void prm_write_mod_reg(u32 val, s16 module, u16 idx);
extern u32 prm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx);

/* Read-modify-write bits in a PRM register (by domain) */
static inline u32 prm_set_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return prm_rmw_mod_reg_bits(bits, bits, module, idx);
}

static inline u32 prm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return prm_rmw_mod_reg_bits(bits, 0x0, module, idx);
}

#endif

/*
 * Bits common to specific registers
 *
 * The 3430 register and bit names are generally used,
 * since they tend to make more sense
 */

/* PM_EVGENONTIM_MPU */
/* Named PM_EVEGENONTIM_MPU on the 24XX */
#define OMAP_ONTIMEVAL_SHIFT				0
#define OMAP_ONTIMEVAL_MASK				(0xffffffff << 0)

/* PM_EVGENOFFTIM_MPU */
/* Named PM_EVEGENOFFTIM_MPU on the 24XX */
#define OMAP_OFFTIMEVAL_SHIFT				0
#define OMAP_OFFTIMEVAL_MASK				(0xffffffff << 0)

/* PRM_CLKSETUP and PRCM_VOLTSETUP */
/* Named PRCM_CLKSSETUP on the 24XX */
#define OMAP_SETUP_TIME_SHIFT				0
#define OMAP_SETUP_TIME_MASK				(0xffff << 0)

/* PRM_CLKSRC_CTRL */
/* Named PRCM_CLKSRC_CTRL on the 24XX */
#define OMAP_SYSCLKDIV_SHIFT				6
#define OMAP_SYSCLKDIV_MASK				(0x3 << 6)
#define OMAP_AUTOEXTCLKMODE_SHIFT			3
#define OMAP_AUTOEXTCLKMODE_MASK			(0x3 << 3)
#define OMAP_SYSCLKSEL_SHIFT				0
#define OMAP_SYSCLKSEL_MASK				(0x3 << 0)

/* PM_EVGENCTRL_MPU */
#define OMAP_OFFLOADMODE_SHIFT				3
#define OMAP_OFFLOADMODE_MASK				(0x3 << 3)
#define OMAP_ONLOADMODE_SHIFT				1
#define OMAP_ONLOADMODE_MASK				(0x3 << 1)
#define OMAP_ENABLE					(1 << 0)

/* PRM_RSTTIME */
/* Named RM_RSTTIME_WKUP on the 24xx */
#define OMAP_RSTTIME2_SHIFT				8
#define OMAP_RSTTIME2_MASK				(0x1f << 8)
#define OMAP_RSTTIME1_SHIFT				0
#define OMAP_RSTTIME1_MASK				(0xff << 0)


/* PRM_RSTCTRL */
/* Named RM_RSTCTRL_WKUP on the 24xx */
/* 2420 calls RST_DPLL3 'RST_DPLL' */
#define OMAP_RST_DPLL3					(1 << 2)
#define OMAP_RST_GS					(1 << 1)


/*
 * Bits common to module-shared registers
 *
 * Not all registers of a particular type support all of these bits -
 * check TRM if you are unsure
#ifndef __ARCH_ARM_MACH_OMAP2_PRM_H
#define __ARCH_ARM_MACH_OMAP2_PRM_H

#include "prcm-common.h"

# ifndef __ASSEMBLER__
extern void __iomem *prm_base;
extern u16 prm_features;
extern void omap2_set_globals_prm(void __iomem *prm);
int omap_prcm_init(void);
int omap2_prm_base_init(void);
int omap2_prcm_base_init(void);
# endif

/*
 * prm_features flag values
 *
 * PRM_HAS_IO_WAKEUP: has IO wakeup capability
 * PRM_HAS_VOLTAGE: has voltage domains
 * PRM_IRQ_DEFAULT: use default irq number for PRM irq
 */
#define PRM_HAS_IO_WAKEUP	BIT(0)
#define PRM_HAS_VOLTAGE		BIT(1)
#define PRM_IRQ_DEFAULT		BIT(2)

/*
 * MAX_MODULE_SOFTRESET_WAIT: Maximum microseconds to wait for OMAP
 * module to softreset
 */
#define MAX_MODULE_SOFTRESET_WAIT		10000

/*
 * MAX_MODULE_HARDRESET_WAIT: Maximum microseconds to wait for an OMAP
 * submodule to exit hardreset
 */
#define MAX_MODULE_HARDRESET_WAIT		10000

/*
 * Register bitfields
 */

/*
 * 24XX: PM_PWSTST_CORE, PM_PWSTST_GFX, PM_PWSTST_MPU, PM_PWSTST_DSP
 *
 * 2430: PM_PWSTST_MDM
 *
 * 3430: PM_PWSTST_IVA2, PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_GFX,
 *	 PM_PWSTST_DSS, PM_PWSTST_CAM, PM_PWSTST_PER, PM_PWSTST_EMU,
 *	 PM_PWSTST_NEON
 */
#define OMAP_INTRANSITION				(1 << 20)
#define OMAP_INTRANSITION_MASK				(1 << 20)


/*
 * 24XX: PM_PWSTST_GFX, PM_PWSTST_DSP
 *
 * 2430: PM_PWSTST_MDM
 *
 * 3430: PM_PWSTST_IVA2, PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_GFX,
 *	 PM_PWSTST_DSS, PM_PWSTST_CAM, PM_PWSTST_PER, PM_PWSTST_EMU,
 *	 PM_PWSTST_NEON
 */
#define OMAP_POWERSTATEST_SHIFT				0
#define OMAP_POWERSTATEST_MASK				(0x3 << 0)

/*
 * 24XX: RM_RSTST_MPU and RM_RSTST_DSP - on 24XX, 'COREDOMAINWKUP_RST' is
 *	 called 'COREWKUP_RST'
 *
 * 3430: RM_RSTST_IVA2, RM_RSTST_MPU, RM_RSTST_GFX, RM_RSTST_DSS,
 *	 RM_RSTST_CAM, RM_RSTST_PER, RM_RSTST_NEON
 */
#define OMAP_COREDOMAINWKUP_RST				(1 << 3)

/*
 * 24XX: RM_RSTST_MPU, RM_RSTST_GFX, RM_RSTST_DSP
 *
 * 2430: RM_RSTST_MDM
 *
 * 3430: RM_RSTST_CORE, RM_RSTST_EMU
 */
#define OMAP_DOMAINWKUP_RST				(1 << 2)

/*
 * 24XX: RM_RSTST_MPU, RM_RSTST_WKUP, RM_RSTST_DSP
 *	 On 24XX, 'GLOBALWARM_RST' is called 'GLOBALWMPU_RST'.
 *
 * 2430: RM_RSTST_MDM
 *
 * 3430: RM_RSTST_CORE, RM_RSTST_EMU
 */
#define OMAP_GLOBALWARM_RST				(1 << 1)
#define OMAP_GLOBALCOLD_RST				(1 << 0)

/*
 * 24XX: PM_WKDEP_GFX, PM_WKDEP_MPU, PM_WKDEP_CORE, PM_WKDEP_DSP
 *	 2420 TRM sometimes uses "EN_WAKEUP" instead of "EN_WKUP"
 *
 * 2430: PM_WKDEP_MDM
 *
 * 3430: PM_WKDEP_IVA2, PM_WKDEP_GFX, PM_WKDEP_DSS, PM_WKDEP_CAM,
 *	 PM_WKDEP_PER
 */
#define OMAP_EN_WKUP					(1 << 4)

/*
 * 24XX: PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *	 PM_PWSTCTRL_DSP
 *
 * 2430: PM_PWSTCTRL_MDM
 *
 * 3430: PM_PWSTCTRL_IVA2, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *	 PM_PWSTCTRL_DSS, PM_PWSTCTRL_CAM, PM_PWSTCTRL_PER,
 *	 PM_PWSTCTRL_NEON
 */
#define OMAP_LOGICRETSTATE				(1 << 2)

/*
 * 24XX: PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *       PM_PWSTCTRL_DSP, PM_PWSTST_MPU
 *
 * 2430: PM_PWSTCTRL_MDM shared bits
 *
 * 3430: PM_PWSTCTRL_IVA2, PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE,
 *	 PM_PWSTCTRL_GFX, PM_PWSTCTRL_DSS, PM_PWSTCTRL_CAM, PM_PWSTCTRL_PER,
 *	 PM_PWSTCTRL_NEON shared bits
 */
#define OMAP_POWERSTATE_SHIFT				0
#define OMAP_POWERSTATE_MASK				(0x3 << 0)

/*
 * Standardized OMAP reset source bits
 *
 * To the extent these happen to match the hardware register bit
 * shifts, it's purely coincidental.  Used by omap-wdt.c.
 * OMAP_UNKNOWN_RST_SRC_ID_SHIFT is a special value, used whenever
 * there are any bits remaining in the global PRM_RSTST register that
 * haven't been identified, or when the PRM code for the current SoC
 * doesn't know how to interpret the register.
 */
#define OMAP_GLOBAL_COLD_RST_SRC_ID_SHIFT			0
#define OMAP_GLOBAL_WARM_RST_SRC_ID_SHIFT			1
#define OMAP_SECU_VIOL_RST_SRC_ID_SHIFT				2
#define OMAP_MPU_WD_RST_SRC_ID_SHIFT				3
#define OMAP_SECU_WD_RST_SRC_ID_SHIFT				4
#define OMAP_EXTWARM_RST_SRC_ID_SHIFT				5
#define OMAP_VDD_MPU_VM_RST_SRC_ID_SHIFT			6
#define OMAP_VDD_IVA_VM_RST_SRC_ID_SHIFT			7
#define OMAP_VDD_CORE_VM_RST_SRC_ID_SHIFT			8
#define OMAP_ICEPICK_RST_SRC_ID_SHIFT				9
#define OMAP_ICECRUSHER_RST_SRC_ID_SHIFT			10
#define OMAP_C2C_RST_SRC_ID_SHIFT				11
#define OMAP_UNKNOWN_RST_SRC_ID_SHIFT				12

#ifndef __ASSEMBLER__

/**
 * struct prm_reset_src_map - map register bitshifts to standard bitshifts
 * @reg_shift: bitshift in the PRM reset source register
 * @std_shift: bitshift equivalent in the standard reset source list
 *
 * The fields are signed because -1 is used as a terminator.
 */
struct prm_reset_src_map {
	s8 reg_shift;
	s8 std_shift;
};

/**
 * struct prm_ll_data - fn ptrs to per-SoC PRM function implementations
 * @read_reset_sources: ptr to the SoC PRM-specific get_reset_source impl
 * @was_any_context_lost_old: ptr to the SoC PRM context loss test fn
 * @clear_context_loss_flags_old: ptr to the SoC PRM context loss flag clear fn
 * @late_init: ptr to the late init function
 * @assert_hardreset: ptr to the SoC PRM hardreset assert impl
 * @deassert_hardreset: ptr to the SoC PRM hardreset deassert impl
 *
 * XXX @was_any_context_lost_old and @clear_context_loss_flags_old are
 * deprecated.
 */
struct prm_ll_data {
	u32 (*read_reset_sources)(void);
	bool (*was_any_context_lost_old)(u8 part, s16 inst, u16 idx);
	void (*clear_context_loss_flags_old)(u8 part, s16 inst, u16 idx);
	int (*late_init)(void);
	int (*assert_hardreset)(u8 shift, u8 part, s16 prm_mod, u16 offset);
	int (*deassert_hardreset)(u8 shift, u8 st_shift, u8 part, s16 prm_mod,
				  u16 offset, u16 st_offset);
	int (*is_hardreset_asserted)(u8 shift, u8 part, s16 prm_mod,
				     u16 offset);
	void (*reset_system)(void);
	int (*clear_mod_irqs)(s16 module, u8 regs, u32 wkst_mask);
	u32 (*vp_check_txdone)(u8 vp_id);
	void (*vp_clear_txdone)(u8 vp_id);
};

extern int prm_register(struct prm_ll_data *pld);
extern int prm_unregister(struct prm_ll_data *pld);

int omap_prm_assert_hardreset(u8 shift, u8 part, s16 prm_mod, u16 offset);
int omap_prm_deassert_hardreset(u8 shift, u8 st_shift, u8 part, s16 prm_mod,
				u16 offset, u16 st_offset);
int omap_prm_is_hardreset_asserted(u8 shift, u8 part, s16 prm_mod, u16 offset);
extern u32 prm_read_reset_sources(void);
extern bool prm_was_any_context_lost_old(u8 part, s16 inst, u16 idx);
extern void prm_clear_context_loss_flags_old(u8 part, s16 inst, u16 idx);
void omap_prm_reset_system(void);

void omap_prm_reconfigure_io_chain(void);
int omap_prm_clear_mod_irqs(s16 module, u8 regs, u32 wkst_mask);

/*
 * Voltage Processor (VP) identifiers
 */
#define OMAP3_VP_VDD_MPU_ID	0
#define OMAP3_VP_VDD_CORE_ID	1
#define OMAP4_VP_VDD_CORE_ID	0
#define OMAP4_VP_VDD_IVA_ID	1
#define OMAP4_VP_VDD_MPU_ID	2

u32 omap_prm_vp_check_txdone(u8 vp_id);
void omap_prm_vp_clear_txdone(u8 vp_id);

#endif


#endif
