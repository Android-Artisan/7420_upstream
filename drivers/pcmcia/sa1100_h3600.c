/*
 * drivers/pcmcia/sa1100_h3600.c
 *
 * PCMCIA implementation routines for H3600
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <mach/h3600.h>

#include "sa1100_generic.h"

static struct pcmcia_irqs irqs[] = {
	{ 0, IRQ_GPIO_H3600_PCMCIA_CD0, "PCMCIA CD0" },
	{ 1, IRQ_GPIO_H3600_PCMCIA_CD1, "PCMCIA CD1" }
};

static int h3600_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	skt->irq = skt->nr ? IRQ_GPIO_H3600_PCMCIA_IRQ1
			   : IRQ_GPIO_H3600_PCMCIA_IRQ0;


	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
#include <mach/h3xxx.h>

#include "sa1100_generic.h"

static int h3600_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int err;

	switch (skt->nr) {
	case 0:
		skt->stat[SOC_STAT_CD].gpio = H3XXX_GPIO_PCMCIA_CD0;
		skt->stat[SOC_STAT_CD].name = "PCMCIA CD0";
		skt->stat[SOC_STAT_RDY].gpio = H3XXX_GPIO_PCMCIA_IRQ0;
		skt->stat[SOC_STAT_RDY].name = "PCMCIA IRQ0";

		err = gpio_request(H3XXX_EGPIO_OPT_NVRAM_ON, "OPT NVRAM ON");
		if (err)
			goto err01;
		err = gpio_direction_output(H3XXX_EGPIO_OPT_NVRAM_ON, 0);
		if (err)
			goto err03;
		err = gpio_request(H3XXX_EGPIO_OPT_ON, "OPT ON");
		if (err)
			goto err03;
		err = gpio_direction_output(H3XXX_EGPIO_OPT_ON, 0);
		if (err)
			goto err04;
		err = gpio_request(H3XXX_EGPIO_OPT_RESET, "OPT RESET");
		if (err)
			goto err04;
		err = gpio_direction_output(H3XXX_EGPIO_OPT_RESET, 0);
		if (err)
			goto err05;
		err = gpio_request(H3XXX_EGPIO_CARD_RESET, "PCMCIA CARD RESET");
		if (err)
			goto err05;
		err = gpio_direction_output(H3XXX_EGPIO_CARD_RESET, 0);
		if (err)
			goto err06;
		break;
	case 1:
		skt->stat[SOC_STAT_CD].gpio = H3XXX_GPIO_PCMCIA_CD1;
		skt->stat[SOC_STAT_CD].name = "PCMCIA CD1";
		skt->stat[SOC_STAT_RDY].gpio = H3XXX_GPIO_PCMCIA_IRQ1;
		skt->stat[SOC_STAT_RDY].name = "PCMCIA IRQ1";
		break;
	}
	return 0;

err06:	gpio_free(H3XXX_EGPIO_CARD_RESET);
err05:	gpio_free(H3XXX_EGPIO_OPT_RESET);
err04:	gpio_free(H3XXX_EGPIO_OPT_ON);
err03:	gpio_free(H3XXX_EGPIO_OPT_NVRAM_ON);
err01:	gpio_free(H3XXX_GPIO_PCMCIA_IRQ0);
	return err;
}

static void h3600_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
  
	/* Disable CF bus: */
	clr_h3600_egpio(IPAQ_EGPIO_OPT_NVRAM_ON);
	clr_h3600_egpio(IPAQ_EGPIO_OPT_ON);
	set_h3600_egpio(IPAQ_EGPIO_OPT_RESET);
	switch (skt->nr) {
	case 0:
		/* Disable CF bus: */
		gpio_set_value(H3XXX_EGPIO_OPT_NVRAM_ON, 0);
		gpio_set_value(H3XXX_EGPIO_OPT_ON, 0);
		gpio_set_value(H3XXX_EGPIO_OPT_RESET, 1);

		gpio_free(H3XXX_EGPIO_CARD_RESET);
		gpio_free(H3XXX_EGPIO_OPT_RESET);
		gpio_free(H3XXX_EGPIO_OPT_ON);
		gpio_free(H3XXX_EGPIO_OPT_NVRAM_ON);
		break;
	case 1:
		break;
	}
}

static void
h3600_pcmcia_socket_state(struct soc_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned long levels = GPLR;

	switch (skt->nr) {
	case 0:
		state->detect = levels & GPIO_H3600_PCMCIA_CD0 ? 0 : 1;
		state->ready = levels & GPIO_H3600_PCMCIA_IRQ0 ? 1 : 0;
		state->bvd1 = 0;
		state->bvd2 = 0;
		state->wrprot = 0; /* Not available on H3600. */
		state->vs_3v = 0;
		state->vs_Xv = 0;
		break;

	case 1:
		state->detect = levels & GPIO_H3600_PCMCIA_CD1 ? 0 : 1;
		state->ready = levels & GPIO_H3600_PCMCIA_IRQ1 ? 1 : 0;
		state->bvd1 = 0;
		state->bvd2 = 0;
		state->wrprot = 0; /* Not available on H3600. */
		state->vs_3v = 0;
		state->vs_Xv = 0;
		break;
	}
	state->bvd1 = 0;
	state->bvd2 = 0;
	state->vs_3v = 0;
	state->vs_Xv = 0;
}

static int
h3600_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	if (state->Vcc != 0 && state->Vcc != 33 && state->Vcc != 50) {
		printk(KERN_ERR "h3600_pcmcia: unrecognized Vcc %u.%uV\n",
		       state->Vcc / 10, state->Vcc % 10);
		return -1;
	}

	if (state->flags & SS_RESET)
		set_h3600_egpio(IPAQ_EGPIO_CARD_RESET);
	else
		clr_h3600_egpio(IPAQ_EGPIO_CARD_RESET);
	gpio_set_value(H3XXX_EGPIO_CARD_RESET, !!(state->flags & SS_RESET));

	/* Silently ignore Vpp, output enable, speaker enable. */

	return 0;
}

static void h3600_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	/* Enable CF bus: */
	set_h3600_egpio(IPAQ_EGPIO_OPT_NVRAM_ON);
	set_h3600_egpio(IPAQ_EGPIO_OPT_ON);
	clr_h3600_egpio(IPAQ_EGPIO_OPT_RESET);

	msleep(10);

	soc_pcmcia_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
	gpio_set_value(H3XXX_EGPIO_OPT_NVRAM_ON, 1);
	gpio_set_value(H3XXX_EGPIO_OPT_ON, 1);
	gpio_set_value(H3XXX_EGPIO_OPT_RESET, 0);

	msleep(10);
}

static void h3600_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));

	/*
	 * FIXME:  This doesn't fit well.  We don't have the mechanism in
	 * the generic PCMCIA layer to deal with the idea of two sockets
	 * on one bus.  We rely on the cs.c behaviour shutting down
	 * socket 0 then socket 1.
	 */
	if (skt->nr == 1) {
		clr_h3600_egpio(IPAQ_EGPIO_OPT_ON);
		clr_h3600_egpio(IPAQ_EGPIO_OPT_NVRAM_ON);
		/* hmm, does this suck power? */
		set_h3600_egpio(IPAQ_EGPIO_OPT_RESET);
		gpio_set_value(H3XXX_EGPIO_OPT_ON, 0);
		gpio_set_value(H3XXX_EGPIO_OPT_NVRAM_ON, 0);
		/* hmm, does this suck power? */
		gpio_set_value(H3XXX_EGPIO_OPT_RESET, 1);
	}
}

struct pcmcia_low_level h3600_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.hw_init		= h3600_pcmcia_hw_init,
	.hw_shutdown		= h3600_pcmcia_hw_shutdown,
	.socket_state		= h3600_pcmcia_socket_state,
	.configure_socket	= h3600_pcmcia_configure_socket,

	.socket_init		= h3600_pcmcia_socket_init,
	.socket_suspend		= h3600_pcmcia_socket_suspend,
};

int __init pcmcia_h3600_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_h3600())
int pcmcia_h3600_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_h3600() || machine_is_h3100())
		ret = sa11xx_drv_pcmcia_probe(dev, &h3600_pcmcia_ops, 0, 2);

	return ret;
}
