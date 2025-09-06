/*
 * arch/arm/mach-orion5x/common.c
 *
 * Core functions for Marvell Orion 5x SoCs
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/mbus.h>
#include <linux/mv643xx_eth.h>
#include <linux/mv643xx_i2c.h>
#include <linux/ata_platform.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/timex.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/hardware.h>
#include <mach/orion5x.h>
#include <plat/ehci-orion.h>
#include <plat/mv_xor.h>
#include <plat/orion_nand.h>
#include <plat/time.h>
#include <linux/dma-mapping.h>
#include <linux/serial_8250.h>
#include <linux/mv643xx_i2c.h>
#include <linux/ata_platform.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <net/dsa.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/system_misc.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <mach/bridge-regs.h>
#include <mach/hardware.h>
#include <mach/orion5x.h>
#include <linux/platform_data/mtd-orion_nand.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <plat/time.h>
#include <plat/common.h>
#include "common.h"

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc orion5x_io_desc[] __initdata = {
	{
		.virtual	= ORION5X_REGS_VIRT_BASE,
		.virtual	= (unsigned long) ORION5X_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(ORION5X_REGS_PHYS_BASE),
		.length		= ORION5X_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= ORION5X_PCIE_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(ORION5X_PCIE_IO_PHYS_BASE),
		.length		= ORION5X_PCIE_IO_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= ORION5X_PCI_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(ORION5X_PCI_IO_PHYS_BASE),
		.length		= ORION5X_PCI_IO_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= ORION5X_PCIE_WA_VIRT_BASE,
		.virtual	= (unsigned long) ORION5X_PCIE_WA_VIRT_BASE,
		.pfn		= __phys_to_pfn(ORION5X_PCIE_WA_PHYS_BASE),
		.length		= ORION5X_PCIE_WA_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init orion5x_map_io(void)
{
	iotable_init(orion5x_io_desc, ARRAY_SIZE(orion5x_io_desc));
}


/*****************************************************************************
 * EHCI
 ****************************************************************************/
static struct orion_ehci_data orion5x_ehci_data = {
	.dram		= &orion5x_mbus_dram_info,
};

static u64 ehci_dmamask = 0xffffffffUL;

 * CLK tree
 ****************************************************************************/
static struct clk *tclk;

void __init clk_init(void)
{
	tclk = clk_register_fixed_rate(NULL, "tclk", NULL, CLK_IS_ROOT,
				       orion5x_tclk);

	orion_clkdev_init(tclk);
}

/*****************************************************************************
 * EHCI0
 ****************************************************************************/
static struct resource orion5x_ehci0_resources[] = {
	{
		.start	= ORION5X_USB0_PHYS_BASE,
		.end	= ORION5X_USB0_PHYS_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_ORION5X_USB0_CTRL,
		.end	= IRQ_ORION5X_USB0_CTRL,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_ehci0 = {
	.name		= "orion-ehci",
	.id		= 0,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &orion5x_ehci_data,
	},
	.resource	= orion5x_ehci0_resources,
	.num_resources	= ARRAY_SIZE(orion5x_ehci0_resources),
};

void __init orion5x_ehci0_init(void)
{
	platform_device_register(&orion5x_ehci0);
void __init orion5x_ehci0_init(void)
{
	orion_ehci_init(ORION5X_USB0_PHYS_BASE, IRQ_ORION5X_USB0_CTRL,
			EHCI_PHY_ORION);
}


/*****************************************************************************
 * EHCI1
 ****************************************************************************/
static struct resource orion5x_ehci1_resources[] = {
	{
		.start	= ORION5X_USB1_PHYS_BASE,
		.end	= ORION5X_USB1_PHYS_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_ORION5X_USB1_CTRL,
		.end	= IRQ_ORION5X_USB1_CTRL,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_ehci1 = {
	.name		= "orion-ehci",
	.id		= 1,
	.dev		= {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &orion5x_ehci_data,
	},
	.resource	= orion5x_ehci1_resources,
	.num_resources	= ARRAY_SIZE(orion5x_ehci1_resources),
};

void __init orion5x_ehci1_init(void)
{
	platform_device_register(&orion5x_ehci1);
void __init orion5x_ehci1_init(void)
{
	orion_ehci_1_init(ORION5X_USB1_PHYS_BASE, IRQ_ORION5X_USB1_CTRL);
}


/*****************************************************************************
 * GigE
 ****************************************************************************/
struct mv643xx_eth_shared_platform_data orion5x_eth_shared_data = {
	.dram		= &orion5x_mbus_dram_info,
	.t_clk		= ORION5X_TCLK,
};

static struct resource orion5x_eth_shared_resources[] = {
	{
		.start	= ORION5X_ETH_PHYS_BASE + 0x2000,
		.end	= ORION5X_ETH_PHYS_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device orion5x_eth_shared = {
	.name		= MV643XX_ETH_SHARED_NAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &orion5x_eth_shared_data,
	},
	.num_resources	= 1,
	.resource	= orion5x_eth_shared_resources,
};

static struct resource orion5x_eth_resources[] = {
	{
		.name	= "eth irq",
		.start	= IRQ_ORION5X_ETH_SUM,
		.end	= IRQ_ORION5X_ETH_SUM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_eth = {
	.name		= MV643XX_ETH_NAME,
	.id		= 0,
	.num_resources	= 1,
	.resource	= orion5x_eth_resources,
};

void __init orion5x_eth_init(struct mv643xx_eth_platform_data *eth_data)
{
	eth_data->shared = &orion5x_eth_shared;
	orion5x_eth.dev.platform_data = eth_data;

	platform_device_register(&orion5x_eth_shared);
	platform_device_register(&orion5x_eth);
 * GE00
 ****************************************************************************/
void __init orion5x_eth_init(struct mv643xx_eth_platform_data *eth_data)
{
	orion_ge00_init(eth_data,
			ORION5X_ETH_PHYS_BASE, IRQ_ORION5X_ETH_SUM,
			IRQ_ORION5X_ETH_ERR,
			MV643XX_TX_CSUM_DEFAULT_LIMIT);
}


/*****************************************************************************
 * Ethernet switch
 ****************************************************************************/
void __init orion5x_eth_switch_init(struct dsa_platform_data *d, int irq)
{
	orion_ge00_switch_init(d, irq);
}


/*****************************************************************************
 * I2C
 ****************************************************************************/
static struct mv64xxx_i2c_pdata orion5x_i2c_pdata = {
	.freq_m		= 8, /* assumes 166 MHz TCLK */
	.freq_n		= 3,
	.timeout	= 1000, /* Default timeout of 1 second */
};

static struct resource orion5x_i2c_resources[] = {
	{
		.name	= "i2c base",
		.start	= I2C_PHYS_BASE,
		.end	= I2C_PHYS_BASE + 0x1f,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "i2c irq",
		.start	= IRQ_ORION5X_I2C,
		.end	= IRQ_ORION5X_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_i2c = {
	.name		= MV64XXX_I2C_CTLR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(orion5x_i2c_resources),
	.resource	= orion5x_i2c_resources,
	.dev		= {
		.platform_data	= &orion5x_i2c_pdata,
	},
};

void __init orion5x_i2c_init(void)
{
	platform_device_register(&orion5x_i2c);
void __init orion5x_i2c_init(void)
{
	orion_i2c_init(I2C_PHYS_BASE, IRQ_ORION5X_I2C, 8);

}


/*****************************************************************************
 * SATA
 ****************************************************************************/
static struct resource orion5x_sata_resources[] = {
	{
		.name	= "sata base",
		.start	= ORION5X_SATA_PHYS_BASE,
		.end	= ORION5X_SATA_PHYS_BASE + 0x5000 - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "sata irq",
		.start	= IRQ_ORION5X_SATA,
		.end	= IRQ_ORION5X_SATA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_sata = {
	.name		= "sata_mv",
	.id		= 0,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(orion5x_sata_resources),
	.resource	= orion5x_sata_resources,
};

void __init orion5x_sata_init(struct mv_sata_platform_data *sata_data)
{
	sata_data->dram = &orion5x_mbus_dram_info;
	orion5x_sata.dev.platform_data = sata_data;
	platform_device_register(&orion5x_sata);
void __init orion5x_sata_init(struct mv_sata_platform_data *sata_data)
{
	orion_sata_init(sata_data, ORION5X_SATA_PHYS_BASE, IRQ_ORION5X_SATA);
}


/*****************************************************************************
 * SPI
 ****************************************************************************/
void __init orion5x_spi_init(void)
{
	orion_spi_init(SPI_PHYS_BASE);
}


/*****************************************************************************
 * UART0
 ****************************************************************************/
static struct plat_serial8250_port orion5x_uart0_data[] = {
	{
		.mapbase	= UART0_PHYS_BASE,
		.membase	= (char *)UART0_VIRT_BASE,
		.irq		= IRQ_ORION5X_UART0,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= ORION5X_TCLK,
	}, {
	},
};

static struct resource orion5x_uart0_resources[] = {
	{
		.start		= UART0_PHYS_BASE,
		.end		= UART0_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_ORION5X_UART0,
		.end		= IRQ_ORION5X_UART0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_uart0 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= orion5x_uart0_data,
	},
	.resource		= orion5x_uart0_resources,
	.num_resources		= ARRAY_SIZE(orion5x_uart0_resources),
};

void __init orion5x_uart0_init(void)
{
	platform_device_register(&orion5x_uart0);
}


/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port orion5x_uart1_data[] = {
	{
		.mapbase	= UART1_PHYS_BASE,
		.membase	= (char *)UART1_VIRT_BASE,
		.irq		= IRQ_ORION5X_UART1,
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= ORION5X_TCLK,
	}, {
	},
};

static struct resource orion5x_uart1_resources[] = {
	{
		.start		= UART1_PHYS_BASE,
		.end		= UART1_PHYS_BASE + 0xff,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= IRQ_ORION5X_UART1,
		.end		= IRQ_ORION5X_UART1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device orion5x_uart1 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM1,
	.dev			= {
		.platform_data	= orion5x_uart1_data,
	},
	.resource		= orion5x_uart1_resources,
	.num_resources		= ARRAY_SIZE(orion5x_uart1_resources),
};

void __init orion5x_uart1_init(void)
{
	platform_device_register(&orion5x_uart1);
}


/*****************************************************************************
 * XOR engine
 ****************************************************************************/
static struct resource orion5x_xor_shared_resources[] = {
	{
		.name	= "xor low",
		.start	= ORION5X_XOR_PHYS_BASE,
		.end	= ORION5X_XOR_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "xor high",
		.start	= ORION5X_XOR_PHYS_BASE + 0x200,
		.end	= ORION5X_XOR_PHYS_BASE + 0x2ff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device orion5x_xor_shared = {
	.name		= MV_XOR_SHARED_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(orion5x_xor_shared_resources),
	.resource	= orion5x_xor_shared_resources,
};

static u64 orion5x_xor_dmamask = DMA_32BIT_MASK;

static struct resource orion5x_xor0_resources[] = {
	[0] = {
		.start	= IRQ_ORION5X_XOR0,
		.end	= IRQ_ORION5X_XOR0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data orion5x_xor0_data = {
	.shared		= &orion5x_xor_shared,
	.hw_id		= 0,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device orion5x_xor0_channel = {
	.name		= MV_XOR_NAME,
	.id		= 0,
	.num_resources	= ARRAY_SIZE(orion5x_xor0_resources),
	.resource	= orion5x_xor0_resources,
	.dev		= {
		.dma_mask		= &orion5x_xor_dmamask,
		.coherent_dma_mask	= DMA_64BIT_MASK,
		.platform_data		= (void *)&orion5x_xor0_data,
	},
};

static struct resource orion5x_xor1_resources[] = {
	[0] = {
		.start	= IRQ_ORION5X_XOR1,
		.end	= IRQ_ORION5X_XOR1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mv_xor_platform_data orion5x_xor1_data = {
	.shared		= &orion5x_xor_shared,
	.hw_id		= 1,
	.pool_size	= PAGE_SIZE,
};

static struct platform_device orion5x_xor1_channel = {
	.name		= MV_XOR_NAME,
	.id		= 1,
	.num_resources	= ARRAY_SIZE(orion5x_xor1_resources),
	.resource	= orion5x_xor1_resources,
	.dev		= {
		.dma_mask		= &orion5x_xor_dmamask,
		.coherent_dma_mask	= DMA_64BIT_MASK,
		.platform_data		= (void *)&orion5x_xor1_data,
	},
};

void __init orion5x_xor_init(void)
{
	platform_device_register(&orion5x_xor_shared);

	/*
	 * two engines can't do memset simultaneously, this limitation
	 * satisfied by removing memset support from one of the engines.
	 */
	dma_cap_set(DMA_MEMCPY, orion5x_xor0_data.cap_mask);
	dma_cap_set(DMA_XOR, orion5x_xor0_data.cap_mask);
	platform_device_register(&orion5x_xor0_channel);

	dma_cap_set(DMA_MEMCPY, orion5x_xor1_data.cap_mask);
	dma_cap_set(DMA_MEMSET, orion5x_xor1_data.cap_mask);
	dma_cap_set(DMA_XOR, orion5x_xor1_data.cap_mask);
	platform_device_register(&orion5x_xor1_channel);
void __init orion5x_uart0_init(void)
{
	orion_uart0_init(UART0_VIRT_BASE, UART0_PHYS_BASE,
			 IRQ_ORION5X_UART0, tclk);
}

/*****************************************************************************
 * UART1
 ****************************************************************************/
void __init orion5x_uart1_init(void)
{
	orion_uart1_init(UART1_VIRT_BASE, UART1_PHYS_BASE,
			 IRQ_ORION5X_UART1, tclk);
}

/*****************************************************************************
 * XOR engine
 ****************************************************************************/
void __init orion5x_xor_init(void)
{
	orion_xor0_init(ORION5X_XOR_PHYS_BASE,
			ORION5X_XOR_PHYS_BASE + 0x200,
			IRQ_ORION5X_XOR0, IRQ_ORION5X_XOR1);
}

/*****************************************************************************
 * Cryptographic Engines and Security Accelerator (CESA)
 ****************************************************************************/
static void __init orion5x_crypto_init(void)
{
	mvebu_mbus_add_window_by_id(ORION_MBUS_SRAM_TARGET,
				    ORION_MBUS_SRAM_ATTR,
				    ORION5X_SRAM_PHYS_BASE,
				    ORION5X_SRAM_SIZE);
	orion_crypto_init(ORION5X_CRYPTO_PHYS_BASE, ORION5X_SRAM_PHYS_BASE,
			  SZ_8K, IRQ_ORION5X_CESA);
}

/*****************************************************************************
 * Watchdog
 ****************************************************************************/
static void __init orion5x_wdt_init(void)
{
	orion_wdt_init();
}


/*****************************************************************************
 * Time handling
 ****************************************************************************/
static void orion5x_timer_init(void)
{
	orion_time_init(IRQ_ORION5X_BRIDGE, ORION5X_TCLK);
}

struct sys_timer orion5x_timer = {
	.init = orion5x_timer_init,
};
void __init orion5x_init_early(void)
{
	u32 rev, dev;
	const char *mbus_soc_name;

	orion_time_set_base(TIMER_VIRT_BASE);

	/* Initialize the MBUS driver */
	orion5x_pcie_id(&dev, &rev);
	if (dev == MV88F5281_DEV_ID)
		mbus_soc_name = "marvell,orion5x-88f5281-mbus";
	else if (dev == MV88F5182_DEV_ID)
		mbus_soc_name = "marvell,orion5x-88f5182-mbus";
	else if (dev == MV88F5181_DEV_ID)
		mbus_soc_name = "marvell,orion5x-88f5181-mbus";
	else if (dev == MV88F6183_DEV_ID)
		mbus_soc_name = "marvell,orion5x-88f6183-mbus";
	else
		mbus_soc_name = NULL;
	mvebu_mbus_init(mbus_soc_name, ORION5X_BRIDGE_WINS_BASE,
			ORION5X_BRIDGE_WINS_SZ,
			ORION5X_DDR_WINS_BASE, ORION5X_DDR_WINS_SZ);
}

void orion5x_setup_wins(void)
{
	/*
	 * The PCIe windows will no longer be statically allocated
	 * here once Orion5x is migrated to the pci-mvebu driver.
	 */
	mvebu_mbus_add_window_remap_by_id(ORION_MBUS_PCIE_IO_TARGET,
					  ORION_MBUS_PCIE_IO_ATTR,
					  ORION5X_PCIE_IO_PHYS_BASE,
					  ORION5X_PCIE_IO_SIZE,
					  ORION5X_PCIE_IO_BUS_BASE);
	mvebu_mbus_add_window_by_id(ORION_MBUS_PCIE_MEM_TARGET,
				    ORION_MBUS_PCIE_MEM_ATTR,
				    ORION5X_PCIE_MEM_PHYS_BASE,
				    ORION5X_PCIE_MEM_SIZE);
	mvebu_mbus_add_window_remap_by_id(ORION_MBUS_PCI_IO_TARGET,
					  ORION_MBUS_PCI_IO_ATTR,
					  ORION5X_PCI_IO_PHYS_BASE,
					  ORION5X_PCI_IO_SIZE,
					  ORION5X_PCI_IO_BUS_BASE);
	mvebu_mbus_add_window_by_id(ORION_MBUS_PCI_MEM_TARGET,
				    ORION_MBUS_PCI_MEM_ATTR,
				    ORION5X_PCI_MEM_PHYS_BASE,
				    ORION5X_PCI_MEM_SIZE);
}

int orion5x_tclk;

static int __init orion5x_find_tclk(void)
{
	u32 dev, rev;

	orion5x_pcie_id(&dev, &rev);
	if (dev == MV88F6183_DEV_ID &&
	    (readl(MPP_RESET_SAMPLE) & 0x00000200) == 0)
		return 133333333;

	return 166666667;
}

void __init orion5x_timer_init(void)
{
	orion5x_tclk = orion5x_find_tclk();

	orion_time_init(ORION5X_BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_ORION5X_BRIDGE, orion5x_tclk);
}


/*****************************************************************************
 * General
 ****************************************************************************/
/*
 * Identify device ID and rev from PCIe configuration header space '0'.
 */
static void __init orion5x_id(u32 *dev, u32 *rev, char **dev_name)
void __init orion5x_id(u32 *dev, u32 *rev, char **dev_name)
{
	orion5x_pcie_id(dev, rev);

	if (*dev == MV88F5281_DEV_ID) {
		if (*rev == MV88F5281_REV_D2) {
			*dev_name = "MV88F5281-D2";
		} else if (*rev == MV88F5281_REV_D1) {
			*dev_name = "MV88F5281-D1";
		} else if (*rev == MV88F5281_REV_D0) {
			*dev_name = "MV88F5281-D0";
		} else {
			*dev_name = "MV88F5281-Rev-Unsupported";
		}
	} else if (*dev == MV88F5182_DEV_ID) {
		if (*rev == MV88F5182_REV_A2) {
			*dev_name = "MV88F5182-A2";
		} else {
			*dev_name = "MV88F5182-Rev-Unsupported";
		}
	} else if (*dev == MV88F5181_DEV_ID) {
		if (*rev == MV88F5181_REV_B1) {
			*dev_name = "MV88F5181-Rev-B1";
		} else if (*rev == MV88F5181L_REV_A1) {
			*dev_name = "MV88F5181L-Rev-A1";
		} else {
			*dev_name = "MV88F5181(L)-Rev-Unsupported";
		}
	} else if (*dev == MV88F6183_DEV_ID) {
		if (*rev == MV88F6183_REV_B0) {
			*dev_name = "MV88F6183-Rev-B0";
		} else {
			*dev_name = "MV88F6183-Rev-Unsupported";
		}
	} else {
		*dev_name = "Device-Unknown";
	}
}

void __init orion5x_init(void)
{
	char *dev_name;
	u32 dev, rev;

	orion5x_id(&dev, &rev, &dev_name);
	printk(KERN_INFO "Orion ID: %s. TCLK=%d.\n", dev_name, ORION5X_TCLK);
	printk(KERN_INFO "Orion ID: %s. TCLK=%d.\n", dev_name, orion5x_tclk);

	/*
	 * Setup Orion address map
	 */
	orion5x_setup_cpu_mbus_bridge();
	orion5x_setup_wins();

	/* Setup root of clk tree */
	clk_init();

	/*
	 * Don't issue "Wait for Interrupt" instruction if we are
	 * running on D0 5281 silicon.
	 */
	if (dev == MV88F5281_DEV_ID && rev == MV88F5281_REV_D0) {
		printk(KERN_INFO "Orion: Applying 5281 D0 WFI workaround.\n");
		disable_hlt();
	}
		cpu_idle_poll_ctrl(true);
	}

	/*
	 * The 5082/5181l/5182/6082/6082l/6183 have crypto
	 * while 5180n/5181/5281 don't have crypto.
	 */
	if ((dev == MV88F5181_DEV_ID && rev >= MV88F5181L_REV_A0) ||
	    dev == MV88F5182_DEV_ID || dev == MV88F6183_DEV_ID)
		orion5x_crypto_init();

	/*
	 * Register watchdog driver
	 */
	orion5x_wdt_init();
}

void orion5x_restart(enum reboot_mode mode, const char *cmd)
{
	/*
	 * Enable and issue soft reset
	 */
	orion5x_setbits(RSTOUTn_MASK, (1 << 2));
	orion5x_setbits(CPU_SOFT_RESET, 1);
	mdelay(200);
	orion5x_clrbits(CPU_SOFT_RESET, 1);
}

/*
 * Many orion-based systems have buggy bootloader implementations.
 * This is a common fixup for bogus memory tags.
 */
void __init tag_fixup_mem32(struct machine_desc *mdesc, struct tag *t,
			    char **from, struct meminfo *meminfo)
void __init tag_fixup_mem32(struct tag *t, char **from)
{
	for (; t->hdr.size; t = tag_next(t))
		if (t->hdr.tag == ATAG_MEM &&
		    (!t->u.mem.size || t->u.mem.size & ~PAGE_MASK ||
		     t->u.mem.start & ~PAGE_MASK)) {
			printk(KERN_WARNING
			       "Clearing invalid memory bank %dKB@0x%08x\n",
			       t->u.mem.size / 1024, t->u.mem.start);
			t->hdr.tag = 0;
		}
}
