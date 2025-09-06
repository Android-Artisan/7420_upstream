/*
 * File:         include/asm-blackfin/cplbinit.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 * Common CPLB definitions for CPLB init
 *
 * Copyright 2006-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_CPLBINIT_H__
#define __ASM_CPLBINIT_H__

#include <asm/blackfin.h>
#include <asm/cplb.h>

#ifdef CONFIG_MPU

#include <asm/cplb-mpu.h>

#else

#define INITIAL_T 0x1
#define SWITCH_T  0x2
#define I_CPLB    0x4
#define D_CPLB    0x8

#define IN_KERNEL 1

enum
{ZERO_P, L1I_MEM, L1D_MEM, SDRAM_KERN , SDRAM_RAM_MTD, SDRAM_DMAZ, RES_MEM, ASYNC_MEM, L2_MEM};

struct cplb_desc {
	u32 start; /* start address */
	u32 end; /* end address */
	u32 psize; /* prefered size if any otherwise 1MB or 4MB*/
	u16 attr;/* attributes */
	u16 i_conf;/* I-CPLB DATA */
	u16 d_conf;/* D-CPLB DATA */
	u16 valid;/* valid */
	const s8 name[30];/* name */
};

struct cplb_tab {
  u_long *tab;
	u16 pos;
	u16 size;
};

extern u_long icplb_table[];
extern u_long dcplb_table[];

/* Till here we are discussing about the static memory management model.
 * However, the operating envoronments commonly define more CPLB
 * descriptors to cover the entire addressable memory than will fit into
 * the available on-chip 16 CPLB MMRs. When this happens, the below table
 * will be used which will hold all the potentially required CPLB descriptors
 *
 * This is how Page descriptor Table is implemented in uClinux/Blackfin.
 */

extern u_long ipdt_table[];
extern u_long dpdt_table[];
#ifdef CONFIG_CPLB_INFO
extern u_long ipdt_swapcount_table[];
extern u_long dpdt_swapcount_table[];
#endif

#endif /* CONFIG_MPU */

extern unsigned long reserved_mem_dcache_on;
extern unsigned long reserved_mem_icache_on;

extern void generate_cpl_tables(void);

#include <linux/threads.h>

#ifdef CONFIG_CPLB_SWITCH_TAB_L1
# define PDT_ATTR __attribute__((l1_data))
#else
# define PDT_ATTR
#endif

struct cplb_entry {
	unsigned long data, addr;
};

struct cplb_boundary {
	unsigned long eaddr; /* End of this region.  */
	unsigned long data; /* CPLB data value.  */
};

extern struct cplb_boundary dcplb_bounds[];
extern struct cplb_boundary icplb_bounds[];
extern int dcplb_nr_bounds, icplb_nr_bounds;

extern struct cplb_entry dcplb_tbl[NR_CPUS][MAX_CPLBS];
extern struct cplb_entry icplb_tbl[NR_CPUS][MAX_CPLBS];
extern int first_switched_icplb;
extern int first_switched_dcplb;

extern int nr_dcplb_miss[], nr_icplb_miss[], nr_icplb_supv_miss[];
extern int nr_dcplb_prot[], nr_cplb_flush[];

#ifdef CONFIG_MPU

extern int first_mask_dcplb;

extern int page_mask_order;
extern int page_mask_nelts;

extern unsigned long *current_rwx_mask[NR_CPUS];

extern void flush_switched_cplbs(unsigned int);
extern void set_mask_dcplbs(unsigned long *, unsigned int);

extern void __noreturn panic_cplb_error(int seqstat, struct pt_regs *);

#endif /* CONFIG_MPU */

extern void bfin_icache_init(struct cplb_entry *icplb_tbl);
extern void bfin_dcache_init(struct cplb_entry *icplb_tbl);

#if defined(CONFIG_BFIN_DCACHE) || defined(CONFIG_BFIN_ICACHE)
extern void generate_cplb_tables_all(void);
extern void generate_cplb_tables_cpu(unsigned int cpu);
#endif
#endif
