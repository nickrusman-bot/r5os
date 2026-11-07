/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2018-2019 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Ricky WU <ricky_wu@realtek.com>
 *   Rui FENG <rui_feng@realsil.com.cn>
 *   Wei WANG <wei_wang@realsil.com.cn>
 *
 * The sequences are the RTS5228 parts of Linux v6.12
 * drivers/misc/cardreader/rts5228.c and rtsx_pcr.c.
 * Register addresses come from include/linux/rtsx_pci.h, which is
 * GPL-2.0-or-later. rts5228.h is GPL-2.0-only and is not copied, so the
 * LDO, DV3318, PME and 5228 SSC-depth writes from that header are not here.
 * Until those writes exist, rtsx_5228_support() returns 0 and this file
 * does not claim 10ec:5228.
 */


#include "rtsx_5228_regs.h"

#include <stdio.h>

#include <KernelExport.h>
#include <bus/PCI.h>


#define TRACE_ALWAYS(x...) dprintf("\33[33mrtsx:\33[0m " x)


struct rtsx_5228 {
	volatile uint8*			registers;
	pci_device_module_info*	pci;
	pci_device*				device;
	sem_id					scan;
	uint32					irq;
	bool					msi;
	bool					card;
	int32					hotplug;
};


static uint32
read4(rtsx_5228* bus, uint32 reg)
{
	return *(volatile uint32*)(bus->registers + reg);
}


static void
write4(rtsx_5228* bus, uint32 reg, uint32 value)
{
	*(volatile uint32*)(bus->registers + reg) = value;
}


static status_t
write_register(rtsx_5228* bus, uint16 address, uint8 mask, uint8 data)
{
	uint32 value = HAIMR_WRITE_START;
	value |= (uint32)(address & 0x3fff) << 16;
	value |= (uint32)mask << 8;
	value |= data;
	write4(bus, RTSX_HAIMR, value);

	for (int i = 0; i < MAX_RW_REG_CNT; i++) {
		value = read4(bus, RTSX_HAIMR);
		if ((value & HAIMR_TRANS_END) == 0) {
			if (data != (uint8)value)
				return B_IO_ERROR;
			return B_OK;
		}
	}
	return B_TIMED_OUT;
}


static status_t
read_register(rtsx_5228* bus, uint16 address, uint8* data)
{
	uint32 value = HAIMR_READ_START;
	value |= (uint32)(address & 0x3fff) << 16;
	write4(bus, RTSX_HAIMR, value);

	for (int i = 0; i < MAX_RW_REG_CNT; i++) {
		value = read4(bus, RTSX_HAIMR);
		if ((value & HAIMR_TRANS_END) == 0) {
			*data = (uint8)value;
			return B_OK;
		}
	}
	return B_TIMED_OUT;
}


static status_t
write_phy(rtsx_5228* bus, uint8 address, uint16 value)
{
	write_register(bus, PHYDATA0, 0xff, (uint8)value);
	write_register(bus, PHYDATA1, 0xff, (uint8)(value >> 8));
	write_register(bus, PHYADDR, 0xff, address);
	write_register(bus, PHYRWCTL, 0xff, 0x81);

	for (int i = 0; i < 100000; i++) {
		uint8 tmp = 0;
		status_t status = read_register(bus, PHYRWCTL, &tmp);
		if (status != B_OK)
			return status;
		if ((tmp & 0x80) == 0)
			return B_OK;
	}
	return B_TIMED_OUT;
}


/* Driving rows from rts5228.c. Index is the vendor drive select. */
static const uint8 kDrive3v3[4][3] = {
	{ 0x13, 0x13, 0x13 },
	{ 0x96, 0x96, 0x96 },
	{ 0x7f, 0x7f, 0x7f },
	{ 0x96, 0x96, 0x96 },
};


static void
fill_driving(rtsx_5228* bus)
{
	const uint8* row = kDrive3v3[CFG_DRIVER_TYPE_B];
	write_register(bus, SD30_CLK_DRIVE_SEL, 0xff, row[0]);
	write_register(bus, SD30_CMD_DRIVE_SEL, 0xff, row[1]);
	write_register(bus, SD30_DAT_DRIVE_SEL, 0xff, row[2]);
}


static int32
hotplug_thread(void* data)
{
	rtsx_5228* bus = (rtsx_5228*)data;
	snooze(200 * 1000);
	if (bus->card && bus->scan >= 0)
		release_sem(bus->scan);
	else if (!bus->card) {
		TRACE_ALWAYS("5228 card removed; mmc_bus does not unpublish yet\n");
	}
	atomic_and(&bus->hotplug, 0);
	return 0;
}


static int32
rtsx_5228_interrupt(void* data)
{
	rtsx_5228* bus = (rtsx_5228*)data;
	uint32 status = read4(bus, RTSX_BIPR);
	write4(bus, RTSX_BIPR, status);

	uint32 enabled = TRANS_OK_INT_EN | TRANS_FAIL_INT_EN | SD_INT_EN
		| SD_OC_INT_EN;
	if ((status & enabled) == 0)
		return B_UNHANDLED_INTERRUPT;
	if (status == 0xffffffff)
		return B_HANDLED_INTERRUPT;

	if ((status & SD_OC_INT) != 0) {
		/* The LDO clear sequence is only in rts5228.h. */
		TRACE_ALWAYS("5228 overcurrent\n");
	}

	if ((status & SD_INT) != 0) {
		bus->card = (status & SD_EXIST) != 0;
		if (atomic_or(&bus->hotplug, 1) == 0) {
			thread_id thread = spawn_kernel_thread(hotplug_thread,
				"rtsx 5228 hotplug", B_NORMAL_PRIORITY, bus);
			if (thread >= 0)
				resume_thread(thread);
			else
				atomic_and(&bus->hotplug, 0);
		}
	}
	return B_HANDLED_INTERRUPT;
}


static status_t
rtsx_5228_init(rtsx_5228* bus)
{
	/*
	 * Linux writes RTS5228_LDO1_CFG1 here. That address is only in
	 * rts5228.h, so it is not written.
	 */
	write4(bus, RTSX_HCBAR, 0);

	write4(bus, RTSX_BIER, TRANS_OK_INT_EN | TRANS_FAIL_INT_EN | SD_INT_EN
		| SD_OC_INT_EN);

	status_t status = write_register(bus, FPDCTL, SSC_POWER_DOWN, 0);
	if (status != B_OK)
		return status;
	snooze(200);

	status = write_phy(bus, PHY_REG_07, PHY_REG_07_VALUE);
	if (status != B_OK)
		return status;

	write_register(bus, CLK_DIV, 0x07, 0x07);
	write_register(bus, HOST_SLEEP_STATE, 0x03, 0x00);
	write_register(bus, CARD_CLK_EN, 0x1e, 0);
	write_register(bus, CHANGE_LINK_STATE, 0x0a, 0);
	write_register(bus, CARD_DRIVE_SEL, 0xff, RTSX_CARD_DRIVE_DEFAULT);
	write_register(bus, SSC_CTL1, 0xff, SSC_8X_EN | SSC_SEL_4M);
	/*
	 * Linux then writes RTS5228_SSC_DEPTH_2M to SSC_CTL2. The generic
	 * depth in rtsx_pci.h is a different value, and the 5228 value is
	 * only in rts5228.h, so SSC_CTL2 is left alone.
	 */
	write_register(bus, CHANGE_LINK_STATE, 0x16, 0x10);
	write_register(bus, IRQSTAT0, LINK_RDY_INT, LINK_RDY_INT);
	write_register(bus, PERST_GLITCH_WIDTH, 0xff, 0x80);
	write_register(bus, RCCTL, 0x01, 0x00);
	write_register(bus, NFTS_TX_CTRL, 0x02, 0);
	write_register(bus, PM_CLK_FORCE_CTL, 1, 1);

	write_register(bus, L1SUB_CONFIG1, AUX_CLK_ACTIVE_SEL_MASK, MAC_CKSW_DONE);
	write_register(bus, L1SUB_CONFIG3, 0xff, 0);
	write_register(bus, FUNC_FORCE_CTL, FUNC_FORCE_UPME_XMT_DBG,
		FUNC_FORCE_UPME_XMT_DBG);
	write_register(bus, PCLK_CTL, PCLK_MODE_SEL, PCLK_MODE_SEL);
	write_register(bus, PM_EVENT_DEBUG, PME_DEBUG_0, PME_DEBUG_0);
	write_register(bus, PM_CLK_FORCE_CTL, CLK_PM_EN, CLK_PM_EN);
	write_register(bus, OLT_LED_CTL, 0x0f, 0x02);
	fill_driving(bus);
	write_register(bus, PETXCFG, 0x30, 0x00);
	write_register(bus, PETXCFG, FORCE_CLKREQ_DELINK_MASK, FORCE_CLKREQ_HIGH);
	write_register(bus, PWD_SUSPEND_EN, 0xff, 0xfb);
	write_register(bus, CARD_PULL_CTL2, 0xff, 0xaa);
	write_register(bus, CARD_PULL_CTL3, 0xff, 0xe9);

	bus->irq = 0;
	bus->msi = false;
	if (bus->pci != NULL && bus->pci->get_msi_count(bus->device) >= 1) {
		uint32 vector = 0;
		if (bus->pci->configure_msi(bus->device, 1, &vector) == B_OK
			&& bus->pci->enable_msi(bus->device) == B_OK) {
			bus->msi = true;
			bus->irq = vector;
			TRACE_ALWAYS("5228 using MSI vector %" B_PRIu32 "\n", vector);
		}
	}
	if (!bus->msi)
		TRACE_ALWAYS("5228 MSI not available, legacy line %" B_PRIu32 "\n",
			bus->irq);

	if (bus->irq != 0) {
		status = install_io_interrupt_handler(bus->irq,
			rtsx_5228_interrupt, bus, 0);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


static status_t (*const kRtsx5228Init)(rtsx_5228*) = rtsx_5228_init;


extern "C" float
rtsx_5228_support(uint16 vendor, uint16 device)
{
	(void)vendor;
	(void)device;
	(void)kRtsx5228Init;
	TRACE_ALWAYS("rtsx: not claiming 10ec:5228\n");
	return 0.0f;
}
