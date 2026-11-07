/*
 * Copyright 2006-2022, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The Haiku copyright and the MIT License above still
 * cover the original work. A build that ships both follows GPL-3.0-or-later,
 * and the MIT notices stay on the Haiku portions.
 */

#ifndef RADEON_HD_H
#define RADEON_HD_H


#include "lock.h"

#include "radeon_reg.h"

//#include "r500_reg.h"  // Not used atm. DCE 0
#include "avivo_reg.h"		// DCE 1
#include "r600_reg.h"		// DCE 2
#include "r700_reg.h"		// DCE 3
#include "evergreen_reg.h"	// DCE 4
#include "ni_reg.h"			// DCE 5
#include "si_reg.h"			// DCE 6
#include "sea_reg.h"		// DCE 8
#include "vol_reg.h"		// DCE 10
#include "car_reg.h"		// DCE 11
#include "pol_reg.h"		// DCE 12

#include <Accelerant.h>
#include <Drivers.h>
#include <edid.h>
#include <PCI.h>


#define VENDOR_ID_ATI	0x1002

// Card chipset flags
#define CHIP_STD		(1 << 0) // Standard chipset
#define CHIP_X2			(1 << 1) // Dual cpu
#define CHIP_IGP		(1 << 2) // IGP chipset
#define CHIP_MOBILE		(1 << 3) // Mobile chipset
#define CHIP_DISCREET	(1 << 4) // Discreet chipset
#define CHIP_APU		(1 << 5) // APU chipset

#define DEVICE_NAME				"radeon_hd"
#define RADEON_ACCELERANT_NAME	"radeon_hd.accelerant"

#define MAX_NAME_LENGTH		32

// Used to collect EDID from boot loader
#define EDID_BOOT_INFO "vesa_edid/v1"
#define MODES_BOOT_INFO "vesa_modes/v1"

#define RHD_POWER_ON       0
#define RHD_POWER_RESET    1   /* off temporarily */
#define RHD_POWER_SHUTDOWN 2   /* long term shutdown */
#define RHD_POWER_UNKNOWN  3   /* initial state */


// Radeon Chipsets
// !! Must match chipset names below
enum radeon_chipset {
	RADEON_R420 = 0,	//r400, Radeon X700-X850
	RADEON_R423,
	RADEON_RV410,
	RADEON_RS400,
	RADEON_RS480,
	RADEON_RS600,
	RADEON_RS690,
	RADEON_RS740,
	RADEON_RV515,
	RADEON_R520,		//r500, DCE 1.0
	RADEON_RV530,		// DCE 1.0
	RADEON_RV560,		// DCE 1.0
	RADEON_RV570,		// DCE 1.0
	RADEON_R580,		// DCE 1.0
	RADEON_R600,		//r600, DCE 2.0
	RADEON_RV610,		// DCE 2.0
	RADEON_RV630,		// DCE 2.0
	RADEON_RV670,		// DCE 2.0
	RADEON_RV620,		// DCE 3.0
	RADEON_RV635,		// DCE 3.0
	RADEON_RS780,		// DCE 3.0
	RADEON_RS880,		// DCE 3.0
	RADEON_RV770,		//r700, DCE 3.1
	RADEON_RV730,		// DCE 3.2
	RADEON_RV710,		// DCE 3.2
	RADEON_RV740,		// DCE 3.2
	RADEON_CEDAR,		//Evergreen, DCE 4.0
	RADEON_REDWOOD,		// DCE 4.0
	RADEON_JUNIPER,		// DCE 4.0
	RADEON_CYPRESS,		// DCE 4.0
	RADEON_HEMLOCK,		// DCE 4.0?
	RADEON_PALM,		//Fusion APU (NI), DCE 4.1
	RADEON_SUMO,		// DCE 4.1
	RADEON_SUMO2,		// DCE 4.1
	RADEON_CAICOS,		//Nothern Islands, DCE 5.0
	RADEON_TURKS,		// DCE 5.0
	RADEON_BARTS,		// DCE 5.0
	RADEON_CAYMAN,		// DCE 5.0
	RADEON_ANTILLES,	// DCE 5.0?
	RADEON_CAPEVERDE,	//Southern Islands, DCE 6.0
	RADEON_PITCAIRN,	// DCE 6.0
	RADEON_TAHITI,		// DCE 6.0
	RADEON_ARUBA,		// DCE 6.1 Trinity/Richland
	RADEON_OLAND,		// DCE 6.4
	RADEON_HAINAN,		// NO DCE, only compute
	RADEON_KAVERI,		//Sea Islands, DCE 8.1
	RADEON_BONAIRE,		// DCE 8.2
	RADEON_KABINI,		// DCE 8.3
	RADEON_MULLINS,		// DCE 8.3
	RADEON_HAWAII,		// DCE 8.5
	RADEON_TOPAZ,		//Volcanic Islands, NO DCE
	RADEON_TONGA,		// DCE 10.0
	RADEON_FIJI,		// DCE 10.1
	RADEON_CARRIZO,		// DCE 11.0
	RADEON_STONEY,		// DCE 11.0
	RADEON_POLARIS10,	// DCE 11.2
	RADEON_POLARIS11,	//Artic Islands, DCE 11.2
	RADEON_POLARIS12,	// DCE 11.2
	RADEON_VEGAM,		// DCE 11.2 (Linux DC enum 11.22)
	RADEON_VEGA10,		// DCE 12.0
	RADEON_VEGA12,		// DCE 12.0
	RADEON_VEGA20,		// DCE 12.1
	RADEON_RAVEN,		// DCN 1.0
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	RADEON_RENOIR,		// DCN 2.1
// SPDX-SnippetEnd
	RADEON_NAVI,		// DCN 2.0 and DCN 3.0
};

// !! Must match chipset families above
static const char radeon_chip_name[][MAX_NAME_LENGTH] = {
	"R420",
	"R423",
	"RV410",
	"RS400",
	"RS480",
	"RS600",
	"RS690",
	"RS740",
	"RV515",
	"R520",
	"RV530",
	"RV560",
	"RV570",
	"R580",
	"R600",
	"RV610",
	"RV630",
	"RV670",
	"RV620",
	"RV635",
	"RS780",
	"RS880",
	"RV770",
	"RV730",
	"RV710",
	"RV740",
	"Cedar",
	"Redwood",
	"Juniper",
	"Cypress",
	"Hemlock",
	"Palm",
	"Sumo",
	"Sumo2",
	"Caicos",
	"Turks",
	"Barts",
	"Cayman",
	"Antilles",
	"Cape Verde",
	"Pitcairn",
	"Tahiti",
	"Aruba",
	"Oland",
	"Hainan",
	"Kaveri",
	"Bonaire",
	"Kabini",
	"Mullins",
	"Hawaii",
	"Topaz",
	"Tonga",
	"Fiji",
	"Carrizo",
	"Stoney Ridge",
	"Polaris 10",
	"Polaris 11",
	"Polaris 12",
	"Vega Mobile",
	"Vega 10",
	"Vega 12",
	"Vega 20",
	"Raven",
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	"Renoir",
// SPDX-SnippetEnd
	"Navi",
};


struct ring_buffer {
	struct lock		lock;
	uint32			register_base;
	uint32			offset;
	uint32			size;
	uint32			position;
	uint32			space_left;
	uint8*			base;
};


struct radeon_shared_info {
	uint32			deviceIndex;		// accelerant index
	uint32			pciID;				// device pci id
	uint32			pciRev;				// device pci revision
	area_id			mode_list_area;		// area containing display mode list
	uint32			mode_count;

	bool			has_rom;			// was rom mapped?
	area_id			rom_area;			// area of mapped rom
	uint32			rom_phys;			// rom base location
	uint32			rom_size;			// rom size
	uint8*			rom;				// cloned, memory mapped PCI ROM

	display_mode	current_mode;
	uint32			bytes_per_row;
	uint32			bits_per_pixel;
	uint32			dpms_mode;

	area_id			registers_area;			// area of memory mapped registers
	uint8*			status_page;
	addr_t			physical_status_page;
	uint32			graphics_memory_size;

	uint8*			frame_buffer;			// virtual memory mapped FB
	area_id			frame_buffer_area;		// area of memory mapped FB
	addr_t			frame_buffer_phys;		// card PCI BAR address of FB
	uint32			frame_buffer_size;		// FB size mapped

	bool			has_edid;
	edid1_info		edid_info;

	struct lock		accelerant_lock;
	struct lock		engine_lock;

	ring_buffer		primary_ring_buffer;

	char			deviceName[MAX_NAME_LENGTH];
	uint16			chipsetID;
	char			chipsetName[MAX_NAME_LENGTH];
	uint32			chipsetFlags;
	uint8			dceMajor;
	uint8			dceMinor;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	// Display engine recorded from the device table. dceMajor is the DCE
	// register generation and stays 0 when the engine is not DCE.
	uint8			displayClass;
	uint8			displayMajor;
	uint8			displayMinor;
	uint8			displayStepping;
// SPDX-SnippetEnd

	uint16			color_data[3 * 256];    // colour lookup table

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	// CPU-visible VRAM window. This is the PCI framebuffer BAR, or the
	// smaller mapping used when that BAR would not map. Offsets are bytes
	// from vram_window_phys.
	// The reserved range is the live scanout. The pool is the larger side
	// that does not overlap it. vram_pool_cursor walks the pool.
	uint64			vram_bar_phys;
	uint64			vram_bar_bytes;
	uint64			vram_window_phys;
	uint64			vram_window_bytes;
	uint64			vram_reserved_offset;
	uint64			vram_reserved_bytes;
	uint64			vram_pool_offset;
	uint64			vram_pool_bytes;
	uint64			vram_pool_cursor;
	// GFX9 doorbell aperture, PCI BAR 2. doorbell_area is -1 when that
	// BAR was not mapped. The accelerant clones the area. This is not
	// the framebuffer and it is not the MMIO BAR.
	area_id			doorbell_area;
	uint64			doorbell_phys;
	uint64			doorbell_bytes;
	// Set by the kernel when it has started the GPU. The accelerant
	// then writes the text and does not start a second time.
	uint32			gfx9_owner;
	char			gfx9_text[8192];
	uint32			gfx9_text_used;
// SPDX-SnippetEnd
};

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
enum {
	RHD_DISPLAY_DCE = 0,
	RHD_DISPLAY_DCN = 1,
	RHD_DISPLAY_NONE = 2
};

inline bool
radeon_hd_uses_dce(uint16 chipsetID)
{
	if (chipsetID == RADEON_HAINAN || chipsetID == RADEON_TOPAZ)
		return false;
	return chipsetID < RADEON_RAVEN;
}


// Allocate bytes from the visible VRAM pool. The offset is from
// vram_window_phys. The address is the CPU mapping of that offset.
// alignment must be a power of two. The pool is not freed.
status_t radeon_vram_alloc(uint64 bytes, uint64 alignment, uint64* _offset,
	uint8** _address);

// Clone the GFX9 doorbell BAR into this team. A missing BAR is logged
// and leaves the ring on the write-pointer register.
void radeon_gfx9_doorbell_attach(area_id area, uint64 bytes);
void radeon_gfx9_doorbell_detach();
// SPDX-SnippetEnd

//----------------- ioctl() interface ----------------

// magic code for ioctls
#define RADEON_PRIVATE_DATA_MAGIC		'rdhd'

// list ioctls
enum {
	RADEON_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,

	RADEON_GET_DEVICE_NAME,
	RADEON_ALLOCATE_GRAPHICS_MEMORY,
	RADEON_FREE_GRAPHICS_MEMORY
};

// retrieve the area_id of the kernel/accelerant shared info
struct radeon_get_private_data {
	uint32	magic;				// magic number
	area_id	shared_info_area;
};

// allocate graphics memory
struct radeon_allocate_graphics_memory {
	uint32	magic;
	uint32	size;
	uint32	alignment;
	uint32	flags;
	uint32	buffer_base;
};

// free graphics memory
struct radeon_free_graphics_memory {
	uint32 	magic;
	uint32	buffer_base;
};

// registers
#define R6XX_CONFIG_APER_SIZE			0x5430	// r600>
#define OLD_CONFIG_APER_SIZE			0x0108	// <r600
#define CONFIG_MEMSIZE					0x5428	// r600>
#define CONFIG_MEMSIZE_TAHITI			0x03de	// tahiti>


#endif	/* RADEON_HD_H */
