/*
 * Copyright (C) 2001 Dave Engebretsen IBM Corporation
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Change Activity:
 * 2001/09/21 : engebret : Created with minimal EPOW and HW exception support.
 * End Change Activity
 */

#include <linux/errno.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/random.h>
#include <linux/sysrq.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/udbg.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/reboot.h>

#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/firmware.h>

#include "pseries.h"

static unsigned char ras_log_buf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(ras_log_buf_lock);

static char mce_data_buf[RTAS_ERROR_LOG_MAX];

static int ras_get_sensor_state_token;
static char global_mce_data_buf[RTAS_ERROR_LOG_MAX];
static DEFINE_PER_CPU(__u64, mce_data_buf);

static int ras_check_exception_token;

#define EPOW_SENSOR_TOKEN	9
#define EPOW_SENSOR_INDEX	0
#define RAS_VECTOR_OFFSET	0x500

static irqreturn_t ras_epow_interrupt(int irq, void *dev_id);
static irqreturn_t ras_error_interrupt(int irq, void *dev_id);


static void request_ras_irqs(struct device_node *np,
			irq_handler_t handler,
			const char *name)
{
	int i, index, count = 0;
	struct of_irq oirq;
	const u32 *opicprop;
	unsigned int opicplen;
	unsigned int virqs[16];

	/* Check for obsolete "open-pic-interrupt" property. If present, then
	 * map those interrupts using the default interrupt host and default
	 * trigger
	 */
	opicprop = of_get_property(np, "open-pic-interrupt", &opicplen);
	if (opicprop) {
		opicplen /= sizeof(u32);
		for (i = 0; i < opicplen; i++) {
			if (count > 15)
				break;
			virqs[count] = irq_create_mapping(NULL, *(opicprop++));
			if (virqs[count] == NO_IRQ)
				printk(KERN_ERR "Unable to allocate interrupt "
				       "number for %s\n", np->full_name);
			else
				count++;

		}
	}
	/* Else use normal interrupt tree parsing */
	else {
		/* First try to do a proper OF tree parsing */
		for (index = 0; of_irq_map_one(np, index, &oirq) == 0;
		     index++) {
			if (count > 15)
				break;
			virqs[count] = irq_create_of_mapping(oirq.controller,
							    oirq.specifier,
							    oirq.size);
			if (virqs[count] == NO_IRQ)
				printk(KERN_ERR "Unable to allocate interrupt "
				       "number for %s\n", np->full_name);
			else
				count++;
		}
	}

	/* Now request them */
	for (i = 0; i < count; i++) {
		if (request_irq(virqs[i], handler, 0, name, NULL)) {
			printk(KERN_ERR "Unable to request interrupt %d for "
			       "%s\n", virqs[i], np->full_name);
			return;
		}
	}
}

/*
 * Initialize handlers for the set of interrupts caused by hardware errors
 * and power system events.
 */
static int __init init_ras_IRQ(void)
{
	struct device_node *np;

	ras_get_sensor_state_token = rtas_token("get-sensor-state");
	ras_check_exception_token = rtas_token("check-exception");

	/* Internal Errors */
	np = of_find_node_by_path("/event-sources/internal-errors");
	if (np != NULL) {
		request_ras_irqs(np, ras_error_interrupt, "RAS_ERROR");
		request_event_sources_irqs(np, ras_error_interrupt,
					   "RAS_ERROR");
		of_node_put(np);
	}

	/* EPOW Events */
	np = of_find_node_by_path("/event-sources/epow-events");
	if (np != NULL) {
		request_ras_irqs(np, ras_epow_interrupt, "RAS_EPOW");
		request_event_sources_irqs(np, ras_epow_interrupt, "RAS_EPOW");
		of_node_put(np);
	}

	return 0;
}
__initcall(init_ras_IRQ);

/*
 * Handle power subsystem events (EPOW).
 *
 * Presently we just log the event has occurred.  This should be fixed
 * to examine the type of power failure and take appropriate action where
 * the time horizon permits something useful to be done.
 */
static irqreturn_t ras_epow_interrupt(int irq, void *dev_id)
{
	int status = 0xdeadbeef;
	int state = 0;
	int critical;

	status = rtas_call(ras_get_sensor_state_token, 2, 2, &state,
			   EPOW_SENSOR_TOKEN, EPOW_SENSOR_INDEX);

	if (state > 3)
		critical = 1;  /* Time Critical */
machine_subsys_initcall(pseries, init_ras_IRQ);

#define EPOW_SHUTDOWN_NORMAL				1
#define EPOW_SHUTDOWN_ON_UPS				2
#define EPOW_SHUTDOWN_LOSS_OF_CRITICAL_FUNCTIONS	3
#define EPOW_SHUTDOWN_AMBIENT_TEMPERATURE_TOO_HIGH	4

static void handle_system_shutdown(char event_modifier)
{
	switch (event_modifier) {
	case EPOW_SHUTDOWN_NORMAL:
		pr_emerg("Firmware initiated power off");
		orderly_poweroff(true);
		break;

	case EPOW_SHUTDOWN_ON_UPS:
		pr_emerg("Loss of power reported by firmware, system is "
			"running on UPS/battery");
		pr_emerg("Check RTAS error log for details");
		break;

	case EPOW_SHUTDOWN_LOSS_OF_CRITICAL_FUNCTIONS:
		pr_emerg("Loss of system critical functions reported by "
			"firmware");
		pr_emerg("Check RTAS error log for details");
		orderly_poweroff(true);
		break;

	case EPOW_SHUTDOWN_AMBIENT_TEMPERATURE_TOO_HIGH:
		pr_emerg("Ambient temperature too high reported by firmware");
		pr_emerg("Check RTAS error log for details");
		orderly_poweroff(true);
		break;

	default:
		pr_err("Unknown power/cooling shutdown event (modifier %d)",
			event_modifier);
	}
}

struct epow_errorlog {
	unsigned char sensor_value;
	unsigned char event_modifier;
	unsigned char extended_modifier;
	unsigned char reserved;
	unsigned char platform_reason;
};

#define EPOW_RESET			0
#define EPOW_WARN_COOLING		1
#define EPOW_WARN_POWER			2
#define EPOW_SYSTEM_SHUTDOWN		3
#define EPOW_SYSTEM_HALT		4
#define EPOW_MAIN_ENCLOSURE		5
#define EPOW_POWER_OFF			7

static void rtas_parse_epow_errlog(struct rtas_error_log *log)
{
	struct pseries_errorlog *pseries_log;
	struct epow_errorlog *epow_log;
	char action_code;
	char modifier;

	pseries_log = get_pseries_errorlog(log, PSERIES_ELOG_SECT_ID_EPOW);
	if (pseries_log == NULL)
		return;

	epow_log = (struct epow_errorlog *)pseries_log->data;
	action_code = epow_log->sensor_value & 0xF;	/* bottom 4 bits */
	modifier = epow_log->event_modifier & 0xF;	/* bottom 4 bits */

	switch (action_code) {
	case EPOW_RESET:
		pr_err("Non critical power or cooling issue cleared");
		break;

	case EPOW_WARN_COOLING:
		pr_err("Non critical cooling issue reported by firmware");
		pr_err("Check RTAS error log for details");
		break;

	case EPOW_WARN_POWER:
		pr_err("Non critical power issue reported by firmware");
		pr_err("Check RTAS error log for details");
		break;

	case EPOW_SYSTEM_SHUTDOWN:
		handle_system_shutdown(epow_log->event_modifier);
		break;

	case EPOW_SYSTEM_HALT:
		pr_emerg("Firmware initiated power off");
		orderly_poweroff(true);
		break;

	case EPOW_MAIN_ENCLOSURE:
	case EPOW_POWER_OFF:
		pr_emerg("Critical power/cooling issue reported by firmware");
		pr_emerg("Check RTAS error log for details");
		pr_emerg("Immediate power off");
		emergency_sync();
		kernel_power_off();
		break;

	default:
		pr_err("Unknown power/cooling event (action code %d)",
			action_code);
	}
}

/* Handle environmental and power warning (EPOW) interrupts. */
static irqreturn_t ras_epow_interrupt(int irq, void *dev_id)
{
	int status;
	int state;
	int critical;

	status = rtas_get_sensor_fast(EPOW_SENSOR_TOKEN, EPOW_SENSOR_INDEX,
				      &state);

	if (state > 3)
		critical = 1;		/* Time Critical */
	else
		critical = 0;

	spin_lock(&ras_log_buf_lock);

	status = rtas_call(ras_check_exception_token, 6, 1, NULL,
			   RAS_VECTOR_OFFSET,
			   irq_map[irq].hwirq,
			   RTAS_EPOW_WARNING | RTAS_POWERMGM_EVENTS,
			   critical, __pa(&ras_log_buf),
				rtas_get_error_log_max());

	udbg_printf("EPOW <0x%lx 0x%x 0x%x>\n",
		    *((unsigned long *)&ras_log_buf), status, state);
	printk(KERN_WARNING "EPOW <0x%lx 0x%x 0x%x>\n",
	       *((unsigned long *)&ras_log_buf), status, state);

	/* format and print the extended information */
	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, 0);

			   RTAS_VECTOR_EXTERNAL_INTERRUPT,
			   virq_to_hw(irq),
			   RTAS_EPOW_WARNING,
			   critical, __pa(&ras_log_buf),
				rtas_get_error_log_max());

	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, 0);

	rtas_parse_epow_errlog((struct rtas_error_log *)ras_log_buf);

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/*
 * Handle hardware error interrupts.
 *
 * RTAS check-exception is called to collect data on the exception.  If
 * the error is deemed recoverable, we log a warning and return.
 * For nonrecoverable errors, an error is logged and we stop all processing
 * as quickly as possible in order to prevent propagation of the failure.
 */
static irqreturn_t ras_error_interrupt(int irq, void *dev_id)
{
	struct rtas_error_log *rtas_elog;
	int status = 0xdeadbeef;
	int status;
	int fatal;

	spin_lock(&ras_log_buf_lock);

	status = rtas_call(ras_check_exception_token, 6, 1, NULL,
			   RAS_VECTOR_OFFSET,
			   irq_map[irq].hwirq,
			   RTAS_INTERNAL_ERROR, 1 /*Time Critical */,
			   RTAS_VECTOR_EXTERNAL_INTERRUPT,
			   virq_to_hw(irq),
			   RTAS_INTERNAL_ERROR, 1 /* Time Critical */,
			   __pa(&ras_log_buf),
				rtas_get_error_log_max());

	rtas_elog = (struct rtas_error_log *)ras_log_buf;

	if ((status == 0) && (rtas_elog->severity >= RTAS_SEVERITY_ERROR_SYNC))
	if (status == 0 &&
	    rtas_error_severity(rtas_elog) >= RTAS_SEVERITY_ERROR_SYNC)
		fatal = 1;
	else
		fatal = 0;

	/* format and print the extended information */
	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, fatal);

	if (fatal) {
		udbg_printf("Fatal HW Error <0x%lx 0x%x>\n",
			    *((unsigned long *)&ras_log_buf), status);
		printk(KERN_EMERG "Error: Fatal hardware error <0x%lx 0x%x>\n",
		       *((unsigned long *)&ras_log_buf), status);

#ifndef DEBUG_RTAS_POWER_OFF
		/* Don't actually power off when debugging so we can test
		 * without actually failing while injecting errors.
		 * Error data will not be logged to syslog.
		 */
		ppc_md.power_off();
#endif
	} else {
		udbg_printf("Recoverable HW Error <0x%lx 0x%x>\n",
			    *((unsigned long *)&ras_log_buf), status);
		printk(KERN_WARNING
		       "Warning: Recoverable hardware error <0x%lx 0x%x>\n",
		       *((unsigned long *)&ras_log_buf), status);
		pr_emerg("Fatal hardware error reported by firmware");
		pr_emerg("Check RTAS error log for details");
		pr_emerg("Immediate power off");
		emergency_sync();
		kernel_power_off();
	} else {
		pr_err("Recoverable hardware error reported by firmware");
	}

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/* Get the error information for errors coming through the
/*
 * Some versions of FWNMI place the buffer inside the 4kB page starting at
 * 0x7000. Other versions place it inside the rtas buffer. We check both.
 * Minimum size of the buffer is 16 bytes.
 */
#define VALID_FWNMI_BUFFER(A) \
	((((A) >= 0x7000) && ((A) <= 0x8000 - 16)) || \
	(((A) >= rtas.base) && ((A) <= (rtas.base + rtas.size - 16))))

/*
 * Get the error information for errors coming through the
 * FWNMI vectors.  The pt_regs' r3 will be updated to reflect
 * the actual r3 if possible, and a ptr to the error log entry
 * will be returned if found.
 *
 * The mce_data_buf does not have any locks or protection around it,
 * If the RTAS error is not of the extended type, then we put it in a per
 * cpu 64bit buffer. If it is the extended type we use global_mce_data_buf.
 *
 * The global_mce_data_buf does not have any locks or protection around it,
 * if a second machine check comes in, or a system reset is done
 * before we have logged the error, then we will get corruption in the
 * error log.  This is preferable over holding off on calling
 * ibm,nmi-interlock which would result in us checkstopping if a
 * second machine check did come in.
 */
static struct rtas_error_log *fwnmi_get_errinfo(struct pt_regs *regs)
{
	unsigned long errdata = regs->gpr[3];
	struct rtas_error_log *errhdr = NULL;
	unsigned long *savep;

	if ((errdata >= 0x7000 && errdata < 0x7fff0) ||
	    (errdata >= rtas.base && errdata < rtas.base + rtas.size - 16)) {
		savep = __va(errdata);
		regs->gpr[3] = savep[0];	/* restore original r3 */
		memset(mce_data_buf, 0, RTAS_ERROR_LOG_MAX);
		memcpy(mce_data_buf, (char *)(savep + 1), RTAS_ERROR_LOG_MAX);
		errhdr = (struct rtas_error_log *)mce_data_buf;
	} else {
		printk("FWNMI: corrupt r3\n");
	}
	unsigned long *savep;
	struct rtas_error_log *h, *errhdr = NULL;

	/* Mask top two bits */
	regs->gpr[3] &= ~(0x3UL << 62);

	if (!VALID_FWNMI_BUFFER(regs->gpr[3])) {
		printk(KERN_ERR "FWNMI: corrupt r3 0x%016lx\n", regs->gpr[3]);
		return NULL;
	}

	savep = __va(regs->gpr[3]);
	regs->gpr[3] = be64_to_cpu(savep[0]);	/* restore original r3 */

	/* If it isn't an extended log we can use the per cpu 64bit buffer */
	h = (struct rtas_error_log *)&savep[1];
	if (!rtas_error_extended(h)) {
		memcpy(this_cpu_ptr(&mce_data_buf), h, sizeof(__u64));
		errhdr = (struct rtas_error_log *)this_cpu_ptr(&mce_data_buf);
	} else {
		int len, error_log_length;

		error_log_length = 8 + rtas_error_extended_log_length(h);
		len = min_t(int, error_log_length, RTAS_ERROR_LOG_MAX);
		memset(global_mce_data_buf, 0, RTAS_ERROR_LOG_MAX);
		memcpy(global_mce_data_buf, h, len);
		errhdr = (struct rtas_error_log *)global_mce_data_buf;
	}

	return errhdr;
}

/* Call this when done with the data returned by FWNMI_get_errinfo.
 * It will release the saved data area for other CPUs in the
 * partition to receive FWNMI errors.
 */
static void fwnmi_release_errinfo(void)
{
	int ret = rtas_call(rtas_token("ibm,nmi-interlock"), 0, 1, NULL);
	if (ret != 0)
		printk("FWNMI: nmi-interlock failed: %d\n", ret);
		printk(KERN_ERR "FWNMI: nmi-interlock failed: %d\n", ret);
}

int pSeries_system_reset_exception(struct pt_regs *regs)
{
	if (fwnmi_active) {
		struct rtas_error_log *errhdr = fwnmi_get_errinfo(regs);
		if (errhdr) {
			/* XXX Should look at FWNMI information */
		}
		fwnmi_release_errinfo();
	}
	return 0; /* need to perform reset */
}

/*
 * See if we can recover from a machine check exception.
 * This is only called on power4 (or above) and only via
 * the Firmware Non-Maskable Interrupts (fwnmi) handler
 * which provides the error analysis for us.
 *
 * Return 1 if corrected (or delivered a signal).
 * Return 0 if there is nothing we can do.
 */
static int recover_mce(struct pt_regs *regs, struct rtas_error_log * err)
{
	int nonfatal = 0;

	if (err->disposition == RTAS_DISP_FULLY_RECOVERED) {
		/* Platform corrected itself */
		nonfatal = 1;
	} else if ((regs->msr & MSR_RI) &&
		   user_mode(regs) &&
		   err->severity == RTAS_SEVERITY_ERROR_SYNC &&
		   err->disposition == RTAS_DISP_NOT_RECOVERED &&
		   err->target == RTAS_TARGET_MEMORY &&
		   err->type == RTAS_TYPE_ECC_UNCORR &&
		   !(current->pid == 0 || is_global_init(current))) {
		/* Kill off a user process with an ECC error */
		printk(KERN_ERR "MCE: uncorrectable ecc error for pid %d\n",
		       current->pid);
		/* XXX something better for ECC error? */
		_exception(SIGBUS, regs, BUS_ADRERR, regs->nip);
		nonfatal = 1;
	}

	log_error((char *)err, ERR_TYPE_RTAS_LOG, !nonfatal);

	return nonfatal;
static int recover_mce(struct pt_regs *regs, struct rtas_error_log *err)
{
	int recovered = 0;
	int disposition = rtas_error_disposition(err);

	if (!(regs->msr & MSR_RI)) {
		/* If MSR_RI isn't set, we cannot recover */
		recovered = 0;

	} else if (disposition == RTAS_DISP_FULLY_RECOVERED) {
		/* Platform corrected itself */
		recovered = 1;

	} else if (disposition == RTAS_DISP_LIMITED_RECOVERY) {
		/* Platform corrected itself but could be degraded */
		printk(KERN_ERR "MCE: limited recovery, system may "
		       "be degraded\n");
		recovered = 1;

	} else if (user_mode(regs) && !is_global_init(current) &&
		   rtas_error_severity(err) == RTAS_SEVERITY_ERROR_SYNC) {

		/*
		 * If we received a synchronous error when in userspace
		 * kill the task. Firmware may report details of the fail
		 * asynchronously, so we can't rely on the target and type
		 * fields being valid here.
		 */
		printk(KERN_ERR "MCE: uncorrectable error, killing task "
		       "%s:%d\n", current->comm, current->pid);

		_exception(SIGBUS, regs, BUS_MCEERR_AR, regs->nip);
		recovered = 1;
	}

	log_error((char *)err, ERR_TYPE_RTAS_LOG, 0);

	return recovered;
}

/*
 * Handle a machine check.
 *
 * Note that on Power 4 and beyond Firmware Non-Maskable Interrupts (fwnmi)
 * should be present.  If so the handler which called us tells us if the
 * error was recovered (never true if RI=0).
 *
 * On hardware prior to Power 4 these exceptions were asynchronous which
 * means we can't tell exactly where it occurred and so we can't recover.
 */
int pSeries_machine_check_exception(struct pt_regs *regs)
{
	struct rtas_error_log *errp;

	if (fwnmi_active) {
		errp = fwnmi_get_errinfo(regs);
		fwnmi_release_errinfo();
		if (errp && recover_mce(regs, errp))
			return 1;
	}

	return 0;
}
