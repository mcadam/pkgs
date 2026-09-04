/*
 * File     : dante-pcie.h
 * Created  : Thu Feb 10 18:18:50 EST 2011
 * Updated  : 2014-01-21
 * Author   : Aidan Williams
 * Synopsis : Dante PCIe Linux ALSA driver header definitions
 *
 * Portions of this software are copyright (c) 2004-2011 Audinate Pty Ltd and/or
 * its licensors. All intellectual property rights in such portions of the
 * Software and documentation are owned by Audinate and are protected by
 * United States copyright laws, other applicable copyright laws
 * and international treaty provisions. Audinate and its suppliers retain
 * all rights not expressly granted.
 *
 * Audinate Copyright Header Version 2
 */

#define DANTE_PCIE_VERSION_MAJOR	1UL
#define	DANTE_PCIE_VERSION_MINOR	2UL
#define	DANTE_PCIE_VERSION_BUGFIX	4UL
#define	DANTE_PCIE_VERSION_TEXT		"rc03"

#define EXTRACT_HI_WORD(x) (sizeof (x) > 4 ? (x) >> 32 & 0xffffffff : 0)
#define EXTRACT_LO_WORD(x) ((x) & 0xffffffff)

#define GPIO_PIN_1 (1 << 1)
#define GPIO_DISABLE_ALL 0xffff

#define EXT_INT_1 (1 << 1)

 /* Bits 0 and 1, read-only.
    Bits 2 and 3, write 1 to assert irq.
    Bits 4 - 14, write 1 to clear.
    Bit  15, read only 
    Bits 16 - 31, reserved. */
#define INT_STAT_RESET_MASK (0x7ff0)
#define INT1_MASK (1 << 5)

 /* GN412x System Registers */
#define PCI_SYS_CFG_SYSTEM 0x800
#define LB_CTL 0x804
#define CLK_CSR 0x808
#define PCI_BAR_CONFIG 0x80c
#define INT_CTRL 0x810
#define INT_STAT 0x814
#define PEX_ERROR_STAT 0x818
 /* 0x81c is unused */
#define INT_CFG(n) (0x820 + (4 * (n)))
#define INT_CFG0 0x820
#define INT_CFG1 0x824
#define INT_CFG2 0x828
#define INT_CFG3 0x82c
#define INT_CFG4 0x830
#define INT_CFG5 0x834
#define INT_CFG6 0x838
#define INT_CFG7 0x83c
#define PCI_TO_ACK_TIME 0x840
#define PEX_CDN_CFG1 0x844
#define PEX_CDN_CFG2 0x848
#define PHY_TEST_CONTROL 0x84c
#define PHY_CONTROL 0x850
#define CDN_LOCK 0x854

 /* GPIO Registers */
#define GPIO_BYPASS_MODE 0xa00
#define GPIO_DIRECTION_MODE 0xa04
#define GPIO_OUTPUT_ENABLE 0xa08
#define GPIO_OUTPUT_VALUE 0xa0c
#define GPIO_INPUT_VALUE 0xa10
#define GPIO_INT_MASK 0xa14
#define GPIO_INT_MASK_CLR 0xa18
#define GPIO_INT_MASK_SET 0xa1c
#define GPIO_INT_STATUS 0xa20
#define GPIO_INT_TYPE 0xa24
#define GPIO_INT_VALUE 0xa28
#define GPIO_INT_ON_ANY 0xa2c

#define SRATE_MASK 0xf0000000
#define SRATE_SHIFT 28

#define DANTE_VERSION 0x70
#define DANTE_SAMPLE_COUNT_LO 0x74
#define DANTE_SAMPLE_COUNT_HI 0x78

/* Registers in the FLEX DMA REG BASE area */
#define VDMA_EVENT_SET 0x8
#define VDMA_EVENT_CLR 0xc
#define VDMA_EVENT 0x10
#define VDMA_EVENT_EN 0x14
#define VDMA_SYS_ADDR_LO 0x18
#define VDMA_SYS_ADDR_HI 0x1c
#define VDMA_DPTR 0x20
#define VDMA_XFER_CTL 0x24
#define VDMA_RA 0x28
#define VDMA_RB 0x2c
#define VDMA_CSR 0x30

#define VDMA_DESCRIPTOR_RAM_BASE 0x4000

/* The trace macro is used to trace execution. It lets you easily
   see which lines of code are being called when. The prints can
   be easily seen using dmesg(1) */
#define TRACE() printk(KERN_ALERT "%s:%d\n", __func__, __LINE__)

#define DANTE_CLEAR 0
#define DANTE_SET 1
#define DANTE_GO 0
#define DANTE_STOP 1

#define DANTE_CHAN 128

#define DANTE_L2P 0
#define DANTE_P2L 1

 /* The documentation is wrong the bit is actually tracking busy state not idle */
#define PDM_BUSY _PDM_IDLE
#define LDM_BUSY _LDM_IDLE

/* Define the names for the card here */
#define CARD_NAME "DantePCIe"
#define LONG_CARD_NAME "Dante PCIe"
#define DRIVER_NAME "DantePCIe"

/* Set-up the PCI vendor/device ID. You can also do this directly in
the dante_ids array later if there is more than one possible
vendor/device id. */

#define VENDOR_ID 0x1a39
#define DEVICE_ID 0x0004

#define NUM_PCM_CAPTURE_STREAMS 1
#define NUM_PCM_PLAYBACK_STREAMS 1

#define NUM_CHANNELS 128

#define PERIODS_MIN 2
#define PERIODS_MAX 512

#define MAX_CHUNKS_PER_IRQ 128

#define SAMPLE_SIZE sizeof(uint32_t)
#define FRAMES_PER_CHUNK 16
#define CHUNK_SIZE (FRAMES_PER_CHUNK * SAMPLE_SIZE)

/* Define the amount of DMA memory required. */
#define DMA_SIZE (PERIODS_MAX * NUM_CHANNELS * CHUNK_SIZE)

#define DMAP_EV(x) (1 << (x))
#define TX_ENABLE_EVENT 2
#define RX_ENABLE_EVENT 3
#define DMAP_EV_CHUNK_START DMAP_EV(0) /* Set externally by FPGA */
#define DMAP_EV_START_BUFFER DMAP_EV(1) /* Signal to driver, cleared by driver */
#define DMAP_EV_TX_ENABLE DMAP_EV(TX_ENABLE_EVENT)
#define DMAP_EV_RX_ENABLE DMAP_EV(RX_ENABLE_EVENT)

#define DMAP_DATA_SIZE 0x30
#define DMAP_TEXT_START DMAP_DATA_SIZE

/* Constants used in the DMA program */
#define DMAP_VAR_ZERO 0x0
#define DMAP_VAR_PLUS1 0x1
#define DMAP_VAR_MINUS1 0x2
#define DMAP_VAR_XFER_CTL_TX 0x3
#define DMAP_VAR_XFER_CTL_RX 0x4
#define DMAP_VAR_CHUNK_STEP 0x5

#define DMAP_VAR_CHANNELS_TX 0x6
#define DMAP_VAR_CHANNELS_RX 0x7
#define DMAP_VAR_PERIODS 0x8
#define DMAP_VAR_CUR_PERIODS 0x9

/* Config variables: host driver code set these before starting
   the DMA program */
#define DMAP_VAR_CHUNKS_TX 0x10 /* chunks per loop */
#define DMAP_VAR_CHUNKS_RX 0x11 /* chunks per loop */

#define DMAP_VAR_TX_CHANNEL_STEP 0x12 /* address step for inner loop in tx/rx buffers */
#define DMAP_VAR_RX_CHANNEL_STEP 0x13 /* address step for inner loop in tx/rx buffers */

#define DMAP_VAR_TX_BUF_ADDR_LO 0x14 /* buffer address for TX (outbound) buffer */
#define DMAP_VAR_TX_BUF_ADDR_HI 0x15
#define DMAP_VAR_RX_BUF_ADDR_LO 0x16 /* buffer address for RX (inbound) buffer */
#define DMAP_VAR_RX_BUF_ADDR_HI 0x17

/* Status variables */
#define DMAP_VAR_CUR_CHUNK_TX 0x18 /* chunk index for tx */
#define DMAP_VAR_CUR_CHUNK_RX 0x19 /* chunk index for rx*/

#define DMAP_VAR_TX_BASE_ADDR_LO 0x1a /* base address for TX (outbound) buffer */
#define DMAP_VAR_TX_BASE_ADDR_HI 0x1b
#define DMAP_VAR_RX_BASE_ADDR_LO 0x1c /* base address for RX (inbound) buffer */
#define DMAP_VAR_RX_BASE_ADDR_HI 0x1d

#define DMAP_VAR_TX_ADDR_LO 0x1e /* current address in TX (outbound) buffer */
#define DMAP_VAR_TX_ADDR_HI 0x1f
#define DMAP_VAR_RX_ADDR_LO 0x20 /* current address in RX (inbound) buffer */
#define DMAP_VAR_RX_ADDR_HI 0x21
/* Must be less than DMA_DATA_SIZE (0x30) -- increment DMAP_DATA_SIZE if neeed */

#define WRITE_DMAP(val, chip, reg) iowrite32((val), (chip)->bar0 + VDMA_DESCRIPTOR_RAM_BASE + sizeof (uint32_t) * (reg))
#define READ_DMAP(chip, reg) ioread32((chip)->bar0 + VDMA_DESCRIPTOR_RAM_BASE + sizeof (uint32_t) * (reg))

/* Taken from drivers/pci/msi.h to enable easy manipulation of MSI registers */
#define msi_control_reg(base)           (base + PCI_MSI_FLAGS)
#define msi_lower_address_reg(base)     (base + PCI_MSI_ADDRESS_LO)
#define msi_upper_address_reg(base)     (base + PCI_MSI_ADDRESS_HI)
#define msi_data_reg(base, is64bit)					\
         (base + ((is64bit == 1) ? PCI_MSI_DATA_64 : PCI_MSI_DATA_32))
#define msi_mask_reg(base, is64bit)					\
         (base + ((is64bit == 1) ? PCI_MSI_MASK_64 : PCI_MSI_MASK_32))
#define is_64bit_address(control)       (!!(control & PCI_MSI_FLAGS_64BIT))
#define is_mask_bit_support(control)    (!!(control & PCI_MSI_FLAGS_MASKBIT))
