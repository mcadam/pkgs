/*
 * File     : dante-pcie.c
 * Created  : Thu Feb 10 18:18:50 EST 2011
 * Updated  : 2016-09-23
 * Author   : Aidan Williams
 * Synopsis : Dante PCIe Linux ALSA driver
 *
 * Portions of this software are copyright (c) 2004-2016 Audinate Pty Ltd and/or
 * its licensors. All intellectual property rights in such portions of the
 * Software and documentation are owned by Audinate and are protected by
 * United States copyright laws, other applicable copyright laws
 * and international treaty provisions. Audinate and its suppliers retain
 * all rights not expressly granted.
 *
 * Audinate Copyright Header Version 2
 */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "dante-pcie.h"
#include "dante-pcie-dmap.h"

static const int rates_defines[] = {SNDRV_PCM_RATE_192000,
				    SNDRV_PCM_RATE_96000,
				    SNDRV_PCM_RATE_48000,
				    0,
				    SNDRV_PCM_RATE_176400,
				    SNDRV_PCM_RATE_88200,
				    SNDRV_PCM_RATE_44100};
static const int rates_raw[] = {192000, 96000, 48000, 0, 176400, 88200, 44100};
static const int max_channels[] = {64, 128, 128, 0, 64, 128, 128};

/* Structure definitions */
struct dante_snd {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct pci_dev *pci;

	int sample_rate;
	int irq;

	unsigned long bar0_addr;
	void __iomem *bar0;

	unsigned long bar4_addr;
	void __iomem *bar4;

	int irq_offset;

	struct snd_pcm_substream *capture_substream;
	struct snd_pcm_substream *playback_substream;

	int playback_period_size;
	int capture_period_size;

	int playback_channels;
	int capture_channels;

	bool engine_running;
	bool capture_running;
	bool playback_running;
};


/* Function forward declarations */
static int snd_dante_playback_open(struct snd_pcm_substream *substream);
static int snd_dante_capture_open(struct snd_pcm_substream *substream);

static int snd_dante_close(struct snd_pcm_substream *substream);

static int snd_dante_pcm_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params);
static int snd_dante_pcm_hw_free(struct snd_pcm_substream *substream);

static int snd_dante_pcm_prepare(struct snd_pcm_substream *substream);
static int snd_dante_pcm_trigger(struct snd_pcm_substream *substream, int cmd);

static snd_pcm_uframes_t snd_dante_pcm_pointer(struct snd_pcm_substream *substream);

static int dante_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void dante_remove(struct pci_dev *dev);

static void reset_device(struct dante_snd *chip);
static void halt_flex_dma(struct dante_snd *chip);
static void start_flex_dma(struct dante_snd *chip);
static void dump_vdma_rx_vars(struct dante_snd *chip);
static void dump_vdma_tx_vars(struct dante_snd *chip);
static void dump_vdma_regs(struct dante_snd *chip);

static int dante_init(void);
static void dante_exit(void);

/* Static data */
static int  index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

static int device_index;

static unsigned int irqcount = 0;

static const struct pci_device_id dante_ids[] = {
    { PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
    { 0, },
};

static const struct snd_pcm_hardware snd_dante_hw = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_NONINTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP_VALID),
	/*
	 *  argh.
	 *
	 *  The ALSA documentation is confusing, ensuring "job security"
	 *    http://www.volkerschatz.com/noise/alsa.html
	 *
	 *  Originally, the driver advertised S24_LE and S32_LE,
	 *  because of unhelpful or downright misleading documentation
	 *  like the following:
	 *
	 *    http://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html
	 *
	 *    PCM formats
	 *
	 *    The full list of formats present the snd_pcm_format_t
	 *    type. The 24-bit linear samples use 32-bit physical
	 *    space, but the sample is stored in the lower three
	 *    bytes. [ ... ]
	 *
	 *  In actual fact, S32_LE/S32_BE covers all "aligned high"
	 *  PCM formats: 32/16/8/...  For example, S16_LE is aligned
	 *  high in a 32-bit word.  The S24_LE format name is really
	 *  indicating that the samples are aligned *low*.
	 *
	 *  Our FPGA hardware uses 24 bits aligned high, for example
	 *    0xfe1312xx 0xfe262800
	 *  This format is S32_LE, *not* S24_LE.
	 *
	 *  Therefore, this declaration is WRONG:
	 *    .formats = ( SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE ),
	 */
	.formats = SNDRV_PCM_FMTBIT_S32_LE,

	.channels_min = 1,
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
};

static struct snd_pcm_ops snd_dante_playback_ops = {
	.open = snd_dante_playback_open,
	.close = snd_dante_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_dante_pcm_hw_params,
	.hw_free = snd_dante_pcm_hw_free,
	.prepare = snd_dante_pcm_prepare,
	.trigger = snd_dante_pcm_trigger,
	.pointer = snd_dante_pcm_pointer
};

static struct snd_pcm_ops snd_dante_capture_ops = {
	.open = snd_dante_capture_open,
	.close = snd_dante_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_dante_pcm_hw_params,
	.hw_free = snd_dante_pcm_hw_free,
	.prepare = snd_dante_pcm_prepare,
	.trigger = snd_dante_pcm_trigger,
	.pointer = snd_dante_pcm_pointer,
};

static struct pci_driver dante_pci_driver = {
	.name = "DantePCIe",
	.id_table = dante_ids,
	.probe = dante_probe,
	.remove = dante_remove,
};

/* Module definitions */
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Audinate Pty. Ltd.");
MODULE_DESCRIPTION("Dante ALSA Driver");

MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");

MODULE_DEVICE_TABLE(pci, dante_ids);

module_param_array(index, int, NULL, 0444);
module_param_array(id, charp, NULL, 0444);
module_param_array(enable, bool, NULL, 0444);

module_init(dante_init);
module_exit(dante_exit);

/* Implementations */
static void init_rates(struct dante_snd *chip, struct snd_pcm_runtime *runtime) {
	runtime->hw.rates = rates_defines[chip->sample_rate];

	runtime->hw.rate_min = rates_raw[chip->sample_rate];
	runtime->hw.rate_max = rates_raw[chip->sample_rate];

	runtime->hw.channels_max = max_channels[chip->sample_rate];

	runtime->hw.buffer_bytes_max = PERIODS_MAX * CHUNK_SIZE * runtime->hw.channels_max;

	runtime->hw.period_bytes_min = CHUNK_SIZE * 1;
	runtime->hw.period_bytes_max = MAX_CHUNKS_PER_IRQ * CHUNK_SIZE * runtime->hw.channels_max;
}

static int snd_dante_playback_open(struct snd_pcm_substream *substream) {
	struct dante_snd *chip = snd_pcm_substream_chip(substream);

	chip->playback_substream = substream;

	substream->runtime->hw = snd_dante_hw;
	init_rates(chip, substream->runtime);

	return 0;
}

static int snd_dante_capture_open(struct snd_pcm_substream *substream) {
	struct dante_snd *chip = snd_pcm_substream_chip(substream);

	chip->capture_substream = substream;

	substream->runtime->hw = snd_dante_hw;
	init_rates(chip, substream->runtime);

	return 0;
}

static int snd_dante_close(struct snd_pcm_substream *substream) {
	struct dante_snd *chip = snd_pcm_substream_chip(substream);

	if (substream == chip->capture_substream) {
		chip->capture_substream = NULL;
		chip->capture_period_size = 0;
	}
	if (substream == chip->playback_substream) {
		chip->playback_substream = NULL;
		chip->playback_period_size = 0;
	}

	return 0;
}

static int snd_dante_pcm_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params) {
	int err;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dante_snd *chip = snd_pcm_substream_chip(substream);
	uint32_t num_chunks;

	if (chip->playback_substream == substream) {
		printk(KERN_DEBUG "hw_params (playback):\n");
	} else {
		printk(KERN_DEBUG "hw_params (capture):\n");
	}

	printk(KERN_DEBUG "  buffer bytes: %d\n", params_buffer_bytes(hw_params));
	printk(KERN_DEBUG "  buffer size : %d\n", params_buffer_size(hw_params));
	printk(KERN_DEBUG "  period bytes: %d\n", params_period_bytes(hw_params));
	printk(KERN_DEBUG "  period size : %d\n", params_period_size(hw_params));
	printk(KERN_DEBUG "  periods     : %d\n", params_periods(hw_params));
	printk(KERN_DEBUG "  channels    : %d\n", params_channels(hw_params));

	if (params_buffer_bytes(hw_params) % params_period_bytes(hw_params) != 0) {
		printk(KERN_INFO "buffer bytes (%d) must be an integer multiple of period bytes (%d)\n",
			   params_buffer_bytes(hw_params), params_period_bytes(hw_params));
		return -1;
	}

	if (params_period_size(hw_params) % 16 != 0) {
		printk(KERN_INFO "period size (%d) must be an integer multiple of 16\n",
			   params_period_size(hw_params));
		return -1;
	}


	if (chip->playback_substream == substream) {
		if (chip->capture_period_size != 0 && chip->capture_period_size != params_period_size(hw_params)) {
			printk(KERN_INFO "playback period size (%d) must be the same at the capture period size (%d)\n",
				   params_period_size(hw_params), chip->capture_period_size);
			return -1;
		}
	} else {
		if (chip->playback_period_size != 0 && chip->playback_period_size != params_period_size(hw_params)) {
			printk(KERN_INFO "capture period size (%d) must be the same at the playback period size (%d)\n",
				   params_period_size(hw_params), chip->playback_period_size);
			return -1;
		}
	}

	/* Buffer memory is managed automatically by snd_pcm_set_managed_buffer_all (kernel >= 5.14) */
	err = 0;

	/*
	   Ensure that the buffer does not pass a 4GB boundary. The
	   DMA engine can not handle incrementing buffer pointers past
	   a 4GB boundary
	 */
	if (EXTRACT_LO_WORD(runtime->dma_addr) + params_buffer_bytes(hw_params) <
	    EXTRACT_LO_WORD(runtime->dma_addr)) {
		printk(KERN_ERR "Allocated pages span a 4GB boundary.");
		return -ENXIO; /* FIXME: What is the right error code to use here */
	}

	num_chunks = runtime->dma_bytes / params_channels(hw_params) / CHUNK_SIZE;

	WRITE_DMAP(params_period_size(hw_params) / 16, chip, DMAP_VAR_PERIODS);

	if (chip->playback_substream == substream) {
		chip->playback_period_size = params_period_size(hw_params);
		chip->playback_channels = params_channels(hw_params);

		WRITE_DMAP(params_channels(hw_params), chip, DMAP_VAR_CHANNELS_TX);
		WRITE_DMAP(num_chunks, chip, DMAP_VAR_CHUNKS_TX);
		WRITE_DMAP(0, chip, DMAP_VAR_CUR_CHUNK_TX);
		WRITE_DMAP(EXTRACT_LO_WORD(runtime->dma_addr), chip, DMAP_VAR_TX_BUF_ADDR_LO);
		WRITE_DMAP(EXTRACT_HI_WORD(runtime->dma_addr), chip, DMAP_VAR_TX_BUF_ADDR_HI);
		WRITE_DMAP(num_chunks * CHUNK_SIZE, chip, DMAP_VAR_TX_CHANNEL_STEP);

	} else {
		chip->capture_period_size = params_period_size(hw_params);
		chip->capture_channels = params_channels(hw_params);

		WRITE_DMAP(params_channels(hw_params), chip, DMAP_VAR_CHANNELS_RX);
		WRITE_DMAP(num_chunks, chip, DMAP_VAR_CHUNKS_RX);
		WRITE_DMAP(0, chip, DMAP_VAR_CUR_CHUNK_RX);
		WRITE_DMAP(EXTRACT_LO_WORD(runtime->dma_addr), chip, DMAP_VAR_RX_BUF_ADDR_LO);
		WRITE_DMAP(EXTRACT_HI_WORD(runtime->dma_addr), chip, DMAP_VAR_RX_BUF_ADDR_HI);
		WRITE_DMAP(num_chunks * CHUNK_SIZE, chip, DMAP_VAR_RX_CHANNEL_STEP);
	}

	dump_vdma_rx_vars(chip);
	dump_vdma_tx_vars(chip);

	return err;
}

static int snd_dante_pcm_hw_free(struct snd_pcm_substream *substream) {
	/* Buffer memory is managed automatically (kernel >= 5.14) */
	return 0;
}

static int snd_dante_pcm_prepare(struct snd_pcm_substream *substream) {
	return 0;
}

static int snd_dante_pcm_trigger(struct snd_pcm_substream *substream, int cmd) {
	struct dante_snd *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (!chip->engine_running) {
			start_flex_dma(chip);
		}
		if (substream == chip->playback_substream) {
		  /* FIXME: may want to check that this actually takes */
			iowrite32(DMAP_EV_TX_ENABLE, chip->bar0 + VDMA_EVENT_SET);
			chip->playback_running = true;
		} else {
			iowrite32(DMAP_EV_RX_ENABLE, chip->bar0 + VDMA_EVENT_SET);
			chip->capture_running = true;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (substream == chip->playback_substream) {
			iowrite32(DMAP_EV_TX_ENABLE, chip->bar0 + VDMA_EVENT_CLR);
			chip->playback_running = false;
		} else {
			iowrite32(DMAP_EV_RX_ENABLE, chip->bar0 + VDMA_EVENT_CLR);
			chip->capture_running = false;
		}

		if (!chip->capture_running && !chip->playback_running) {
			halt_flex_dma(chip);
		}
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		/* FIXME: Do we need to handle this case? */
		TRACE();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t snd_dante_pcm_pointer(struct snd_pcm_substream *substream) {
	struct dante_snd *chip = snd_pcm_substream_chip(substream);
	int channels = (substream == chip->playback_substream) ? chip->playback_channels : chip->capture_channels;
	uint32_t low, offset;

	if (substream == chip->playback_substream) {
		low = READ_DMAP(chip, DMAP_VAR_TX_BASE_ADDR_LO);
	} else {
		low = READ_DMAP(chip, DMAP_VAR_RX_BASE_ADDR_LO);
	}
	offset = low - EXTRACT_LO_WORD(substream->runtime->dma_addr);

#if 0
	printk(KERN_ALERT "%s 0x%x %d",
	       (substream == chip->playback_substream) ? "playback" : "capture",
	       low, bytes_to_frames(substream->runtime, offset * channels));
#endif

	return bytes_to_frames(substream->runtime, offset * channels);
}

static irqreturn_t snd_dante_interrupt(int irq, void *dev_id) {
	struct dante_snd *chip = dev_id;
	uint32_t stat;

	stat = ioread32(chip->bar4 + INT_STAT);
	if (stat == 0) {
		return IRQ_NONE;
	}
	/* If we get any interrupt other than external interrupt 1, something
	   went badly wrong */
	if (stat & ~INT1_MASK) {
		printk(KERN_ERR "Invalid interrupt status: %x", stat);
		reset_device(chip);
		return IRQ_HANDLED;
	}

	irqcount++;
#if 0
	/* This code can be useful for debugging problems with interrupts and the VDMA
	   sequencer, so left in */
	if (irqcount % 6000 == 0) {
	  printk(KERN_ERR "IRQ COUNT: %u\n", irqcount);
	}
#endif

	/* Acknowledge and clear the interrupt */
	iowrite32(INT1_MASK, chip->bar4 + INT_STAT);

	if (chip->capture_running) {
		snd_pcm_period_elapsed(chip->capture_substream);
	}
	if (chip->playback_running) {
		snd_pcm_period_elapsed(chip->playback_substream);
	}

	return IRQ_HANDLED;
}


static void download_flex_ram(struct dante_snd *chip, unsigned int index,
			      uint32_t const *src, unsigned int count) {
	/* FIXME: Error check that it fits */
	unsigned int i;
	uint32_t *dest = chip->bar0 + VDMA_DESCRIPTOR_RAM_BASE + index * sizeof(uint32_t);
	for (i = 0; i < count; i++) {
		iowrite32(cpu_to_le32(src[i]), dest);
		dest++;
	}
}

#define DUMP_VAR(var) do {						\
		uint32_t r = READ_DMAP(chip, DMAP_VAR_##var);		\
		printk(KERN_ALERT #var " %08x\n", r);		\
	} while(0)


static void dump_vdma_tx_vars(struct dante_snd *chip) {
	DUMP_VAR(PERIODS);
	DUMP_VAR(CUR_PERIODS);
	DUMP_VAR(CHANNELS_TX);
	DUMP_VAR(CHUNK_STEP);
	DUMP_VAR(CHUNKS_TX);
	DUMP_VAR(CUR_CHUNK_TX);
	DUMP_VAR(TX_CHANNEL_STEP);
	DUMP_VAR(TX_BUF_ADDR_LO);
	DUMP_VAR(TX_BUF_ADDR_HI);
	DUMP_VAR(TX_BASE_ADDR_LO);
	DUMP_VAR(TX_BASE_ADDR_HI);
}

static void dump_vdma_rx_vars(struct dante_snd *chip) {
	DUMP_VAR(PERIODS);
	DUMP_VAR(CUR_PERIODS);
	DUMP_VAR(CHANNELS_RX);
	DUMP_VAR(CHUNK_STEP);
	DUMP_VAR(CHUNKS_RX);
	DUMP_VAR(CUR_CHUNK_RX);
	DUMP_VAR(RX_CHANNEL_STEP);
	DUMP_VAR(RX_BUF_ADDR_LO);
	DUMP_VAR(RX_BUF_ADDR_HI);
	DUMP_VAR(RX_BASE_ADDR_LO);
	DUMP_VAR(RX_BASE_ADDR_HI);
}

static void dump_vdma_regs(struct dante_snd *chip) {
	uint32_t r;

	r = ioread32(chip->bar0 + VDMA_EVENT);
	printk(KERN_ALERT "VDMA_EVENT: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_EVENT_EN);
	printk(KERN_ALERT "VDMA_EVENT_EN: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_SYS_ADDR_LO);
	printk(KERN_ALERT "VDMA_SYS_ADDR_LO: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_SYS_ADDR_HI);
	printk(KERN_ALERT "VDMA_SYS_ADDR_HI: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_DPTR);
	printk(KERN_ALERT "VDMA_DPTR: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_XFER_CTL);
	printk(KERN_ALERT "VDMA_XFER_CTL: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_RA);
	printk(KERN_ALERT "VDMA_RA: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_RB);
	printk(KERN_ALERT "VDMA_RB: %08x\n", r);

	r = ioread32(chip->bar0 + VDMA_CSR);
	printk(KERN_ALERT "VDMA_CSR: %08x\n", r);

}

static void reset_device(struct dante_snd *chip) {
	/* Set external interrupt #1 to rising edge triggered */
	iowrite32(EXT_INT_1, chip->bar4 + INT_CTRL);

	/* For GPIO PIN 1, bypass the GPIO block, directly to external interrupt #1 */
	iowrite32(GPIO_PIN_1, chip->bar4 + GPIO_BYPASS_MODE);

	/* FlexDMA controller config */

	/* Output enable all event pins, useful for checking on a 'scope */
	iowrite32(-1U, chip->bar0 + VDMA_EVENT_EN);

	/* Set all (non-bypass) GPIO pins to be in output mode */
	iowrite32(0, chip->bar4 + GPIO_DIRECTION_MODE);
	/* Disable output in all pins */
	iowrite32(0, chip->bar4 + GPIO_OUTPUT_ENABLE);

	/* Clear existing events */
	iowrite32(-1U, chip->bar0 + VDMA_EVENT_CLR);

	/* Clear PCI interrupts */
	iowrite32(INT_STAT_RESET_MASK, chip->bar4 + INT_STAT);

	/* Disable all GPIO interrupts */
	iowrite32(GPIO_DISABLE_ALL, chip->bar4 + GPIO_INT_MASK_SET);

	/* Disable all interrupt source on all interrupts */
	iowrite32(0, chip->bar4 + INT_CFG0);
	iowrite32(0, chip->bar4 + INT_CFG1);
	iowrite32(0, chip->bar4 + INT_CFG2);
	iowrite32(0, chip->bar4 + INT_CFG3);
	iowrite32(0, chip->bar4 + INT_CFG4);
	iowrite32(0, chip->bar4 + INT_CFG5);
	iowrite32(0, chip->bar4 + INT_CFG6);
	iowrite32(0, chip->bar4 + INT_CFG7);

	halt_flex_dma(chip);
}

static void start_flex_dma(struct dante_snd *chip) {
	/* Load the start address */
	iowrite32(DMAP_TEXT_START, chip->bar0 + VDMA_DPTR);

	/* Clear existing event */
	iowrite32(DMAP_EV_START_BUFFER, chip->bar0 + VDMA_EVENT_CLR);

	/* Clear existing interrupt */
	iowrite32(INT1_MASK, chip->bar4 + INT_STAT);

	/* Enable external interrupt 1 for PCI interrupt */
	iowrite32(INT1_MASK, chip->bar4 + INT_CFG(chip->irq_offset));

	/* Start execution */
	iowrite32(1, chip->bar0 + VDMA_CSR);

	chip->engine_running = true;
}

static void halt_flex_dma(struct dante_snd *chip) {
	int i;
	uint32_t csr;

	csr = ioread32(chip->bar0 + VDMA_CSR);
	if (csr == 0x2) {
		printk(KERN_ALERT "WARNING: csr set to unexpected value 0x2. "
			   "Trying to clear.\n");
		iowrite32(0, chip->bar0 + VDMA_CSR);
	}

	if (csr == 0x3) {
		printk(KERN_ALERT "WARNING: csr set to unexpected value 0x3. "
			   "Trying to clear.\n");
		iowrite32(1, chip->bar0 + VDMA_CSR);
	}

	for (i = 0; i < 10; i++) {
		csr = ioread32(chip->bar0 + VDMA_CSR);
#if defined(DEBUG_HALT)
		printk(KERN_ALERT "Attempt to stop #%d\n", i);
		dump_vdma_regs(chip);
		dump_vdma_rx_vars(chip);
#endif

		if (csr == 0) {
			break;
		}

		if ((csr & 0x2) == 0) {
			iowrite32(0x2, chip->bar0 + VDMA_CSR);
		} else {
			/* Ideally we wouldn't call udelay, however this function can
			   be called from an interrupt context, so we don't have
			   a good altnernative */
			udelay(100);
		}
	}

	if (csr != 0) {
		dump_vdma_regs(chip);
		printk(KERN_ERR "Couldn't stop the DMA engine\n");
	} else {
		chip->engine_running = 0;
	}
}

/* Constructor */
static int snd_dante_create(struct snd_card *card) {
	struct snd_pcm *pcm;
	struct dante_snd *chip = card->private_data;
	int err;
	unsigned int dante_version;

	err = pci_enable_device(chip->pci);
	if (err != 0) {
		printk(KERN_ERR "pci_enable_device failed: (%d)", err);
		err = -ENXIO;
		goto error;
	}

	pci_set_master(chip->pci);

	err = pci_enable_msi(chip->pci);
	if (err != 0) {
		printk(KERN_ERR "unable to enable MSI. (%d)", err);
		err = -ENXIO;
		goto error;
	}

	err = pci_request_regions(chip->pci, "Dante");
	if (err < 0) {
		goto error;
	}

	/* Initialise hardware */
	chip->bar0_addr = pci_resource_start(chip->pci, 0);
	chip->bar0 = ioremap(chip->bar0_addr, pci_resource_len(chip->pci, 0));
	if (chip->bar0 == NULL) {
		printk(KERN_ERR "ioremap error\n");
		err = -ENXIO;
		goto error;
	}

	chip->bar4_addr = pci_resource_start(chip->pci, 4);
	chip->bar4 = ioremap(chip->bar4_addr, pci_resource_len(chip->pci, 4));
	if (chip->bar4 == NULL) {
		printk(KERN_ERR "ioremap error\n");
		err = -ENXIO;
		goto error;
	}

	/* We reset the device before enabling interrupts to ensure it won't
	   be generating any spurious interrupts */
	reset_device(chip);

	if (request_irq(chip->pci->irq, snd_dante_interrupt, 0, "DantePCIe", chip) < 0) {
		printk(KERN_ERR "request_irq error: %d\n", chip->pci->irq);
		err = -ENXIO;
		goto error;
	}
	chip->irq = chip->pci->irq;


	/*
	 * Workaround for Gennum bug
	 */
	{
		u16 control, msi_data;
		int pos;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
		struct msi_desc *entry;
#endif

		/* Here we update the Linux internal data structures,
		   so that if Linux rewrites the MSI control register
		   behind our backs it does so correctly */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0))
		list_for_each_entry(entry, &chip->pci->msi_list, list) {
		  if (entry->irq == chip->pci->irq) {
		      entry->msi_attrib.multiple = 2;
		  }
		}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
		list_for_each_entry(entry, &chip->pci->dev.msi_list, list) {
		  if (entry->irq == chip->pci->irq) {
		      entry->msi_attrib.multiple = 2;
		  }
		}
#endif
		/* Note: msi_list and msi_attrib removed in kernel 5.17+; Gennum fixup skipped */

		/* Update the MSI control register to convice it to work correctly */
		pos = pci_find_capability(chip->pci, PCI_CAP_ID_MSI);
		pci_read_config_word(chip->pci, msi_control_reg(pos), &control);
		control |= 2 << 4;
		pci_write_config_word(chip->pci, msi_control_reg(pos), control);

		/* Work out which IRQ offset we should be using based on the data
		   message allocated to the chip by Linux */
		pci_read_config_word(chip->pci, msi_data_reg(pos, is_64bit_address(control)), &msi_data);
		chip->irq_offset = msi_data & 0x3;
	}

	dante_version = ioread32(chip->bar0 + DANTE_VERSION);
	printk(KERN_INFO "Dante PCIe Soundcard Driver v%lu.%lu.%lu%s (0x%x)\n",
		DANTE_PCIE_VERSION_MAJOR,
		DANTE_PCIE_VERSION_MINOR,
		DANTE_PCIE_VERSION_BUGFIX,
		DANTE_PCIE_VERSION_TEXT,
		dante_version);

	chip->sample_rate = (dante_version & SRATE_MASK) >> SRATE_SHIFT;

	if (chip->sample_rate > (sizeof rates_defines / sizeof rates_defines[0])) {
		printk(KERN_ERR "invalid sample rate id: %d\n", chip->sample_rate);
		err = -ENXIO;
		goto error;
	}

	/* Ensure the program fits in the flex ram */
	if (sizeof dmap_data + sizeof dmap_text > pci_resource_len(chip->pci, 4)) {
		printk(KERN_ERR "flex ram program too large\n");
		err = -ENXIO;
		goto error;
	}

	download_flex_ram(chip, 0x0,  dmap_data, DMAP_DATA_SIZE);
	download_flex_ram(chip, DMAP_TEXT_START, dmap_text,
			  sizeof dmap_text / sizeof dmap_text[0]);

	/* Setup General datastructures */
	strcpy(card->driver, DRIVER_NAME);
	strcpy(card->shortname, CARD_NAME);
	sprintf(card->longname, LONG_CARD_NAME);

	/* Set up the PCM stream */
	err = snd_pcm_new(card, card->shortname, 0, NUM_PCM_PLAYBACK_STREAMS,
			  NUM_PCM_CAPTURE_STREAMS, &pcm);

	if (err != 0)  {
		goto error;
	}

	pcm->private_data = chip;
	chip->pcm = pcm;
	sprintf(pcm->name, "%s PCM", card->shortname);

	/* set operators */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_dante_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_dante_capture_ops);

	/* pre-allocation of buffers, need contiguous memory */
	snd_pcm_set_managed_buffer_all(
				pcm, SNDRV_DMA_TYPE_DEV, &(chip->pci->dev),
				DMA_SIZE, DMA_SIZE);

	return 0;
error:
	return err;
}

/* Destructor */
static void snd_dante_free(struct snd_card *card) {
	struct dante_snd *chip = card->private_data;
	/* Gennum fixup: Reset multiple back to 0 */
	{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
		struct msi_desc *entry;
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0))
		list_for_each_entry(entry, &chip->pci->msi_list, list) {
			if (entry->irq == chip->pci->irq) {
				entry->msi_attrib.multiple = 0;
			}
		}
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
		list_for_each_entry(entry, &chip->pci->dev.msi_list, list) {
			if (entry->irq == chip->pci->irq) {
				entry->msi_attrib.multiple = 0;
			}
		}
#endif
		/* Note: msi_list and msi_attrib removed in kernel 5.17+; Gennum fixup skipped */
	}

	reset_device(chip);

	if (chip->irq) {
		free_irq(chip->irq, chip);
		chip->irq = 0;
	}

	if (chip->bar0) {
		iounmap(chip->bar0);
		chip->bar0 = NULL;
	}

	if (chip->bar4) {
		iounmap(chip->bar4);
		chip->bar4 = NULL;
	}

	pci_disable_msi(chip->pci);

	/* Free dma allocated */
	pci_disable_device(chip->pci);

	/* disable the PCI device */
	if (chip->bar0_addr) {
		pci_release_regions(chip->pci);
		chip->bar0_addr = 0;
		chip->bar4_addr = 0;
	}
}


/* PCI LIFE CYCLE FUNCTIONS */
static int dante_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id) {
	struct snd_card *card = NULL;
	struct dante_snd *chip;
	int err;

	if (device_index >= SNDRV_CARDS) {
		err = -ENODEV;
		goto error;
	}

	if (!enable[device_index]) {
		device_index++;
		err = -ENOENT;
		goto error;
	}


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 15))
      err = snd_card_create(index[device_index], id[device_index], THIS_MODULE,
                              sizeof(struct dante_snd), &card);
#else
      err = snd_card_new(&pci_dev->dev, index[device_index], id[device_index], THIS_MODULE,
                              sizeof(struct dante_snd), &card);
#endif


	if (err < 0) {
		goto error;
	}

	/* Basic initialisation of the private data so that
	   free works correctly */
	chip = card->private_data;
	chip->pci = pci_dev;
	chip->card = card;

	/* Set the destructor that is called when the card is freed.
	 This ensures any PCI resources are correctly freed */
	card->private_free = snd_dante_free;

	/* Set-up and initialise the device */
	err = snd_dante_create(card);
	if (err < 0) {
		goto error;
	}

	/* Register the device with the ALSA system */
	err = snd_card_register(card);
	if (err != 0) {
		goto error;
	}

	/* We are succesful, so link up these final bits */
	pci_set_drvdata(pci_dev, card);
	snd_card_set_dev(card, &pci_dev->dev);

	device_index++;

	return 0;
error:
	snd_card_free(card);
	return err;
}

static void dante_remove(struct pci_dev *pci_dev) {
	/* Called when the PCI device is removed */
	snd_card_free(pci_get_drvdata(pci_dev));
	pci_set_drvdata(pci_dev, NULL);
}

/* MODULE LIFECYCLE FUNCTIONS */
static int dante_init(void) {
	/* Called at module init time */
	return pci_register_driver(&dante_pci_driver);
}

static void dante_exit(void) {
	/* Called at module exit time */
	pci_unregister_driver(&dante_pci_driver);
}
