/*
 * File:         arch/blackfin/kernel/bfin_gpio.c
 * Based on:
 * Author:       Michael Hennerich (hennerich@blackfin.uclinux.org)
 *
 * Created:
 * Description:  GPIO Abstraction Layer
 *
 * Modified:
 *               Copyright 2008 Analog Devices Inc.
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
 */

/*
*  Number     BF537/6/4    BF561    BF533/2/1	   BF549/8/4/2
*
*  GPIO_0       PF0         PF0        PF0	   PA0...PJ13
*  GPIO_1       PF1         PF1        PF1
*  GPIO_2       PF2         PF2        PF2
*  GPIO_3       PF3         PF3        PF3
*  GPIO_4       PF4         PF4        PF4
*  GPIO_5       PF5         PF5        PF5
*  GPIO_6       PF6         PF6        PF6
*  GPIO_7       PF7         PF7        PF7
*  GPIO_8       PF8         PF8        PF8
*  GPIO_9       PF9         PF9        PF9
*  GPIO_10      PF10        PF10       PF10
*  GPIO_11      PF11        PF11       PF11
*  GPIO_12      PF12        PF12       PF12
*  GPIO_13      PF13        PF13       PF13
*  GPIO_14      PF14        PF14       PF14
*  GPIO_15      PF15        PF15       PF15
*  GPIO_16      PG0         PF16
*  GPIO_17      PG1         PF17
*  GPIO_18      PG2         PF18
*  GPIO_19      PG3         PF19
*  GPIO_20      PG4         PF20
*  GPIO_21      PG5         PF21
*  GPIO_22      PG6         PF22
*  GPIO_23      PG7         PF23
*  GPIO_24      PG8         PF24
*  GPIO_25      PG9         PF25
*  GPIO_26      PG10        PF26
*  GPIO_27      PG11        PF27
*  GPIO_28      PG12        PF28
*  GPIO_29      PG13        PF29
*  GPIO_30      PG14        PF30
*  GPIO_31      PG15        PF31
*  GPIO_32      PH0         PF32
*  GPIO_33      PH1         PF33
*  GPIO_34      PH2         PF34
*  GPIO_35      PH3         PF35
*  GPIO_36      PH4         PF36
*  GPIO_37      PH5         PF37
*  GPIO_38      PH6         PF38
*  GPIO_39      PH7         PF39
*  GPIO_40      PH8         PF40
*  GPIO_41      PH9         PF41
*  GPIO_42      PH10        PF42
*  GPIO_43      PH11        PF43
*  GPIO_44      PH12        PF44
*  GPIO_45      PH13        PF45
*  GPIO_46      PH14        PF46
*  GPIO_47      PH15        PF47
*/

 * GPIO Abstraction Layer
 *
 * Copyright 2006-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <asm/portmux.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#if ANOMALY_05000311 || ANOMALY_05000323
enum {
	AWA_data = SYSCR,
	AWA_data_clear = SYSCR,
	AWA_data_set = SYSCR,
	AWA_toggle = SYSCR,
	AWA_maska = BFIN_UART_SCR,
	AWA_maska_clear = BFIN_UART_SCR,
	AWA_maska_set = BFIN_UART_SCR,
	AWA_maska_toggle = BFIN_UART_SCR,
	AWA_maskb = BFIN_UART_GCTL,
	AWA_maskb_clear = BFIN_UART_GCTL,
	AWA_maskb_set = BFIN_UART_GCTL,
	AWA_maskb_toggle = BFIN_UART_GCTL,
	AWA_dir = SPORT1_STAT,
	AWA_polar = SPORT1_STAT,
	AWA_edge = SPORT1_STAT,
	AWA_both = SPORT1_STAT,
#if ANOMALY_05000311
	AWA_inen = TIMER_ENABLE,
#elif ANOMALY_05000323
	AWA_inen = DMA1_1_CONFIG,
#endif
};
	/* Anomaly Workaround */
#define AWA_DUMMY_READ(name) bfin_read16(AWA_ ## name)
#else
#define AWA_DUMMY_READ(...)  do { } while (0)
#endif

#ifdef BF533_FAMILY
static struct gpio_port_t *gpio_bankb[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) FIO_FLAG_D,
};
#endif

#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
static struct gpio_port_t *gpio_bankb[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) PORTFIO,
	(struct gpio_port_t *) PORTGIO,
	(struct gpio_port_t *) PORTHIO,
};

static unsigned short *port_fer[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
static struct gpio_port_t * const gpio_array[] = {
#if defined(BF533_FAMILY) || defined(BF538_FAMILY)
	(struct gpio_port_t *) FIO_FLAG_D,
#elif defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
	(struct gpio_port_t *) PORTFIO,
	(struct gpio_port_t *) PORTGIO,
	(struct gpio_port_t *) PORTHIO,
#elif defined(BF561_FAMILY)
	(struct gpio_port_t *) FIO0_FLAG_D,
	(struct gpio_port_t *) FIO1_FLAG_D,
	(struct gpio_port_t *) FIO2_FLAG_D,
#else
# error no gpio arrays defined
#endif
};

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
static unsigned short * const port_fer[] = {
	(unsigned short *) PORTF_FER,
	(unsigned short *) PORTG_FER,
	(unsigned short *) PORTH_FER,
};
#endif

#ifdef BF527_FAMILY
static unsigned short *port_mux[gpio_bank(MAX_BLACKFIN_GPIOS)] = {

# if !defined(BF537_FAMILY)
static unsigned short * const port_mux[] = {
	(unsigned short *) PORTF_MUX,
	(unsigned short *) PORTG_MUX,
	(unsigned short *) PORTH_MUX,
};

static const
u8 pmux_offset[][16] =
	{{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 4, 6, 8, 8, 10, 10 }, /* PORTF */
	 { 0, 0, 0, 0, 0, 2, 2, 4, 4, 6, 8, 10, 10, 10, 12, 12 }, /* PORTG */
	 { 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 4, 4, 4, 4, 4 }, /* PORTH */
	};
#endif

#ifdef BF561_FAMILY
static struct gpio_port_t *gpio_bankb[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *) FIO0_FLAG_D,
	(struct gpio_port_t *) FIO1_FLAG_D,
	(struct gpio_port_t *) FIO2_FLAG_D,
};
#endif

#ifdef BF548_FAMILY
static struct gpio_port_t *gpio_array[gpio_bank(MAX_BLACKFIN_GPIOS)] = {
	(struct gpio_port_t *)PORTA_FER,
	(struct gpio_port_t *)PORTB_FER,
	(struct gpio_port_t *)PORTC_FER,
	(struct gpio_port_t *)PORTD_FER,
	(struct gpio_port_t *)PORTE_FER,
	(struct gpio_port_t *)PORTF_FER,
	(struct gpio_port_t *)PORTG_FER,
	(struct gpio_port_t *)PORTH_FER,
	(struct gpio_port_t *)PORTI_FER,
	(struct gpio_port_t *)PORTJ_FER,
};
#endif

static unsigned short reserved_gpio_map[gpio_bank(MAX_BLACKFIN_GPIOS)];
static unsigned short reserved_peri_map[gpio_bank(MAX_RESOURCES)];

#define RESOURCE_LABEL_SIZE 	16
u8 pmux_offset[][16] = {
#  if defined(CONFIG_BF52x)
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 4, 6, 8, 8, 10, 10 }, /* PORTF */
	{ 0, 0, 0, 0, 0, 2, 2, 4, 4, 6, 8, 10, 10, 10, 12, 12 }, /* PORTG */
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 4, 4, 4, 4, 4, 4, 4 }, /* PORTH */
#  elif defined(CONFIG_BF51x)
	{ 0, 2, 2, 2, 2, 2, 2, 4, 6, 6, 6, 8, 8, 8, 8, 10 }, /* PORTF */
	{ 0, 0, 0, 2, 4, 6, 6, 6, 8, 10, 10, 12, 14, 14, 14, 14 }, /* PORTG */
	{ 0, 0, 0, 0, 2, 2, 4, 6, 10, 10, 10, 10, 10, 10, 10, 10 }, /* PORTH */
#  endif
};
# endif

#elif defined(BF538_FAMILY)
static unsigned short * const port_fer[] = {
	(unsigned short *) PORTCIO_FER,
	(unsigned short *) PORTDIO_FER,
	(unsigned short *) PORTEIO_FER,
};
#endif

#define RESOURCE_LABEL_SIZE	16

static struct str_ident {
	char name[RESOURCE_LABEL_SIZE];
} str_ident[MAX_RESOURCES];

#if defined(CONFIG_PM)
#if defined(CONFIG_BF54x)
static struct gpio_port_s gpio_bank_saved[gpio_bank(MAX_BLACKFIN_GPIOS)];
#else
static unsigned short wakeup_map[gpio_bank(MAX_BLACKFIN_GPIOS)];
static unsigned char wakeup_flags_map[MAX_BLACKFIN_GPIOS];
static struct gpio_port_s gpio_bank_saved[gpio_bank(MAX_BLACKFIN_GPIOS)];

#ifdef BF533_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PROG_INTB};
#endif

#ifdef BF537_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PROG_INTB, IRQ_PORTG_INTB, IRQ_MAC_TX};
#endif

#ifdef BF527_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PORTF_INTB, IRQ_PORTG_INTB, IRQ_PORTH_INTB};
#endif

#ifdef BF561_FAMILY
static unsigned int sic_iwr_irqs[gpio_bank(MAX_BLACKFIN_GPIOS)] = {IRQ_PROG0_INTB, IRQ_PROG1_INTB, IRQ_PROG2_INTB};
#endif
#endif
#endif /* CONFIG_PM */

#if defined(BF548_FAMILY)
inline int check_gpio(unsigned gpio)
{
	if (gpio == GPIO_PB15 || gpio == GPIO_PC14 || gpio == GPIO_PC15
	    || gpio == GPIO_PH14 || gpio == GPIO_PH15
	    || gpio == GPIO_PJ14 || gpio == GPIO_PJ15
	    || gpio > MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}
#else
inline int check_gpio(unsigned gpio)
{
	if (gpio >= MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}
#endif

void gpio_error(unsigned gpio)
static struct gpio_port_s gpio_bank_saved[GPIO_BANK_NUM];
# ifdef BF538_FAMILY
static unsigned short port_fer_saved[3];
# endif
#endif

static void gpio_error(unsigned gpio)
{
	printk(KERN_ERR "bfin-gpio: GPIO %d wasn't requested!\n", gpio);
}

static void set_label(unsigned short ident, const char *label)
{
	if (label && str_ident) {
	if (label) {
		strncpy(str_ident[ident].name, label,
			 RESOURCE_LABEL_SIZE);
		str_ident[ident].name[RESOURCE_LABEL_SIZE - 1] = 0;
	}
}

static char *get_label(unsigned short ident)
{
	if (!str_ident)
		return "UNKNOWN";

	return (*str_ident[ident].name ? str_ident[ident].name : "UNKNOWN");
}

static int cmp_label(unsigned short ident, const char *label)
{
	if (label == NULL) {
		dump_stack();
		printk(KERN_ERR "Please provide none-null label\n");
	}

	if (label && str_ident)
		return strncmp(str_ident[ident].name,
				 label, strlen(label));
	if (label)
		return strcmp(str_ident[ident].name, label);
	else
		return -EINVAL;
}

#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
static void port_setup(unsigned gpio, unsigned short usage)
{
	if (!check_gpio(gpio)) {
		if (usage == GPIO_USAGE)
			*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
		else
			*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
		SSYNC();
	}
}
#elif defined(BF548_FAMILY)
static void port_setup(unsigned gpio, unsigned short usage)
{
	if (usage == GPIO_USAGE)
		gpio_array[gpio_bank(gpio)]->port_fer &= ~gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->port_fer |= gpio_bit(gpio);
	SSYNC();
}
#else
# define port_setup(...)  do { } while (0)
#endif

#ifdef BF537_FAMILY
static struct {
	unsigned short res;
	unsigned short offset;
} port_mux_lut[] = {
	{.res = P_PPI0_D13, .offset = 11},
	{.res = P_PPI0_D14, .offset = 11},
	{.res = P_PPI0_D15, .offset = 11},
	{.res = P_SPORT1_TFS, .offset = 11},
	{.res = P_SPORT1_TSCLK, .offset = 11},
	{.res = P_SPORT1_DTPRI, .offset = 11},
	{.res = P_PPI0_D10, .offset = 10},
	{.res = P_PPI0_D11, .offset = 10},
	{.res = P_PPI0_D12, .offset = 10},
	{.res = P_SPORT1_RSCLK, .offset = 10},
	{.res = P_SPORT1_RFS, .offset = 10},
	{.res = P_SPORT1_DRPRI, .offset = 10},
	{.res = P_PPI0_D8, .offset = 9},
	{.res = P_PPI0_D9, .offset = 9},
	{.res = P_SPORT1_DRSEC, .offset = 9},
	{.res = P_SPORT1_DTSEC, .offset = 9},
	{.res = P_TMR2, .offset = 8},
	{.res = P_PPI0_FS3, .offset = 8},
	{.res = P_TMR3, .offset = 7},
	{.res = P_SPI0_SSEL4, .offset = 7},
	{.res = P_TMR4, .offset = 6},
	{.res = P_SPI0_SSEL5, .offset = 6},
	{.res = P_TMR5, .offset = 5},
	{.res = P_SPI0_SSEL6, .offset = 5},
	{.res = P_UART1_RX, .offset = 4},
	{.res = P_UART1_TX, .offset = 4},
	{.res = P_TMR6, .offset = 4},
	{.res = P_TMR7, .offset = 4},
	{.res = P_UART0_RX, .offset = 3},
	{.res = P_UART0_TX, .offset = 3},
	{.res = P_DMAR0, .offset = 3},
	{.res = P_DMAR1, .offset = 3},
	{.res = P_SPORT0_DTSEC, .offset = 1},
	{.res = P_SPORT0_DRSEC, .offset = 1},
	{.res = P_CAN0_RX, .offset = 1},
	{.res = P_CAN0_TX, .offset = 1},
	{.res = P_SPI0_SSEL7, .offset = 1},
	{.res = P_SPORT0_TFS, .offset = 0},
	{.res = P_SPORT0_DTPRI, .offset = 0},
	{.res = P_SPI0_SSEL2, .offset = 0},
	{.res = P_SPI0_SSEL3, .offset = 0},
};

static void portmux_setup(unsigned short per, unsigned short function)
{
	u16 y, offset, muxreg;

	for (y = 0; y < ARRAY_SIZE(port_mux_lut); y++) {
		if (port_mux_lut[y].res == per) {

			/* SET PORTMUX REG */

			offset = port_mux_lut[y].offset;
			muxreg = bfin_read_PORT_MUX();

			if (offset != 1)
				muxreg &= ~(1 << offset);
			else
				muxreg &= ~(3 << 1);

			muxreg |= (function << offset);
			bfin_write_PORT_MUX(muxreg);
		}
	}
}
#elif defined(BF548_FAMILY)
inline void portmux_setup(unsigned short portno, unsigned short function)
{
	u32 pmux;

	pmux = gpio_array[gpio_bank(portno)]->port_mux;

	pmux &= ~(0x3 << (2 * gpio_sub_n(portno)));
	pmux |= (function & 0x3) << (2 * gpio_sub_n(portno));

	gpio_array[gpio_bank(portno)]->port_mux = pmux;
}

inline u16 get_portmux(unsigned short portno)
{
	u32 pmux;

	pmux = gpio_array[gpio_bank(portno)]->port_mux;

	return (pmux >> (2 * gpio_sub_n(portno)) & 0x3);
}
#elif defined(BF527_FAMILY)
inline void portmux_setup(unsigned short portno, unsigned short function)
{
	u16 pmux, ident = P_IDENT(portno);
	u8 offset = pmux_offset[gpio_bank(ident)][gpio_sub_n(ident)];

	pmux = *port_mux[gpio_bank(ident)];
#define map_entry(m, i)      reserved_##m##_map[gpio_bank(i)]
#define is_reserved(m, i, e) (map_entry(m, i) & gpio_bit(i))
#define reserve(m, i)        (map_entry(m, i) |= gpio_bit(i))
#define unreserve(m, i)      (map_entry(m, i) &= ~gpio_bit(i))
#define DECLARE_RESERVED_MAP(m, c) static unsigned short reserved_##m##_map[c]

DECLARE_RESERVED_MAP(gpio, GPIO_BANK_NUM);
DECLARE_RESERVED_MAP(peri, DIV_ROUND_UP(MAX_RESOURCES, GPIO_BANKSIZE));
DECLARE_RESERVED_MAP(gpio_irq, GPIO_BANK_NUM);

inline int check_gpio(unsigned gpio)
{
	if (gpio >= MAX_BLACKFIN_GPIOS)
		return -EINVAL;
	return 0;
}

static void port_setup(unsigned gpio, unsigned short usage)
{
#if defined(BF538_FAMILY)
	/*
	 * BF538/9 Port C,D and E are special.
	 * Inverted PORT_FER polarity on CDE and no PORF_FER on F
	 * Regular PORT F GPIOs are handled here, CDE are exclusively
	 * managed by GPIOLIB
	 */

	if (gpio < MAX_BLACKFIN_GPIOS || gpio >= MAX_RESOURCES)
		return;

	gpio -= MAX_BLACKFIN_GPIOS;

	if (usage == GPIO_USAGE)
		*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
	else
		*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
	SSYNC();
	return;
#endif

	if (check_gpio(gpio))
		return;

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
	if (usage == GPIO_USAGE)
		*port_fer[gpio_bank(gpio)] &= ~gpio_bit(gpio);
	else
		*port_fer[gpio_bank(gpio)] |= gpio_bit(gpio);
	SSYNC();
#endif
}

#ifdef BF537_FAMILY
static const s8 port_mux[] = {
	[GPIO_PF0] = 3,
	[GPIO_PF1] = 3,
	[GPIO_PF2] = 4,
	[GPIO_PF3] = 4,
	[GPIO_PF4] = 5,
	[GPIO_PF5] = 6,
	[GPIO_PF6] = 7,
	[GPIO_PF7] = 8,
	[GPIO_PF8 ... GPIO_PF15] = -1,
	[GPIO_PG0 ... GPIO_PG7] = -1,
	[GPIO_PG8] = 9,
	[GPIO_PG9] = 9,
	[GPIO_PG10] = 10,
	[GPIO_PG11] = 10,
	[GPIO_PG12] = 10,
	[GPIO_PG13] = 11,
	[GPIO_PG14] = 11,
	[GPIO_PG15] = 11,
	[GPIO_PH0 ... GPIO_PH15] = -1,
	[PORT_PJ0 ... PORT_PJ3] = -1,
	[PORT_PJ4] = 1,
	[PORT_PJ5] = 1,
	[PORT_PJ6 ... PORT_PJ9] = -1,
	[PORT_PJ10] = 0,
	[PORT_PJ11] = 0,
};

static int portmux_group_check(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	s8 offset = port_mux[ident];
	u16 m, pmux, pfunc, mask;

	if (offset < 0)
		return 0;

	pmux = bfin_read_PORT_MUX();
	for (m = 0; m < ARRAY_SIZE(port_mux); ++m) {
		if (m == ident)
			continue;
		if (port_mux[m] != offset)
			continue;
		if (!is_reserved(peri, m, 1))
			continue;

		if (offset == 1)
			mask = 3;
		else
			mask = 1;

		pfunc = (pmux >> offset) & mask;
		if (pfunc != (function & mask)) {
			pr_err("pin group conflict! request pin %d func %d conflict with pin %d func %d\n",
				ident, function, m, pfunc);
			return -EINVAL;
		}
	}

	return 0;
}

static void portmux_setup(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	s8 offset = port_mux[ident];
	u16 pmux, mask;

	if (offset == -1)
		return;

	pmux = bfin_read_PORT_MUX();
	if (offset == 1)
		mask = 3;
	else
		mask = 1;

	pmux &= ~(mask << offset);
	pmux |= ((function & mask) << offset);

	bfin_write_PORT_MUX(pmux);
}
#elif defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
static int portmux_group_check(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	u8 offset = pmux_offset[gpio_bank(ident)][gpio_sub_n(ident)];
	u16 pin, gpiopin, pfunc;

	for (pin = 0; pin < GPIO_BANKSIZE; ++pin) {
		if (offset != pmux_offset[gpio_bank(ident)][pin])
			continue;

		gpiopin = gpio_bank(ident) * GPIO_BANKSIZE + pin;
		if (gpiopin == ident)
			continue;
		if (!is_reserved(peri, gpiopin, 1))
			continue;

		pfunc = *port_mux[gpio_bank(ident)];
		pfunc = (pfunc >> offset) & 3;
		if (pfunc != function) {
			pr_err("pin group conflict! request pin %d func %d conflict with pin %d func %d\n",
				ident, function, gpiopin, pfunc);
			return -EINVAL;
		}
	}

	return 0;
}

inline void portmux_setup(unsigned short per)
{
	u16 ident = P_IDENT(per);
	u16 function = P_FUNCT2MUX(per);
	u8 offset = pmux_offset[gpio_bank(ident)][gpio_sub_n(ident)];
	u16 pmux;

	pmux = *port_mux[gpio_bank(ident)];
	if  (((pmux >> offset) & 3) == function)
		return;
	pmux &= ~(3 << offset);
	pmux |= (function & 3) << offset;
	*port_mux[gpio_bank(ident)] = pmux;
	SSYNC();
}
#else
# define portmux_setup(...)  do { } while (0)
#endif

static int __init bfin_gpio_init(void)
{
	printk(KERN_INFO "Blackfin GPIO Controller\n");

	return 0;
}
arch_initcall(bfin_gpio_init);


#ifndef BF548_FAMILY
static int portmux_group_check(unsigned short per)
{
	return 0;
}
#endif

/***********************************************************
*
* FUNCTIONS: Blackfin General Purpose Ports Access Functions
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
*
*
* DESCRIPTION: These functions abstract direct register access
*              to Blackfin processor General Purpose
*              Ports Regsiters
*
* CAUTION: These functions do not belong to the GPIO Driver API
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

/* Set a specific bit */

#define SET_GPIO(name) \
void set_gpio_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	local_irq_save(flags); \
	if (arg) \
		gpio_bankb[gpio_bank(gpio)]->name |= gpio_bit(gpio); \
	else \
		gpio_bankb[gpio_bank(gpio)]->name &= ~gpio_bit(gpio); \
	AWA_DUMMY_READ(name); \
	local_irq_restore(flags); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO(dir)
SET_GPIO(inen)
SET_GPIO(polar)
SET_GPIO(edge)
SET_GPIO(both)


#if ANOMALY_05000311 || ANOMALY_05000323
	flags = hard_local_irq_save(); \
	if (arg) \
		gpio_array[gpio_bank(gpio)]->name |= gpio_bit(gpio); \
	else \
		gpio_array[gpio_bank(gpio)]->name &= ~gpio_bit(gpio); \
	AWA_DUMMY_READ(name); \
	hard_local_irq_restore(flags); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO(dir)   /* set_gpio_dir() */
SET_GPIO(inen)  /* set_gpio_inen() */
SET_GPIO(polar) /* set_gpio_polar() */
SET_GPIO(edge)  /* set_gpio_edge() */
SET_GPIO(both)  /* set_gpio_both() */


#define SET_GPIO_SC(name) \
void set_gpio_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	local_irq_save(flags); \
	if (arg) \
		gpio_bankb[gpio_bank(gpio)]->name ## _set = gpio_bit(gpio); \
	else \
		gpio_bankb[gpio_bank(gpio)]->name ## _clear = gpio_bit(gpio); \
	AWA_DUMMY_READ(name); \
	local_irq_restore(flags); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);
#else
#define SET_GPIO_SC(name) \
void set_gpio_ ## name(unsigned gpio, unsigned short arg) \
{ \
	if (arg) \
		gpio_bankb[gpio_bank(gpio)]->name ## _set = gpio_bit(gpio); \
	else \
		gpio_bankb[gpio_bank(gpio)]->name ## _clear = gpio_bit(gpio); \
} \
EXPORT_SYMBOL(set_gpio_ ## name);
#endif
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		flags = hard_local_irq_save(); \
	if (arg) \
		gpio_array[gpio_bank(gpio)]->name ## _set = gpio_bit(gpio); \
	else \
		gpio_array[gpio_bank(gpio)]->name ## _clear = gpio_bit(gpio); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
	} \
} \
EXPORT_SYMBOL(set_gpio_ ## name);

SET_GPIO_SC(maska)
SET_GPIO_SC(maskb)
SET_GPIO_SC(data)

#if ANOMALY_05000311 || ANOMALY_05000323
void set_gpio_toggle(unsigned gpio)
{
	unsigned long flags;
	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
	AWA_DUMMY_READ(toggle);
	local_irq_restore(flags);
}
#else
void set_gpio_toggle(unsigned gpio)
{
	gpio_bankb[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
}
#endif
void set_gpio_toggle(unsigned gpio)
{
	unsigned long flags;
	if (ANOMALY_05000311 || ANOMALY_05000323)
		flags = hard_local_irq_save();
	gpio_array[gpio_bank(gpio)]->toggle = gpio_bit(gpio);
	if (ANOMALY_05000311 || ANOMALY_05000323) {
		AWA_DUMMY_READ(toggle);
		hard_local_irq_restore(flags);
	}
}
EXPORT_SYMBOL(set_gpio_toggle);


/*Set current PORT date (16-bit word)*/

#if ANOMALY_05000311 || ANOMALY_05000323
#define SET_GPIO_P(name) \
void set_gpiop_ ## name(unsigned gpio, unsigned short arg) \
{ \
	unsigned long flags; \
	local_irq_save(flags); \
	gpio_bankb[gpio_bank(gpio)]->name = arg; \
	AWA_DUMMY_READ(name); \
	local_irq_restore(flags); \
} \
EXPORT_SYMBOL(set_gpiop_ ## name);
#else
#define SET_GPIO_P(name) \
void set_gpiop_ ## name(unsigned gpio, unsigned short arg) \
{ \
	gpio_bankb[gpio_bank(gpio)]->name = arg; \
} \
EXPORT_SYMBOL(set_gpiop_ ## name);
#endif
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		flags = hard_local_irq_save(); \
	gpio_array[gpio_bank(gpio)]->name = arg; \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
	} \
} \
EXPORT_SYMBOL(set_gpiop_ ## name);

SET_GPIO_P(data)
SET_GPIO_P(dir)
SET_GPIO_P(inen)
SET_GPIO_P(polar)
SET_GPIO_P(edge)
SET_GPIO_P(both)
SET_GPIO_P(maska)
SET_GPIO_P(maskb)

/* Get a specific bit */
#if ANOMALY_05000311 || ANOMALY_05000323
#define GET_GPIO(name) \
unsigned short get_gpio_ ## name(unsigned gpio) \
{ \
	unsigned long flags; \
	unsigned short ret; \
	local_irq_save(flags); \
	ret = 0x01 & (gpio_bankb[gpio_bank(gpio)]->name >> gpio_sub_n(gpio)); \
	AWA_DUMMY_READ(name); \
	local_irq_restore(flags); \
	return ret; \
} \
EXPORT_SYMBOL(get_gpio_ ## name);
#else
#define GET_GPIO(name) \
unsigned short get_gpio_ ## name(unsigned gpio) \
{ \
	return (0x01 & (gpio_bankb[gpio_bank(gpio)]->name >> gpio_sub_n(gpio))); \
} \
EXPORT_SYMBOL(get_gpio_ ## name);
#endif
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		flags = hard_local_irq_save(); \
	ret = 0x01 & (gpio_array[gpio_bank(gpio)]->name >> gpio_sub_n(gpio)); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
	} \
	return ret; \
} \
EXPORT_SYMBOL(get_gpio_ ## name);

GET_GPIO(data)
GET_GPIO(dir)
GET_GPIO(inen)
GET_GPIO(polar)
GET_GPIO(edge)
GET_GPIO(both)
GET_GPIO(maska)
GET_GPIO(maskb)

/*Get current PORT date (16-bit word)*/

#if ANOMALY_05000311 || ANOMALY_05000323
#define GET_GPIO_P(name) \
unsigned short get_gpiop_ ## name(unsigned gpio) \
{ \
	unsigned long flags; \
	unsigned short ret; \
	local_irq_save(flags); \
	ret = (gpio_bankb[gpio_bank(gpio)]->name); \
	AWA_DUMMY_READ(name); \
	local_irq_restore(flags); \
	return ret; \
} \
EXPORT_SYMBOL(get_gpiop_ ## name);
#else
#define GET_GPIO_P(name) \
unsigned short get_gpiop_ ## name(unsigned gpio) \
{ \
	return (gpio_bankb[gpio_bank(gpio)]->name);\
} \
EXPORT_SYMBOL(get_gpiop_ ## name);
#endif
	if (ANOMALY_05000311 || ANOMALY_05000323) \
		flags = hard_local_irq_save(); \
	ret = (gpio_array[gpio_bank(gpio)]->name); \
	if (ANOMALY_05000311 || ANOMALY_05000323) { \
		AWA_DUMMY_READ(name); \
		hard_local_irq_restore(flags); \
	} \
	return ret; \
} \
EXPORT_SYMBOL(get_gpiop_ ## name);

GET_GPIO_P(data)
GET_GPIO_P(dir)
GET_GPIO_P(inen)
GET_GPIO_P(polar)
GET_GPIO_P(edge)
GET_GPIO_P(both)
GET_GPIO_P(maska)
GET_GPIO_P(maskb)


#ifdef CONFIG_PM
DECLARE_RESERVED_MAP(wakeup, GPIO_BANK_NUM);

static const unsigned int sic_iwr_irqs[] = {
#if defined(BF533_FAMILY)
	IRQ_PROG_INTB
#elif defined(BF537_FAMILY)
	IRQ_PF_INTB_WATCH, IRQ_PORTG_INTB, IRQ_PH_INTB_MAC_TX
#elif defined(BF538_FAMILY)
	IRQ_PORTF_INTB
#elif defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
	IRQ_PORTF_INTB, IRQ_PORTG_INTB, IRQ_PORTH_INTB
#elif defined(BF561_FAMILY)
	IRQ_PROG0_INTB, IRQ_PROG1_INTB, IRQ_PROG2_INTB
#else
# error no SIC_IWR defined
#endif
};

/***********************************************************
*
* FUNCTIONS: Blackfin PM Setup API
*
* INPUTS/OUTPUTS:
* gpio - GPIO Number between 0 and MAX_BLACKFIN_GPIOS
* type -
*	PM_WAKE_RISING
*	PM_WAKE_FALLING
*	PM_WAKE_HIGH
*	PM_WAKE_LOW
*	PM_WAKE_BOTH_EDGES
*
* DESCRIPTION: Blackfin PM Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/
int gpio_pm_wakeup_request(unsigned gpio, unsigned char type)
{
	unsigned long flags;

	if ((check_gpio(gpio) < 0) || !type)
		return -EINVAL;

	local_irq_save(flags);
	wakeup_map[gpio_bank(gpio)] |= gpio_bit(gpio);
	wakeup_flags_map[gpio] = type;
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_pm_wakeup_request);

void gpio_pm_wakeup_free(unsigned gpio)
int bfin_gpio_pm_wakeup_ctrl(unsigned gpio, unsigned ctrl)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save(flags);

	wakeup_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_pm_wakeup_free);

static int bfin_gpio_wakeup_type(unsigned gpio, unsigned char type)
{
	port_setup(gpio, GPIO_USAGE);
	set_gpio_dir(gpio, 0);
	set_gpio_inen(gpio, 1);

	if (type & (PM_WAKE_RISING | PM_WAKE_FALLING))
		set_gpio_edge(gpio, 1);
	 else
		set_gpio_edge(gpio, 0);

	if ((type & (PM_WAKE_BOTH_EDGES)) == (PM_WAKE_BOTH_EDGES))
		set_gpio_both(gpio, 1);
	else
		set_gpio_both(gpio, 0);

	if ((type & (PM_WAKE_FALLING | PM_WAKE_LOW)))
		set_gpio_polar(gpio, 1);
	else
		set_gpio_polar(gpio, 0);

	SSYNC();
		return -EINVAL;

	flags = hard_local_irq_save();
	if (ctrl)
		reserve(wakeup, gpio);
	else
		unreserve(wakeup, gpio);

	set_gpio_maskb(gpio, ctrl);
	hard_local_irq_restore(flags);

	return 0;
}

u32 bfin_pm_standby_setup(void)
{
	u16 bank, mask, i, gpio;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = wakeup_map[gpio_bank(i)];
		bank = gpio_bank(i);

		gpio_bank_saved[bank].maskb = gpio_bankb[bank]->maskb;
		gpio_bankb[bank]->maskb = 0;

		if (mask) {
#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
			gpio_bank_saved[bank].fer   = *port_fer[bank];
#endif
			gpio_bank_saved[bank].inen  = gpio_bankb[bank]->inen;
			gpio_bank_saved[bank].polar = gpio_bankb[bank]->polar;
			gpio_bank_saved[bank].dir   = gpio_bankb[bank]->dir;
			gpio_bank_saved[bank].edge  = gpio_bankb[bank]->edge;
			gpio_bank_saved[bank].both  = gpio_bankb[bank]->both;
			gpio_bank_saved[bank].reserved =
						reserved_gpio_map[bank];

			gpio = i;

			while (mask) {
				if ((mask & 1) && (wakeup_flags_map[gpio] !=
					PM_WAKE_IGNORE)) {
					reserved_gpio_map[gpio_bank(gpio)] |=
							gpio_bit(gpio);
					bfin_gpio_wakeup_type(gpio,
						wakeup_flags_map[gpio]);
					set_gpio_data(gpio, 0); /*Clear*/
				}
				gpio++;
				mask >>= 1;
			}

			bfin_internal_set_wake(sic_iwr_irqs[bank], 1);
			gpio_bankb[bank]->maskb_set = wakeup_map[gpio_bank(i)];
		}
	}

	AWA_DUMMY_READ(maskb_set);

	return 0;
}

void bfin_pm_standby_restore(void)
int bfin_gpio_pm_standby_ctrl(unsigned ctrl)
{
	u16 bank, mask, i;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		mask = wakeup_map[gpio_bank(i)];
		bank = gpio_bank(i);

		if (mask) {
#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
			*port_fer[bank]   	= gpio_bank_saved[bank].fer;
#endif
			gpio_bankb[bank]->inen  = gpio_bank_saved[bank].inen;
			gpio_bankb[bank]->dir   = gpio_bank_saved[bank].dir;
			gpio_bankb[bank]->polar = gpio_bank_saved[bank].polar;
			gpio_bankb[bank]->edge  = gpio_bank_saved[bank].edge;
			gpio_bankb[bank]->both  = gpio_bank_saved[bank].both;

			reserved_gpio_map[bank] =
					gpio_bank_saved[bank].reserved;
			bfin_internal_set_wake(sic_iwr_irqs[bank], 0);
		}

		gpio_bankb[bank]->maskb = gpio_bank_saved[bank].maskb;
	}
	AWA_DUMMY_READ(maskb);
}

void bfin_gpio_pm_hibernate_suspend(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
			gpio_bank_saved[bank].fer   = *port_fer[bank];
#ifdef BF527_FAMILY
			gpio_bank_saved[bank].mux   = *port_mux[bank];
#else
			if (bank == 0)
				gpio_bank_saved[bank].mux   = bfin_read_PORT_MUX();
#endif
#endif
			gpio_bank_saved[bank].data  = gpio_bankb[bank]->data;
			gpio_bank_saved[bank].inen  = gpio_bankb[bank]->inen;
			gpio_bank_saved[bank].polar = gpio_bankb[bank]->polar;
			gpio_bank_saved[bank].dir   = gpio_bankb[bank]->dir;
			gpio_bank_saved[bank].edge  = gpio_bankb[bank]->edge;
			gpio_bank_saved[bank].both  = gpio_bankb[bank]->both;
			gpio_bank_saved[bank].maska  = gpio_bankb[bank]->maska;
	}

	AWA_DUMMY_READ(maska);
}

void bfin_gpio_pm_hibernate_restore(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
			bank = gpio_bank(i);

#if defined(BF527_FAMILY) || defined(BF537_FAMILY)
#ifdef BF527_FAMILY
			*port_mux[bank] = gpio_bank_saved[bank].mux;
#else
			if (bank == 0)
				bfin_write_PORT_MUX(gpio_bank_saved[bank].mux);
#endif
			*port_fer[bank]   	= gpio_bank_saved[bank].fer;
#endif
			gpio_bankb[bank]->inen  = gpio_bank_saved[bank].inen;
			gpio_bankb[bank]->dir   = gpio_bank_saved[bank].dir;
			gpio_bankb[bank]->polar = gpio_bank_saved[bank].polar;
			gpio_bankb[bank]->edge  = gpio_bank_saved[bank].edge;
			gpio_bankb[bank]->both  = gpio_bank_saved[bank].both;

			gpio_bankb[bank]->data_set = gpio_bank_saved[bank].data
							| gpio_bank_saved[bank].dir;

			gpio_bankb[bank]->maska = gpio_bank_saved[bank].maska;
	}
	AWA_DUMMY_READ(maska);
}


#endif
#else /* BF548_FAMILY */
#ifdef CONFIG_PM

u32 bfin_pm_standby_setup(void)
{
	return 0;
}

void bfin_pm_standby_restore(void)
{

}

		mask = map_entry(wakeup, i);
		bank = gpio_bank(i);

		if (mask)
			bfin_internal_set_wake(sic_iwr_irqs[bank], ctrl);
	}
	return 0;
}

void bfin_gpio_pm_hibernate_suspend(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

			gpio_bank_saved[bank].fer  = gpio_array[bank]->port_fer;
			gpio_bank_saved[bank].mux  = gpio_array[bank]->port_mux;
			gpio_bank_saved[bank].data  = gpio_array[bank]->port_data;
			gpio_bank_saved[bank].data  = gpio_array[bank]->port_data;
			gpio_bank_saved[bank].inen  = gpio_array[bank]->port_inen;
			gpio_bank_saved[bank].dir   = gpio_array[bank]->port_dir_set;
	}
#ifdef BF538_FAMILY
	for (i = 0; i < ARRAY_SIZE(port_fer_saved); ++i)
		port_fer_saved[i] = *port_fer[i];
#endif

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
		gpio_bank_saved[bank].fer = *port_fer[bank];
#if defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
		gpio_bank_saved[bank].mux = *port_mux[bank];
#else
		if (bank == 0)
			gpio_bank_saved[bank].mux = bfin_read_PORT_MUX();
#endif
#endif
		gpio_bank_saved[bank].data  = gpio_array[bank]->data;
		gpio_bank_saved[bank].inen  = gpio_array[bank]->inen;
		gpio_bank_saved[bank].polar = gpio_array[bank]->polar;
		gpio_bank_saved[bank].dir   = gpio_array[bank]->dir;
		gpio_bank_saved[bank].edge  = gpio_array[bank]->edge;
		gpio_bank_saved[bank].both  = gpio_array[bank]->both;
		gpio_bank_saved[bank].maska = gpio_array[bank]->maska;
	}

#ifdef BFIN_SPECIAL_GPIO_BANKS
	bfin_special_gpio_pm_hibernate_suspend();
#endif

	AWA_DUMMY_READ(maska);
}

void bfin_gpio_pm_hibernate_restore(void)
{
	int i, bank;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
			bank = gpio_bank(i);

			gpio_array[bank]->port_mux  = gpio_bank_saved[bank].mux;
			gpio_array[bank]->port_fer  = gpio_bank_saved[bank].fer;
			gpio_array[bank]->port_inen  = gpio_bank_saved[bank].inen;
			gpio_array[bank]->port_dir_set   = gpio_bank_saved[bank].dir;
			gpio_array[bank]->port_set = gpio_bank_saved[bank].data
							| gpio_bank_saved[bank].dir;
	}
}
#endif

unsigned short get_gpio_dir(unsigned gpio)
{
	return (0x01 & (gpio_array[gpio_bank(gpio)]->port_dir_clear >> gpio_sub_n(gpio)));
}
EXPORT_SYMBOL(get_gpio_dir);

#endif /* BF548_FAMILY */

/***********************************************************
*
* FUNCTIONS: 	Blackfin Peripheral Resource Allocation
#ifdef BF538_FAMILY
	for (i = 0; i < ARRAY_SIZE(port_fer_saved); ++i)
		*port_fer[i] = port_fer_saved[i];
#endif

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i += GPIO_BANKSIZE) {
		bank = gpio_bank(i);

#if defined(CONFIG_BF52x) || defined(BF537_FAMILY) || defined(CONFIG_BF51x)
#if defined(CONFIG_BF52x) || defined(CONFIG_BF51x)
		*port_mux[bank] = gpio_bank_saved[bank].mux;
#else
		if (bank == 0)
			bfin_write_PORT_MUX(gpio_bank_saved[bank].mux);
#endif
		*port_fer[bank] = gpio_bank_saved[bank].fer;
#endif
		gpio_array[bank]->inen  = gpio_bank_saved[bank].inen;
		gpio_array[bank]->data_set = gpio_bank_saved[bank].data
						& gpio_bank_saved[bank].dir;
		gpio_array[bank]->dir   = gpio_bank_saved[bank].dir;
		gpio_array[bank]->polar = gpio_bank_saved[bank].polar;
		gpio_array[bank]->edge  = gpio_bank_saved[bank].edge;
		gpio_array[bank]->both  = gpio_bank_saved[bank].both;
		gpio_array[bank]->maska = gpio_bank_saved[bank].maska;
	}

#ifdef BFIN_SPECIAL_GPIO_BANKS
	bfin_special_gpio_pm_hibernate_restore();
#endif

	AWA_DUMMY_READ(maska);
}


#endif

/***********************************************************
*
* FUNCTIONS:	Blackfin Peripheral Resource Allocation
*		and PortMux Setup
*
* INPUTS/OUTPUTS:
* per	Peripheral Identifier
* label	String
*
* DESCRIPTION: Blackfin Peripheral Resource Allocation and Setup API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

#ifdef BF548_FAMILY
int peripheral_request(unsigned short per, const char *label)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	/*
	 * Don't cares are pins with only one dedicated function
	 */

	if (per & P_DONTCARE)
		return 0;

	if (!(per & P_DEFINED))
		return -ENODEV;

	if (check_gpio(ident) < 0)
		return -EINVAL;

	local_irq_save(flags);

	if (unlikely(reserved_gpio_map[gpio_bank(ident)] & gpio_bit(ident))) {
		dump_stack();
		printk(KERN_ERR
		    "%s: Peripheral %d is already reserved as GPIO by %s !\n",
		       __func__, ident, get_label(ident));
		local_irq_restore(flags);
		return -EBUSY;
	}

	if (unlikely(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident))) {

		u16 funct = get_portmux(ident);

		/*
		 * Pin functions like AMC address strobes my
		 * be requested and used by several drivers
		 */

		if (!((per & P_MAYSHARE) && (funct == P_FUNCT2MUX(per)))) {

			/*
			 * Allow that the identical pin function can
			 * be requested from the same driver twice
			 */

			if (cmp_label(ident, label) == 0)
				goto anyway;

			dump_stack();
			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already reserved by %s !\n",
			       __func__, ident, P_FUNCT2MUX(per), get_label(ident));
			local_irq_restore(flags);
			return -EBUSY;
		}
	}

 anyway:
	reserved_peri_map[gpio_bank(ident)] |= gpio_bit(ident);

	portmux_setup(ident, P_FUNCT2MUX(per));
	port_setup(ident, PERIPHERAL_USAGE);

	local_irq_restore(flags);
	set_label(ident, label);

	return 0;
}
EXPORT_SYMBOL(peripheral_request);
#else

int peripheral_request(unsigned short per, const char *label)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	/*
	 * Don't cares are pins with only one dedicated function
	 */

	if (per & P_DONTCARE)
		return 0;

	if (!(per & P_DEFINED))
		return -ENODEV;

	local_irq_save(flags);

	if (!check_gpio(ident)) {

		if (unlikely(reserved_gpio_map[gpio_bank(ident)] & gpio_bit(ident))) {
			dump_stack();
			printk(KERN_ERR
			       "%s: Peripheral %d is already reserved as GPIO by %s !\n",
			       __func__, ident, get_label(ident));
			local_irq_restore(flags);
			return -EBUSY;
		}

	}

	if (unlikely(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident))) {
	BUG_ON(ident >= MAX_RESOURCES);

	flags = hard_local_irq_save();

	/* If a pin can be muxed as either GPIO or peripheral, make
	 * sure it is not already a GPIO pin when we request it.
	 */
	if (unlikely(!check_gpio(ident) && is_reserved(gpio, ident, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR
		       "%s: Peripheral %d is already reserved as GPIO by %s !\n",
		       __func__, ident, get_label(ident));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}

	if (unlikely(is_reserved(peri, ident, 1))) {

		/*
		 * Pin functions like AMC address strobes my
		 * be requested and used by several drivers
		 */

		if (!(per & P_MAYSHARE)) {

			/*
			 * Allow that the identical pin function can
			 * be requested from the same driver twice
			 */

			if (cmp_label(ident, label) == 0)
				goto anyway;

			dump_stack();
			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already"
			       " reserved by %s !\n",
			       __func__, ident, P_FUNCT2MUX(per),
				get_label(ident));
			local_irq_restore(flags);
			return -EBUSY;
		}

	}

 anyway:
	portmux_setup(per, P_FUNCT2MUX(per));

	port_setup(ident, PERIPHERAL_USAGE);

	reserved_peri_map[gpio_bank(ident)] |= gpio_bit(ident);
	local_irq_restore(flags);
			if (system_state == SYSTEM_BOOTING)
				dump_stack();
			printk(KERN_ERR
			       "%s: Peripheral %d function %d is already reserved by %s !\n",
			       __func__, ident, P_FUNCT2MUX(per), get_label(ident));
			hard_local_irq_restore(flags);
			return -EBUSY;
		}
	}

	if (unlikely(portmux_group_check(per))) {
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
 anyway:
	reserve(peri, ident);

	portmux_setup(per);
	port_setup(ident, PERIPHERAL_USAGE);

	hard_local_irq_restore(flags);
	set_label(ident, label);

	return 0;
}
EXPORT_SYMBOL(peripheral_request);
#endif

int peripheral_request_list(const unsigned short per[], const char *label)
{
	u16 cnt;
	int ret;

	for (cnt = 0; per[cnt] != 0; cnt++) {

		ret = peripheral_request(per[cnt], label);

		if (ret < 0) {
			for ( ; cnt > 0; cnt--)
				peripheral_free(per[cnt - 1]);

			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(peripheral_request_list);

void peripheral_free(unsigned short per)
{
	unsigned long flags;
	unsigned short ident = P_IDENT(per);

	if (per & P_DONTCARE)
		return;

	if (!(per & P_DEFINED))
		return;

	if (check_gpio(ident) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_peri_map[gpio_bank(ident)] & gpio_bit(ident)))) {
		local_irq_restore(flags);
	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(peri, ident, 0))) {
		hard_local_irq_restore(flags);
		return;
	}

	if (!(per & P_MAYSHARE))
		port_setup(ident, GPIO_USAGE);

	reserved_peri_map[gpio_bank(ident)] &= ~gpio_bit(ident);

	set_label(ident, "free");

	local_irq_restore(flags);
	unreserve(peri, ident);

	set_label(ident, "free");

	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(peripheral_free);

void peripheral_free_list(const unsigned short per[])
{
	u16 cnt;
	for (cnt = 0; per[cnt] != 0; cnt++)
		peripheral_free(per[cnt]);
}
EXPORT_SYMBOL(peripheral_free_list);

/***********************************************************
*
* FUNCTIONS: Blackfin GPIO Driver
*
* INPUTS/OUTPUTS:
* gpio	PIO Number between 0 and MAX_BLACKFIN_GPIOS
* label	String
*
* DESCRIPTION: Blackfin GPIO Driver API
*
* CAUTION:
*************************************************************
* MODIFICATION HISTORY :
**************************************************************/

int gpio_request(unsigned gpio, const char *label)
int bfin_gpio_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	local_irq_save(flags);
	flags = hard_local_irq_save();

	/*
	 * Allow that the identical GPIO can
	 * be requested from the same driver twice
	 * Do nothing and return -
	 */

	if (cmp_label(gpio, label) == 0) {
		local_irq_restore(flags);
		return 0;
	}

	if (unlikely(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		dump_stack();
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
			 gpio, get_label(gpio));
		local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(reserved_peri_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		local_irq_restore(flags);
		return -EBUSY;
	}

	reserved_gpio_map[gpio_bank(gpio)] |= gpio_bit(gpio);

	local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);
	set_label(gpio, label);

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
		hard_local_irq_restore(flags);
		return 0;
	}

	if (unlikely(is_reserved(gpio, gpio, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
		       gpio, get_label(gpio));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(is_reserved(peri, gpio, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(is_reserved(gpio_irq, gpio, 1))) {
		printk(KERN_NOTICE "bfin-gpio: GPIO %d is already reserved as gpio-irq!"
		       " (Documentation/blackfin/bfin-gpio-notes.txt)\n", gpio);
	} else {	/* Reset POLAR setting when acquiring a gpio for the first time */
		set_gpio_polar(gpio, 0);
	}

	reserve(gpio, gpio);
	set_label(gpio, label);

	hard_local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_request);

void bfin_gpio_free(unsigned gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	local_irq_save(flags);

	if (unlikely(!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio)))) {
		dump_stack();
		gpio_error(gpio);
		local_irq_restore(flags);
		return;
	}

	reserved_gpio_map[gpio_bank(gpio)] &= ~gpio_bit(gpio);

	set_label(gpio, "free");

	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_free);


#ifdef BF548_FAMILY
int gpio_direction_input(unsigned gpio)
{
	unsigned long flags;

	if (!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	local_irq_save(flags);
	gpio_array[gpio_bank(gpio)]->port_dir_clear = gpio_bit(gpio);
	gpio_array[gpio_bank(gpio)]->port_inen |= gpio_bit(gpio);
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;

	if (!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	local_irq_save(flags);
	gpio_array[gpio_bank(gpio)]->port_inen &= ~gpio_bit(gpio);
	gpio_set_value(gpio, value);
	gpio_array[gpio_bank(gpio)]->port_dir_set = gpio_bit(gpio);
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

void gpio_set_value(unsigned gpio, int arg)
{
	if (arg)
		gpio_array[gpio_bank(gpio)]->port_set = gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->port_clear = gpio_bit(gpio);
}
EXPORT_SYMBOL(gpio_set_value);

int gpio_get_value(unsigned gpio)
{
	return (1 & (gpio_array[gpio_bank(gpio)]->port_data >> gpio_sub_n(gpio)));
}
EXPORT_SYMBOL(gpio_get_value);

void bfin_gpio_irq_prepare(unsigned gpio)
{
	unsigned long flags;

	port_setup(gpio, GPIO_USAGE);

	local_irq_save(flags);
	gpio_array[gpio_bank(gpio)]->port_dir_clear = gpio_bit(gpio);
	gpio_array[gpio_bank(gpio)]->port_inen |= gpio_bit(gpio);
	local_irq_restore(flags);
}

#else

int gpio_get_value(unsigned gpio)
{
	unsigned long flags;
	int ret;

	if (unlikely(get_gpio_edge(gpio))) {
		local_irq_save(flags);
		set_gpio_edge(gpio, 0);
		ret = get_gpio_data(gpio);
		set_gpio_edge(gpio, 1);
		local_irq_restore(flags);

	might_sleep();

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(gpio, gpio, 0))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		gpio_error(gpio);
		hard_local_irq_restore(flags);
		return;
	}

	unreserve(gpio, gpio);

	set_label(gpio, "free");

	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(bfin_gpio_free);

#ifdef BFIN_SPECIAL_GPIO_BANKS
DECLARE_RESERVED_MAP(special_gpio, gpio_bank(MAX_RESOURCES));

int bfin_special_gpio_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	flags = hard_local_irq_save();

	/*
	 * Allow that the identical GPIO can
	 * be requested from the same driver twice
	 * Do nothing and return -
	 */

	if (cmp_label(gpio, label) == 0) {
		hard_local_irq_restore(flags);
		return 0;
	}

	if (unlikely(is_reserved(special_gpio, gpio, 1))) {
		hard_local_irq_restore(flags);
		printk(KERN_ERR "bfin-gpio: GPIO %d is already reserved by %s !\n",
		       gpio, get_label(gpio));

		return -EBUSY;
	}
	if (unlikely(is_reserved(peri, gpio, 1))) {
		hard_local_irq_restore(flags);
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));

		return -EBUSY;
	}

	reserve(special_gpio, gpio);
	reserve(peri, gpio);

	set_label(gpio, label);
	hard_local_irq_restore(flags);
	port_setup(gpio, GPIO_USAGE);

	return 0;
}
EXPORT_SYMBOL(bfin_special_gpio_request);

void bfin_special_gpio_free(unsigned gpio)
{
	unsigned long flags;

	might_sleep();

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(special_gpio, gpio, 0))) {
		gpio_error(gpio);
		hard_local_irq_restore(flags);
		return;
	}

	unreserve(special_gpio, gpio);
	unreserve(peri, gpio);
	set_label(gpio, "free");
	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(bfin_special_gpio_free);
#endif


int bfin_gpio_irq_request(unsigned gpio, const char *label)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return -EINVAL;

	flags = hard_local_irq_save();

	if (unlikely(is_reserved(peri, gpio, 1))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		printk(KERN_ERR
		       "bfin-gpio: GPIO %d is already reserved as Peripheral by %s !\n",
		       gpio, get_label(gpio));
		hard_local_irq_restore(flags);
		return -EBUSY;
	}
	if (unlikely(is_reserved(gpio, gpio, 1)))
		printk(KERN_NOTICE "bfin-gpio: GPIO %d is already reserved by %s! "
		       "(Documentation/blackfin/bfin-gpio-notes.txt)\n",
		       gpio, get_label(gpio));

	reserve(gpio_irq, gpio);
	set_label(gpio, label);

	hard_local_irq_restore(flags);

	port_setup(gpio, GPIO_USAGE);

	return 0;
}

void bfin_gpio_irq_free(unsigned gpio)
{
	unsigned long flags;

	if (check_gpio(gpio) < 0)
		return;

	flags = hard_local_irq_save();

	if (unlikely(!is_reserved(gpio_irq, gpio, 0))) {
		if (system_state == SYSTEM_BOOTING)
			dump_stack();
		gpio_error(gpio);
		hard_local_irq_restore(flags);
		return;
	}

	unreserve(gpio_irq, gpio);

	set_label(gpio, "free");

	hard_local_irq_restore(flags);
}

static inline void __bfin_gpio_direction_input(unsigned gpio)
{
	gpio_array[gpio_bank(gpio)]->dir &= ~gpio_bit(gpio);
	gpio_array[gpio_bank(gpio)]->inen |= gpio_bit(gpio);
}

int bfin_gpio_direction_input(unsigned gpio)
{
	unsigned long flags;

	if (unlikely(!is_reserved(gpio, gpio, 0))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	flags = hard_local_irq_save();
	__bfin_gpio_direction_input(gpio);
	AWA_DUMMY_READ(inen);
	hard_local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_direction_input);

void bfin_gpio_irq_prepare(unsigned gpio)
{
	port_setup(gpio, GPIO_USAGE);
}

void bfin_gpio_set_value(unsigned gpio, int arg)
{
	if (arg)
		gpio_array[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
	else
		gpio_array[gpio_bank(gpio)]->data_clear = gpio_bit(gpio);
}
EXPORT_SYMBOL(bfin_gpio_set_value);

int bfin_gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;

	if (unlikely(!is_reserved(gpio, gpio, 0))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	flags = hard_local_irq_save();

	gpio_array[gpio_bank(gpio)]->inen &= ~gpio_bit(gpio);
	gpio_set_value(gpio, value);
	gpio_array[gpio_bank(gpio)]->dir |= gpio_bit(gpio);

	AWA_DUMMY_READ(dir);
	hard_local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(bfin_gpio_direction_output);

int bfin_gpio_get_value(unsigned gpio)
{
	unsigned long flags;

	if (unlikely(get_gpio_edge(gpio))) {
		int ret;
		flags = hard_local_irq_save();
		set_gpio_edge(gpio, 0);
		ret = get_gpio_data(gpio);
		set_gpio_edge(gpio, 1);
		hard_local_irq_restore(flags);
		return ret;
	} else
		return get_gpio_data(gpio);
}
EXPORT_SYMBOL(gpio_get_value);


int gpio_direction_input(unsigned gpio)
{
	unsigned long flags;

	if (!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->dir &= ~gpio_bit(gpio);
	gpio_bankb[gpio_bank(gpio)]->inen |= gpio_bit(gpio);
	AWA_DUMMY_READ(inen);
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;

	if (!(reserved_gpio_map[gpio_bank(gpio)] & gpio_bit(gpio))) {
		gpio_error(gpio);
		return -EINVAL;
	}

	local_irq_save(flags);
	gpio_bankb[gpio_bank(gpio)]->inen &= ~gpio_bit(gpio);

	if (value)
		gpio_bankb[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
	else
		gpio_bankb[gpio_bank(gpio)]->data_clear = gpio_bit(gpio);

	gpio_bankb[gpio_bank(gpio)]->dir |= gpio_bit(gpio);
	AWA_DUMMY_READ(dir);
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);
EXPORT_SYMBOL(bfin_gpio_get_value);

/* If we are booting from SPI and our board lacks a strong enough pull up,
 * the core can reset and execute the bootrom faster than the resistor can
 * pull the signal logically high.  To work around this (common) error in
 * board design, we explicitly set the pin back to GPIO mode, force /CS
 * high, and wait for the electrons to do their thing.
 *
 * This function only makes sense to be called from reset code, but it
 * lives here as we need to force all the GPIO states w/out going through
 * BUG() checks and such.
 */
void bfin_gpio_reset_spi0_ssel1(void)
{
	u16 gpio = P_IDENT(P_SPI0_SSEL1);

	port_setup(gpio, GPIO_USAGE);
	gpio_bankb[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
void bfin_reset_boot_spi_cs(unsigned short pin)
{
	unsigned short gpio = P_IDENT(pin);
	port_setup(gpio, GPIO_USAGE);
	gpio_array[gpio_bank(gpio)]->data_set = gpio_bit(gpio);
	AWA_DUMMY_READ(data_set);
	udelay(1);
}

void bfin_gpio_irq_prepare(unsigned gpio)
{
	port_setup(gpio, GPIO_USAGE);
}

#endif /*BF548_FAMILY */

#if defined(CONFIG_PROC_FS)
static int gpio_proc_read(char *buf, char **start, off_t offset,
			  int len, int *unused_i, void *unused_v)
{
	int c, outlen = 0;

	for (c = 0; c < MAX_RESOURCES; c++) {
		if (!check_gpio(c) && (reserved_gpio_map[gpio_bank(c)] & gpio_bit(c)))
			len = sprintf(buf, "GPIO_%d: %s \t\tGPIO %s\n", c,
				 get_label(c), get_gpio_dir(c) ? "OUTPUT" : "INPUT");
		else if (reserved_peri_map[gpio_bank(c)] & gpio_bit(c))
			len = sprintf(buf, "GPIO_%d: %s \t\tPeripheral\n", c, get_label(c));
		else
			continue;
		buf += len;
		outlen += len;
	}
	return outlen;
}

#if defined(CONFIG_PROC_FS)
static int gpio_proc_show(struct seq_file *m, void *v)
{
	int c, irq, gpio;

	for (c = 0; c < MAX_RESOURCES; c++) {
		irq = is_reserved(gpio_irq, c, 1);
		gpio = is_reserved(gpio, c, 1);
		if (!check_gpio(c) && (gpio || irq))
			seq_printf(m, "GPIO_%d: \t%s%s \t\tGPIO %s\n", c,
				 get_label(c), (gpio && irq) ? " *" : "",
				 get_gpio_dir(c) ? "OUTPUT" : "INPUT");
		else if (is_reserved(peri, c, 1))
			seq_printf(m, "GPIO_%d: \t%s \t\tPeripheral\n", c, get_label(c));
		else
			continue;
	}

	return 0;
}

static int gpio_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpio_proc_show, NULL);
}

static const struct file_operations gpio_proc_ops = {
	.open		= gpio_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static __init int gpio_register_proc(void)
{
	struct proc_dir_entry *proc_gpio;

	proc_gpio = create_proc_entry("gpio", S_IRUGO, NULL);
	if (proc_gpio)
		proc_gpio->read_proc = gpio_proc_read;
	return proc_gpio != NULL;
}
__initcall(gpio_register_proc);
#endif
	proc_gpio = proc_create("gpio", 0, NULL, &gpio_proc_ops);
	return proc_gpio == NULL;
}
__initcall(gpio_register_proc);
#endif

#ifdef CONFIG_GPIOLIB
static int bfin_gpiolib_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_direction_input(gpio);
}

static int bfin_gpiolib_direction_output(struct gpio_chip *chip, unsigned gpio, int level)
{
	return bfin_gpio_direction_output(gpio, level);
}

static int bfin_gpiolib_get_value(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_get_value(gpio);
}

static void bfin_gpiolib_set_value(struct gpio_chip *chip, unsigned gpio, int value)
{
	return bfin_gpio_set_value(gpio, value);
}

static int bfin_gpiolib_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_request(gpio, chip->label);
}

static void bfin_gpiolib_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	return bfin_gpio_free(gpio);
}

static int bfin_gpiolib_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	return gpio + GPIO_IRQ_BASE;
}

static struct gpio_chip bfin_chip = {
	.label			= "BFIN-GPIO",
	.direction_input	= bfin_gpiolib_direction_input,
	.get			= bfin_gpiolib_get_value,
	.direction_output	= bfin_gpiolib_direction_output,
	.set			= bfin_gpiolib_set_value,
	.request		= bfin_gpiolib_gpio_request,
	.free			= bfin_gpiolib_gpio_free,
	.to_irq			= bfin_gpiolib_gpio_to_irq,
	.base			= 0,
	.ngpio			= MAX_BLACKFIN_GPIOS,
};

static int __init bfin_gpiolib_setup(void)
{
	return gpiochip_add(&bfin_chip);
}
arch_initcall(bfin_gpiolib_setup);
#endif
