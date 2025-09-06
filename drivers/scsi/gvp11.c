#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/zorro.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/irq.h>
#include <linux/spinlock.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "wd33c93.h"
#include "gvp11.h"

#include<linux/stat.h>

#define DMA(ptr) ((gvp11_scsiregs *)((ptr)->base))
#define HDATA(ptr) ((struct WD33C93_hostdata *)((ptr)->hostdata))

static irqreturn_t gvp11_intr (int irq, void *_instance)
{
    unsigned long flags;
    unsigned int status;
    struct Scsi_Host *instance = (struct Scsi_Host *)_instance;

    status = DMA(instance)->CNTR;
    if (!(status & GVP11_DMAC_INT_PENDING))
	return IRQ_NONE;

    spin_lock_irqsave(instance->host_lock, flags);
    wd33c93_intr(instance);
    spin_unlock_irqrestore(instance->host_lock, flags);
    return IRQ_HANDLED;

#include "scsi.h"
#include "wd33c93.h"
#include "gvp11.h"


#define CHECK_WD33C93

struct gvp11_hostdata {
	struct WD33C93_hostdata wh;
	struct gvp11_scsiregs *regs;
};

static irqreturn_t gvp11_intr(int irq, void *data)
{
	struct Scsi_Host *instance = data;
	struct gvp11_hostdata *hdata = shost_priv(instance);
	unsigned int status = hdata->regs->CNTR;
	unsigned long flags;

	if (!(status & GVP11_DMAC_INT_PENDING))
		return IRQ_NONE;

	spin_lock_irqsave(instance->host_lock, flags);
	wd33c93_intr(instance);
	spin_unlock_irqrestore(instance->host_lock, flags);
	return IRQ_HANDLED;
}

static int gvp11_xfer_mask = 0;

void gvp11_setup (char *str, int *ints)
{
    gvp11_xfer_mask = ints[1];
void gvp11_setup(char *str, int *ints)
{
	gvp11_xfer_mask = ints[1];
}

static int dma_setup(struct scsi_cmnd *cmd, int dir_in)
{
    unsigned short cntr = GVP11_DMAC_INT_ENABLE;
    unsigned long addr = virt_to_bus(cmd->SCp.ptr);
    int bank_mask;
    static int scsi_alloc_out_of_range = 0;

    /* use bounce buffer if the physical address is bad */
    if (addr & HDATA(cmd->device->host)->dma_xfer_mask)
    {
	HDATA(cmd->device->host)->dma_bounce_len = (cmd->SCp.this_residual + 511)
	    & ~0x1ff;

 	if( !scsi_alloc_out_of_range ) {
	    HDATA(cmd->device->host)->dma_bounce_buffer =
		kmalloc (HDATA(cmd->device->host)->dma_bounce_len, GFP_KERNEL);
	    HDATA(cmd->device->host)->dma_buffer_pool = BUF_SCSI_ALLOCED;
	}

	if (scsi_alloc_out_of_range ||
	    !HDATA(cmd->device->host)->dma_bounce_buffer) {
	    HDATA(cmd->device->host)->dma_bounce_buffer =
		amiga_chip_alloc(HDATA(cmd->device->host)->dma_bounce_len,
				       "GVP II SCSI Bounce Buffer");

	    if(!HDATA(cmd->device->host)->dma_bounce_buffer)
	    {
		HDATA(cmd->device->host)->dma_bounce_len = 0;
		return 1;
	    }

	    HDATA(cmd->device->host)->dma_buffer_pool = BUF_CHIP_ALLOCED;
	}

	/* check if the address of the bounce buffer is OK */
	addr = virt_to_bus(HDATA(cmd->device->host)->dma_bounce_buffer);

	if (addr & HDATA(cmd->device->host)->dma_xfer_mask) {
	    /* fall back to Chip RAM if address out of range */
	    if( HDATA(cmd->device->host)->dma_buffer_pool == BUF_SCSI_ALLOCED) {
		kfree (HDATA(cmd->device->host)->dma_bounce_buffer);
		scsi_alloc_out_of_range = 1;
	    } else {
		amiga_chip_free (HDATA(cmd->device->host)->dma_bounce_buffer);
            }
		
	    HDATA(cmd->device->host)->dma_bounce_buffer =
		amiga_chip_alloc(HDATA(cmd->device->host)->dma_bounce_len,
				       "GVP II SCSI Bounce Buffer");

	    if(!HDATA(cmd->device->host)->dma_bounce_buffer)
	    {
		HDATA(cmd->device->host)->dma_bounce_len = 0;
		return 1;
	    }

	    addr = virt_to_bus(HDATA(cmd->device->host)->dma_bounce_buffer);
	    HDATA(cmd->device->host)->dma_buffer_pool = BUF_CHIP_ALLOCED;
	}
	    
	if (!dir_in) {
	    /* copy to bounce buffer for a write */
	    memcpy (HDATA(cmd->device->host)->dma_bounce_buffer,
		    cmd->SCp.ptr, cmd->SCp.this_residual);
	}
    }

    /* setup dma direction */
    if (!dir_in)
	cntr |= GVP11_DMAC_DIR_WRITE;

    HDATA(cmd->device->host)->dma_dir = dir_in;
    DMA(cmd->device->host)->CNTR = cntr;

    /* setup DMA *physical* address */
    DMA(cmd->device->host)->ACR = addr;

    if (dir_in)
	/* invalidate any cache */
	cache_clear (addr, cmd->SCp.this_residual);
    else
	/* push any dirty cache */
	cache_push (addr, cmd->SCp.this_residual);

    if ((bank_mask = (~HDATA(cmd->device->host)->dma_xfer_mask >> 18) & 0x01c0))
	    DMA(cmd->device->host)->BANK = bank_mask & (addr >> 18);

    /* start DMA */
    DMA(cmd->device->host)->ST_DMA = 1;

    /* return success */
    return 0;
	struct Scsi_Host *instance = cmd->device->host;
	struct gvp11_hostdata *hdata = shost_priv(instance);
	struct WD33C93_hostdata *wh = &hdata->wh;
	struct gvp11_scsiregs *regs = hdata->regs;
	unsigned short cntr = GVP11_DMAC_INT_ENABLE;
	unsigned long addr = virt_to_bus(cmd->SCp.ptr);
	int bank_mask;
	static int scsi_alloc_out_of_range = 0;

	/* use bounce buffer if the physical address is bad */
	if (addr & wh->dma_xfer_mask) {
		wh->dma_bounce_len = (cmd->SCp.this_residual + 511) & ~0x1ff;

		if (!scsi_alloc_out_of_range) {
			wh->dma_bounce_buffer =
				kmalloc(wh->dma_bounce_len, GFP_KERNEL);
			wh->dma_buffer_pool = BUF_SCSI_ALLOCED;
		}

		if (scsi_alloc_out_of_range ||
		    !wh->dma_bounce_buffer) {
			wh->dma_bounce_buffer =
				amiga_chip_alloc(wh->dma_bounce_len,
						 "GVP II SCSI Bounce Buffer");

			if (!wh->dma_bounce_buffer) {
				wh->dma_bounce_len = 0;
				return 1;
			}

			wh->dma_buffer_pool = BUF_CHIP_ALLOCED;
		}

		/* check if the address of the bounce buffer is OK */
		addr = virt_to_bus(wh->dma_bounce_buffer);

		if (addr & wh->dma_xfer_mask) {
			/* fall back to Chip RAM if address out of range */
			if (wh->dma_buffer_pool == BUF_SCSI_ALLOCED) {
				kfree(wh->dma_bounce_buffer);
				scsi_alloc_out_of_range = 1;
			} else {
				amiga_chip_free(wh->dma_bounce_buffer);
			}

			wh->dma_bounce_buffer =
				amiga_chip_alloc(wh->dma_bounce_len,
						 "GVP II SCSI Bounce Buffer");

			if (!wh->dma_bounce_buffer) {
				wh->dma_bounce_len = 0;
				return 1;
			}

			addr = virt_to_bus(wh->dma_bounce_buffer);
			wh->dma_buffer_pool = BUF_CHIP_ALLOCED;
		}

		if (!dir_in) {
			/* copy to bounce buffer for a write */
			memcpy(wh->dma_bounce_buffer, cmd->SCp.ptr,
			       cmd->SCp.this_residual);
		}
	}

	/* setup dma direction */
	if (!dir_in)
		cntr |= GVP11_DMAC_DIR_WRITE;

	wh->dma_dir = dir_in;
	regs->CNTR = cntr;

	/* setup DMA *physical* address */
	regs->ACR = addr;

	if (dir_in) {
		/* invalidate any cache */
		cache_clear(addr, cmd->SCp.this_residual);
	} else {
		/* push any dirty cache */
		cache_push(addr, cmd->SCp.this_residual);
	}

	bank_mask = (~wh->dma_xfer_mask >> 18) & 0x01c0;
	if (bank_mask)
		regs->BANK = bank_mask & (addr >> 18);

	/* start DMA */
	regs->ST_DMA = 1;

	/* return success */
	return 0;
}

static void dma_stop(struct Scsi_Host *instance, struct scsi_cmnd *SCpnt,
		     int status)
{
    /* stop DMA */
    DMA(instance)->SP_DMA = 1;
    /* remove write bit from CONTROL bits */
    DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;

    /* copy from a bounce buffer, if necessary */
    if (status && HDATA(instance)->dma_bounce_buffer) {
	if (HDATA(instance)->dma_dir && SCpnt)
	    memcpy (SCpnt->SCp.ptr, 
		    HDATA(instance)->dma_bounce_buffer,
		    SCpnt->SCp.this_residual);
	
	if (HDATA(instance)->dma_buffer_pool == BUF_SCSI_ALLOCED)
	    kfree (HDATA(instance)->dma_bounce_buffer);
	else
	    amiga_chip_free(HDATA(instance)->dma_bounce_buffer);
	
	HDATA(instance)->dma_bounce_buffer = NULL;
	HDATA(instance)->dma_bounce_len = 0;
    }
}

#define CHECK_WD33C93

int __init gvp11_detect(struct scsi_host_template *tpnt)
{
    static unsigned char called = 0;
    struct Scsi_Host *instance;
    unsigned long address;
    unsigned int epc;
    struct zorro_dev *z = NULL;
    unsigned int default_dma_xfer_mask;
    wd33c93_regs regs;
    int num_gvp11 = 0;
#ifdef CHECK_WD33C93
    volatile unsigned char *sasr_3393, *scmd_3393;
    unsigned char save_sasr;
    unsigned char q, qq;
#endif

    if (!MACH_IS_AMIGA || called)
	return 0;
    called = 1;

    tpnt->proc_name = "GVP11";
    tpnt->proc_info = &wd33c93_proc_info;

    while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
	/* 
	 * This should (hopefully) be the correct way to identify
	 * all the different GVP SCSI controllers (except for the
	 * SERIES I though).
	 */

	if (z->id == ZORRO_PROD_GVP_COMBO_030_R3_SCSI ||
	    z->id == ZORRO_PROD_GVP_SERIES_II)
	    default_dma_xfer_mask = ~0x00ffffff;
	else if (z->id == ZORRO_PROD_GVP_GFORCE_030_SCSI ||
		 z->id == ZORRO_PROD_GVP_A530_SCSI ||
		 z->id == ZORRO_PROD_GVP_COMBO_030_R4_SCSI)
	    default_dma_xfer_mask = ~0x01ffffff;
	else if (z->id == ZORRO_PROD_GVP_A1291 ||
		 z->id == ZORRO_PROD_GVP_GFORCE_040_SCSI_1)
	    default_dma_xfer_mask = ~0x07ffffff;
	else
	    continue;

	/*
	 * Rumors state that some GVP ram boards use the same product
	 * code as the SCSI controllers. Therefore if the board-size
	 * is not 64KB we asume it is a ram board and bail out.
	 */
	if (z->resource.end-z->resource.start != 0xffff)
		continue;

	address = z->resource.start;
	if (!request_mem_region(address, 256, "wd33c93"))
	    continue;

#ifdef CHECK_WD33C93

	/*
	 * These darn GVP boards are a problem - it can be tough to tell
	 * whether or not they include a SCSI controller. This is the
	 * ultimate Yet-Another-GVP-Detection-Hack in that it actually
	 * probes for a WD33c93 chip: If we find one, it's extremely
	 * likely that this card supports SCSI, regardless of Product_
	 * Code, Board_Size, etc. 
	 */

    /* Get pointers to the presumed register locations and save contents */

	sasr_3393 = &(((gvp11_scsiregs *)(ZTWO_VADDR(address)))->SASR);
	scmd_3393 = &(((gvp11_scsiregs *)(ZTWO_VADDR(address)))->SCMD);
	save_sasr = *sasr_3393;

    /* First test the AuxStatus Reg */

	q = *sasr_3393;		/* read it */
	if (q & 0x08)		/* bit 3 should always be clear */
		goto release;
	*sasr_3393 = WD_AUXILIARY_STATUS;	 /* setup indirect address */
	if (*sasr_3393 == WD_AUXILIARY_STATUS) { /* shouldn't retain the write */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		goto release;
		}
	if (*sasr_3393 != q) {	/* should still read the same */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		goto release;
		}
	if (*scmd_3393 != q)	/* and so should the image at 0x1f */
		goto release;


    /* Ok, we probably have a wd33c93, but let's check a few other places
     * for good measure. Make sure that this works for both 'A and 'B    
     * chip versions.
     */

	*sasr_3393 = WD_SCSI_STATUS;
	q = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_SCSI_STATUS;
	qq = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = q;
	if (qq != q)			/* should be read only */
		goto release;
	*sasr_3393 = 0x1e;	/* this register is unimplemented */
	q = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = ~q;
	*sasr_3393 = 0x1e;
	qq = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = q;
	if (qq != q || qq != 0xff)	/* should be read only, all 1's */
		goto release;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	q = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	qq = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = q;
	if (qq != (~q & 0xff))		/* should be read/write */
		goto release;
#endif

	instance = scsi_register (tpnt, sizeof (struct WD33C93_hostdata));
	if(instance == NULL)
		goto release;
	instance->base = ZTWO_VADDR(address);
	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = z->slotaddr;

	if (gvp11_xfer_mask)
		HDATA(instance)->dma_xfer_mask = gvp11_xfer_mask;
	else
		HDATA(instance)->dma_xfer_mask = default_dma_xfer_mask;


	DMA(instance)->secret2 = 1;
	DMA(instance)->secret1 = 0;
	DMA(instance)->secret3 = 15;
	while (DMA(instance)->CNTR & GVP11_DMAC_BUSY) ;
	DMA(instance)->CNTR = 0;

	DMA(instance)->BANK = 0;

	epc = *(unsigned short *)(ZTWO_VADDR(address) + 0x8000);

	/*
	 * Check for 14MHz SCSI clock
	 */
	regs.SASR = &(DMA(instance)->SASR);
	regs.SCMD = &(DMA(instance)->SCMD);
	HDATA(instance)->no_sync = 0xff;
	HDATA(instance)->fast = 0;
	HDATA(instance)->dma_mode = CTRL_DMA;
	wd33c93_init(instance, regs, dma_setup, dma_stop,
		     (epc & GVP_SCSICLKMASK) ? WD33C93_FS_8_10
					     : WD33C93_FS_12_15);

	request_irq(IRQ_AMIGA_PORTS, gvp11_intr, IRQF_SHARED, "GVP11 SCSI",
		    instance);
	DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;
	num_gvp11++;
	continue;

release:
	release_mem_region(address, 256);
    }

    return num_gvp11;
	struct gvp11_hostdata *hdata = shost_priv(instance);
	struct WD33C93_hostdata *wh = &hdata->wh;
	struct gvp11_scsiregs *regs = hdata->regs;

	/* stop DMA */
	regs->SP_DMA = 1;
	/* remove write bit from CONTROL bits */
	regs->CNTR = GVP11_DMAC_INT_ENABLE;

	/* copy from a bounce buffer, if necessary */
	if (status && wh->dma_bounce_buffer) {
		if (wh->dma_dir && SCpnt)
			memcpy(SCpnt->SCp.ptr, wh->dma_bounce_buffer,
			       SCpnt->SCp.this_residual);

		if (wh->dma_buffer_pool == BUF_SCSI_ALLOCED)
			kfree(wh->dma_bounce_buffer);
		else
			amiga_chip_free(wh->dma_bounce_buffer);

		wh->dma_bounce_buffer = NULL;
		wh->dma_bounce_len = 0;
	}
}

static int gvp11_bus_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *instance = cmd->device->host;

	/* FIXME perform bus-specific reset */

	/* FIXME 2: shouldn't we no-op this function (return
	   FAILED), and fall back to host reset function,
	   wd33c93_host_reset ? */

	spin_lock_irq(cmd->device->host->host_lock);
	wd33c93_host_reset(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);
	spin_lock_irq(instance->host_lock);
	wd33c93_host_reset(cmd);
	spin_unlock_irq(instance->host_lock);

	return SUCCESS;
}


#define HOSTS_C

#include "gvp11.h"

static struct scsi_host_template driver_template = {
	.proc_name		= "GVP11",
	.name			= "GVP Series II SCSI",
	.detect			= gvp11_detect,
	.release		= gvp11_release,
static struct scsi_host_template gvp11_scsi_template = {
	.module			= THIS_MODULE,
	.name			= "GVP Series II SCSI",
	.show_info		= wd33c93_show_info,
	.write_info		= wd33c93_write_info,
	.proc_name		= "GVP11",
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_bus_reset_handler	= gvp11_bus_reset,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"

int gvp11_release(struct Scsi_Host *instance)
{
#ifdef MODULE
    DMA(instance)->CNTR = 0;
    release_mem_region(ZTWO_PADDR(instance->base), 256);
    free_irq(IRQ_AMIGA_PORTS, instance);
    wd33c93_release();
#endif
    return 1;
}

static int check_wd33c93(struct gvp11_scsiregs *regs)
{
#ifdef CHECK_WD33C93
	volatile unsigned char *sasr_3393, *scmd_3393;
	unsigned char save_sasr;
	unsigned char q, qq;

	/*
	 * These darn GVP boards are a problem - it can be tough to tell
	 * whether or not they include a SCSI controller. This is the
	 * ultimate Yet-Another-GVP-Detection-Hack in that it actually
	 * probes for a WD33c93 chip: If we find one, it's extremely
	 * likely that this card supports SCSI, regardless of Product_
	 * Code, Board_Size, etc.
	 */

	/* Get pointers to the presumed register locations and save contents */

	sasr_3393 = &regs->SASR;
	scmd_3393 = &regs->SCMD;
	save_sasr = *sasr_3393;

	/* First test the AuxStatus Reg */

	q = *sasr_3393;	/* read it */
	if (q & 0x08)	/* bit 3 should always be clear */
		return -ENODEV;
	*sasr_3393 = WD_AUXILIARY_STATUS;	/* setup indirect address */
	if (*sasr_3393 == WD_AUXILIARY_STATUS) {	/* shouldn't retain the write */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		return -ENODEV;
	}
	if (*sasr_3393 != q) {	/* should still read the same */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		return -ENODEV;
	}
	if (*scmd_3393 != q)	/* and so should the image at 0x1f */
		return -ENODEV;

	/*
	 * Ok, we probably have a wd33c93, but let's check a few other places
	 * for good measure. Make sure that this works for both 'A and 'B
	 * chip versions.
	 */

	*sasr_3393 = WD_SCSI_STATUS;
	q = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_SCSI_STATUS;
	qq = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = q;
	if (qq != q)	/* should be read only */
		return -ENODEV;
	*sasr_3393 = 0x1e;	/* this register is unimplemented */
	q = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = ~q;
	*sasr_3393 = 0x1e;
	qq = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = q;
	if (qq != q || qq != 0xff)	/* should be read only, all 1's */
		return -ENODEV;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	q = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	qq = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = q;
	if (qq != (~q & 0xff))	/* should be read/write */
		return -ENODEV;
#endif /* CHECK_WD33C93 */

	return 0;
}

static int gvp11_probe(struct zorro_dev *z, const struct zorro_device_id *ent)
{
	struct Scsi_Host *instance;
	unsigned long address;
	int error;
	unsigned int epc;
	unsigned int default_dma_xfer_mask;
	struct gvp11_hostdata *hdata;
	struct gvp11_scsiregs *regs;
	wd33c93_regs wdregs;

	default_dma_xfer_mask = ent->driver_data;

	/*
	 * Rumors state that some GVP ram boards use the same product
	 * code as the SCSI controllers. Therefore if the board-size
	 * is not 64KB we assume it is a ram board and bail out.
	 */
	if (zorro_resource_len(z) != 0x10000)
		return -ENODEV;

	address = z->resource.start;
	if (!request_mem_region(address, 256, "wd33c93"))
		return -EBUSY;

	regs = ZTWO_VADDR(address);

	error = check_wd33c93(regs);
	if (error)
		goto fail_check_or_alloc;

	instance = scsi_host_alloc(&gvp11_scsi_template,
				   sizeof(struct gvp11_hostdata));
	if (!instance) {
		error = -ENOMEM;
		goto fail_check_or_alloc;
	}

	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = z->slotaddr;

	regs->secret2 = 1;
	regs->secret1 = 0;
	regs->secret3 = 15;
	while (regs->CNTR & GVP11_DMAC_BUSY)
		;
	regs->CNTR = 0;
	regs->BANK = 0;

	wdregs.SASR = &regs->SASR;
	wdregs.SCMD = &regs->SCMD;

	hdata = shost_priv(instance);
	if (gvp11_xfer_mask)
		hdata->wh.dma_xfer_mask = gvp11_xfer_mask;
	else
		hdata->wh.dma_xfer_mask = default_dma_xfer_mask;

	hdata->wh.no_sync = 0xff;
	hdata->wh.fast = 0;
	hdata->wh.dma_mode = CTRL_DMA;
	hdata->regs = regs;

	/*
	 * Check for 14MHz SCSI clock
	 */
	epc = *(unsigned short *)(ZTWO_VADDR(address) + 0x8000);
	wd33c93_init(instance, wdregs, dma_setup, dma_stop,
		     (epc & GVP_SCSICLKMASK) ? WD33C93_FS_8_10
					     : WD33C93_FS_12_15);

	error = request_irq(IRQ_AMIGA_PORTS, gvp11_intr, IRQF_SHARED,
			    "GVP11 SCSI", instance);
	if (error)
		goto fail_irq;

	regs->CNTR = GVP11_DMAC_INT_ENABLE;

	error = scsi_add_host(instance, NULL);
	if (error)
		goto fail_host;

	zorro_set_drvdata(z, instance);
	scsi_scan_host(instance);
	return 0;

fail_host:
	free_irq(IRQ_AMIGA_PORTS, instance);
fail_irq:
	scsi_host_put(instance);
fail_check_or_alloc:
	release_mem_region(address, 256);
	return error;
}

static void gvp11_remove(struct zorro_dev *z)
{
	struct Scsi_Host *instance = zorro_get_drvdata(z);
	struct gvp11_hostdata *hdata = shost_priv(instance);

	hdata->regs->CNTR = 0;
	scsi_remove_host(instance);
	free_irq(IRQ_AMIGA_PORTS, instance);
	scsi_host_put(instance);
	release_mem_region(z->resource.start, 256);
}

	/*
	 * This should (hopefully) be the correct way to identify
	 * all the different GVP SCSI controllers (except for the
	 * SERIES I though).
	 */

static struct zorro_device_id gvp11_zorro_tbl[] = {
	{ ZORRO_PROD_GVP_COMBO_030_R3_SCSI,	~0x00ffffff },
	{ ZORRO_PROD_GVP_SERIES_II,		~0x00ffffff },
	{ ZORRO_PROD_GVP_GFORCE_030_SCSI,	~0x01ffffff },
	{ ZORRO_PROD_GVP_A530_SCSI,		~0x01ffffff },
	{ ZORRO_PROD_GVP_COMBO_030_R4_SCSI,	~0x01ffffff },
	{ ZORRO_PROD_GVP_A1291,			~0x07ffffff },
	{ ZORRO_PROD_GVP_GFORCE_040_SCSI_1,	~0x07ffffff },
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, gvp11_zorro_tbl);

static struct zorro_driver gvp11_driver = {
	.name		= "gvp11",
	.id_table	= gvp11_zorro_tbl,
	.probe		= gvp11_probe,
	.remove		= gvp11_remove,
};

static int __init gvp11_init(void)
{
	return zorro_register_driver(&gvp11_driver);
}
module_init(gvp11_init);

static void __exit gvp11_exit(void)
{
	zorro_unregister_driver(&gvp11_driver);
}
module_exit(gvp11_exit);

MODULE_DESCRIPTION("GVP Series II SCSI");
MODULE_LICENSE("GPL");
