#ifndef __ARCH_ORION5X_MPP_H
#define __ARCH_ORION5X_MPP_H

enum orion5x_mpp_type {
	/*
	 * This MPP is unused.
	 */
	MPP_UNUSED,

	/*
	 * This MPP pin is used as a generic GPIO pin.  Valid for
	 * MPPs 0-15 and device bus data pins 16-31.  On 5182, also
	 * valid for MPPs 16-19.
	 */
	MPP_GPIO,

	/*
	 * This MPP is used as PCIe_RST_OUTn pin.  Valid for
	 * MPP 0 only.
	 */
	MPP_PCIE_RST_OUTn,

	/*
	 * This MPP is used as PCI arbiter pin (REQn/GNTn).
	 * Valid for MPPs 0-7 only.
	 */
	MPP_PCI_ARB,

	/*
	 * This MPP is used as PCI_PMEn pin.  Valid for MPP 2 only.
	 */
	MPP_PCI_PMEn,

	/*
	 * This MPP is used as GigE half-duplex (COL, CRS) or GMII
	 * (RXERR, CRS, TXERR, TXD[7:4], RXD[7:4]) pin.  Valid for
	 * MPPs 8-19 only.
	 */
	MPP_GIGE,

	/*
	 * This MPP is used as NAND REn/WEn pin.  Valid for MPPs
	 * 4-7 and 12-17 only, and only on the 5181l/5182/5281.
	 */
	MPP_NAND,

	/*
	 * This MPP is used as a PCI clock output pin.  Valid for
	 * MPPs 6-7 only, and only on the 5181l.
	 */
	MPP_PCI_CLK,

	/*
	 * This MPP is used as a SATA presence/activity LED.
	 * Valid for MPPs 4-7 and 12-15 only, and only on the 5182.
	 */
	MPP_SATA_LED,

	/*
	 * This MPP is used as UART1 RXD/TXD/CTSn/RTSn pin.
	 * Valid for MPPs 16-19 only.
	 */
	MPP_UART,
};

struct orion5x_mpp_mode {
	int			mpp;
	enum orion5x_mpp_type	type;
};

void orion5x_mpp_conf(struct orion5x_mpp_mode *mode);

#define MPP(_num, _sel, _in, _out, _F5181l, _F5182, _F5281) ( \
	/* MPP number */		((_num) & 0xff) | \
	/* MPP select value */		(((_sel) & 0xf) << 8) | \
	/* may be input signal */	((!!(_in)) << 12) | \
	/* may be output signal */	((!!(_out)) << 13) | \
	/* available on F5181l */	((!!(_F5181l)) << 14) | \
	/* available on F5182 */	((!!(_F5182)) << 15) | \
	/* available on F5281 */	((!!(_F5281)) << 16))

				/* num sel  i  o  5181 5182 5281 */

#define MPP_F5181_MASK		MPP(0,  0x0, 0, 0, 1,   0,   0)
#define MPP_F5182_MASK		MPP(0,  0x0, 0, 0, 0,   1,   0)
#define MPP_F5281_MASK		MPP(0,  0x0, 0, 0, 0,   0,   1)

#define MPP0_UNUSED	        MPP(0,  0x3, 0, 0, 1,   1,   1)
#define MPP0_GPIO		MPP(0,  0x3, 1, 1, 1,   1,   1)
#define MPP0_PCIE_RST_OUTn	MPP(0,  0x0, 0, 0, 1,   1,   1)
#define MPP0_PCI_ARB            MPP(0,  0x2, 0, 0, 1,   1,   1)

#define MPP1_UNUSED		MPP(1,  0x0, 0, 0, 1,   1,   1)
#define MPP1_GPIO		MPP(1,  0x0, 1, 1, 1,   1,   1)
#define MPP1_PCI_ARB            MPP(1,  0x2, 0, 0, 1,   1,   1)

#define MPP2_UNUSED		MPP(2,  0x0, 0, 0, 1,   1,   1)
#define MPP2_GPIO		MPP(2,  0x0, 1, 1, 1,   1,   1)
#define MPP2_PCI_ARB            MPP(2,  0x2, 0, 0, 1,   1,   1)
#define MPP2_PCI_PMEn           MPP(2,  0x3, 0, 0, 1,   1,   1)

#define MPP3_UNUSED		MPP(3,  0x0, 0, 0, 1,   1,   1)
#define MPP3_GPIO		MPP(3,  0x0, 1, 1, 1,   1,   1)
#define MPP3_PCI_ARB            MPP(3,  0x2, 0, 0, 1,   1,   1)

#define MPP4_UNUSED		MPP(4,  0x0, 0, 0, 1,   1,   1)
#define MPP4_GPIO		MPP(4,  0x0, 1, 1, 1,   1,   1)
#define MPP4_PCI_ARB            MPP(4,  0x2, 0, 0, 1,   1,   1)
#define MPP4_NAND               MPP(4,  0x4, 0, 0, 0,   1,   1)
#define MPP4_SATA_LED           MPP(4,  0x5, 0, 0, 0,   1,   0)

#define MPP5_UNUSED		MPP(5,  0x0, 0, 0, 1,   1,   1)
#define MPP5_GPIO		MPP(5,  0x0, 1, 1, 1,   1,   1)
#define MPP5_PCI_ARB            MPP(5,  0x2, 0, 0, 1,   1,   1)
#define MPP5_NAND               MPP(5,  0x4, 0, 0, 0,   1,   1)
#define MPP5_SATA_LED           MPP(5,  0x5, 0, 0, 0,   1,   0)

#define MPP6_UNUSED		MPP(6,  0x0, 0, 0, 1,   1,   1)
#define MPP6_GPIO		MPP(6,  0x0, 1, 1, 1,   1,   1)
#define MPP6_PCI_ARB            MPP(6,  0x2, 0, 0, 1,   1,   1)
#define MPP6_NAND               MPP(6,  0x4, 0, 0, 0,   1,   1)
#define MPP6_PCI_CLK            MPP(6,  0x5, 0, 0, 1,   0,   0)
#define MPP6_SATA_LED           MPP(6,  0x5, 0, 0, 0,   1,   0)

#define MPP7_UNUSED		MPP(7,  0x0, 0, 0, 1,   1,   1)
#define MPP7_GPIO		MPP(7,  0x0, 1, 1, 1,   1,   1)
#define MPP7_PCI_ARB            MPP(7,  0x2, 0, 0, 1,   1,   1)
#define MPP7_NAND               MPP(7,  0x4, 0, 0, 0,   1,   1)
#define MPP7_PCI_CLK            MPP(7,  0x5, 0, 0, 1,   0,   0)
#define MPP7_SATA_LED           MPP(7,  0x5, 0, 0, 0,   1,   0)

#define MPP8_UNUSED		MPP(8,  0x0, 0, 0, 1,   1,   1)
#define MPP8_GPIO		MPP(8,  0x0, 1, 1, 1,   1,   1)
#define MPP8_GIGE               MPP(8,  0x1, 0, 0, 1,   1,   1)

#define MPP9_UNUSED		MPP(9,  0x0, 0, 0, 1,   1,   1)
#define MPP9_GPIO		MPP(9,  0x0, 1, 1, 1,   1,   1)
#define MPP9_GIGE               MPP(9,  0x1, 0, 0, 1,   1,   1)

#define MPP10_UNUSED		MPP(10, 0x0, 0, 0, 1,   1,   1)
#define MPP10_GPIO		MPP(10, 0x0, 1, 1, 1,   1,   1)
#define MPP10_GIGE              MPP(10, 0x1, 0, 0, 1,   1,   1)

#define MPP11_UNUSED		MPP(11, 0x0, 0, 0, 1,   1,   1)
#define MPP11_GPIO		MPP(11, 0x0, 1, 1, 1,   1,   1)
#define MPP11_GIGE              MPP(11, 0x1, 0, 0, 1,   1,   1)

#define MPP12_UNUSED		MPP(12, 0x0, 0, 0, 1,   1,   1)
#define MPP12_GPIO		MPP(12, 0x0, 1, 1, 1,   1,   1)
#define MPP12_GIGE              MPP(12, 0x1, 0, 0, 1,   1,   1)
#define MPP12_NAND              MPP(12, 0x4, 0, 0, 0,   1,   1)
#define MPP12_SATA_LED          MPP(12, 0x5, 0, 0, 0,   1,   0)

#define MPP13_UNUSED		MPP(13, 0x0, 0, 0, 1,   1,   1)
#define MPP13_GPIO		MPP(13, 0x0, 1, 1, 1,   1,   1)
#define MPP13_GIGE              MPP(13, 0x1, 0, 0, 1,   1,   1)
#define MPP13_NAND              MPP(13, 0x4, 0, 0, 0,   1,   1)
#define MPP13_SATA_LED          MPP(13, 0x5, 0, 0, 0,   1,   0)

#define MPP14_UNUSED		MPP(14, 0x0, 0, 0, 1,   1,   1)
#define MPP14_GPIO		MPP(14, 0x0, 1, 1, 1,   1,   1)
#define MPP14_GIGE              MPP(14, 0x1, 0, 0, 1,   1,   1)
#define MPP14_NAND              MPP(14, 0x4, 0, 0, 0,   1,   1)
#define MPP14_SATA_LED          MPP(14, 0x5, 0, 0, 0,   1,   0)

#define MPP15_UNUSED		MPP(15, 0x0, 0, 0, 1,   1,   1)
#define MPP15_GPIO		MPP(15, 0x0, 1, 1, 1,   1,   1)
#define MPP15_GIGE              MPP(15, 0x1, 0, 0, 1,   1,   1)
#define MPP15_NAND              MPP(15, 0x4, 0, 0, 0,   1,   1)
#define MPP15_SATA_LED          MPP(15, 0x5, 0, 0, 0,   1,   0)

#define MPP16_UNUSED		MPP(16, 0x0, 0, 0, 1,   1,   1)
#define MPP16_GPIO		MPP(16, 0x5, 1, 1, 0,   1,   0)
#define MPP16_GIGE              MPP(16, 0x1, 0, 0, 1,   1,   1)
#define MPP16_NAND              MPP(16, 0x4, 0, 0, 0,   1,   1)
#define MPP16_UART              MPP(16, 0x0, 0, 0, 0,   1,   1)

#define MPP17_UNUSED		MPP(17, 0x0, 0, 0, 1,   1,   1)
#define MPP17_GPIO		MPP(17, 0x5, 1, 1, 0,   1,   0)
#define MPP17_GIGE              MPP(17, 0x1, 0, 0, 1,   1,   1)
#define MPP17_NAND              MPP(17, 0x4, 0, 0, 0,   1,   1)
#define MPP17_UART              MPP(17, 0x0, 0, 0, 0,   1,   1)

#define MPP18_UNUSED		MPP(18, 0x0, 0, 0, 1,   1,   1)
#define MPP18_GPIO		MPP(18, 0x5, 1, 1, 0,   1,   0)
#define MPP18_GIGE              MPP(18, 0x1, 0, 0, 1,   1,   1)
#define MPP18_UART              MPP(18, 0x0, 0, 0, 0,   1,   1)

#define MPP19_UNUSED		MPP(19, 0x0, 0, 0, 1,   1,   1)
#define MPP19_GPIO		MPP(19, 0x5, 1, 1, 0,   1,   0)
#define MPP19_GIGE              MPP(19, 0x1, 0, 0, 1,   1,   1)
#define MPP19_UART              MPP(19, 0x0, 0, 0, 0,   1,   1)

#define MPP_MAX			19

void orion5x_mpp_conf(unsigned int *mpp_list);

#endif
