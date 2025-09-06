/*
 * File:         arch/blackfin/mach-bf527/boards/cm-bf527.c
 * Based on:     arch/blackfin/mach-bf537/boards/stamp.c
 * Author:       Aidan Williams <aidan@nicta.com.au>
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2005 National ICT Australia (NICTA)
 *               Copyright 2004-2008 Analog Devices Inc.
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

#include <linux/device.h>
 * Copyright 2004-2009 Analog Devices Inc.
 *           2008-2009 Bluetechnix
 *                2005 National ICT Australia (NICTA)
 *                      Aidan Williams <aidan@nicta.com.au>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/etherdevice.h>
#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
#include <linux/usb/isp1362.h>
#endif
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/usb/sl811.h>
#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
#include <linux/usb/musb.h>
#endif
#include <asm/cplb.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/usb/musb.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/nand.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>
#include <linux/spi/ad7877.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "Bluetechnix CM-BF527";

/*
 *  Driver needs to know address, irq and flag pin.
 */

#define ISP1761_BASE       0x203C0000
#define ISP1761_IRQ        IRQ_PF7

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
static struct resource bfin_isp1761_resources[] = {
	[0] = {
		.name	= "isp1761-regs",
		.start  = ISP1761_BASE + 0x00000000,
		.end    = ISP1761_BASE + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = ISP1761_IRQ,
		.end    = ISP1761_IRQ,
#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
#include <linux/usb/isp1760.h>
static struct resource bfin_isp1760_resources[] = {
	[0] = {
		.start  = 0x203C0000,
		.end    = 0x203C0000 + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_PF7,
		.end    = IRQ_PF7,
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

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
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

#if IS_ENABLED(CONFIG_USB_MUSB_HDRC)
static struct resource musb_resources[] = {
	[0] = {
		.start	= 0xffc03800,
		.end	= 0xffc03cff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {	/* general IRQ */
		.start	= IRQ_USB_INT0,
		.end	= IRQ_USB_INT0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
		.name	= "mc"
	},
	[2] = {	/* DMA IRQ */
		.start	= IRQ_USB_DMA,
		.end	= IRQ_USB_DMA,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

		.name	= "dma"
	},
};

static struct musb_hdrc_config musb_config = {
	.multipoint	= 0,
	.dyn_fifo	= 0,
	.soft_con	= 1,
	.dma		= 1,
	.num_eps	= 8,
	.dma_channels	= 8,
	.gpio_vrsel	= GPIO_PF11,
	/* Some custom boards need to be active low, just set it to "0"
	 * if it is the case.
	 */
	.gpio_vrsel_active	= 1,
	.clkin          = 24,           /* musb CLKIN in MHZ */
};

static struct musb_hdrc_platform_data musb_plat = {
#if defined(CONFIG_USB_MUSB_OTG)
	.mode		= MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
	.mode		= MUSB_HOST,
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
	.mode		= MUSB_PERIPHERAL,
#endif
	.multipoint	= 0,
	.config		= &musb_config,
};

static u64 musb_dmamask = ~(u32)0;

static struct platform_device musb_device = {
	.name		= "musb_hdrc",
	.name		= "musb-blackfin",
	.id		= 0,
	.dev = {
		.dma_mask		= &musb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &musb_plat,
	},
	.num_resources	= ARRAY_SIZE(musb_resources),
	.resource	= musb_resources,
};
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
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
	.end   = 0x201fffff,
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

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
#if IS_ENABLED(CONFIG_MTD_NAND_BF5XX)
static struct mtd_partition partition_info[] = {
	{
		.name = "linux kernel(nand)",
		.offset = 0,
		.size = 4 * SIZE_1M,
		.size = 4 * 1024 * 1024,
	},
	{
		.name = "file system(nand)",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct bf5xx_nand_platform bf5xx_nand_platform = {
	.page_size = NFC_PG_SIZE_256,
	.data_width = NFC_NWIDTH_8,
	.partitions = partition_info,
	.nr_partitions = ARRAY_SIZE(partition_info),
	.rd_dly = 3,
	.wr_dly = 3,
};

static struct resource bf5xx_nand_resources[] = {
	{
		.start = NFC_CTL,
		.end = NFC_DATA_RD + 2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = CH_NFC,
		.end = CH_NFC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bf5xx_nand_device = {
	.name = "bf5xx-nand",
	.id = 0,
	.num_resources = ARRAY_SIZE(bf5xx_nand_resources),
	.resource = bf5xx_nand_resources,
	.dev = {
		.platform_data = &bf5xx_nand_platform,
	},
};
#endif

#if defined(CONFIG_BFIN_CFPCMCIA) || defined(CONFIG_BFIN_CFPCMCIA_MODULE)
#if IS_ENABLED(CONFIG_BFIN_CFPCMCIA)
static struct resource bfin_pcmcia_cf_resources[] = {
	{
		.start = 0x20310000, /* IO PORT */
		.end = 0x20312000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20311000, /* Attribute Memory */
		.end = 0x20311FFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF4,
		.end = IRQ_PF4,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	}, {
		.start = 6, /* Card Detect PF6 */
		.end = 6,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_pcmcia_cf_device = {
	.name = "bfin_cf_pcmcia",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_pcmcia_cf_resources),
	.resource = bfin_pcmcia_cf_resources,
};
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
#if IS_ENABLED(CONFIG_RTC_DRV_BFIN)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
#if IS_ENABLED(CONFIG_SMC91X)
#include <linux/smc91x.h>

static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda = RPC_LED_100_10,
	.ledb = RPC_LED_TX_RX,
};

static struct resource smc91x_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x20300300,
		.end = 0x20300300 + 16,
		.flags = IORESOURCE_MEM,
	}, {

		.start = IRQ_PF7,
		.end = IRQ_PF7,
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

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= 0x203FB800,
		.end	= 0x203FB800 + 8,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};
#endif

#if IS_ENABLED(CONFIG_DM9000)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= 0x203FB800,
		.end	= 0x203FB800 + 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0x203FB804,
		.end	= 0x203FB804 + 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_PF9,
		.end	= IRQ_PF9,
		.flags	= (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE),
	},
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
};
#endif

#if defined(CONFIG_USB_SL811_HCD) || defined(CONFIG_USB_SL811_HCD_MODULE)
static struct resource sl811_hcd_resources[] = {
	{
		.start = 0x20340000,
		.end = 0x20340000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20340004,
		.end = 0x20340004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = CONFIG_USB_SL811_BFIN_IRQ,
		.end = CONFIG_USB_SL811_BFIN_IRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

#if defined(CONFIG_USB_SL811_BFIN_USE_VBUS)
void sl811_port_power(struct device *dev, int is_on)
{
	gpio_request(CONFIG_USB_SL811_BFIN_GPIO_VBUS, "usb:SL811_VBUS");
	gpio_direction_output(CONFIG_USB_SL811_BFIN_GPIO_VBUS, is_on);
}
#endif

static struct sl811_platform_data sl811_priv = {
	.potpg = 10,
	.power = 250,       /* == 500mA */
#if defined(CONFIG_USB_SL811_BFIN_USE_VBUS)
	.port_power = &sl811_port_power,
#endif
};

static struct platform_device sl811_hcd_device = {
	.name = "sl811-hcd",
	.id = 0,
	.dev = {
		.platform_data = &sl811_priv,
	},
	.num_resources = ARRAY_SIZE(sl811_hcd_resources),
	.resource = sl811_hcd_resources,
};
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
static struct resource isp1362_hcd_resources[] = {
	{
		.start = 0x20360000,
		.end = 0x20360000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20360004,
		.end = 0x20360004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = CONFIG_USB_ISP1362_BFIN_GPIO_IRQ,
		.end = CONFIG_USB_ISP1362_BFIN_GPIO_IRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
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

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
static struct platform_device bfin_mac_device = {
	.name = "bfin_mac",
};
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
#if IS_ENABLED(CONFIG_BFIN_MAC)
#include <linux/bfin_mac.h>
static const unsigned short bfin_mac_peripherals[] = P_RMII0;

static struct bfin_phydev_platform_data bfin_phydev_data[] = {
	{
		.addr = 1,
		.irq = IRQ_MAC_PHYINT,
	},
};

static struct bfin_mii_bus_platform_data bfin_mii_bus_data = {
	.phydev_number = 1,
	.phydev_data = bfin_phydev_data,
	.phy_mode = PHY_INTERFACE_MODE_RMII,
	.mac_peripherals = bfin_mac_peripherals,
};

static struct platform_device bfin_mii_bus = {
	.name = "bfin_mii_bus",
	.dev = {
		.platform_data = &bfin_mii_bus_data,
	}
};

static struct platform_device bfin_mac_device = {
	.name = "bfin_mac",
	.dev = {
		.platform_data = &bfin_mii_bus,
	}
};
#endif

#if IS_ENABLED(CONFIG_USB_NET2272)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x20300000,
		.end = 0x20300000 + 0x100,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF7,
		.end = IRQ_PF7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device net2272_bfin_device = {
	.name = "net2272",
	.id = -1,
	.num_resources = ARRAY_SIZE(net2272_bfin_resources),
	.resource = net2272_bfin_resources,
};
#endif

#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
#if IS_ENABLED(CONFIG_MTD_M25P80)
static struct mtd_partition bfin_spi_flash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_flash_data = {
	.name = "m25p80",
	.parts = bfin_spi_flash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_flash_partitions),
	.type = "m25p16",
};

/* SPI flash chip (m25p64) */
static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_SPI_ADC_BF533) \
	|| defined(CONFIG_SPI_ADC_BF533_MODULE)
/* SPI ADC chip */
static struct bfin5xx_spi_chip spi_adc_chip_info = {
	.enable_dma = 1,         /* use dma transfer with this chip*/
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
static struct bfin5xx_spi_chip ad1836_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_AD9960) || defined(CONFIG_AD9960_MODULE)
static struct bfin5xx_spi_chip ad9960_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SPI_MMC) || defined(CONFIG_SPI_MMC_MODULE)
static struct bfin5xx_spi_chip spi_mmc_chip_info = {
	.enable_dma = 1,
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_PBX)
static struct bfin5xx_spi_chip spi_si3xxx_chip_info = {
	.ctl_reg	= 0x4, /* send zero */
	.enable_dma	= 0,
	.bits_per_word	= 8,
	.cs_change_per_word = 1,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
static struct bfin5xx_spi_chip spi_ad7877_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};

};
#endif

#if IS_ENABLED(CONFIG_MMC_SPI)
static struct bfin5xx_spi_chip  mmc_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7877)
static const struct ad7877_platform_data bfin_ad7877_ts_info = {
	.model			= 7877,
	.vref_delay_usecs	= 50,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.pressure_max		= 1000,
	.pressure_min		= 0,
	.stopacq_polarity 	= 1,
	.first_conversion_delay = 3,
	.acquisition_time 	= 1,
	.averaging 		= 1,
	.pen_down_acc_interval 	= 1,
};
#endif

#if defined(CONFIG_SND_SOC_WM8731) || defined(CONFIG_SND_SOC_WM8731_MODULE) \
	 && defined(CONFIG_SND_SOC_WM8731_SPI)
static struct bfin5xx_spi_chip spi_wm8731_chip_info = {
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

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if IS_ENABLED(CONFIG_MTD_M25P80)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. On STAMP537 it is SPISSEL1*/
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if defined(CONFIG_SPI_ADC_BF533) \
	|| defined(CONFIG_SPI_ADC_BF533_MODULE)
	{
		.modalias = "bfin_spi_adc", /* Name of spi_driver for this device */
		.max_speed_hz = 6250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. */
		.platform_data = NULL, /* No spi_driver specific config */
		.controller_data = &spi_adc_chip_info,
	},
#endif

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
#if defined(CONFIG_AD9960) || defined(CONFIG_AD9960_MODULE)
	{
		.modalias = "ad9960-spi",
		.max_speed_hz = 10000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = &ad9960_spi_chip_info,
	},
#endif
#if defined(CONFIG_SPI_MMC) || defined(CONFIG_SPI_MMC_MODULE)
	{
		.modalias = "spi_mmc_dummy",
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 0,
		.platform_data = NULL,
		.controller_data = &spi_mmc_chip_info,
		.mode = SPI_MODE_3,
	},
	{
		.modalias = "spi_mmc",
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = CONFIG_SPI_MMC_CS_CHAN,
		.platform_data = NULL,
		.controller_data = &spi_mmc_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_PBX)
	{
		.modalias = "fxs-spi",
		.max_speed_hz = 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 8 - CONFIG_J11_JUMPER,
		.controller_data = &spi_si3xxx_chip_info,
		.mode = SPI_MODE_3,
	},
	{
		.modalias = "fxo-spi",
		.max_speed_hz = 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 8 - CONFIG_J19_JUMPER,
		.controller_data = &spi_si3xxx_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
#if IS_ENABLED(CONFIG_SND_BF5XX_SOC_AD183X)
	{
		.modalias = "ad183x",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
	},
#endif
#if IS_ENABLED(CONFIG_MMC_SPI)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5,
		.controller_data = &mmc_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_AD7877)
	{
		.modalias		= "ad7877",
		.platform_data		= &bfin_ad7877_ts_info,
		.irq			= IRQ_PF8,
		.max_speed_hz	= 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select  = 2,
		.controller_data = &spi_ad7877_chip_info,
	},
#endif
#if defined(CONFIG_SND_SOC_WM8731) || defined(CONFIG_SND_SOC_WM8731_MODULE) \
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_WM8731) \
	 && defined(CONFIG_SND_SOC_WM8731_SPI)
	{
		.modalias	= "wm8731",
		.max_speed_hz	= 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select    = 5,
		.controller_data = &spi_wm8731_chip_info,
		.mode = SPI_MODE_0,
	},
#endif
#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
		.mode = SPI_MODE_0,
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

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

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
	},
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
#endif  /* spi master and devices */

#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
static struct platform_device bfin_fb_adv7393_device = {
	.name = "bfin-adv7393",
};
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
static struct resource bfin_uart_resources[] = {
#ifdef CONFIG_SERIAL_BFIN_UART0
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	{
		.start = 0xFFC02000,
		.end = 0xFFC020FF,
		.flags = IORESOURCE_MEM,
	},
#endif
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
#if IS_ENABLED(CONFIG_MTD_GPIO_ADDR)
static struct mtd_partition cm_partitions[] = {
	{
		.name   = "bootloader(nor)",
		.size   = 0x40000,
		.offset = 0,
	}, {
		.name   = "linux kernel(nor)",
		.size   = 0x100000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name   = "file system(nor)",
		.size   = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data cm_flash_data = {
	.width    = 2,
	.parts    = cm_partitions,
	.nr_parts = ARRAY_SIZE(cm_partitions),
};

static unsigned cm_flash_gpios[] = { GPIO_PH9, GPIO_PG11 };

static struct resource cm_flash_resource[] = {
	{
		.name  = "cfi_probe",
		.start = 0x20000000,
		.end   = 0x201fffff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = (unsigned long)cm_flash_gpios,
		.end   = ARRAY_SIZE(cm_flash_gpios),
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device cm_flash_device = {
	.name          = "gpio-addr-flash",
	.id            = 0,
	.dev = {
		.platform_data = &cm_flash_data,
	},
	.num_resources = ARRAY_SIZE(cm_flash_resource),
	.resource      = cm_flash_resource,
};
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = UART0_THR,
		.end = UART0_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_TX,
		.end = IRQ_UART0_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_ERROR,
		.end = IRQ_UART0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_TX,
		.end = CH_UART0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX,
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
#ifdef CONFIG_SERIAL_BFIN_UART1
static struct resource bfin_uart1_resources[] = {
	{
		.start = UART1_THR,
		.end = UART1_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART1_TX,
		.end = IRQ_UART1_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_RX,
		.end = IRQ_UART1_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_ERROR,
		.end = IRQ_UART1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_TX,
		.end = CH_UART1_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART1_RX,
		.end = CH_UART1_RX,
		.flags = IORESOURCE_DMA,
	},
#ifdef CONFIG_BFIN_UART1_CTSRTS
	{	/* CTS pin */
		.start = GPIO_PF9,
		.end = GPIO_PF9,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin */
		.start = GPIO_PF10,
		.end = GPIO_PF10,
		.flags = IORESOURCE_IO,
	},
#endif
};

static unsigned short bfin_uart1_peripherals[] = {
	P_UART1_TX, P_UART1_RX, 0
};

static struct platform_device bfin_uart1_device = {
	.name = "bfin-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_uart1_resources),
	.resource = bfin_uart1_resources,
	.dev = {
		.platform_data = &bfin_uart1_peripherals, /* Passed to driver */
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
#ifdef CONFIG_BFIN_SIR1
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
#ifdef CONFIG_BFIN_SIR1
static struct resource bfin_sir1_resources[] = {
	{
		.start = 0xFFC02000,
		.end = 0xFFC020FF,
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

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
	{
		.start = IRQ_UART1_RX,
		.end = IRQ_UART1_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_RX,
		.end = CH_UART1_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir1_device = {
	.name = "bfin_sir",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_sir1_resources),
	.resource = bfin_sir1_resources,
};
#endif
#endif

#if IS_ENABLED(CONFIG_I2C_BLACKFIN_TWI)
static const u16 bfin_twi0_pins[] = {P_TWI0_SCL, P_TWI0_SDA, 0};

static struct resource bfin_twi0_resource[] = {
	[0] = {
		.start = TWI0_REGBASE,
		.end   = TWI0_REGBASE,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TWI,
		.end   = IRQ_TWI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_bfin_twi_device = {
	.name = "i2c-bfin-twi",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_twi0_resource),
	.resource = bfin_twi0_resource,
};
#endif

#ifdef CONFIG_I2C_BOARDINFO
static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if defined(CONFIG_TWI_LCD) || defined(CONFIG_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
		.type = "pcf8574_lcd",
	},
#endif
#if defined(CONFIG_TWI_KEYPAD) || defined(CONFIG_TWI_KEYPAD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.type = "pcf8574_keypad",
		.irq = IRQ_PF8,
	},
#endif
};
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
static struct platform_device bfin_sport0_uart_device = {
	.name = "bfin-sport-uart",
	.id = 0,
	.dev = {
		.platform_data = &bfin_twi0_pins,
	},
};
#endif

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if IS_ENABLED(CONFIG_BFIN_TWI_LCD)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if IS_ENABLED(CONFIG_INPUT_PCF8574)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = IRQ_PF8,
	},
#endif
#if IS_ENABLED(CONFIG_FB_BFIN_7393)
	{
		I2C_BOARD_INFO("bfin-adv7393", 0x2B),
	},
#endif
};

#if IS_ENABLED(CONFIG_SERIAL_BFIN_SPORT)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
static struct resource bfin_sport0_uart_resources[] = {
	{
		.start = SPORT0_TCR1,
		.end = SPORT0_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT0_RX,
		.end = IRQ_SPORT0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT0_ERROR,
		.end = IRQ_SPORT0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport0_peripherals[] = {
	P_SPORT0_TFS, P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_RFS,
	P_SPORT0_DRPRI, P_SPORT0_RSCLK, 0
};

static struct platform_device bfin_sport0_uart_device = {
	.name = "bfin-sport-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sport0_uart_resources),
	.resource = bfin_sport0_uart_resources,
	.dev = {
		.platform_data = &bfin_sport0_peripherals, /* Passed to driver */
	},
};
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
static struct resource bfin_sport1_uart_resources[] = {
	{
		.start = SPORT1_TCR1,
		.end = SPORT1_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT1_RX,
		.end = IRQ_SPORT1_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT1_ERROR,
		.end = IRQ_SPORT1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport1_peripherals[] = {
	P_SPORT1_TFS, P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_RFS,
	P_SPORT1_DRPRI, P_SPORT1_RSCLK, 0
};

static struct platform_device bfin_sport1_uart_device = {
	.name = "bfin-sport-uart",
	.id = 1,
};
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	.num_resources = ARRAY_SIZE(bfin_sport1_uart_resources),
	.resource = bfin_sport1_uart_resources,
	.dev = {
		.platform_data = &bfin_sport1_peripherals, /* Passed to driver */
	},
};
#endif
#endif

#if IS_ENABLED(CONFIG_KEYBOARD_GPIO)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PF14, 1, "gpio-keys: BTN0"},
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

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_100, 400000000),
	VRPAIR(VLEV_105, 426000000),
	VRPAIR(VLEV_110, 500000000),
	VRPAIR(VLEV_115, 533000000),
	VRPAIR(VLEV_120, 600000000),
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

static struct platform_device *stamp_devices[] __initdata = {

	&bfin_dpmc,

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
	&bf5xx_nand_device,
#endif

#if defined(CONFIG_BFIN_CFPCMCIA) || defined(CONFIG_BFIN_CFPCMCIA_MODULE)
	&bfin_pcmcia_cf_device,
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_USB_SL811_HCD) || defined(CONFIG_USB_SL811_HCD_MODULE)
	&sl811_hcd_device,
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
	&isp1362_hcd_device,
#endif

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
	&musb_device,
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
	&dm9000_device,
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
	&bfin_mac_device,
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	&net2272_bfin_device,
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
	&bfin_fb_adv7393_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
	&bfin_uart_device,
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
	&bfin_sir_device,
#endif

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
	&i2c_bfin_twi_device,
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
	&bfin_sport0_uart_device,
	&bfin_sport1_uart_device,
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ezkit_flash_device,
#endif

	&bfin_gpios_device,
};

static int __init stamp_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);

#ifdef CONFIG_I2C_BOARDINFO
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
#endif

	platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
static struct platform_device *cmbf527_devices[] __initdata = {

	&bfin_dpmc,

#if IS_ENABLED(CONFIG_MTD_NAND_BF5XX)
	&bf5xx_nand_device,
#endif

#if IS_ENABLED(CONFIG_BFIN_CFPCMCIA)
	&bfin_pcmcia_cf_device,
#endif

#if IS_ENABLED(CONFIG_RTC_DRV_BFIN)
	&rtc_device,
#endif

#if IS_ENABLED(CONFIG_USB_ISP1760_HCD)
	&bfin_isp1760_device,
#endif

#if IS_ENABLED(CONFIG_USB_MUSB_HDRC)
	&musb_device,
#endif

#if IS_ENABLED(CONFIG_SMC91X)
	&smc91x_device,
#endif

#if IS_ENABLED(CONFIG_DM9000)
	&dm9000_device,
#endif

#if IS_ENABLED(CONFIG_BFIN_MAC)
	&bfin_mii_bus,
	&bfin_mac_device,
#endif

#if IS_ENABLED(CONFIG_USB_NET2272)
	&net2272_bfin_device,
#endif

#if IS_ENABLED(CONFIG_SPI_BFIN5XX)
	&bfin_spi0_device,
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if IS_ENABLED(CONFIG_BFIN_SIR)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#ifdef CONFIG_BFIN_SIR1
	&bfin_sir1_device,
#endif
#endif

#if IS_ENABLED(CONFIG_I2C_BLACKFIN_TWI)
	&i2c_bfin_twi_device,
#endif

#if IS_ENABLED(CONFIG_SERIAL_BFIN_SPORT)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#endif

#if IS_ENABLED(CONFIG_KEYBOARD_GPIO)
	&bfin_device_gpiokeys,
#endif

#if IS_ENABLED(CONFIG_MTD_GPIO_ADDR)
	&cm_flash_device,
#endif
};

static int __init cm_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
	platform_add_devices(cmbf527_devices, ARRAY_SIZE(cmbf527_devices));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(stamp_init);
arch_initcall(cm_init);

static struct platform_device *cmbf527_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT_CONSOLE)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(cmbf527_early_devices,
		ARRAY_SIZE(cmbf527_early_devices));
}

void native_machine_restart(char *cmd)
{
	/* workaround reboot hang when booting from SPI */
	if ((bfin_read_SYSCR() & 0x7) == 0x3)
		bfin_gpio_reset_spi0_ssel1();
}

void bfin_get_ether_addr(char *addr)
{
	random_ether_addr(addr);
	printk(KERN_WARNING "%s:%s: Setting Ethernet MAC to a random one\n", __FILE__, __func__);
		bfin_reset_boot_spi_cs(P_DEFAULT_BOOT_SPI_CS);
}

int bfin_get_ether_addr(char *addr)
{
	return 1;
}
EXPORT_SYMBOL(bfin_get_ether_addr);
