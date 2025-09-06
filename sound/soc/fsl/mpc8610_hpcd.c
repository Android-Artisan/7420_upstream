/**
 * Freescale MPC8610HPCD ALSA SoC Fabric driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007-2008 Freescale Semiconductor, Inc.  This file is licensed
 * under the terms of the GNU General Public License version 2.  This
 * program is licensed "as is" without any warranty of any kind, whether
 * express or implied.
 * Freescale MPC8610HPCD ALSA SoC Machine driver
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2007-2010 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <asm/immap_86xx.h>

#include "../codecs/cs4270.h"
#include "fsl_dma.h"
#include "fsl_ssi.h"

/**
 * mpc8610_hpcd_data: fabric-specific ASoC device data
#include <linux/fsl/guts.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <sound/soc.h>

#include "fsl_dma.h"
#include "fsl_ssi.h"
#include "fsl_utils.h"

/* There's only one global utilities register */
static phys_addr_t guts_phys;

/**
 * mpc8610_hpcd_data: machine-specific ASoC device data
 *
 * This structure contains data for a single sound platform device on an
 * MPC8610 HPCD.  Some of the data is taken from the device tree.
 */
struct mpc8610_hpcd_data {
	struct snd_soc_device sound_devdata;
	struct snd_soc_dai_link dai;
	struct snd_soc_machine machine;
	struct snd_soc_dai_link dai[2];
	struct snd_soc_card card;
	unsigned int dai_format;
	unsigned int codec_clk_direction;
	unsigned int cpu_clk_direction;
	unsigned int clk_frequency;
	struct ccsr_guts __iomem *guts;
	struct ccsr_ssi __iomem *ssi;
	unsigned int ssi_id;    	/* 0 = SSI1, 1 = SSI2, etc */
	unsigned int ssi_irq;
	unsigned int dma_id;    	/* 0 = DMA1, 1 = DMA2, etc */
	unsigned int dma_irq[2];
	struct ccsr_dma_channel __iomem *dma[2];
	unsigned int dma_channel_id[2]; /* 0 = ch 0, 1 = ch 1, etc*/
};

/**
 * mpc8610_hpcd_machine_probe: initalize the board
 *
 * This function is called when platform_device_add() is called.  It is used
 * to initialize the board-specific hardware.
 *
 * Here we program the DMACR and PMUXCR registers.
 */
static int mpc8610_hpcd_machine_probe(struct platform_device *sound_device)
{
	struct mpc8610_hpcd_data *machine_data =
		sound_device->dev.platform_data;

	/* Program the signal routing between the SSI and the DMA */
	guts_set_dmacr(machine_data->guts, machine_data->dma_id,
		machine_data->dma_channel_id[0], CCSR_GUTS_DMACR_DEV_SSI);
	guts_set_dmacr(machine_data->guts, machine_data->dma_id,
		machine_data->dma_channel_id[1], CCSR_GUTS_DMACR_DEV_SSI);

	guts_set_pmuxcr_dma(machine_data->guts, machine_data->dma_id,
		machine_data->dma_channel_id[0], 0);
	guts_set_pmuxcr_dma(machine_data->guts, machine_data->dma_id,
		machine_data->dma_channel_id[1], 0);

	guts_set_pmuxcr_dma(machine_data->guts, 1, 0, 0);
	guts_set_pmuxcr_dma(machine_data->guts, 1, 3, 0);
	guts_set_pmuxcr_dma(machine_data->guts, 0, 3, 0);

	switch (machine_data->ssi_id) {
	case 0:
		clrsetbits_be32(&machine_data->guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI1_MASK, CCSR_GUTS_PMUXCR_SSI1_SSI);
		break;
	case 1:
		clrsetbits_be32(&machine_data->guts->pmuxcr,
	unsigned int ssi_id;		/* 0 = SSI1, 1 = SSI2, etc */
	unsigned int dma_id[2];		/* 0 = DMA1, 1 = DMA2, etc */
	unsigned int dma_channel_id[2]; /* 0 = ch 0, 1 = ch 1, etc*/
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[2][DAI_NAME_SIZE]; /* One for each DMA channel */
};

/**
 * mpc8610_hpcd_machine_probe: initialize the board
 *
 * This function is used to initialize the board-specific hardware.
 *
 * Here we program the DMACR and PMUXCR registers.
 */
static int mpc8610_hpcd_machine_probe(struct snd_soc_card *card)
{
	struct mpc8610_hpcd_data *machine_data =
		container_of(card, struct mpc8610_hpcd_data, card);
	struct ccsr_guts __iomem *guts;

	guts = ioremap(guts_phys, sizeof(struct ccsr_guts));
	if (!guts) {
		dev_err(card->dev, "could not map global utilities\n");
		return -ENOMEM;
	}

	/* Program the signal routing between the SSI and the DMA */
	guts_set_dmacr(guts, machine_data->dma_id[0],
		       machine_data->dma_channel_id[0],
		       CCSR_GUTS_DMACR_DEV_SSI);
	guts_set_dmacr(guts, machine_data->dma_id[1],
		       machine_data->dma_channel_id[1],
		       CCSR_GUTS_DMACR_DEV_SSI);

	guts_set_pmuxcr_dma(guts, machine_data->dma_id[0],
			    machine_data->dma_channel_id[0], 0);
	guts_set_pmuxcr_dma(guts, machine_data->dma_id[1],
			    machine_data->dma_channel_id[1], 0);

	switch (machine_data->ssi_id) {
	case 0:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI1_MASK, CCSR_GUTS_PMUXCR_SSI1_SSI);
		break;
	case 1:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI2_MASK, CCSR_GUTS_PMUXCR_SSI2_SSI);
		break;
	}

	iounmap(guts);

	return 0;
}

/**
 * mpc8610_hpcd_startup: program the board with various hardware parameters
 *
 * This function takes board-specific information, like clock frequencies
 * and serial data formats, and passes that information to the codec and
 * transport drivers.
 */
static int mpc8610_hpcd_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct mpc8610_hpcd_data *machine_data =
		rtd->socdev->dev->platform_data;
	int ret = 0;

	/* Tell the CPU driver what the serial protocol is. */
	ret = snd_soc_dai_set_fmt(cpu_dai, machine_data->dai_format);
	if (ret < 0) {
		dev_err(substream->pcm->card->dev,
			"could not set CPU driver audio format\n");
		return ret;
	}

	/* Tell the codec driver what the serial protocol is. */
	ret = snd_soc_dai_set_fmt(codec_dai, machine_data->dai_format);
	if (ret < 0) {
		dev_err(substream->pcm->card->dev,
			"could not set codec driver audio format\n");
		return ret;
	}

	/*
	 * Tell the CPU driver what the clock frequency is, and whether it's a
	 * slave or master.
	 */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0,
					machine_data->clk_frequency,
					machine_data->cpu_clk_direction);
	if (ret < 0) {
		dev_err(substream->pcm->card->dev,
			"could not set CPU driver clock parameters\n");
	struct mpc8610_hpcd_data *machine_data =
		container_of(rtd->card, struct mpc8610_hpcd_data, card);
	struct device *dev = rtd->card->dev;
	int ret = 0;

	/* Tell the codec driver what the serial protocol is. */
	ret = snd_soc_dai_set_fmt(rtd->codec_dai, machine_data->dai_format);
	if (ret < 0) {
		dev_err(dev, "could not set codec driver audio format\n");
		return ret;
	}

	/*
	 * Tell the codec driver what the MCLK frequency is, and whether it's
	 * a slave or master.
	 */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
					machine_data->clk_frequency,
					machine_data->codec_clk_direction);
	if (ret < 0) {
		dev_err(substream->pcm->card->dev,
			"could not set codec driver clock params\n");
	ret = snd_soc_dai_set_sysclk(rtd->codec_dai, 0,
				     machine_data->clk_frequency,
				     machine_data->codec_clk_direction);
	if (ret < 0) {
		dev_err(dev, "could not set codec driver clock params\n");
		return ret;
	}

	return 0;
}

/**
 * mpc8610_hpcd_machine_remove: Remove the sound device
 *
 * This function is called to remove the sound device for one SSI.  We
 * de-program the DMACR and PMUXCR register.
 */
int mpc8610_hpcd_machine_remove(struct platform_device *sound_device)
{
	struct mpc8610_hpcd_data *machine_data =
		sound_device->dev.platform_data;

	/* Restore the signal routing */

	guts_set_dmacr(machine_data->guts, machine_data->dma_id,
		machine_data->dma_channel_id[0], 0);
	guts_set_dmacr(machine_data->guts, machine_data->dma_id,
		machine_data->dma_channel_id[1], 0);

	switch (machine_data->ssi_id) {
	case 0:
		clrsetbits_be32(&machine_data->guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI1_MASK, CCSR_GUTS_PMUXCR_SSI1_LA);
		break;
	case 1:
		clrsetbits_be32(&machine_data->guts->pmuxcr,
static int mpc8610_hpcd_machine_remove(struct snd_soc_card *card)
{
	struct mpc8610_hpcd_data *machine_data =
		container_of(card, struct mpc8610_hpcd_data, card);
	struct ccsr_guts __iomem *guts;

	guts = ioremap(guts_phys, sizeof(struct ccsr_guts));
	if (!guts) {
		dev_err(card->dev, "could not map global utilities\n");
		return -ENOMEM;
	}

	/* Restore the signal routing */

	guts_set_dmacr(guts, machine_data->dma_id[0],
		       machine_data->dma_channel_id[0], 0);
	guts_set_dmacr(guts, machine_data->dma_id[1],
		       machine_data->dma_channel_id[1], 0);

	switch (machine_data->ssi_id) {
	case 0:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI1_MASK, CCSR_GUTS_PMUXCR_SSI1_LA);
		break;
	case 1:
		clrsetbits_be32(&guts->pmuxcr,
			CCSR_GUTS_PMUXCR_SSI2_MASK, CCSR_GUTS_PMUXCR_SSI2_LA);
		break;
	}

	iounmap(guts);

	return 0;
}

/**
 * mpc8610_hpcd_ops: ASoC fabric driver operations
 * mpc8610_hpcd_ops: ASoC machine driver operations
 */
static struct snd_soc_ops mpc8610_hpcd_ops = {
	.startup = mpc8610_hpcd_startup,
};

/**
 * mpc8610_hpcd_machine: ASoC machine data
 */
static struct snd_soc_machine mpc8610_hpcd_machine = {
	.probe = mpc8610_hpcd_machine_probe,
	.remove = mpc8610_hpcd_machine_remove,
	.name = "MPC8610 HPCD",
	.num_links = 1,
};

/**
 * mpc8610_hpcd_probe: OF probe function for the fabric driver
 *
 * This function gets called when an SSI node is found in the device tree.
 *
 * Although this is a fabric driver, the SSI node is the "master" node with
 * respect to audio hardware connections.  Therefore, we create a new ASoC
 * device for each new SSI node that has a codec attached.
 *
 * FIXME: Currently, we only support one DMA controller, so if there are
 * multiple SSI nodes with codecs, only the first will be supported.
 *
 * FIXME: Even if we did support multiple DMA controllers, we have no
 * mechanism for assigning DMA controllers and channels to the individual
 * SSI devices.  We also probably aren't compatible with the generic Elo DMA
 * device driver.
 */
static int mpc8610_hpcd_probe(struct of_device *ofdev,
	const struct of_device_id *match)
{
	struct device_node *np = ofdev->node;
	struct device_node *codec_np = NULL;
	struct device_node *guts_np = NULL;
	struct device_node *dma_np = NULL;
	struct device_node *dma_channel_np = NULL;
	const phandle *codec_ph;
	const char *sprop;
	const u32 *iprop;
	struct resource res;
	struct platform_device *sound_device = NULL;
	struct mpc8610_hpcd_data *machine_data;
	struct fsl_ssi_info ssi_info;
	struct fsl_dma_info dma_info;
	int ret = -ENODEV;

	machine_data = kzalloc(sizeof(struct mpc8610_hpcd_data), GFP_KERNEL);
	if (!machine_data)
		return -ENOMEM;

	memset(&ssi_info, 0, sizeof(ssi_info));
	memset(&dma_info, 0, sizeof(dma_info));

	ssi_info.dev = &ofdev->dev;

	/*
	 * We are only interested in SSIs with a codec phandle in them, so let's
	 * make sure this SSI has one.
	 */
	codec_ph = of_get_property(np, "codec-handle", NULL);
	if (!codec_ph)
		goto error;

	codec_np = of_find_node_by_phandle(*codec_ph);
	if (!codec_np)
		goto error;

	/* The MPC8610 HPCD only knows about the CS4270 codec, so reject
	   anything else. */
	if (!of_device_is_compatible(codec_np, "cirrus,cs4270"))
		goto error;
 * mpc8610_hpcd_probe: platform probe function for the machine driver
 *
 * Although this is a machine driver, the SSI node is the "master" node with
 * respect to audio hardware connections.  Therefore, we create a new ASoC
 * device for each new SSI node that has a codec attached.
 */
static int mpc8610_hpcd_probe(struct platform_device *pdev)
{
	struct device *dev = pdev->dev.parent;
	/* ssi_pdev is the platform device for the SSI node that probed us */
	struct platform_device *ssi_pdev =
		container_of(dev, struct platform_device, dev);
	struct device_node *np = ssi_pdev->dev.of_node;
	struct device_node *codec_np = NULL;
	struct mpc8610_hpcd_data *machine_data;
	int ret = -ENODEV;
	const char *sprop;
	const u32 *iprop;

	/* Find the codec node for this SSI. */
	codec_np = of_parse_phandle(np, "codec-handle", 0);
	if (!codec_np) {
		dev_err(dev, "invalid codec node\n");
		return -EINVAL;
	}

	machine_data = kzalloc(sizeof(struct mpc8610_hpcd_data), GFP_KERNEL);
	if (!machine_data) {
		ret = -ENOMEM;
		goto error_alloc;
	}

	machine_data->dai[0].cpu_dai_name = dev_name(&ssi_pdev->dev);
	machine_data->dai[0].ops = &mpc8610_hpcd_ops;

	/* ASoC core can match codec with device node */
	machine_data->dai[0].codec_of_node = codec_np;

	/* The DAI name from the codec (snd_soc_dai_driver.name) */
	machine_data->dai[0].codec_dai_name = "cs4270-hifi";

	/* We register two DAIs per SSI, one for playback and the other for
	 * capture.  Currently, we only support codecs that have one DAI for
	 * both playback and capture.
	 */
	memcpy(&machine_data->dai[1], &machine_data->dai[0],
	       sizeof(struct snd_soc_dai_link));

	/* Get the device ID */
	iprop = of_get_property(np, "cell-index", NULL);
	if (!iprop) {
		dev_err(&ofdev->dev, "cell-index property not found\n");
		ret = -EINVAL;
		goto error;
	}
	machine_data->ssi_id = *iprop;
	ssi_info.id = *iprop;
		dev_err(&pdev->dev, "cell-index property not found\n");
		ret = -EINVAL;
		goto error;
	}
	machine_data->ssi_id = be32_to_cpup(iprop);

	/* Get the serial format and clock direction. */
	sprop = of_get_property(np, "fsl,mode", NULL);
	if (!sprop) {
		dev_err(&ofdev->dev, "fsl,mode property not found\n");
		dev_err(&pdev->dev, "fsl,mode property not found\n");
		ret = -EINVAL;
		goto error;
	}

	if (strcasecmp(sprop, "i2s-slave") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_I2S;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;

		/*
		 * In i2s-slave mode, the codec has its own clock source, so we
		machine_data->dai_format =
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;

		/* In i2s-slave mode, the codec has its own clock source, so we
		 * need to get the frequency from the device tree and pass it to
		 * the codec driver.
		 */
		iprop = of_get_property(codec_np, "clock-frequency", NULL);
		if (!iprop || !*iprop) {
			dev_err(&ofdev->dev, "codec bus-frequency property "
				"is missing or invalid\n");
			ret = -EINVAL;
			goto error;
		}
		machine_data->clk_frequency = *iprop;
	} else if (strcasecmp(sprop, "i2s-master") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_I2S;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "lj-slave") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_LEFT_J;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "lj-master") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_LEFT_J;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "rj-slave") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_RIGHT_J;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "rj-master") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_RIGHT_J;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "ac97-slave") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_AC97;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "ac97-master") == 0) {
		machine_data->dai_format = SND_SOC_DAIFMT_AC97;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else {
		dev_err(&ofdev->dev,
			"unrecognized fsl,mode property \"%s\"\n", sprop);
			dev_err(&pdev->dev, "codec bus-frequency "
				"property is missing or invalid\n");
			ret = -EINVAL;
			goto error;
		}
		machine_data->clk_frequency = be32_to_cpup(iprop);
	} else if (strcasecmp(sprop, "i2s-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "lj-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBM_CFM;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "lj-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_CBS_CFS;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "rj-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_CBM_CFM;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "rj-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_RIGHT_J | SND_SOC_DAIFMT_CBS_CFS;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else if (strcasecmp(sprop, "ac97-slave") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_AC97 | SND_SOC_DAIFMT_CBM_CFM;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_OUT;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_IN;
	} else if (strcasecmp(sprop, "ac97-master") == 0) {
		machine_data->dai_format =
			SND_SOC_DAIFMT_AC97 | SND_SOC_DAIFMT_CBS_CFS;
		machine_data->codec_clk_direction = SND_SOC_CLOCK_IN;
		machine_data->cpu_clk_direction = SND_SOC_CLOCK_OUT;
	} else {
		dev_err(&pdev->dev,
			"unrecognized fsl,mode property '%s'\n", sprop);
		ret = -EINVAL;
		goto error;
	}

	if (!machine_data->clk_frequency) {
		dev_err(&ofdev->dev, "unknown clock frequency\n");
		dev_err(&pdev->dev, "unknown clock frequency\n");
		ret = -EINVAL;
		goto error;
	}

	/* Read the SSI information from the device tree */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&ofdev->dev, "could not obtain SSI address\n");
		goto error;
	}
	if (!res.start) {
		dev_err(&ofdev->dev, "invalid SSI address\n");
		goto error;
	}
	ssi_info.ssi_phys = res.start;

	machine_data->ssi = ioremap(ssi_info.ssi_phys, sizeof(struct ccsr_ssi));
	if (!machine_data->ssi) {
		dev_err(&ofdev->dev, "could not map SSI address %x\n",
			ssi_info.ssi_phys);
		ret = -EINVAL;
		goto error;
	}
	ssi_info.ssi = machine_data->ssi;


	/* Get the IRQ of the SSI */
	machine_data->ssi_irq = irq_of_parse_and_map(np, 0);
	if (!machine_data->ssi_irq) {
		dev_err(&ofdev->dev, "could not get SSI IRQ\n");
		ret = -EINVAL;
		goto error;
	}
	ssi_info.irq = machine_data->ssi_irq;


	/* Map the global utilities registers. */
	guts_np = of_find_compatible_node(NULL, NULL, "fsl,mpc8610-guts");
	if (!guts_np) {
		dev_err(&ofdev->dev, "could not obtain address of GUTS\n");
		ret = -EINVAL;
		goto error;
	}
	machine_data->guts = of_iomap(guts_np, 0);
	of_node_put(guts_np);
	if (!machine_data->guts) {
		dev_err(&ofdev->dev, "could not map GUTS\n");
		ret = -EINVAL;
		goto error;
	}

	/* Find the DMA channels to use.  For now, we always use the first DMA
	   controller. */
	for_each_compatible_node(dma_np, NULL, "fsl,mpc8610-dma") {
		iprop = of_get_property(dma_np, "cell-index", NULL);
		if (iprop && (*iprop == 0)) {
			of_node_put(dma_np);
			break;
		}
	}
	if (!dma_np) {
		dev_err(&ofdev->dev, "could not find DMA node\n");
		ret = -EINVAL;
		goto error;
	}
	machine_data->dma_id = *iprop;

	/*
	 * Find the DMA channels to use.  For now, we always use DMA channel 0
	 * for playback, and DMA channel 1 for capture.
	 */
	while ((dma_channel_np = of_get_next_child(dma_np, dma_channel_np))) {
		iprop = of_get_property(dma_channel_np, "cell-index", NULL);
		/* Is it DMA channel 0? */
		if (iprop && (*iprop == 0)) {
			/* dma_channel[0] and dma_irq[0] are for playback */
			dma_info.dma_channel[0] = of_iomap(dma_channel_np, 0);
			dma_info.dma_irq[0] =
				irq_of_parse_and_map(dma_channel_np, 0);
			machine_data->dma_channel_id[0] = *iprop;
			continue;
		}
		if (iprop && (*iprop == 1)) {
			/* dma_channel[1] and dma_irq[1] are for capture */
			dma_info.dma_channel[1] = of_iomap(dma_channel_np, 0);
			dma_info.dma_irq[1] =
				irq_of_parse_and_map(dma_channel_np, 0);
			machine_data->dma_channel_id[1] = *iprop;
			continue;
		}
	}
	if (!dma_info.dma_channel[0] || !dma_info.dma_channel[1] ||
	    !dma_info.dma_irq[0] || !dma_info.dma_irq[1]) {
		dev_err(&ofdev->dev, "could not find DMA channels\n");
		ret = -EINVAL;
		goto error;
	}

	dma_info.ssi_stx_phys = ssi_info.ssi_phys +
		offsetof(struct ccsr_ssi, stx0);
	dma_info.ssi_srx_phys = ssi_info.ssi_phys +
		offsetof(struct ccsr_ssi, srx0);

	/* We have the DMA information, so tell the DMA driver what it is */
	if (!fsl_dma_configure(&dma_info)) {
		dev_err(&ofdev->dev, "could not instantiate DMA device\n");
		ret = -EBUSY;
		goto error;
	}

	/*
	 * Initialize our DAI data structure.  We should probably get this
	 * information from the device tree.
	 */
	machine_data->dai.name = "CS4270";
	machine_data->dai.stream_name = "CS4270";

	machine_data->dai.cpu_dai = fsl_ssi_create_dai(&ssi_info);
	machine_data->dai.codec_dai = &cs4270_dai; /* The codec_dai we want */
	machine_data->dai.ops = &mpc8610_hpcd_ops;

	mpc8610_hpcd_machine.dai_link = &machine_data->dai;

	/* Allocate a new audio platform device structure */
	sound_device = platform_device_alloc("soc-audio", -1);
	if (!sound_device) {
		dev_err(&ofdev->dev, "platform device allocation failed\n");
		ret = -ENOMEM;
		goto error;
	}

	machine_data->sound_devdata.machine = &mpc8610_hpcd_machine;
	machine_data->sound_devdata.codec_dev = &soc_codec_device_cs4270;
	machine_data->sound_devdata.platform = &fsl_soc_platform;

	sound_device->dev.platform_data = machine_data;


	/* Set the platform device and ASoC device to point to each other */
	platform_set_drvdata(sound_device, &machine_data->sound_devdata);

	machine_data->sound_devdata.dev = &sound_device->dev;


	/* Tell ASoC to probe us.  This will call mpc8610_hpcd_machine.probe(),
	   if it exists. */
	ret = platform_device_add(sound_device);

	if (ret) {
		dev_err(&ofdev->dev, "platform device add failed\n");
		goto error;
	}

	dev_set_drvdata(&ofdev->dev, sound_device);
	/* Find the playback DMA channel to use. */
	machine_data->dai[0].platform_name = machine_data->platform_name[0];
	ret = fsl_asoc_get_dma_channel(np, "fsl,playback-dma",
				       &machine_data->dai[0],
				       &machine_data->dma_channel_id[0],
				       &machine_data->dma_id[0]);
	if (ret) {
		dev_err(&pdev->dev, "missing/invalid playback DMA phandle\n");
		goto error;
	}

	/* Find the capture DMA channel to use. */
	machine_data->dai[1].platform_name = machine_data->platform_name[1];
	ret = fsl_asoc_get_dma_channel(np, "fsl,capture-dma",
				       &machine_data->dai[1],
				       &machine_data->dma_channel_id[1],
				       &machine_data->dma_id[1]);
	if (ret) {
		dev_err(&pdev->dev, "missing/invalid capture DMA phandle\n");
		goto error;
	}

	/* Initialize our DAI data structure.  */
	machine_data->dai[0].stream_name = "playback";
	machine_data->dai[1].stream_name = "capture";
	machine_data->dai[0].name = machine_data->dai[0].stream_name;
	machine_data->dai[1].name = machine_data->dai[1].stream_name;

	machine_data->card.probe = mpc8610_hpcd_machine_probe;
	machine_data->card.remove = mpc8610_hpcd_machine_remove;
	machine_data->card.name = pdev->name; /* The platform driver name */
	machine_data->card.owner = THIS_MODULE;
	machine_data->card.dev = &pdev->dev;
	machine_data->card.num_links = 2;
	machine_data->card.dai_link = machine_data->dai;

	/* Register with ASoC */
	ret = snd_soc_register_card(&machine_data->card);
	if (ret) {
		dev_err(&pdev->dev, "could not register card\n");
		goto error;
	}

	of_node_put(codec_np);

	return 0;

error:
	of_node_put(codec_np);
	of_node_put(guts_np);
	of_node_put(dma_np);
	of_node_put(dma_channel_np);

	if (sound_device)
		platform_device_unregister(sound_device);

	if (machine_data->dai.cpu_dai)
		fsl_ssi_destroy_dai(machine_data->dai.cpu_dai);

	if (ssi_info.ssi)
		iounmap(ssi_info.ssi);

	if (ssi_info.irq)
		irq_dispose_mapping(ssi_info.irq);

	if (dma_info.dma_channel[0])
		iounmap(dma_info.dma_channel[0]);

	if (dma_info.dma_channel[1])
		iounmap(dma_info.dma_channel[1]);

	if (dma_info.dma_irq[0])
		irq_dispose_mapping(dma_info.dma_irq[0]);

	if (dma_info.dma_irq[1])
		irq_dispose_mapping(dma_info.dma_irq[1]);

	if (machine_data->guts)
		iounmap(machine_data->guts);

	kfree(machine_data);

	kfree(machine_data);
error_alloc:
	of_node_put(codec_np);
	return ret;
}

/**
 * mpc8610_hpcd_remove: remove the OF device
 *
 * This function is called when the OF device is removed.
 */
static int mpc8610_hpcd_remove(struct of_device *ofdev)
{
	struct platform_device *sound_device = dev_get_drvdata(&ofdev->dev);
	struct mpc8610_hpcd_data *machine_data =
		sound_device->dev.platform_data;

	platform_device_unregister(sound_device);

	if (machine_data->dai.cpu_dai)
		fsl_ssi_destroy_dai(machine_data->dai.cpu_dai);

	if (machine_data->ssi)
		iounmap(machine_data->ssi);

	if (machine_data->dma[0])
		iounmap(machine_data->dma[0]);

	if (machine_data->dma[1])
		iounmap(machine_data->dma[1]);

	if (machine_data->dma_irq[0])
		irq_dispose_mapping(machine_data->dma_irq[0]);

	if (machine_data->dma_irq[1])
		irq_dispose_mapping(machine_data->dma_irq[1]);

	if (machine_data->guts)
		iounmap(machine_data->guts);

	kfree(machine_data);
	sound_device->dev.platform_data = NULL;

	dev_set_drvdata(&ofdev->dev, NULL);
 * mpc8610_hpcd_remove: remove the platform device
 *
 * This function is called when the platform device is removed.
 */
static int mpc8610_hpcd_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct mpc8610_hpcd_data *machine_data =
		container_of(card, struct mpc8610_hpcd_data, card);

	snd_soc_unregister_card(card);
	kfree(machine_data);

	return 0;
}

static struct of_device_id mpc8610_hpcd_match[] = {
	{
		.compatible = "fsl,mpc8610-ssi",
	},
	{}
};
MODULE_DEVICE_TABLE(of, mpc8610_hpcd_match);

static struct of_platform_driver mpc8610_hpcd_of_driver = {
	.owner  	= THIS_MODULE,
	.name   	= "mpc8610_hpcd",
	.match_table    = mpc8610_hpcd_match,
	.probe  	= mpc8610_hpcd_probe,
	.remove 	= mpc8610_hpcd_remove,
};

/**
 * mpc8610_hpcd_init: fabric driver initialization.
static struct platform_driver mpc8610_hpcd_driver = {
	.probe = mpc8610_hpcd_probe,
	.remove = mpc8610_hpcd_remove,
	.driver = {
		/* The name must match 'compatible' property in the device tree,
		 * in lowercase letters.
		 */
		.name = "snd-soc-mpc8610hpcd",
	},
};

/**
 * mpc8610_hpcd_init: machine driver initialization.
 *
 * This function is called when this module is loaded.
 */
static int __init mpc8610_hpcd_init(void)
{
	int ret;

	printk(KERN_INFO "Freescale MPC8610 HPCD ALSA SoC fabric driver\n");

	ret = of_register_platform_driver(&mpc8610_hpcd_of_driver);

	if (ret)
		printk(KERN_ERR
			"mpc8610-hpcd: failed to register platform driver\n");

	return ret;
}

/**
 * mpc8610_hpcd_exit: fabric driver exit
	struct device_node *guts_np;
	struct resource res;

	pr_info("Freescale MPC8610 HPCD ALSA SoC machine driver\n");

	/* Get the physical address of the global utilities registers */
	guts_np = of_find_compatible_node(NULL, NULL, "fsl,mpc8610-guts");
	if (of_address_to_resource(guts_np, 0, &res)) {
		pr_err("mpc8610-hpcd: missing/invalid global utilities node\n");
		return -EINVAL;
	}
	guts_phys = res.start;

	return platform_driver_register(&mpc8610_hpcd_driver);
}

/**
 * mpc8610_hpcd_exit: machine driver exit
 *
 * This function is called when this driver is unloaded.
 */
static void __exit mpc8610_hpcd_exit(void)
{
	of_unregister_platform_driver(&mpc8610_hpcd_of_driver);
	platform_driver_unregister(&mpc8610_hpcd_driver);
}

module_init(mpc8610_hpcd_init);
module_exit(mpc8610_hpcd_exit);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale MPC8610 HPCD ALSA SoC fabric driver");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Freescale MPC8610 HPCD ALSA SoC machine driver");
MODULE_LICENSE("GPL v2");
