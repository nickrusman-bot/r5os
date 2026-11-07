/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG <wei_wang@realsil.com.cn>
 *
 * The addresses below are the ones this port uses from Linux v6.12
 * include/linux/rtsx_pci.h. That file is GPL-2.0-or-later.
 * drivers/misc/cardreader/rts5228.h is GPL-2.0-only and is not copied.
 */


#ifndef RTSX_5228_REGS_H
#define RTSX_5228_REGS_H


#define RTSX_HCBAR			0x00
#define RTSX_HCBCTLR			0x04
#define   STOP_CMD			(0x01 << 28)

#define RTSX_HDBAR			0x08
#define RTSX_HDBCTLR			0x0C
#define   STOP_DMA			(0x01 << 28)

#define RTSX_HAIMR			0x10
#define   HAIMR_TRANS_START		(0x01 << 31)
#define   HAIMR_WRITE			(0x01 << 30)
#define   HAIMR_READ_START		HAIMR_TRANS_START
#define   HAIMR_WRITE_START		(HAIMR_TRANS_START | HAIMR_WRITE)
#define   HAIMR_TRANS_END		HAIMR_TRANS_START

#define RTSX_BIPR			0x14
#define   TRANS_OK_INT			(1 << 29)
#define   TRANS_FAIL_INT		(1 << 28)
#define   SD_INT			(1 << 25)
#define   SD_EXIST			(1 << 16)
#define   SD_OC_INT			(1 << 22)

#define RTSX_BIER			0x18
#define   TRANS_OK_INT_EN		(1 << 29)
#define   TRANS_FAIL_INT_EN		(1 << 28)
#define   SD_INT_EN			(1 << 25)
#define   SD_OC_INT_EN			(1 << 22)

#define MAX_RW_REG_CNT			1024

#define FPDCTL				0xFC00
#define   SSC_POWER_DOWN		0x01
#define CLK_DIV				0xFC03
#define SSC_CTL1			0xFC11
#define   SSC_RSTB			0x80
#define   SSC_8X_EN			0x40
#define   SSC_SEL_4M			0x10
#define SSC_CTL2			0xFC12
#define RCCTL				0xFC14
#define OLT_LED_CTL			0xFC1E
#define   LED_SHINE_MASK		0x08
#define   LED_SHINE_EN			0x08
#define   LED_SHINE_DISABLE		0x00
#define GPIO_CTL			0xFC1F

#define CARD_PULL_CTL2			0xFD61
#define CARD_PULL_CTL3			0xFD62
#define CARD_OE				0xFD55
#define   SD_OUTPUT_EN			0x04
#define SD30_CLK_DRIVE_SEL		0xFD5A
#define SD30_CMD_DRIVE_SEL		0xFD5E
#define SD30_DAT_DRIVE_SEL		0xFD5F
#define CARD_DRIVE_SEL			0xFD53
#define CARD_CLK_EN			0xFD69

#define SD_CFG1				0xFDA0
#define   SD_CLK_DIVIDE_0		0x00
#define   SD_CLK_DIVIDE_128		0x80
#define   SD_BUS_WIDTH_1BIT		0x00
#define   SD_20_MODE			0x00

#define PHYRWCTL			0xFE3C
#define PHYDATA0			0xFE3D
#define PHYDATA1			0xFE3E
#define PHYADDR				0xFE3F

#define IRQSTAT0			0xFE21
#define   LINK_RDY_INT			0x20
#define PCLK_CTL			0xFE55
#define   PCLK_MODE_SEL			0x20
#define ASPM_FORCE_CTL			0xFE57
#define PM_CLK_FORCE_CTL		0xFE58
#define   CLK_PM_EN			0x01
#define FUNC_FORCE_CTL			0xFE59
#define   FUNC_FORCE_UPME_XMT_DBG	0x02
#define CHANGE_LINK_STATE		0xFE5B
#define PERST_GLITCH_WIDTH		0xFE5C
#define HOST_SLEEP_STATE		0xFE60
#define PM_EVENT_DEBUG			0xFE71
#define   PME_DEBUG_0			0x08
#define NFTS_TX_CTRL			0xFE72
#define PWD_SUSPEND_EN			0xFE76
#define L1SUB_CONFIG1			0xFE8D
#define   AUX_CLK_ACTIVE_SEL_MASK	0x01
#define   MAC_CKSW_DONE			0x00
#define L1SUB_CONFIG3			0xFE8F
#define PETXCFG				0xFF03
#define   FORCE_CLKREQ_DELINK_MASK	0x80
#define   FORCE_CLKREQ_LOW		0x80
#define   FORCE_CLKREQ_HIGH		0x00

#define PHY_REG_07			0x07
#define PHY_REG_07_VALUE		0x8F40

#define OUTPUT_3V3			0
#define CFG_DRIVER_TYPE_B		0x03
#define MS_DRIVE_8mA			(0x01 << 6)
#define GPIO_DRIVE_8mA			0x01
#define RTSX_CARD_DRIVE_DEFAULT		(MS_DRIVE_8mA | GPIO_DRIVE_8mA)


#endif
