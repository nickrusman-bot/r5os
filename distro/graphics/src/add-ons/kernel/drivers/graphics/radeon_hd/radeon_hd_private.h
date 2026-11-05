/*
 * Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 */
#ifndef RADEON_RD_PRIVATE_H
#define RADEON_RD_PRIVATE_H


#include <AGP.h>
#include <KernelExport.h>
#include <PCI.h>


#include "radeon_hd.h"
#include "lock.h"


// PCI Base Address Registers
#define PCI_BAR_FB      0

#define RADEON_BIOS8(adr, v) 	(adr[v])
#define RADEON_BIOS16(adr, v) 	((adr[v]) | (adr[(v) + 1] << 8))
#define RADEON_BIOS32(adr, v)	\
	((RADEON_BIOS16(adr, v) | RADEON_BIOS16(adr, v + 2) << 16))


struct radeon_info {
	int32			open_count;
	status_t		init_status;
	int32			id;
	pci_info*		pci;
	addr_t			registers;

	uint8*			atom_buffer;	// buffer for atombios

	area_id			registers_area;
	area_id			framebuffer_area;
	area_id			rom_area;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	area_id			doorbell_area;
	uint8*			doorbell;
	// Kernel view of the VRAM pool only. r5_gfx9_kernel_start creates it
	// when gfx9_ring and gfx9_kernel are on, and r5_gfx9_kernel_stop
	// deletes it. The cloneable framebuffer keeps the user bit.
	area_id			kernel_window_area;
	uint8*			kernel_window;
// SPDX-SnippetEnd

	struct radeon_shared_info* shared_info;
	area_id			shared_area;

	uint32			pciID;
	const char*		deviceName;
	uint16			chipsetID;
	const char*		chipsetName;
	uint32			chipsetFlags;
	uint8			dceMajor;
	uint8			dceMinor;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	uint8			displayClass;
	uint8			displayMajor;
	uint8			displayMinor;
	uint8			displayStepping;
// SPDX-SnippetEnd
};


extern status_t radeon_hd_init(radeon_info& info);
extern void radeon_hd_uninit(radeon_info& info);


#endif  /* RADEON_RD_PRIVATE_H */
