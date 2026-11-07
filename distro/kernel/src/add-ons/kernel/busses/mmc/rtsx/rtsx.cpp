/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2026 R5OS Contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


/*
 * Realtek RTS52xx / RTL84xx card reader, as a Haiku MMC bus.
 *
 * The command engine, the clock, and the power sequence are the OpenBSD
 * rtsx(4) driver (sys/dev/ic/rtsx.c,v 1.22 and sys/dev/pci/rtsx_pci.c,v 1.16).
 * RTS5228 is a separate flag. OpenBSD has no init sequence for that chip.
 * Nothing in this file is taken from Linux drivers/misc/cardreader/rts5228.c
 * or rts5228.h. The GPL init lives in rtsx_5228.cpp. This file calls
 * rtsx_5228_support() when that file is linked. The call returns 0 while
 * the GPL-2.0-only register header is not copied, so 10ec:5228 is not claimed.
 */


#include "rtsxreg.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <KernelExport.h>
#include <bus/PCI.h>

#include "IOSchedulerSimple.h"
#include "mmc.h"


static int32 rtsx_interrupt(void* data);


extern "C" float rtsx_5228_support(uint16 vendor, uint16 device)
	__attribute__((weak));


//#define TRACE_RTSX
#ifdef TRACE_RTSX
#	define TRACE(x...) dprintf("\33[33mrtsx:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...) dprintf("\33[33mrtsx:\33[0m " x)
#define ERROR(x...) dprintf("\33[33mrtsx:\33[0m " x)


#define RTSX_DEVICE_MODULE_NAME "busses/mmc/rtsx/driver_v1"
#define RTSX_MMC_BUS_MODULE_NAME "busses/mmc/rtsx/device/v1"

#define RTSX_PCI_CONFIG 0x1c

/* OpenBSD rtsxvar.h chip flags, plus RTS5228. */
#define RTSX_F_CARD_PRESENT 0x01
#define RTSX_F_SDIO_SUPPORT 0x02
#define RTSX_F_5209 0x04
#define RTSX_F_5229 0x08
#define RTSX_F_5229_TYPE_C 0x10
#define RTSX_F_525A 0x20
#define RTSX_F_5228 0x40

#define RTSX_HOSTCMD_MAX 256
#define RTSX_CMD_BYTES (sizeof(uint32) * RTSX_HOSTCMD_MAX)

/* SD OCR bits named by OpenBSD rtsxreg.h. Values are the SD spec. */
#define RTSX_OCR_3_0V_3_1V (1U << 18)
#define RTSX_OCR_3_1V_3_2V (1U << 19)
#define RTSX_OCR_3_2V_3_3V (1U << 20)
#define RTSX_OCR_3_3V_3_4V (1U << 21)
#define RTSX_OCR_33 (RTSX_OCR_3_0V_3_1V | RTSX_OCR_3_1V_3_2V \
	| RTSX_OCR_3_2V_3_3V | RTSX_OCR_3_3V_3_4V)

enum {
	RTSX_CLK_OFF = 0,
	RTSX_CLK_400KHZ = 400,
	RTSX_CLK_25MHZ = 25000,
	RTSX_CLK_50MHZ = 50000,
};


struct rtsx_device_info {
	uint16 deviceId;
	uint32 flags;
	uint8 bar;
	const char* name;
};


static const rtsx_device_info kDevices[] = {
	{ 0x5209, RTSX_F_5209, 0, "Realtek RTS5209" },
	{ 0x5227, 0, 0, "Realtek RTS5227" },
	{ 0x5228, RTSX_F_5228, 0, "Realtek RTS5228" },
	{ 0x5229, RTSX_F_5229, 0, "Realtek RTS5229" },
	{ 0x522a, 0, 0, "Realtek RTS522A" },
	{ 0x5249, RTSX_F_5229, 0, "Realtek RTS5249" },
	{ 0x525a, RTSX_F_525A, 1, "Realtek RTS525A" },
	{ 0x5286, 0, 0, "Realtek RTL8402" },
	{ 0x5287, 0, 0, "Realtek RTL8411B" },
	{ 0x5289, 0, 0, "Realtek RTL8411" },
};


static const rtsx_device_info*
find_device(uint16 deviceId)
{
	for (uint32 i = 0; i < sizeof(kDevices) / sizeof(kDevices[0]); i++) {
		if (kDevices[i].deviceId == deviceId)
			return &kDevices[i];
	}
	return NULL;
}


class RtsxBus {
public:
								RtsxBus(volatile uint8* registers, uint8 irq,
									uint32 flags, area_id regsArea);
								~RtsxBus();

			status_t			Init();
			status_t			InitCheck() { return fStatus; }
			status_t			SetClock(uint32 kilohertz);
			status_t			ExecuteCommand(uint8 command, uint32 argument,
									uint32* response);
			status_t			DoIO(uint8 command, IOOperation* operation,
									bool offsetAsSectors);
			void				SetScanSemaphore(sem_id sem);
			void				SetBusWidth(int width);
			void				SetCardType(card_type type);
			void				TerminateBus();
			int32				HandleInterrupt();

private:
			bool				ChipReady() const
									{ return (fFlags & RTSX_F_5228) == 0; }
			uint32				Read4(uint32 reg)
									{ return *(volatile uint32*)(fRegisters + reg); }
			void				Write4(uint32 reg, uint32 value)
									{ *(volatile uint32*)(fRegisters + reg) = value; }
			status_t			ReadReg(uint16 address, uint8* value);
			status_t			WriteReg(uint16 address, uint8 mask, uint8 value);
			status_t			WritePhy(uint8 address, uint16 value);
			status_t			ReadConfig(uint8 function, uint16 address,
									uint32* value);
			status_t			InitChip(bool attaching);
			status_t			PowerOff();
			status_t			PowerOn();
			status_t			SwitchClock(uint8 n, int divisor, int mcu);
			status_t			StopClock();
			status_t			EnsurePower();
			status_t			SoftReset();
			status_t			WaitInterrupt(uint32 mask, bigtime_t timeout);
			void				HostCommand(uint8 command, uint16 reg, uint8 mask,
									uint8 data);
			status_t			SendHostCommands();
			status_t			ResponseType(uint8 command, uint8* type);
			status_t			SubmitCommand(uint8 command, uint32 argument,
									uint32* response, bool withData, bool readData,
									phys_addr_t dataPhys, size_t dataLength);
			void				NoteCard(bool present);

			volatile uint8*		fRegisters;
			area_id				fRegsArea;
			uint8				fIrq;
			uint32				fFlags;
			status_t			fStatus;
			bool				fPowered;
			bool				fHandlerInstalled;
			sem_id				fLock;
			sem_id				fDone;
			sem_id				fScanSemaphore;
			int32				fIntrStatus;
			area_id				fCmdArea;
			uint32*				fCmdBuf;
			phys_addr_t			fCmdPhys;
			int					fCmdCount;
			card_type			fCardType;
};


device_manager_info* gDeviceManager;
static module_info* sMMCBusModule;


RtsxBus::RtsxBus(volatile uint8* registers, uint8 irq, uint32 flags,
	area_id regsArea)
	:
	fRegisters(registers),
	fRegsArea(regsArea),
	fIrq(irq),
	fFlags(flags),
	fStatus(B_OK),
	fPowered(false),
	fHandlerInstalled(false),
	fLock(-1),
	fDone(-1),
	fScanSemaphore(-1),
	fIntrStatus(0),
	fCmdArea(-1),
	fCmdBuf(NULL),
	fCmdPhys(0),
	fCmdCount(0),
	fCardType(CARD_TYPE_UNKNOWN)
{
}


RtsxBus::~RtsxBus()
{
	if (fHandlerInstalled)
		remove_io_interrupt_handler(fIrq, rtsx_interrupt, this);

	if (fRegsArea >= 0)
		delete_area(fRegsArea);
	if (fCmdArea >= 0)
		delete_area(fCmdArea);
	if (fDone >= 0)
		delete_sem(fDone);
	if (fLock >= 0)
		delete_sem(fLock);
}


status_t
RtsxBus::Init()
{
	if (!ChipReady()) {
		TRACE_ALWAYS("RTS5228 attached. Chip init is an open point;"
			" no registers written\n");
		return B_OK;
	}

	fLock = create_sem(1, "rtsx lock");
	fDone = create_sem(0, "rtsx done");
	if (fLock < 0 || fDone < 0) {
		fStatus = B_NO_MEMORY;
		return fStatus;
	}

	void* buffer = NULL;
	fCmdArea = create_area("rtsx commands", &buffer, B_ANY_KERNEL_ADDRESS,
		B_PAGE_SIZE, B_LOMEM, B_READ_AREA | B_WRITE_AREA);
	if (fCmdArea < 0) {
		fStatus = fCmdArea;
		return fStatus;
	}
	fCmdBuf = (uint32*)buffer;

	physical_entry entry;
	fStatus = get_memory_map(fCmdBuf, B_PAGE_SIZE, &entry, 1);
	if (fStatus != B_OK || entry.size < RTSX_CMD_BYTES
		|| entry.address >= 0x100000000ULL) {
		ERROR("command buffer is not one DMA segment below 4 GiB\n");
		fStatus = B_ERROR;
		return fStatus;
	}
	fCmdPhys = entry.address;

	fStatus = InitChip(true);
	if (fStatus != B_OK)
		return fStatus;

	uint32 sdio = 0;
	if (ReadConfig(0, RTSX_SDIOCFG_REG, &sdio) == B_OK) {
		if ((sdio & (RTSX_SDIOCFG_SDIO_ONLY | RTSX_SDIOCFG_HAVE_SDIO)) != 0)
			fFlags |= RTSX_F_SDIO_SUPPORT;
	}

	fStatus = install_io_interrupt_handler(fIrq, rtsx_interrupt, this, 0);
	if (fStatus != B_OK) {
		ERROR("install interrupt %u: %s\n", fIrq, strerror(fStatus));
		return fStatus;
	}
	fHandlerInstalled = true;
	return B_OK;
}


status_t
RtsxBus::ReadReg(uint16 address, uint8* value)
{
	Write4(RTSX_HAIMR, RTSX_HAIMR_BUSY | ((uint32)(address & 0x3fff) << 16));
	uint32 reg = 0;
	for (int tries = 1024; tries > 0; tries--) {
		reg = Read4(RTSX_HAIMR);
		if ((reg & RTSX_HAIMR_BUSY) == 0) {
			*value = (uint8)reg;
			return B_OK;
		}
	}
	return B_TIMED_OUT;
}


status_t
RtsxBus::WriteReg(uint16 address, uint8 mask, uint8 value)
{
	Write4(RTSX_HAIMR, RTSX_HAIMR_BUSY | RTSX_HAIMR_WRITE
		| ((uint32)(address & 0x3fff) << 16)
		| ((uint32)mask << 8) | value);
	for (int tries = 1024; tries > 0; tries--) {
		uint32 reg = Read4(RTSX_HAIMR);
		if ((reg & RTSX_HAIMR_BUSY) == 0) {
			if (value != (reg & 0xff))
				return B_IO_ERROR;
			return B_OK;
		}
	}
	return B_TIMED_OUT;
}


status_t
RtsxBus::WritePhy(uint8 address, uint16 value)
{
	status_t error = WriteReg(RTSX_PHY_DATA0, 0xff, (uint8)value);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_PHY_DATA1, 0xff, (uint8)(value >> 8));
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_PHY_ADDR, 0xff, address);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_PHY_RWCTL, 0xff, RTSX_PHY_BUSY | RTSX_PHY_WRITE);
	if (error != B_OK)
		return error;

	for (int timeout = 100000; timeout > 0; timeout--) {
		uint8 rwctl = 0;
		error = ReadReg(RTSX_PHY_RWCTL, &rwctl);
		if (error != B_OK)
			return error;
		if ((rwctl & RTSX_PHY_BUSY) == 0)
			return B_OK;
	}
	return B_TIMED_OUT;
}


status_t
RtsxBus::ReadConfig(uint8 function, uint16 address, uint32* value)
{
	status_t error = WriteReg(RTSX_CFGADDR0, 0xff, (uint8)address);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CFGADDR1, 0xff, (uint8)(address >> 8));
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CFGRWCTL, 0xff,
		RTSX_CFG_BUSY | (function & 0x03 << 4));
	if (error != B_OK)
		return error;

	uint8 rwctl = 0;
	for (int tries = 1024; tries > 0; tries--) {
		error = ReadReg(RTSX_CFGRWCTL, &rwctl);
		if (error != B_OK)
			return error;
		if ((rwctl & RTSX_CFG_BUSY) == 0)
			break;
		if (tries == 1)
			return B_IO_ERROR;
	}

	uint8 data[4];
	uint16 regs[4] = { RTSX_CFGDATA0, RTSX_CFGDATA1, RTSX_CFGDATA2, RTSX_CFGDATA3 };
	for (int i = 0; i < 4; i++) {
		error = ReadReg(regs[i], &data[i]);
		if (error != B_OK)
			return error;
	}
	*value = ((uint32)data[3] << 24) | ((uint32)data[2] << 16)
		| ((uint32)data[1] << 8) | data[0];
	return B_OK;
}


status_t
RtsxBus::InitChip(bool attaching)
{
	if ((fFlags & RTSX_F_5229) != 0) {
		uint8 version = 0;
		status_t error = ReadReg(RTSX_DUMMY_REG, &version);
		if (error != B_OK)
			return error;
		switch (version & 0x0f) {
			case RTSX_IC_VERSION_A:
			case RTSX_IC_VERSION_B:
			case RTSX_IC_VERSION_D:
				break;
			case RTSX_IC_VERSION_C:
				fFlags |= RTSX_F_5229_TYPE_C;
				break;
			default:
				ERROR("unknown RTS5229 ic %02x\n", version);
				return B_ERROR;
		}
	}

	status_t error = WriteReg(RTSX_NFTS_TX_CTRL, RTSX_INT_READ_CLR, 0);
	if (error != B_OK)
		return error;

	uint32 status = Read4(RTSX_BIPR);
	Write4(RTSX_BIPR, status);
	if (attaching && (status & RTSX_SD_EXIST) != 0)
		fFlags |= RTSX_F_CARD_PRESENT;

	Write4(RTSX_BIER, RTSX_TRANS_OK_INT_EN | RTSX_TRANS_FAIL_INT_EN
		| RTSX_SD_INT_EN);

	error = WriteReg(RTSX_FPDCTL, RTSX_SSC_POWER_DOWN, 0);
	if (error != B_OK)
		return error;
	snooze(200);

	/*
	 * OpenBSD rtsx.c says these two PHY values are magic numbers from the
	 * Linux driver, for the chips OpenBSD already supports. RTS5228 does
	 * not reach this function.
	 */
	if ((fFlags & RTSX_F_5209) != 0)
		error = WritePhy(0x00, 0xb966);
	else
		error = WritePhy(0x00, 0xba42);
	if (error != B_OK) {
		ERROR("cannot write phy register\n");
		return error;
	}

	error = WriteReg(RTSX_CLK_DIV, 0x07, 0xff);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_HOST_SLEEP_STATE,
		RTSX_HOST_ENTER_S1 | RTSX_HOST_ENTER_S3, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_CLK_EN, RTSX_CARD_CLK_EN_ALL, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CHANGE_LINK_STATE,
		RTSX_FORCE_RST_CORE_EN | RTSX_NON_STICKY_RST_N_DBG | 0x04, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SD30_DRIVE_SEL, 0xff, RTSX_SD30_DRIVE_SEL_3V3);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SSC_CTL1, 0xff, RTSX_SSC_8X_EN | RTSX_SSC_SEL_4M);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SSC_CTL2, 0xff, 0x12);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CHANGE_LINK_STATE, RTSX_MAC_PHY_RST_N_DBG, 0xff);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_IRQSTAT0, RTSX_LINK_READY_INT, 0xff);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_PERST_GLITCH_WIDTH, 0xff, 0x80);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_RCCTL, RTSX_RCCTL_F_2M, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_PETXCFG, RTSX_PETXCFG_CLKREQ_PIN, 0xff);
	if (error != B_OK)
		return error;

	if ((fFlags & RTSX_F_5209) != 0) {
		error = WriteReg(RTSX_CARD_GPIO, 0xff, 0x03);
		if (error != B_OK)
			return error;
		error = WriteReg(RTSX_CARD_GPIO_DIR, 0xff, 0x03);
	} else {
		error = WriteReg(RTSX_GPIO_CTL, RTSX_GPIO_LED_ON, 0xff);
		if (error != B_OK)
			return error;
		error = WriteReg(RTSX_LDO_PWR_SEL, RTSX_LDO_PWR_SEL_DV33, 0);
		if (error != B_OK)
			return error;
		error = WriteReg(RTSX_LDO_PWR_SEL, RTSX_LDO_PWR_SEL_3V3, 0xff);
		if (error != B_OK)
			return error;
		error = WriteReg(RTSX_OLT_LED_CTL, RTSX_OLT_LED_PERIOD, 0xff);
	}
	return error;
}


status_t
RtsxBus::StopClock()
{
	status_t error = WriteReg(RTSX_CARD_CLK_EN, RTSX_CARD_CLK_EN_ALL, 0);
	if (error != B_OK)
		return error;
	return WriteReg(RTSX_SD_BUS_STAT, RTSX_SD_CLK_FORCE_STOP, 0xff);
}


status_t
RtsxBus::PowerOff()
{
	status_t error = StopClock();
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_OE, RTSX_CARD_OUTPUT_EN, 0);
	if (error != B_OK)
		return error;

	uint8 disable3 = RTSX_PULL_CTL_DISABLE3;
	if ((fFlags & RTSX_F_5209) != 0) {
		error = WriteReg(RTSX_PWR_GATE_CTRL, RTSX_LDO3318_OFF, 0xff);
	} else {
		error = WriteReg(RTSX_PWR_GATE_CTRL,
			RTSX_LDO3318_VCC1 | RTSX_LDO3318_VCC2, 0);
		if ((fFlags & RTSX_F_5229_TYPE_C) != 0)
			disable3 = RTSX_PULL_CTL_DISABLE3_TYPE_C;
	}
	if (error != B_OK)
		return error;

	error = WriteReg(RTSX_CARD_PWR_CTL, RTSX_SD_PWR_OFF, 0xff);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_PWR_CTL, RTSX_PMOS_STRG_800mA, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_PULL_CTL1, 0xff, RTSX_PULL_CTL_DISABLE12);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_PULL_CTL2, 0xff, RTSX_PULL_CTL_DISABLE12);
	if (error != B_OK)
		return error;
	fPowered = false;
	return WriteReg(RTSX_CARD_PULL_CTL3, 0xff, disable3);
}


status_t
RtsxBus::PowerOn()
{
	status_t error = B_OK;
	if ((fFlags & RTSX_F_525A) != 0) {
		error = WriteReg(RTSX_LDO_VCC_CFG1, RTSX_LDO_VCC_TUNE_MASK,
			RTSX_LDO_VCC_3V3);
		if (error != B_OK)
			return error;
	}

	error = WriteReg(RTSX_CARD_SELECT, 0xff, RTSX_SD_MOD_SEL);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_SHARE_MODE, 0xff, RTSX_CARD_SHARE_48_SD);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_CLK_EN, RTSX_SD_CLK_EN, 0xff);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_PULL_CTL1, 0xff, RTSX_PULL_CTL_ENABLE12);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_PULL_CTL2, 0xff, RTSX_PULL_CTL_ENABLE12);
	if (error != B_OK)
		return error;

	uint8 enable3 = RTSX_PULL_CTL_ENABLE3;
	if ((fFlags & RTSX_F_5229_TYPE_C) != 0)
		enable3 = RTSX_PULL_CTL_ENABLE3_TYPE_C;
	error = WriteReg(RTSX_CARD_PULL_CTL3, 0xff, enable3);
	if (error != B_OK)
		return error;

	error = WriteReg(RTSX_CARD_PWR_CTL, RTSX_SD_PARTIAL_PWR_ON, 0xff);
	if (error != B_OK)
		return error;
	if ((fFlags & RTSX_F_5209) != 0)
		error = WriteReg(RTSX_PWR_GATE_CTRL, RTSX_LDO3318_SUSPEND, 0xff);
	else
		error = WriteReg(RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC1, 0xff);
	if (error != B_OK)
		return error;

	snooze(200);

	error = WriteReg(RTSX_CARD_PWR_CTL, RTSX_SD_PWR_OFF, 0);
	if (error != B_OK)
		return error;
	if ((fFlags & RTSX_F_5209) != 0)
		error = WriteReg(RTSX_PWR_GATE_CTRL, RTSX_LDO3318_OFF, 0);
	else
		error = WriteReg(RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC2, 0xff);
	if (error != B_OK)
		return error;

	error = WriteReg(RTSX_CARD_OE, 0xff, RTSX_SD_OUTPUT_EN);
	if (error == B_OK)
		fPowered = true;
	return error;
}


status_t
RtsxBus::EnsurePower()
{
	if (fPowered)
		return B_OK;
	if ((fFlags & RTSX_F_CARD_PRESENT) == 0)
		return B_DEV_NOT_READY;

	status_t error = PowerOff();
	if (error != B_OK)
		return error;
	snooze(200);
	error = PowerOn();
	if (error != B_OK)
		return error;
	return WriteReg(RTSX_SD_CFG1, RTSX_BUS_WIDTH_MASK, RTSX_BUS_WIDTH_1);
}


status_t
RtsxBus::SwitchClock(uint8 n, int divisor, int mcu)
{
	status_t error = WriteReg(RTSX_SD_CFG1, RTSX_SD_MODE_MASK, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ, 0xff);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CARD_CLK_SOURCE, 0xff,
		RTSX_CRC_FIX_CLK | RTSX_SD30_VAR_CLK0 | RTSX_SAMPLE_VAR_CLK1);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SD_SAMPLE_POINT_CTL, RTSX_SD20_RX_SEL_MASK, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SD_PUSH_POINT_CTL, 0xff, RTSX_SD20_TX_NEG_EDGE);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_CLK_DIV, 0xff, (uint8)((divisor << 4) | mcu));
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SSC_CTL1, RTSX_RSTB, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SSC_CTL2, RTSX_SSC_DEPTH_MASK, 0);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SSC_DIV_N_0, 0xff, n);
	if (error != B_OK)
		return error;
	error = WriteReg(RTSX_SSC_CTL1, RTSX_RSTB, 0xff);
	if (error != B_OK)
		return error;
	snooze(100);
	return WriteReg(RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ, 0);
}


status_t
RtsxBus::SetClock(uint32 kilohertz)
{
	if (!ChipReady())
		return B_NOT_SUPPORTED;

	acquire_sem(fLock);
	status_t error = B_OK;
	if (kilohertz == RTSX_CLK_OFF) {
		error = StopClock();
		release_sem(fLock);
		return error;
	}

	error = EnsurePower();
	if (error != B_OK) {
		release_sem(fLock);
		return error;
	}

	uint8 n;
	int divisor;
	int mcu;
	if (kilohertz >= RTSX_CLK_50MHZ) {
		n = 100;
		divisor = RTSX_CLK_DIV_2;
		mcu = 7;
		error = WriteReg(RTSX_SD_CFG1, RTSX_CLK_DIVIDE_MASK, 0);
	} else if (kilohertz >= RTSX_CLK_25MHZ) {
		n = 100;
		divisor = RTSX_CLK_DIV_4;
		mcu = 7;
		error = WriteReg(RTSX_SD_CFG1, RTSX_CLK_DIVIDE_MASK, 0);
	} else {
		n = 80;
		divisor = RTSX_CLK_DIV_8;
		mcu = 7;
		error = WriteReg(RTSX_SD_CFG1, RTSX_CLK_DIVIDE_128, 0xff);
	}
	if (error == B_OK)
		error = SwitchClock(n, divisor, mcu);
	release_sem(fLock);
	return error;
}


void
RtsxBus::SetBusWidth(int width)
{
	if (!ChipReady())
		return;

	uint8 bits = RTSX_BUS_WIDTH_1;
	if (width == 8)
		bits = RTSX_BUS_WIDTH_8;
	else if (width == 4)
		bits = RTSX_BUS_WIDTH_4;

	acquire_sem(fLock);
	WriteReg(RTSX_SD_CFG1, RTSX_BUS_WIDTH_MASK, bits);
	release_sem(fLock);
}


status_t
RtsxBus::SoftReset()
{
	Write4(RTSX_HCBCTLR, RTSX_STOP_CMD);
	status_t error = WriteReg(RTSX_CARD_STOP, RTSX_SD_STOP | RTSX_SD_CLR_ERR,
		RTSX_SD_STOP | RTSX_SD_CLR_ERR);
	Write4(RTSX_HDBCTLR, RTSX_STOP_DMA);
	status_t error2 = WriteReg(RTSX_DMACTL, RTSX_DMA_RST, RTSX_DMA_RST);
	status_t error3 = WriteReg(RTSX_RBCTL, RTSX_RB_FLUSH, RTSX_RB_FLUSH);
	if (error != B_OK)
		return error;
	if (error2 != B_OK)
		return error2;
	return error3;
}


status_t
RtsxBus::WaitInterrupt(uint32 mask, bigtime_t timeout)
{
	mask |= RTSX_TRANS_FAIL_INT;
	while ((atomic_get(&fIntrStatus) & (int32)mask) == 0) {
		status_t error = acquire_sem_etc(fDone, 1, B_RELATIVE_TIMEOUT, timeout);
		if (error == B_TIMED_OUT) {
			SoftReset();
			return B_TIMED_OUT;
		}
		if (error != B_OK && error != B_INTERRUPTED)
			return error;
	}

	int32 status = atomic_get(&fIntrStatus);
	atomic_and(&fIntrStatus, ~status);
	if ((fFlags & RTSX_F_CARD_PRESENT) == 0)
		return B_DEV_NOT_READY;
	if ((status & RTSX_TRANS_FAIL_INT) != 0)
		return B_IO_ERROR;
	return B_OK;
}


void
RtsxBus::HostCommand(uint8 command, uint16 reg, uint8 mask, uint8 data)
{
	if (fCmdCount >= RTSX_HOSTCMD_MAX)
		return;
	fCmdBuf[fCmdCount++] = ((uint32)(command & 0x3) << 30)
		| ((uint32)(reg & 0x3fff) << 16)
		| ((uint32)mask << 8)
		| data;
}


status_t
RtsxBus::SendHostCommands()
{
	memory_write_barrier();
	Write4(RTSX_HCBAR, (uint32)fCmdPhys);
	Write4(RTSX_HCBCTLR, ((fCmdCount * 4) & 0x00ffffff) | RTSX_START_CMD
		| RTSX_HW_AUTO_RSP);
	return B_OK;
}


status_t
RtsxBus::ResponseType(uint8 command, uint8* type)
{
	switch (command) {
		case GO_IDLE_STATE:
			*type = RTSX_SD_RSP_TYPE_R0;
			return B_OK;
		case SD_APP_CMD:
		case SD_ERASE_WR_BLK_START:
		case SD_ERASE_WR_BLK_END:
		case SD_READ_SINGLE_BLOCK:
		case SD_READ_MULTIPLE_BLOCKS:
		case SD_WRITE_SINGLE_BLOCK:
		case SD_WRITE_MULTIPLE_BLOCKS:
			*type = RTSX_SD_RSP_TYPE_R1;
			return B_OK;
		case SELECT_DESELECT_CARD:
		case SD_ERASE:
		case SD_STOP_TRANSMISSION:
			*type = RTSX_SD_RSP_TYPE_R1B;
			return B_OK;
		case ALL_SEND_CID:
		case SEND_CSD:
			*type = RTSX_SD_RSP_TYPE_R2;
			return B_OK;
		case MMC_SEND_OP_COND:
		case SD_SEND_OP_COND:
			*type = RTSX_SD_RSP_TYPE_R3;
			return B_OK;
		case SD_SET_BUS_WIDTH:
			*type = fCardType == CARD_TYPE_MMC
				? RTSX_SD_RSP_TYPE_R1B : RTSX_SD_RSP_TYPE_R1;
			return B_OK;
		case SD_SEND_RELATIVE_ADDR:
			*type = fCardType == CARD_TYPE_MMC
				? RTSX_SD_RSP_TYPE_R1 : RTSX_SD_RSP_TYPE_R6;
			return B_OK;
		case SD_SEND_IF_COND:
			*type = fCardType == CARD_TYPE_MMC
				? RTSX_SD_RSP_TYPE_R1 : RTSX_SD_RSP_TYPE_R7;
			return B_OK;
		default:
			return B_BAD_VALUE;
	}
}


status_t
RtsxBus::SubmitCommand(uint8 command, uint32 argument, uint32* response,
	bool withData, bool readData, phys_addr_t dataPhys, size_t dataLength)
{
	if (command == 5 && (fFlags & RTSX_F_SDIO_SUPPORT) == 0)
		return B_NOT_SUPPORTED;

	uint8 rspType = 0;
	status_t error = ResponseType(command, &rspType);
	if (error != B_OK)
		return error;

	memset(fCmdBuf, 0, RTSX_CMD_BYTES);
	fCmdCount = 0;
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CMD0, 0xff, 0x40 | command);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CMD1, 0xff, argument >> 24);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CMD2, 0xff, argument >> 16);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CMD3, 0xff, argument >> 8);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CMD4, 0xff, argument);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CFG2, 0xff, rspType);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_CARD_DATA_SOURCE, 0x01,
		RTSX_PINGPONG_BUFFER);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_TRANSFER, 0xff,
		RTSX_TM_CMD_RSP | RTSX_SD_TRANSFER_START);
	HostCommand(RTSX_CHECK_REG_CMD, RTSX_SD_TRANSFER,
		RTSX_SD_TRANSFER_END | RTSX_SD_STAT_IDLE,
		RTSX_SD_TRANSFER_END | RTSX_SD_STAT_IDLE);

	int readAt = fCmdCount;
	if (rspType == RTSX_SD_RSP_TYPE_R2) {
		for (uint16 reg = RTSX_PPBUF_BASE2 + 15; reg > RTSX_PPBUF_BASE2; reg--)
			HostCommand(RTSX_READ_REG_CMD, reg, 0, 0);
		HostCommand(RTSX_READ_REG_CMD, RTSX_SD_CMD5, 0, 0);
	} else if (rspType != RTSX_SD_RSP_TYPE_R0) {
		for (uint16 reg = RTSX_SD_CMD0; reg <= RTSX_SD_CMD4; reg++)
			HostCommand(RTSX_READ_REG_CMD, reg, 0, 0);
	}

	atomic_set(&fIntrStatus, 0);
	error = SendHostCommands();
	if (error != B_OK)
		return error;
	error = WaitInterrupt(RTSX_TRANS_OK_INT, 1000000);
	if (error != B_OK)
		return error;

	if (response != NULL && rspType == RTSX_SD_RSP_TYPE_R2) {
		uint8 bytes[16];
		for (int i = 0; i < 16; i++)
			bytes[i] = (uint8)fCmdBuf[readAt + i];
		for (int i = 0; i < 4; i++) {
			response[i] = ((uint32)bytes[i * 4] << 24)
				| ((uint32)bytes[i * 4 + 1] << 16)
				| ((uint32)bytes[i * 4 + 2] << 8)
				| bytes[i * 4 + 3];
		}
	} else if (response != NULL && rspType != RTSX_SD_RSP_TYPE_R0) {
		/* SD_CMD0 echoes the opcode. The next four bytes are the response. */
		uint8 b1 = (uint8)fCmdBuf[readAt + 1];
		uint8 b2 = (uint8)fCmdBuf[readAt + 2];
		uint8 b3 = (uint8)fCmdBuf[readAt + 3];
		uint8 b4 = (uint8)fCmdBuf[readAt + 4];
		response[0] = ((uint32)b1 << 24) | ((uint32)b2 << 16)
			| ((uint32)b3 << 8) | b4;
	}

	if (!withData)
		return B_OK;

	uint8 cfg2 = RTSX_SD_NO_CHECK_WAIT_CRC_TO | RTSX_SD_CHECK_CRC16
		| RTSX_SD_NO_WAIT_BUSY_END | RTSX_SD_RSP_LEN_0;
	uint8 dmaDir;
	uint8 mode;
	bool multiple = command == SD_READ_MULTIPLE_BLOCKS
		|| command == SD_WRITE_MULTIPLE_BLOCKS;
	if (readData) {
		dmaDir = RTSX_DMA_DIR_FROM_CARD;
		/* AUTO_READ4 sends CMD12. mmc_disk does not. AUTO_READ3 does not. */
		mode = multiple ? RTSX_TM_AUTO_READ4 : RTSX_TM_AUTO_READ3;
		cfg2 |= RTSX_SD_CALCULATE_CRC7 | RTSX_SD_CHECK_CRC7;
	} else {
		dmaDir = RTSX_DMA_DIR_TO_CARD;
		mode = multiple ? RTSX_TM_AUTO_WRITE4 : RTSX_TM_AUTO_WRITE3;
		cfg2 |= RTSX_SD_NO_CALCULATE_CRC7 | RTSX_SD_NO_CHECK_CRC7;
	}

	fCmdCount = 0;
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_CFG2, 0xff, cfg2);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_BYTE_CNT_L, 0xff, 512 & 0xff);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_BYTE_CNT_H, 0xff, 512 >> 8);
	uint32 blocks = (uint32)(dataLength / 512);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_BLOCK_CNT_L, 0xff, blocks);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_BLOCK_CNT_H, 0xff, blocks >> 8);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_CARD_DATA_SOURCE, 0x01, RTSX_RING_BUFFER);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_IRQSTAT0, RTSX_DMA_DONE_INT,
		RTSX_DMA_DONE_INT);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_DMATC3, 0xff, dataLength >> 24);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_DMATC2, 0xff, dataLength >> 16);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_DMATC1, 0xff, dataLength >> 8);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_DMATC0, 0xff, dataLength);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_DMACTL,
		0x03 | RTSX_DMA_PACK_SIZE_MASK,
		dmaDir | RTSX_DMA_EN | RTSX_DMA_512);
	HostCommand(RTSX_WRITE_REG_CMD, RTSX_SD_TRANSFER, 0xff,
		mode | RTSX_SD_TRANSFER_START);
	HostCommand(RTSX_CHECK_REG_CMD, RTSX_SD_TRANSFER, RTSX_SD_TRANSFER_END,
		RTSX_SD_TRANSFER_END);

	atomic_set(&fIntrStatus, 0);
	error = SendHostCommands();
	if (error != B_OK)
		return error;

	Write4(RTSX_HDBAR, (uint32)dataPhys);
	Write4(RTSX_HDBCTLR, RTSX_TRIG_DMA | (readData ? RTSX_DMA_READ : 0)
		| ((uint32)dataLength & 0x00ffffff));
	error = WaitInterrupt(RTSX_TRANS_OK_INT, 10000000);
	if (error != B_OK) {
		uint8 stat1 = 0;
		if (ReadReg(RTSX_SD_STAT1, &stat1) == B_OK
			&& (stat1 & RTSX_SD_CRC_ERR) != 0)
			ERROR("CRC error\n");
	}
	return error;
}


status_t
RtsxBus::ExecuteCommand(uint8 command, uint32 argument, uint32* response)
{
	if (!ChipReady())
		return B_NOT_SUPPORTED;

	acquire_sem(fLock);
	status_t error = EnsurePower();
	if (error == B_OK)
		error = SubmitCommand(command, argument, response, false, false, 0, 0);
	release_sem(fLock);
	return error;
}


status_t
RtsxBus::DoIO(uint8 command, IOOperation* operation, bool offsetAsSectors)
{
	if (!ChipReady())
		return B_NOT_SUPPORTED;

	const generic_io_vec* vecs = operation->Vecs();
	generic_size_t length = operation->Length();
	off_t offset = operation->Offset();
	bool readData = !operation->IsWrite();
	generic_size_t vecOffset = 0;

	if (length == 0)
		return B_OK;
	if ((length % 512) != 0 || (offset % 512) != 0)
		return B_BAD_VALUE;

	acquire_sem(fLock);
	status_t error = EnsurePower();
	while (error == B_OK && length > 0) {
		if (vecs->length <= vecOffset) {
			vecs++;
			vecOffset = 0;
			continue;
		}
		size_t chunk = (size_t)std::min(length, vecs->length - vecOffset);
		chunk -= chunk % 512;
		phys_addr_t phys = vecs->base + vecOffset;
		if (chunk == 0 || phys >= 0x100000000ULL
			|| phys + chunk > 0x100000000ULL) {
			error = B_BAD_ADDRESS;
			break;
		}

		uint32 argument = (uint32)(offset / (offsetAsSectors ? 512 : 1));
		error = SubmitCommand(command, argument, NULL, true, readData, phys,
			chunk);
		if (error != B_OK)
			break;
		length -= chunk;
		offset += chunk;
		vecOffset += chunk;
	}
	release_sem(fLock);
	return error;
}


void
RtsxBus::NoteCard(bool present)
{
	if (present)
		fFlags |= RTSX_F_CARD_PRESENT;
	else {
		fFlags &= ~RTSX_F_CARD_PRESENT;
		fPowered = false;
	}
	if (fScanSemaphore >= 0)
		release_sem_etc(fScanSemaphore, 1, B_DO_NOT_RESCHEDULE);
}


void
RtsxBus::SetScanSemaphore(sem_id sem)
{
	fScanSemaphore = sem;
	if (ChipReady() && (fFlags & RTSX_F_CARD_PRESENT) != 0)
		release_sem(fScanSemaphore);
}


void
RtsxBus::SetCardType(card_type type)
{
	fCardType = type;
}


void
RtsxBus::TerminateBus()
{
	if (!ChipReady())
		return;
	acquire_sem(fLock);
	PowerOff();
	release_sem(fLock);
}


int32
RtsxBus::HandleInterrupt()
{
	if (!ChipReady())
		return B_UNHANDLED_INTERRUPT;

	uint32 enabled = Read4(RTSX_BIER);
	uint32 status = Read4(RTSX_BIPR);
	Write4(RTSX_BIPR, status);

	if ((enabled & status) == 0 || status == 0xffffffff)
		return B_UNHANDLED_INTERRUPT;

	if ((status & RTSX_SD_INT) != 0)
		NoteCard((status & RTSX_SD_EXIST) != 0);

	if ((status & (RTSX_TRANS_OK_INT | RTSX_TRANS_FAIL_INT)) != 0) {
		atomic_or(&fIntrStatus, (int32)status);
		release_sem_etc(fDone, 1, B_DO_NOT_RESCHEDULE);
	}
	return B_HANDLED_INTERRUPT;
}


static int32
rtsx_interrupt(void* data)
{
	return ((RtsxBus*)data)->HandleInterrupt();
}


struct RtsxDevice {
	device_node* node;
};


static pci_device_module_info*
parent_pci(device_node* node, pci_device** device)
{
	device_node* parent = gDeviceManager->get_parent_node(node);
	pci_device_module_info* pci = NULL;
	gDeviceManager->get_driver(parent, (driver_module_info**)&pci,
		(void**)device);
	gDeviceManager->put_node(parent);
	return pci;
}


static float
supports_device(device_node* parent)
{
	const char* bus = NULL;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) != B_OK
		|| strcmp(bus, "pci") != 0)
		return 0.0f;

	static bool sLoaded = false;
	if (!sLoaded) {
		TRACE_ALWAYS("rtsx: loaded\n");
		sLoaded = true;
	}

	uint16 vendor = 0;
	uint16 device = 0;
	uint16 type = 0;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID, &vendor, false)
			!= B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID, &device, false)
			!= B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_TYPE, &type, false)
			!= B_OK)
		return 0.0f;

	/*
	 * RTS5228 has no chip init yet. Do not claim it: a published bus with
	 * no commands is an empty slot. The id stays in the table for the
	 * later init file.
	 */
	if (vendor == 0x10ec && device == 0x5228) {
		if (rtsx_5228_support != NULL)
			return rtsx_5228_support(vendor, device);
		TRACE_ALWAYS("rtsx: not claiming 10ec:5228\n");
		return 0.0f;
	}

	/* Same rule as OpenBSD: an SDHCI class stays with sdhci. */
	if (vendor != 0x10ec || type != 0xff || find_device(device) == NULL)
		return 0.0f;

	TRACE("match %04x:%04x\n", vendor, device);
	return 0.8f;
}


static status_t
register_device(device_node* parent)
{
	uint16 device = 0;
	gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID, &device, false);
	const rtsx_device_info* info = find_device(device);
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = info != NULL ? info->name : "Realtek card reader" } },
		{ NULL }
	};
	return gDeviceManager->register_node(parent, RTSX_DEVICE_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
init_device(device_node* node, void** cookie)
{
	RtsxDevice* device = new(std::nothrow) RtsxDevice;
	if (device == NULL)
		return B_NO_MEMORY;
	device->node = node;
	*cookie = device;
	return B_OK;
}


static void
uninit_device(void* cookie)
{
	RtsxDevice* device = (RtsxDevice*)cookie;
	delete device;
}


static status_t
register_child_devices(void* cookie)
{
	RtsxDevice* context = (RtsxDevice*)cookie;
	pci_device* pciDevice = NULL;
	pci_device_module_info* pci = parent_pci(context->node, &pciDevice);

	uint16 deviceId = 0;
	gDeviceManager->get_attr_uint16(context->node, B_DEVICE_ID, &deviceId, true);
	const rtsx_device_info* info = find_device(deviceId);
	if (info == NULL)
		return B_BAD_VALUE;

	uint32 config = pci->read_pci_config(pciDevice, RTSX_PCI_CONFIG, 4);
	if ((config & RTSX_CFG_ASIC) != 0) {
		ERROR("%s: no asic\n", info->name);
		return B_ERROR;
	}

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = info->name } },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, { .string = MMC_BUS_MODULE_NAME } },
		{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "mmc" } },
		{ B_DMA_ALIGNMENT, B_UINT32_TYPE, { .ui32 = 511 } },
		{ B_DMA_HIGH_ADDRESS, B_UINT64_TYPE, { .ui64 = 0x100000000LL } },
		{ B_DMA_BOUNDARY, B_UINT32_TYPE, { .ui32 = (1 << 19) - 1 } },
		{ B_DMA_MAX_SEGMENT_COUNT, B_UINT32_TYPE, { .ui32 = 1 } },
		{ B_DMA_MAX_SEGMENT_BLOCKS, B_UINT32_TYPE, { .ui32 = 128 } },
		{ NULL }
	};
	return gDeviceManager->register_node(context->node, RTSX_MMC_BUS_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
init_bus(device_node* node, void** cookie)
{
	device_node* parent = gDeviceManager->get_parent_node(node);
	RtsxDevice* context = NULL;
	gDeviceManager->get_driver(parent, NULL, (void**)&context);
	gDeviceManager->put_node(parent);

	pci_device* pciDevice = NULL;
	pci_device_module_info* pci = parent_pci(context->node, &pciDevice);
	pci_info pciInfo;
	pci->get_pci_info(pciDevice, &pciInfo);

	uint16 deviceId = 0;
	gDeviceManager->get_attr_uint16(context->node, B_DEVICE_ID, &deviceId, true);
	const rtsx_device_info* info = find_device(deviceId);
	if (info == NULL)
		return B_BAD_VALUE;

	uint8 bar = info->bar;
	phys_addr_t physical = pciInfo.u.h0.base_registers[bar];
	uint64 size = pciInfo.u.h0.base_register_sizes[bar];
	if (physical == 0 || size == 0) {
		ERROR("no registers\n");
		return B_IO_ERROR;
	}

	uint16 command = pci->read_pci_config(pciDevice, PCI_command, 2);
	command &= ~(PCI_command_int_disable | PCI_command_io);
	command |= PCI_command_master | PCI_command_memory;
	pci->write_pci_config(pciDevice, PCI_command, 2, command);

	volatile uint8* regs = NULL;
	area_id area = map_physical_memory("rtsx registers", physical, size,
		B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&regs);
	if (area < B_OK)
		return area;

	RtsxBus* bus = new(std::nothrow) RtsxBus(regs, pciInfo.u.h0.interrupt_line,
		info->flags, area);
	if (bus == NULL) {
		delete_area(area);
		return B_NO_MEMORY;
	}

	status_t error = bus->Init();
	if (error != B_OK) {
		delete bus;
		return error;
	}

	*cookie = bus;
	return B_OK;
}


static void
uninit_bus(void* cookie)
{
	RtsxBus* bus = (RtsxBus*)cookie;
	delete bus;
}


static status_t
set_clock(void* controller, uint32 kilohertz)
{
	return ((RtsxBus*)controller)->SetClock(kilohertz);
}


static status_t
execute_command(void* controller, uint8 command, uint32 argument, uint32* response)
{
	return ((RtsxBus*)controller)->ExecuteCommand(command, argument, response);
}


static status_t
do_io(void* controller, uint8 command, IOOperation* operation, bool offsetAsSectors)
{
	return ((RtsxBus*)controller)->DoIO(command, operation, offsetAsSectors);
}


static void
set_scan_semaphore(void* controller, sem_id sem)
{
	((RtsxBus*)controller)->SetScanSemaphore(sem);
}


static void
set_bus_width(void* controller, int width)
{
	((RtsxBus*)controller)->SetBusWidth(width);
}


static void
set_card_type(void* controller, card_type type)
{
	((RtsxBus*)controller)->SetCardType(type);
}


static void
terminate_bus(void* controller)
{
	((RtsxBus*)controller)->TerminateBus();
}


static driver_module_info sRtsxDevice = {
	{
		RTSX_DEVICE_MODULE_NAME,
		0,
		NULL
	},
	supports_device,
	register_device,
	init_device,
	uninit_device,
	register_child_devices,
	NULL,
	NULL,
};


static mmc_bus_interface sRtsxBusModule = {
	.info = {
		.info = {
			.name = RTSX_MMC_BUS_MODULE_NAME,
		},
		.init_driver = init_bus,
		.uninit_driver = uninit_bus,
	},
	.set_clock = set_clock,
	.execute_command = execute_command,
	.do_io = do_io,
	.set_scan_semaphore = set_scan_semaphore,
	.set_bus_width = set_bus_width,
	.terminate_bus = terminate_bus,
	.set_card_type = set_card_type,
};


module_dependency module_dependencies[] = {
	{ MMC_BUS_MODULE_NAME, &sMMCBusModule },
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{}
};


module_info* modules[] = {
	(module_info*)&sRtsxDevice,
	(module_info*)&sRtsxBusModule,
	NULL
};
