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
#include "a2091.h"

#include<linux/stat.h>

#define DMA(ptr) ((a2091_scsiregs *)((ptr)->base))
#define HDATA(ptr) ((struct WD33C93_hostdata *)((ptr)->hostdata))

static irqreturn_t a2091_intr (int irq, void *_instance)
{
    unsigned long flags;
    unsigned int status;
    struct Scsi_Host *instance = (struct Scsi_Host *)_instance;

    status = DMA(instance)->ISTR;
    if (!(status & (ISTR_INT_F|ISTR_INT_P)) || !(status & ISTR_INTS))
	return IRQ_NONE;

    spin_lock_irqsave(instance->host_lock, flags);
    wd33c93_intr(instance);
    spin_unlock_irqrestore(instance->host_lock, flags);
    return IRQ_HANDLED;

#include "scsi.h"
#include "wd33c93.h"
#include "a2091.h"


struct a2091_hostdata {
	struct WD33C93_hostdata wh;
	struct a2091_scsiregs *regs;
};

static irqreturn_t a2091_intr(int irq, void *data)
{
	struct Scsi_Host *instance = data;
	struct a2091_hostdata *hdata = shost_priv(instance);
	unsigned int status = hdata->regs->ISTR;
	unsigned long flags;

	if (!(status & (ISTR_INT_F | ISTR_INT_P)) || !(status & ISTR_INTS))
		return IRQ_NONE;

	spin_lock_irqsave(instance->host_lock, flags);
	wd33c93_intr(instance);
	spin_unlock_irqrestore(instance->host_lock, flags);
	return IRQ_HANDLED;
}

static int dma_setup(struct scsi_cmnd *cmd, int dir_in)
{
    unsigned short cntr = CNTR_PDMD | CNTR_INTEN;
    unsigned long addr = virt_to_bus(cmd->SCp.ptr);
    struct Scsi_Host *instance = cmd->device->host;

    /* don't allow DMA if the physical address is bad */
    if (addr & A2091_XFER_MASK)
    {
	HDATA(instance)->dma_bounce_len = (cmd->SCp.this_residual + 511)
	    & ~0x1ff;
	HDATA(instance)->dma_bounce_buffer =
	    kmalloc (HDATA(instance)->dma_bounce_len, GFP_KERNEL);
	
	/* can't allocate memory; use PIO */
	if (!HDATA(instance)->dma_bounce_buffer) {
	    HDATA(instance)->dma_bounce_len = 0;
	    return 1;
	}

	/* get the physical address of the bounce buffer */
	addr = virt_to_bus(HDATA(instance)->dma_bounce_buffer);

	/* the bounce buffer may not be in the first 16M of physmem */
	if (addr & A2091_XFER_MASK) {
	    /* we could use chipmem... maybe later */
	    kfree (HDATA(instance)->dma_bounce_buffer);
	    HDATA(instance)->dma_bounce_buffer = NULL;
	    HDATA(instance)->dma_bounce_len = 0;
	    return 1;
	}

	if (!dir_in) {
		/* copy to bounce buffer for a write */
		memcpy (HDATA(instance)->dma_bounce_buffer,
			cmd->SCp.ptr, cmd->SCp.this_residual);
	}
    }

    /* setup dma direction */
    if (!dir_in)
	cntr |= CNTR_DDIR;

    /* remember direction */
    HDATA(cmd->device->host)->dma_dir = dir_in;

    DMA(cmd->device->host)->CNTR = cntr;

    /* setup DMA *physical* address */
    DMA(cmd->device->host)->ACR = addr;

    if (dir_in){
	/* invalidate any cache */
	cache_clear (addr, cmd->SCp.this_residual);
    }else{
	/* push any dirty cache */
	cache_push (addr, cmd->SCp.this_residual);
      }
    /* start DMA */
    DMA(cmd->device->host)->ST_DMA = 1;

    /* return success */
    return 0;
}

static void dma_stop(struct Scsi_Host *instance, struct scsi_cmnd *SCpnt,
		      int status)
{
    /* disable SCSI interrupts */
    unsigned short cntr = CNTR_PDMD;

    if (!HDATA(instance)->dma_dir)
	    cntr |= CNTR_DDIR;

    /* disable SCSI interrupts */
    DMA(instance)->CNTR = cntr;

    /* flush if we were reading */
    if (HDATA(instance)->dma_dir) {
	DMA(instance)->FLUSH = 1;
	while (!(DMA(instance)->ISTR & ISTR_FE_FLG))
	    ;
    }

    /* clear a possible interrupt */
    DMA(instance)->CINT = 1;

    /* stop DMA */
    DMA(instance)->SP_DMA = 1;

    /* restore the CONTROL bits (minus the direction flag) */
    DMA(instance)->CNTR = CNTR_PDMD | CNTR_INTEN;

    /* copy from a bounce buffer, if necessary */
    if (status && HDATA(instance)->dma_bounce_buffer) {
	if( HDATA(instance)->dma_dir )
		memcpy (SCpnt->SCp.ptr, 
			HDATA(instance)->dma_bounce_buffer,
			SCpnt->SCp.this_residual);
	kfree (HDATA(instance)->dma_bounce_buffer);
	HDATA(instance)->dma_bounce_buffer = NULL;
	HDATA(instance)->dma_bounce_len = 0;
    }
}

int __init a2091_detect(struct scsi_host_template *tpnt)
{
    static unsigned char called = 0;
    struct Scsi_Host *instance;
    unsigned long address;
    struct zorro_dev *z = NULL;
    wd33c93_regs regs;
    int num_a2091 = 0;

    if (!MACH_IS_AMIGA || called)
	return 0;
    called = 1;

    tpnt->proc_name = "A2091";
    tpnt->proc_info = &wd33c93_proc_info;

    while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
	if (z->id != ZORRO_PROD_CBM_A590_A2091_1 &&
	    z->id != ZORRO_PROD_CBM_A590_A2091_2)
	    continue;
	address = z->resource.start;
	if (!request_mem_region(address, 256, "wd33c93"))
	    continue;

	instance = scsi_register (tpnt, sizeof (struct WD33C93_hostdata));
	if (instance == NULL) {
	    release_mem_region(address, 256);
	    continue;
	}
	instance->base = ZTWO_VADDR(address);
	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = z->slotaddr;
	DMA(instance)->DAWR = DAWR_A2091;
	regs.SASR = &(DMA(instance)->SASR);
	regs.SCMD = &(DMA(instance)->SCMD);
	HDATA(instance)->no_sync = 0xff;
	HDATA(instance)->fast = 0;
	HDATA(instance)->dma_mode = CTRL_DMA;
	wd33c93_init(instance, regs, dma_setup, dma_stop, WD33C93_FS_8_10);
	request_irq(IRQ_AMIGA_PORTS, a2091_intr, IRQF_SHARED, "A2091 SCSI",
		    instance);
	DMA(instance)->CNTR = CNTR_PDMD | CNTR_INTEN;
	num_a2091++;
    }

    return num_a2091;
	struct Scsi_Host *instance = cmd->device->host;
	struct a2091_hostdata *hdata = shost_priv(instance);
	struct WD33C93_hostdata *wh = &hdata->wh;
	struct a2091_scsiregs *regs = hdata->regs;
	unsigned short cntr = CNTR_PDMD | CNTR_INTEN;
	unsigned long addr = virt_to_bus(cmd->SCp.ptr);

	/* don't allow DMA if the physical address is bad */
	if (addr & A2091_XFER_MASK) {
		wh->dma_bounce_len = (cmd->SCp.this_residual + 511) & ~0x1ff;
		wh->dma_bounce_buffer = kmalloc(wh->dma_bounce_len,
						GFP_KERNEL);

		/* can't allocate memory; use PIO */
		if (!wh->dma_bounce_buffer) {
			wh->dma_bounce_len = 0;
			return 1;
		}

		/* get the physical address of the bounce buffer */
		addr = virt_to_bus(wh->dma_bounce_buffer);

		/* the bounce buffer may not be in the first 16M of physmem */
		if (addr & A2091_XFER_MASK) {
			/* we could use chipmem... maybe later */
			kfree(wh->dma_bounce_buffer);
			wh->dma_bounce_buffer = NULL;
			wh->dma_bounce_len = 0;
			return 1;
		}

		if (!dir_in) {
			/* copy to bounce buffer for a write */
			memcpy(wh->dma_bounce_buffer, cmd->SCp.ptr,
			       cmd->SCp.this_residual);
		}
	}

	/* setup dma direction */
	if (!dir_in)
		cntr |= CNTR_DDIR;

	/* remember direction */
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
	/* start DMA */
	regs->ST_DMA = 1;

	/* return success */
	return 0;
}

static void dma_stop(struct Scsi_Host *instance, struct scsi_cmnd *SCpnt,
		     int status)
{
	struct a2091_hostdata *hdata = shost_priv(instance);
	struct WD33C93_hostdata *wh = &hdata->wh;
	struct a2091_scsiregs *regs = hdata->regs;

	/* disable SCSI interrupts */
	unsigned short cntr = CNTR_PDMD;

	if (!wh->dma_dir)
		cntr |= CNTR_DDIR;

	/* disable SCSI interrupts */
	regs->CNTR = cntr;

	/* flush if we were reading */
	if (wh->dma_dir) {
		regs->FLUSH = 1;
		while (!(regs->ISTR & ISTR_FE_FLG))
			;
	}

	/* clear a possible interrupt */
	regs->CINT = 1;

	/* stop DMA */
	regs->SP_DMA = 1;

	/* restore the CONTROL bits (minus the direction flag) */
	regs->CNTR = CNTR_PDMD | CNTR_INTEN;

	/* copy from a bounce buffer, if necessary */
	if (status && wh->dma_bounce_buffer) {
		if (wh->dma_dir)
			memcpy(SCpnt->SCp.ptr, wh->dma_bounce_buffer,
			       SCpnt->SCp.this_residual);
		kfree(wh->dma_bounce_buffer);
		wh->dma_bounce_buffer = NULL;
		wh->dma_bounce_len = 0;
	}
}

static int a2091_bus_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *instance = cmd->device->host;

	/* FIXME perform bus-specific reset */

	/* FIXME 2: kill this function, and let midlayer fall back
	   to the same action, calling wd33c93_host_reset() */

	spin_lock_irq(cmd->device->host->host_lock);
	wd33c93_host_reset(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);
	spin_lock_irq(instance->host_lock);
	wd33c93_host_reset(cmd);
	spin_unlock_irq(instance->host_lock);

	return SUCCESS;
}

#define HOSTS_C

static struct scsi_host_template driver_template = {
	.proc_name		= "A2901",
	.name			= "Commodore A2091/A590 SCSI",
	.detect			= a2091_detect,
	.release		= a2091_release,
static struct scsi_host_template a2091_scsi_template = {
	.module			= THIS_MODULE,
	.name			= "Commodore A2091/A590 SCSI",
	.show_info		= wd33c93_show_info,
	.write_info		= wd33c93_write_info,
	.proc_name		= "A2901",
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_bus_reset_handler	= a2091_bus_reset,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"

int a2091_release(struct Scsi_Host *instance)
{
#ifdef MODULE
	DMA(instance)->CNTR = 0;
	release_mem_region(ZTWO_PADDR(instance->base), 256);
	free_irq(IRQ_AMIGA_PORTS, instance);
	wd33c93_release();
#endif
	return 1;
}

static int a2091_probe(struct zorro_dev *z, const struct zorro_device_id *ent)
{
	struct Scsi_Host *instance;
	int error;
	struct a2091_scsiregs *regs;
	wd33c93_regs wdregs;
	struct a2091_hostdata *hdata;

	if (!request_mem_region(z->resource.start, 256, "wd33c93"))
		return -EBUSY;

	instance = scsi_host_alloc(&a2091_scsi_template,
				   sizeof(struct a2091_hostdata));
	if (!instance) {
		error = -ENOMEM;
		goto fail_alloc;
	}

	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = z->slotaddr;

	regs = ZTWO_VADDR(z->resource.start);
	regs->DAWR = DAWR_A2091;

	wdregs.SASR = &regs->SASR;
	wdregs.SCMD = &regs->SCMD;

	hdata = shost_priv(instance);
	hdata->wh.no_sync = 0xff;
	hdata->wh.fast = 0;
	hdata->wh.dma_mode = CTRL_DMA;
	hdata->regs = regs;

	wd33c93_init(instance, wdregs, dma_setup, dma_stop, WD33C93_FS_8_10);
	error = request_irq(IRQ_AMIGA_PORTS, a2091_intr, IRQF_SHARED,
			    "A2091 SCSI", instance);
	if (error)
		goto fail_irq;

	regs->CNTR = CNTR_PDMD | CNTR_INTEN;

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
fail_alloc:
	release_mem_region(z->resource.start, 256);
	return error;
}

static void a2091_remove(struct zorro_dev *z)
{
	struct Scsi_Host *instance = zorro_get_drvdata(z);
	struct a2091_hostdata *hdata = shost_priv(instance);

	hdata->regs->CNTR = 0;
	scsi_remove_host(instance);
	free_irq(IRQ_AMIGA_PORTS, instance);
	scsi_host_put(instance);
	release_mem_region(z->resource.start, 256);
}

static struct zorro_device_id a2091_zorro_tbl[] = {
	{ ZORRO_PROD_CBM_A590_A2091_1 },
	{ ZORRO_PROD_CBM_A590_A2091_2 },
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, a2091_zorro_tbl);

static struct zorro_driver a2091_driver = {
	.name		= "a2091",
	.id_table	= a2091_zorro_tbl,
	.probe		= a2091_probe,
	.remove		= a2091_remove,
};

static int __init a2091_init(void)
{
	return zorro_register_driver(&a2091_driver);
}
module_init(a2091_init);

static void __exit a2091_exit(void)
{
	zorro_unregister_driver(&a2091_driver);
}
module_exit(a2091_exit);

MODULE_DESCRIPTION("Commodore A2091/A590 SCSI");
MODULE_LICENSE("GPL");
