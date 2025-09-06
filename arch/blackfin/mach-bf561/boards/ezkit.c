/*
 * File:         arch/blackfin/mach-bf561/ezkit.c
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
 * Copyright 2004-2009 Analog Devices Inc.
 *               2005 National ICT Australia (NICTA)
 *                    Aidan Williams <aidan@nicta.com.au>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADDS-BF561-EZKIT";

#define ISP1761_BASE       0x2C0F0000
#define ISP1761_IRQ        IRQ_PF10

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
static struct resource bfin_isp1761_resources[] = {
	{
		.name	= "isp1761-regs",
		.start  = ISP1761_BASE + 0x00000000,
		.end    = ISP1761_BASE + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = ISP1761_IRQ,
		.end    = ISP1761_IRQ,
const char bfin_board_name[] = "ADI BF561-EZKIT";

#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
#include <linux/usb/isp1760.h>
static struct resource bfin_isp1760_resources[] = {
	[0] = {
		.start  = 0x2C0F0000,
		.end    = 0x203C0000 + 0xfffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_PF10,
		.end    = IRQ_PF10,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_isp1761_device = {
	.name           = "isp1761",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(bfin_isp1761_resources),
	.resource       = bfin_isp1761_resources,
};

static struct platform_device *bfin_isp1761_devices[] = {
	&bfin_isp1761_device,
};

int __init bfin_isp1761_init(void)
{
	unsigned int num_devices = ARRAY_SIZE(bfin_isp1761_devices);

	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	set_irq_type(ISP1761_IRQ, IRQF_TRIGGER_FALLING);

	return platform_add_devices(bfin_isp1761_devices, num_devices);
}

void __exit bfin_isp1761_exit(void)
{
	platform_device_unregister(&bfin_isp1761_device);
}

arch_initcall(bfin_isp1761_init);
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
static struct isp1760_platform_data isp1760_priv = {
	.is_isp1761 = 0,
	.bus_width_16 = 1,
	.port1_otg = 0,
	.analog_oc = 0,
	.dack_polarity_high = 0,
	.dreq_polarity_high = 0,
};

static struct platform_device bfin_isp1760_device = {
	.name           = "isp1760",
	.id             = 0,
	.dev = {
		.platform_data = &isp1760_priv,
	},
	.num_resources  = ARRAY_SIZE(bfin_isp1760_resources),
	.resource       = bfin_isp1760_resources,
};
#endif

#if IS_ENABLED(CONFIG_USB_ISP1362_HCD)
#include <linux/usb/isp1362.h>

static struct resource isp1362_hcd_resources[] = {
	{
		.start = 0x2c060000,
		.end = 0x2c060000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x2c060004,
		.end = 0x2c060004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF8,
		.end = IRQ_PF8,
		.flags = IORESOURCE_IRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct isp1362_platform_data isp1362_priv = {
	.sel15Kres = 1,
	.clknotstop = 0,
	.oc_enable = 0,
	.int_act_high = 0,
	.int_edge_triggered = 0,
	.remote_wakeup_connected = 0,
	.no_power_switching = 1,
	.power_switching_mode = 0,
};

static struct platform_device isp1362_hcd_device = {
	.name = "isp1362-hcd",
	.id = 0,
	.dev = {
		.platform_data = &isp1362_priv,
	},
	.num_resources = ARRAY_SIZE(isp1362_hcd_resources),
	.resource = isp1362_hcd_resources,
};
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
#if IS_ENABLED(CONFIG_USB_NET2272)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x2C000000,
		.end = 0x2C000000 + 0x7F,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 1,
		.flags = IORESOURCE_BUS,
	}, {
		.start = IRQ_PF10,
		.end = IRQ_PF10,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device net2272_bfin_device = {
	.name = "net2272",
	.id = -1,
	.num_resources = ARRAY_SIZE(net2272_bfin_resources),
	.resource = net2272_bfin_resources,
};
#endif

/*
 *  USB-LAN EzExtender board
 *  Driver needs to know address, irq and flag pin.
 */
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
#if IS_ENABLED(CONFIG_SMC91X)
#include <linux/smc91x.h>

static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_8BIT | SMC91X_USE_16BIT | SMC91X_USE_32BIT |
		 SMC91X_NOWAIT,
	.leda = RPC_LED_100_10,
	.ledb = RPC_LED_TX_RX,
};

static struct resource smc91x_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x2C010300,
		.end = 0x2C010300 + 16,
		.flags = IORESOURCE_MEM,
	}, {

		.start = IRQ_PF9,
		.end = IRQ_PF9,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device smc91x_device = {
	.name = "smc91x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smc91x_resources),
	.resource = smc91x_resources,
};
#endif

#if defined(CONFIG_AX88180) || defined(CONFIG_AX88180_MODULE)
static struct resource ax88180_resources[] = {
	[0] = {
		.start	= 0x2c000000,
		.end	= 0x2c000000 + 0x8000,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PF10,
		.end	= IRQ_PF10,
		.flags	= (IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL),
	},
};

static struct platform_device ax88180_device = {
	.name		= "ax88180",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ax88180_resources),
	.resource	= ax88180_resources,
};
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
static struct resource bfin_uart_resources[] = {
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device bfin_uart_device = {
	.name = "bfin-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_uart_resources),
	.resource = bfin_uart_resources,
};
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
static struct resource bfin_sir_resources[] = {
#ifdef CONFIG_BFIN_SIR0
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = BFIN_UART_THR,
		.end = BFIN_UART_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART_TX,
		.end = IRQ_UART_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART_RX,
		.end = IRQ_UART_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART_ERROR,
		.end = IRQ_UART_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART_TX,
		.end = CH_UART_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART_RX,
		.end = CH_UART_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart0_peripherals[] = {
	P_UART0_TX, P_UART0_RX, 0
};

static struct platform_device bfin_uart0_device = {
	.name = "bfin-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_uart0_resources),
	.resource = bfin_uart0_resources,
	.dev = {
		.platform_data = &bfin_uart0_peripherals, /* Passed to driver */
	},
};
#endif
#endif

#if IS_ENABLED(CONFIG_BFIN_SIR)
#ifdef CONFIG_BFIN_SIR0
static struct resource bfin_sir0_resources[] = {
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
#endif
};

static struct platform_device bfin_sir_device = {
	.name = "bfin_sir",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sir_resources),
	.resource = bfin_sir_resources,
};
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir0_device = {
	.name = "bfin_sir",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sir0_resources),
	.resource = bfin_sir0_resources,
};
#endif
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP)
static struct mtd_partition ezkit_partitions[] = {
	{
		.name       = "bootloader(nor)",
		.size       = 0x40000,
		.offset     = 0,
	}, {
		.name       = "linux kernel(nor)",
		.size       = 0x1C0000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "file system(nor)",
		.size       = MTDPART_SIZ_FULL,
		.size       = 0x800000 - 0x40000 - 0x1C0000 - 0x2000 * 8,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "config(nor)",
		.size       = 0x2000 * 7,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "u-boot env(nor)",
		.size       = 0x2000,
		.offset     = MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data ezkit_flash_data = {
	.width      = 2,
	.parts      = ezkit_partitions,
	.nr_parts   = ARRAY_SIZE(ezkit_partitions),
};

static struct resource ezkit_flash_resource = {
	.start = 0x20000000,
	.end   = 0x207fffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device ezkit_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &ezkit_flash_data,
	},
	.num_resources = 1,
	.resource      = &ezkit_flash_resource,
};
#endif

#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
static struct bfin5xx_spi_chip ad1836_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
static struct bfin5xx_spi_chip spidev_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	[0] = {
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = CH_SPI,
		.end   = CH_SPI,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI,
		.end   = IRQ_SPI,
		.flags = IORESOURCE_IRQ,
	}
};

/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct platform_device bfin_spi0_device = {
	.name = "bfin-spi",
	.id = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bfin_spi0_info, /* Passed to driver */
	},
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
	{
		.modalias = "ad1836-spi",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = CONFIG_SND_BLACKFIN_SPI_PFBIT,
		.controller_data = &ad1836_spi_chip_info,
	},
#endif
#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD183X)
	{
		.modalias = "ad183x",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
		.platform_data = "ad1836", /* only includes chip name for the moment */
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_SPI_SPIDEV)
	{
		.modalias = "spidev",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = &spidev_chip_info,
	},
#endif
};

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#if IS_ENABLED(CONFIG_KEYBOARD_GPIO)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PF5, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PF6, 1, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PF7, 1, "gpio-keys: BTN2"},
	{BTN_3, GPIO_PF8, 1, "gpio-keys: BTN3"},
};

static struct gpio_keys_platform_data bfin_gpio_keys_data = {
	.buttons        = bfin_gpio_keys_table,
	.nbuttons       = ARRAY_SIZE(bfin_gpio_keys_table),
};

static struct platform_device bfin_device_gpiokeys = {
	.name      = "gpio-keys",
	.dev = {
		.platform_data = &bfin_gpio_keys_data,
	},
};
#endif

static struct resource bfin_gpios_resources = {
	.start = 0,
	.end   = MAX_BLACKFIN_GPIOS - 1,
	.flags = IORESOURCE_IRQ,
};

static struct platform_device bfin_gpios_device = {
	.name = "simple-gpio",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_gpios_resources,
};

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
#include <linux/i2c-gpio.h>

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= 1,
	.scl_pin		= 0,
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.udelay			= 40,
#if IS_ENABLED(CONFIG_I2C_GPIO)
#include <linux/i2c-gpio.h>

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= GPIO_PF1,
	.scl_pin		= GPIO_PF0,
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.udelay			= 10,
};

static struct platform_device i2c_gpio_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &i2c_gpio_data,
	},
};
#endif

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_085, 250000000),
	VRPAIR(VLEV_090, 300000000),
	VRPAIR(VLEV_095, 313000000),
	VRPAIR(VLEV_100, 350000000),
	VRPAIR(VLEV_105, 400000000),
	VRPAIR(VLEV_110, 444000000),
	VRPAIR(VLEV_115, 450000000),
	VRPAIR(VLEV_120, 475000000),
	VRPAIR(VLEV_125, 500000000),
	VRPAIR(VLEV_130, 600000000),
};

static struct bfin_dpmc_platform_data bfin_dmpc_vreg_data = {
	.tuple_tab = cclk_vlev_datasheet,
	.tabsize = ARRAY_SIZE(cclk_vlev_datasheet),
	.vr_settling_time = 25 /* us */,
};

static struct platform_device bfin_dpmc = {
	.name = "bfin dpmc",
	.dev = {
		.platform_data = &bfin_dmpc_vreg_data,
	},
};

#if IS_ENABLED(CONFIG_VIDEO_BLACKFIN_CAPTURE)
#include <linux/videodev2.h>
#include <media/blackfin/bfin_capture.h>
#include <media/blackfin/ppi.h>

static const unsigned short ppi_req[] = {
	P_PPI0_D0, P_PPI0_D1, P_PPI0_D2, P_PPI0_D3,
	P_PPI0_D4, P_PPI0_D5, P_PPI0_D6, P_PPI0_D7,
	P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
	0,
};

static const struct ppi_info ppi_info = {
	.type = PPI_TYPE_PPI,
	.dma_ch = CH_PPI0,
	.irq_err = IRQ_PPI1_ERROR,
	.base = (void __iomem *)PPI0_CONTROL,
	.pin_req = ppi_req,
};

#if IS_ENABLED(CONFIG_VIDEO_ADV7183)
#include <media/adv7183.h>
static struct v4l2_input adv7183_inputs[] = {
	{
		.index = 0,
		.name = "Composite",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
		.capabilities = V4L2_IN_CAP_STD,
	},
	{
		.index = 1,
		.name = "S-Video",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
		.capabilities = V4L2_IN_CAP_STD,
	},
	{
		.index = 2,
		.name = "Component",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
		.capabilities = V4L2_IN_CAP_STD,
	},
};

static struct bcap_route adv7183_routes[] = {
	{
		.input = ADV7183_COMPOSITE4,
		.output = ADV7183_8BIT_OUT,
	},
	{
		.input = ADV7183_SVIDEO0,
		.output = ADV7183_8BIT_OUT,
	},
	{
		.input = ADV7183_COMPONENT0,
		.output = ADV7183_8BIT_OUT,
	},
};


static const unsigned adv7183_gpio[] = {
	GPIO_PF13, /* reset pin */
	GPIO_PF2,  /* output enable pin */
};

static struct bfin_capture_config bfin_capture_data = {
	.card_name = "BF561",
	.inputs = adv7183_inputs,
	.num_inputs = ARRAY_SIZE(adv7183_inputs),
	.routes = adv7183_routes,
	.i2c_adapter_id = 0,
	.board_info = {
		.type = "adv7183",
		.addr = 0x20,
		.platform_data = (void *)adv7183_gpio,
	},
	.ppi_info = &ppi_info,
	.ppi_control = (PACK_EN | DLEN_8 | DMA32 | FLD_SEL),
};
#endif

static struct platform_device bfin_capture_device = {
	.name = "bfin_capture",
	.dev = {
		.platform_data = &bfin_capture_data,
	},
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_I2S)
static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	/* TODO: add platform data here */
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_AC97)
static struct platform_device bfin_ac97 = {
	.name = "bfin-ac97",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	/* TODO: add platform data here */
};
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD1836)
static const char * const ad1836_link[] = {
	"bfin-i2s.0",
	"spi0.4",
};
static struct platform_device bfin_ad1836_machine = {
	.name = "bfin-snd-ad1836",
	.id = -1,
	.dev = {
		.platform_data = (void *)ad1836_link,
	},
};
#endif

static struct platform_device *ezkit_devices[] __initdata = {

	&bfin_dpmc,

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_AX88180) || defined(CONFIG_AX88180_MODULE)
	&ax88180_device,
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	&net2272_bfin_device,
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
	&bfin_uart_device,
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
	&bfin_sir_device,
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
	&i2c_gpio_device,
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
	&isp1362_hcd_device,
#endif

	&bfin_gpios_device,

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ezkit_flash_device,
#endif
};

#if IS_ENABLED(CONFIG_SMC91X)
	&smc91x_device,
#endif

#if IS_ENABLED(CONFIG_USB_NET2272)
	&net2272_bfin_device,
#endif

#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
	&bfin_isp1760_device,
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
	&bfin_spi0_device,
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif

#if IS_ENABLED(CONFIG_BFIN_SIR)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#endif

#if IS_ENABLED(CONFIG_KEYBOARD_GPIO)
	&bfin_device_gpiokeys,
#endif

#if IS_ENABLED(CONFIG_I2C_GPIO)
	&i2c_gpio_device,
#endif

#if IS_ENABLED(CONFIG_USB_ISP1362_HCD)
	&isp1362_hcd_device,
#endif

#if IS_ENABLED(CONFIG_MTD_PHYSMAP)
	&ezkit_flash_device,
#endif

#if IS_ENABLED(CONFIG_VIDEO_BLACKFIN_CAPTURE)
	&bfin_capture_device,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_I2S)
	&bfin_i2s,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_AC97)
	&bfin_ac97,
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD1836)
	&bfin_ad1836_machine,
#endif
};

static int __init net2272_init(void)
{
#if IS_ENABLED(CONFIG_USB_NET2272)
	int ret;

	ret = gpio_request(GPIO_PF11, "net2272");
	if (ret)
		return ret;

	/* Reset the USB chip */
	gpio_direction_output(GPIO_PF11, 0);
	mdelay(2);
	gpio_set_value(GPIO_PF11, 1);
#endif

	return 0;
}

static int __init ezkit_init(void)
{
	int ret;

	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	ret = platform_add_devices(ezkit_devices, ARRAY_SIZE(ezkit_devices));
	if (ret < 0)
		return ret;

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
#if IS_ENABLED(CONFIG_SMC91X)
	bfin_write_FIO0_DIR(bfin_read_FIO0_DIR() | (1 << 12));
	SSYNC();
#endif

#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD183X)
	bfin_write_FIO0_DIR(bfin_read_FIO0_DIR() | (1 << 15));
	bfin_write_FIO0_FLAG_S(1 << 15);
	SSYNC();
	/*
	 * This initialization lasts for approximately 4500 MCLKs.
	 * MCLK = 12.288MHz
	 */
	udelay(400);
#endif

	if (net2272_init())
		pr_warning("unable to configure net2272; it probably won't work\n");

	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(ezkit_init);

static struct platform_device *ezkit_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(ezkit_early_devices,
		ARRAY_SIZE(ezkit_early_devices));
}
