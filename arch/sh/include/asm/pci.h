#ifndef __ASM_SH_PCI_H
#define __ASM_SH_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses()	1
#define pcibios_scan_all_fns(a, b)	0

/*
 * A board can define one or more PCI channels that represent built-in (or
 * external) PCI controllers.
 */
struct pci_channel {
	struct pci_ops *pci_ops;
	struct resource *io_resource;
	struct resource *mem_resource;
	int first_devfn;
	int last_devfn;
};

/*
 * Each board initializes this array and terminates it with a NULL entry.
 */
extern struct pci_channel board_pci_channels[];

#define PCIBIOS_MIN_IO		board_pci_channels->io_resource->start
#define PCIBIOS_MIN_MEM		board_pci_channels->mem_resource->start

/*
 * I/O routine helpers
 */
#if defined(CONFIG_CPU_SUBTYPE_SH7780) || defined(CONFIG_CPU_SUBTYPE_SH7785)
#define PCI_IO_AREA		0xFE400000
#define PCI_IO_SIZE		0x00400000
#elif defined(CONFIG_CPU_SH5)
extern unsigned long PCI_IO_AREA;
#define PCI_IO_SIZE		0x00010000
#else
#define PCI_IO_AREA		0xFE240000
#define PCI_IO_SIZE		0x00040000
#endif

#define PCI_MEM_SIZE		0x01000000

#define SH4_PCIIOBR_MASK	0xFFFC0000
#define pci_ioaddr(addr)	(PCI_IO_AREA + (addr & ~SH4_PCIIOBR_MASK))

#if defined(CONFIG_PCI)
#define is_pci_ioaddr(port)		\
	(((port) >= PCIBIOS_MIN_IO) &&	\
	 ((port) < (PCIBIOS_MIN_IO + PCI_IO_SIZE)))
#define is_pci_memaddr(port)		\
	(((port) >= PCIBIOS_MIN_MEM) &&	\
	 ((port) < (PCIBIOS_MIN_MEM + PCI_MEM_SIZE)))
#else
#define is_pci_ioaddr(port)	(0)
#define is_pci_memaddr(port)	(0)
#endif

struct pci_dev;

extern void pcibios_set_master(struct pci_dev *dev);

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

	struct pci_channel	*next;
	struct pci_bus		*bus;

	struct pci_ops		*pci_ops;

	struct resource		*resources;
	unsigned int		nr_resources;

	unsigned long		io_offset;
	unsigned long		mem_offset;

	unsigned long		reg_base;
	unsigned long		io_map_base;

	unsigned int		index;
	unsigned int		need_domain_info;

	/* Optional error handling */
	struct timer_list	err_timer, serr_timer;
	unsigned int		err_irq, serr_irq;
};

/* arch/sh/drivers/pci/pci.c */
extern raw_spinlock_t pci_config_lock;

extern int register_pci_controller(struct pci_channel *hose);
extern void pcibios_report_status(unsigned int status_mask, int warn);

/* arch/sh/drivers/pci/common.c */
extern int early_read_config_byte(struct pci_channel *hose, int top_bus,
				  int bus, int devfn, int offset, u8 *value);
extern int early_read_config_word(struct pci_channel *hose, int top_bus,
				  int bus, int devfn, int offset, u16 *value);
extern int early_read_config_dword(struct pci_channel *hose, int top_bus,
				   int bus, int devfn, int offset, u32 *value);
extern int early_write_config_byte(struct pci_channel *hose, int top_bus,
				   int bus, int devfn, int offset, u8 value);
extern int early_write_config_word(struct pci_channel *hose, int top_bus,
				   int bus, int devfn, int offset, u16 value);
extern int early_write_config_dword(struct pci_channel *hose, int top_bus,
				    int bus, int devfn, int offset, u32 value);
extern void pcibios_enable_timers(struct pci_channel *hose);
extern unsigned int pcibios_handle_status_errors(unsigned long addr,
				 unsigned int status, struct pci_channel *hose);
extern int pci_is_66mhz_capable(struct pci_channel *hose,
				int top_bus, int current_bus);

extern unsigned long PCIBIOS_MIN_IO, PCIBIOS_MIN_MEM;

struct pci_dev;

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
	enum pci_mmap_state mmap_state, int write_combine);
extern void pcibios_set_master(struct pci_dev *dev);

/* Dynamic DMA mapping stuff.
 * SuperH has everything mapped statically like x86.
 */

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

/* pci_unmap_{single,page} being a nop depends upon the
 * configuration.
 */
#ifdef CONFIG_SH_PCIDMA_NONCOHERENT
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	\
	dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)		\
	__u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)			\
	((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)		\
	(((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)			\
	((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)		\
	(((PTR)->LEN_NAME) = (VAL))
#else
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)
#endif

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif

/* Board-specific fixup routines. */
void pcibios_fixup(void);
int pcibios_init_platform(void);
int pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin);

#ifdef CONFIG_PCI_AUTO
int pciauto_assign_resources(int busno, struct pci_channel *hose);
#endif

#endif /* __KERNEL__ */

/* generic pci stuff */
#include <asm-generic/pci.h>
#define PCI_DMA_BUS_IS_PHYS	(dma_ops->is_phys)

#ifdef CONFIG_PCI
/*
 * None of the SH PCI controllers support MWI, it is always treated as a
 * direct memory write.
 */
#define PCI_DISABLE_MWI
#endif

/* Board-specific fixup routines. */
int pcibios_map_platform_irq(const struct pci_dev *dev, u8 slot, u8 pin);

#define pci_domain_nr(bus) ((struct pci_channel *)(bus)->sysdata)->index

static inline int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_channel *hose = bus->sysdata;
	return hose->need_domain_info;
}

/* Chances are this interrupt is wired PC-style ...  */
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? 15 : 14;
}

/* generic DMA-mapping stuff */
#include <asm-generic/pci-dma-compat.h>

#endif /* __KERNEL__ */
#endif /* __ASM_SH_PCI_H */

