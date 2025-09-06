/*
 * File:         include/asm-blackfin/simple_bf533_dma.h
 * Based on:     none - original work
 * Author:       LG Soft India
 *               Copyright (C) 2004-2005 Analog Devices Inc.
 * Created:      Tue Sep 21 2004
 * Description:  This file contains the major Data structures and constants
 * 		 used for DMA Implementation in BF533
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * dma.h - Blackfin DMA defines/structures/etc...
 *
 * Copyright 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_DMA_H_
#define _BLACKFIN_DMA_H_

#include <asm/io.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <asm/signal.h>

#include <linux/kernel.h>
#include <mach/dma.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <asm/blackfin.h>

#define MAX_DMA_ADDRESS PAGE_OFFSET

/*****************************************************************************
*        Generic DMA  Declarations
*
****************************************************************************/
enum dma_chan_status {
	DMA_CHANNEL_FREE,
	DMA_CHANNEL_REQUESTED,
	DMA_CHANNEL_ENABLED,
};
#include <linux/interrupt.h>
#include <mach/dma.h>
#include <linux/atomic.h>
#include <asm/blackfin.h>
#include <asm/page.h>
#include <asm-generic/dma.h>
#include <asm/bfin_dma.h>

/*-------------------------
 * config reg bits value
 *-------------------------*/
#define DATA_SIZE_8 		0
#define DATA_SIZE_16 		1
#define DATA_SIZE_32 		2

#define DMA_FLOW_STOP 		0
#define DMA_FLOW_AUTO 		1
#define DMA_FLOW_ARRAY 		4
#define DMA_FLOW_SMALL 		6
#define DMA_FLOW_LARGE 		7

#define DIMENSION_LINEAR    0
#define DIMENSION_2D           1

#define DIR_READ     0
#define DIR_WRITE    1

#define INTR_DISABLE   0
#define INTR_ON_BUF    2
#define INTR_ON_ROW    3

#define DMA_NOSYNC_KEEP_DMA_BUF	0
#define DMA_SYNC_RESTART	1

struct dmasg {
	unsigned long next_desc_addr;
	unsigned long start_addr;
	unsigned short cfg;
	unsigned short x_count;
	short x_modify;
	unsigned short y_count;
	short y_modify;
} __attribute__((packed));

struct dma_register {
	unsigned long next_desc_ptr;	/* DMA Next Descriptor Pointer register */
	unsigned long start_addr;	/* DMA Start address  register */

#define DATA_SIZE_8			0
#define DATA_SIZE_16		1
#define DATA_SIZE_32		2
#ifdef CONFIG_BF60x
#define DATA_SIZE_64		3
#endif

#define DMA_FLOW_STOP		0
#define DMA_FLOW_AUTO		1
#ifdef CONFIG_BF60x
#define DMA_FLOW_LIST		4
#define DMA_FLOW_ARRAY		5
#define DMA_FLOW_LIST_DEMAND	6
#define DMA_FLOW_ARRAY_DEMAND	7
#else
#define DMA_FLOW_ARRAY		4
#define DMA_FLOW_SMALL		6
#define DMA_FLOW_LARGE		7
#endif

#define DIMENSION_LINEAR	0
#define DIMENSION_2D		1

#define DIR_READ			0
#define DIR_WRITE			1

#define INTR_DISABLE		0
#ifdef CONFIG_BF60x
#define INTR_ON_PERI			1
#endif
#define INTR_ON_BUF			2
#define INTR_ON_ROW			3

#define DMA_NOSYNC_KEEP_DMA_BUF	0
#define DMA_SYNC_RESTART		1

#ifdef DMA_MMR_SIZE_32
#define DMA_MMR_SIZE_TYPE long
#define DMA_MMR_READ bfin_read32
#define DMA_MMR_WRITE bfin_write32
#else
#define DMA_MMR_SIZE_TYPE short
#define DMA_MMR_READ bfin_read16
#define DMA_MMR_WRITE bfin_write16
#endif

struct dma_desc_array {
	unsigned long start_addr;
	unsigned DMA_MMR_SIZE_TYPE cfg;
	unsigned DMA_MMR_SIZE_TYPE x_count;
	DMA_MMR_SIZE_TYPE x_modify;
} __attribute__((packed));

struct dmasg {
	void *next_desc_addr;
	unsigned long start_addr;
	unsigned DMA_MMR_SIZE_TYPE cfg;
	unsigned DMA_MMR_SIZE_TYPE x_count;
	DMA_MMR_SIZE_TYPE x_modify;
	unsigned DMA_MMR_SIZE_TYPE y_count;
	DMA_MMR_SIZE_TYPE y_modify;
} __attribute__((packed));

struct dma_register {
	void *next_desc_ptr;	/* DMA Next Descriptor Pointer register */
	unsigned long start_addr;	/* DMA Start address  register */
#ifdef CONFIG_BF60x
	unsigned long cfg;	/* DMA Configuration register */

	unsigned long x_count;	/* DMA x_count register */

	long x_modify;	/* DMA x_modify register */

	unsigned long y_count;	/* DMA y_count register */

	long y_modify;	/* DMA y_modify register */

	unsigned long reserved;
	unsigned long reserved2;

	void *curr_desc_ptr;	/* DMA Current Descriptor Pointer
					   register */
	void *prev_desc_ptr;	/* DMA previous initial Descriptor Pointer
					   register */
	unsigned long curr_addr_ptr;	/* DMA Current Address Pointer
						   register */
	unsigned long irq_status;	/* DMA irq status register */

	unsigned long curr_x_count;	/* DMA Current x-count register */

	unsigned long curr_y_count;	/* DMA Current y-count register */

	unsigned long reserved3;

	unsigned long bw_limit_count;	/* DMA band width limit count register */
	unsigned long curr_bw_limit_count;	/* DMA Current band width limit
							count register */
	unsigned long bw_monitor_count;	/* DMA band width limit count register */
	unsigned long curr_bw_monitor_count;	/* DMA Current band width limit
							count register */
#else
	unsigned short cfg;	/* DMA Configuration register */
	unsigned short dummy1;	/* DMA Configuration register */

	unsigned long reserved;

	unsigned short x_count;	/* DMA x_count register */
	unsigned short dummy2;

	short x_modify;	/* DMA x_modify register */
	unsigned short dummy3;

	unsigned short y_count;	/* DMA y_count register */
	unsigned short dummy4;

	short y_modify;	/* DMA y_modify register */
	unsigned short dummy5;

	unsigned long curr_desc_ptr;	/* DMA Current Descriptor Pointer
	void *curr_desc_ptr;	/* DMA Current Descriptor Pointer
					   register */
	unsigned long curr_addr_ptr;	/* DMA Current Address Pointer
						   register */
	unsigned short irq_status;	/* DMA irq status register */
	unsigned short dummy6;

	unsigned short peripheral_map;	/* DMA peripheral map register */
	unsigned short dummy7;

	unsigned short curr_x_count;	/* DMA Current x-count register */
	unsigned short dummy8;

	unsigned long reserved2;

	unsigned short curr_y_count;	/* DMA Current y-count register */
	unsigned short dummy9;

	unsigned long reserved3;

};

typedef irqreturn_t(*dma_interrupt_t) (int irq, void *dev_id);

struct dma_channel {
	struct mutex dmalock;
	char *device_id;
	enum dma_chan_status chan_status;
	struct dma_register *regs;
	struct dmasg *sg;		/* large mode descriptor */
	unsigned int ctrl_num;	/* controller number */
	dma_interrupt_t irq_callback;
	void *data;
	unsigned int dma_enable_flag;
	unsigned int loopback_flag;
#endif

};

struct dma_channel {
	const char *device_id;
	atomic_t chan_status;
	volatile struct dma_register *regs;
	struct dmasg *sg;		/* large mode descriptor */
	unsigned int irq;
	void *data;
#ifdef CONFIG_PM
	unsigned short saved_peripheral_map;
#endif
};

#ifdef CONFIG_PM
int blackfin_dma_suspend(void);
void blackfin_dma_resume(void);
#endif

/*******************************************************************************
*	DMA API's
*******************************************************************************/
/* functions to set register mode */
void set_dma_start_addr(unsigned int channel, unsigned long addr);
void set_dma_next_desc_addr(unsigned int channel, unsigned long addr);
void set_dma_curr_desc_addr(unsigned int channel, unsigned long addr);
void set_dma_x_count(unsigned int channel, unsigned short x_count);
void set_dma_x_modify(unsigned int channel, short x_modify);
void set_dma_y_count(unsigned int channel, unsigned short y_count);
void set_dma_y_modify(unsigned int channel, short y_modify);
void set_dma_config(unsigned int channel, unsigned short config);
unsigned short set_bfin_dma_config(char direction, char flow_mode,
				   char intr_mode, char dma_mode, char width,
				   char syncmode);
void set_dma_curr_addr(unsigned int channel, unsigned long addr);

/* get curr status for polling */
unsigned short get_dma_curr_irqstat(unsigned int channel);
unsigned short get_dma_curr_xcount(unsigned int channel);
unsigned short get_dma_curr_ycount(unsigned int channel);
unsigned long get_dma_next_desc_ptr(unsigned int channel);
unsigned long get_dma_curr_desc_ptr(unsigned int channel);
unsigned long get_dma_curr_addr(unsigned int channel);

/* set large DMA mode descriptor */
void set_dma_sg(unsigned int channel, struct dmasg *sg, int nr_sg);

/* check if current channel is in use */
int dma_channel_active(unsigned int channel);

/* common functions must be called in any mode */
void free_dma(unsigned int channel);
int dma_channel_active(unsigned int channel); /* check if a channel is in use */
void disable_dma(unsigned int channel);
void enable_dma(unsigned int channel);
int request_dma(unsigned int channel, char *device_id);
int set_dma_callback(unsigned int channel, dma_interrupt_t callback,
		     void *data);
void dma_disable_irq(unsigned int channel);
void dma_enable_irq(unsigned int channel);
void clear_dma_irqstat(unsigned int channel);
void *dma_memcpy(void *dest, const void *src, size_t count);
void *safe_dma_memcpy(void *dest, const void *src, size_t count);

extern int channel2irq(unsigned int channel);
extern struct dma_register *dma_io_base_addr[MAX_BLACKFIN_DMA_CHANNEL];
extern struct dma_channel dma_ch[MAX_DMA_CHANNELS];
extern struct dma_register * const dma_io_base_addr[MAX_DMA_CHANNELS];
extern int channel2irq(unsigned int channel);

static inline void set_dma_start_addr(unsigned int channel, unsigned long addr)
{
	dma_ch[channel].regs->start_addr = addr;
}
static inline void set_dma_next_desc_addr(unsigned int channel, void *addr)
{
	dma_ch[channel].regs->next_desc_ptr = addr;
}
static inline void set_dma_curr_desc_addr(unsigned int channel, void *addr)
{
	dma_ch[channel].regs->curr_desc_ptr = addr;
}
static inline void set_dma_x_count(unsigned int channel, unsigned DMA_MMR_SIZE_TYPE x_count)
{
	dma_ch[channel].regs->x_count = x_count;
}
static inline void set_dma_y_count(unsigned int channel, unsigned DMA_MMR_SIZE_TYPE y_count)
{
	dma_ch[channel].regs->y_count = y_count;
}
static inline void set_dma_x_modify(unsigned int channel, DMA_MMR_SIZE_TYPE x_modify)
{
	dma_ch[channel].regs->x_modify = x_modify;
}
static inline void set_dma_y_modify(unsigned int channel, DMA_MMR_SIZE_TYPE y_modify)
{
	dma_ch[channel].regs->y_modify = y_modify;
}
static inline void set_dma_config(unsigned int channel, unsigned DMA_MMR_SIZE_TYPE config)
{
	dma_ch[channel].regs->cfg = config;
}
static inline void set_dma_curr_addr(unsigned int channel, unsigned long addr)
{
	dma_ch[channel].regs->curr_addr_ptr = addr;
}

#ifdef CONFIG_BF60x
static inline unsigned long
set_bfin_dma_config2(char direction, char flow_mode, char intr_mode,
		     char dma_mode, char mem_width, char syncmode, char peri_width)
{
	unsigned long config = 0;

	switch (intr_mode) {
	case INTR_ON_BUF:
		if (dma_mode == DIMENSION_2D)
			config = DI_EN_Y;
		else
			config = DI_EN_X;
		break;
	case INTR_ON_ROW:
		config = DI_EN_X;
		break;
	case INTR_ON_PERI:
		config = DI_EN_P;
		break;
	};

	return config | (direction << 1) | (mem_width << 8) | (dma_mode << 26) |
		(flow_mode << 12) | (syncmode << 2) | (peri_width << 4);
}
#endif

static inline unsigned DMA_MMR_SIZE_TYPE
set_bfin_dma_config(char direction, char flow_mode,
		    char intr_mode, char dma_mode, char mem_width, char syncmode)
{
#ifdef CONFIG_BF60x
	return set_bfin_dma_config2(direction, flow_mode, intr_mode, dma_mode,
		mem_width, syncmode, mem_width);
#else
	return (direction << 1) | (mem_width << 2) | (dma_mode << 4) |
		(intr_mode << 6) | (flow_mode << 12) | (syncmode << 5);
#endif
}

static inline unsigned DMA_MMR_SIZE_TYPE get_dma_curr_irqstat(unsigned int channel)
{
	return dma_ch[channel].regs->irq_status;
}
static inline unsigned DMA_MMR_SIZE_TYPE get_dma_curr_xcount(unsigned int channel)
{
	return dma_ch[channel].regs->curr_x_count;
}
static inline unsigned DMA_MMR_SIZE_TYPE get_dma_curr_ycount(unsigned int channel)
{
	return dma_ch[channel].regs->curr_y_count;
}
static inline void *get_dma_next_desc_ptr(unsigned int channel)
{
	return dma_ch[channel].regs->next_desc_ptr;
}
static inline void *get_dma_curr_desc_ptr(unsigned int channel)
{
	return dma_ch[channel].regs->curr_desc_ptr;
}
static inline unsigned DMA_MMR_SIZE_TYPE get_dma_config(unsigned int channel)
{
	return dma_ch[channel].regs->cfg;
}
static inline unsigned long get_dma_curr_addr(unsigned int channel)
{
	return dma_ch[channel].regs->curr_addr_ptr;
}

static inline void set_dma_sg(unsigned int channel, struct dmasg *sg, int ndsize)
{
	/* Make sure the internal data buffers in the core are drained
	 * so that the DMA descriptors are completely written when the
	 * DMA engine goes to fetch them below.
	 */
	SSYNC();

	dma_ch[channel].regs->next_desc_ptr = sg;
	dma_ch[channel].regs->cfg =
		(dma_ch[channel].regs->cfg & ~NDSIZE) |
		((ndsize << NDSIZE_OFFSET) & NDSIZE);
}

static inline int dma_channel_active(unsigned int channel)
{
	return atomic_read(&dma_ch[channel].chan_status);
}

static inline void disable_dma(unsigned int channel)
{
	dma_ch[channel].regs->cfg &= ~DMAEN;
	SSYNC();
}
static inline void enable_dma(unsigned int channel)
{
	dma_ch[channel].regs->curr_x_count = 0;
	dma_ch[channel].regs->curr_y_count = 0;
	dma_ch[channel].regs->cfg |= DMAEN;
}
int set_dma_callback(unsigned int channel, irq_handler_t callback, void *data);

static inline void dma_disable_irq(unsigned int channel)
{
	disable_irq(dma_ch[channel].irq);
}
static inline void dma_disable_irq_nosync(unsigned int channel)
{
	disable_irq_nosync(dma_ch[channel].irq);
}
static inline void dma_enable_irq(unsigned int channel)
{
	enable_irq(dma_ch[channel].irq);
}
static inline void clear_dma_irqstat(unsigned int channel)
{
	dma_ch[channel].regs->irq_status = DMA_DONE | DMA_ERR | DMA_PIRQ;
}

void *dma_memcpy(void *dest, const void *src, size_t count);
void *dma_memcpy_nocache(void *dest, const void *src, size_t count);
void *safe_dma_memcpy(void *dest, const void *src, size_t count);
void blackfin_dma_early_init(void);
void early_dma_memcpy(void *dest, const void *src, size_t count);
void early_dma_memcpy_done(void);

#endif
