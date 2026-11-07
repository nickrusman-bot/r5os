/*
 * Copyright 2006-2011, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Fredrik Holmqvis, fredrik.holmqvist@gmail.com
 *		Alexander von Gluck, kallisti5@unixzen.com
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The Haiku copyright and the MIT License above still
 * cover the original work. A build that ships both follows GPL-3.0-or-later,
 * and the MIT notices stay on the Haiku portions.
 */



#include "radeon_hd.h"
#include "r5_vram_reserve.h"
#include "sensors.h"

#include "atombios/atombios.h"
#include "driver.h"
#include "utility.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <ACPI.h>
#include <boot_item.h>
#include <frame_buffer_console.h>
#include <driver_settings.h>
#include <util/AreaKeeper.h>
#include <util/kernel_cpp.h>
#include <vm/vm.h>


#define TRACE_DEVICE
#ifdef TRACE_DEVICE
#	define TRACE(x...) dprintf("radeon_hd: " x)
#else
#	define TRACE(x) ;
#endif

#define ERROR(x...) dprintf("radeon_hd: " x)


//	#pragma mark -


static status_t
mapAtomBIOSACPI(radeon_info &info, uint32& romSize)
{
	TRACE("%s: seeking AtomBIOS from ACPI\n", __func__);

	uint8* rom;
	acpi_module_info* acpiModule;

	status_t status = get_module(B_ACPI_MODULE_NAME, (module_info**)&acpiModule);
	if (status < B_OK)
		return status;

	UEFI_ACPI_VFCT* vfct;
	GOP_VBIOS_CONTENT* vbios;
	VFCT_IMAGE_HEADER* vhdr;
	status = acpiModule->get_table("VFCT", 0, (void**)&vfct);
	if (status != B_OK) {
		put_module(B_ACPI_MODULE_NAME);
		return status;
	}

	vbios = (GOP_VBIOS_CONTENT*)((char*)vfct + vfct->VBIOSImageOffset);
	vhdr = &vbios->VbiosHeader;
	TRACE("%s: ACPI VFCT contains a BIOS for: %" B_PRIx32 ":%" B_PRIx32 ":%"
		B_PRId32 " %04x:%04x\n", __func__,
		vhdr->PCIBus, vhdr->PCIDevice, vhdr->PCIFunction, vhdr->VendorID, vhdr->DeviceID);

	if (info.pci->vendor_id != vhdr->VendorID || info.pci->device_id != vhdr->DeviceID
		|| info.pci->bus != vhdr->PCIBus || info.pci->device != vhdr->PCIDevice
		|| info.pci->function != vhdr->PCIFunction) {
		TRACE("%s: not valid AtomBIOS rom for current device\n", __func__);
		put_module(B_ACPI_MODULE_NAME);
		return B_ERROR;
	}

	rom = vbios->VbiosContent;
	romSize = vhdr->ImageLength;
	// see if valid AtomBIOS rom
	uint16 romHeader = RADEON_BIOS16(rom, 0x48);
	bool romValid = !memcmp(&rom[romHeader + 4], "ATOM", 4)
		|| !memcmp(&rom[romHeader + 4], "MOTA", 4);

	if (romValid == false) {
		TRACE("%s: not valid AtomBIOS rom at ACPI\n", __func__);
		put_module(B_ACPI_MODULE_NAME);
		return B_ERROR;
	}

	uint32 areaSize = ROUNDUP(romSize, 1 << 16);
	info.rom_area = create_area("radeon hd AtomBIOS",
		(void**)&info.atom_buffer, B_ANY_KERNEL_ADDRESS,
		areaSize, B_NO_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);

	if (info.rom_area < 0) {
		ERROR("%s: unable to map kernel AtomBIOS space!\n",
			__func__);
		put_module(B_ACPI_MODULE_NAME);
		return B_NO_MEMORY;
	}

	memset((void*)info.atom_buffer, 0, areaSize);
		// Prevent unknown code execution by AtomBIOS parser
	memcpy(info.atom_buffer, (void*)rom, romSize);
		// Copy AtomBIOS to kernel area

	// validate copied rom is valid
	romHeader = RADEON_BIOS16(info.atom_buffer, 0x48);
	romValid = !memcmp(&info.atom_buffer[romHeader + 4], "ATOM", 4)
		|| !memcmp(&info.atom_buffer[romHeader + 4], "MOTA", 4);

	if (romValid == true) {
		// XXX: We should only KERNEL_READ_AREA + CLONE here, but AtomBIOS calls fail w/RO #19348
		//set_area_protection(info.rom_area,
		//	B_KERNEL_READ_AREA | B_CLONEABLE_AREA);
		ERROR("%s: AtomBIOS verified and locked (%" B_PRIu32 ")\n", __func__, romSize);
	} else
		ERROR("%s: AtomBIOS memcpy failed!\n", __func__);

	put_module(B_ACPI_MODULE_NAME);

	return romValid ? B_OK : B_ERROR;
}


static size_t
radeon_get_rom_size(uint8* rom, size_t romSize)
{
	uint8* image = rom;
	uint8* end = rom + romSize;
	uint32 length = 0;
	bool lastImage;
	if (image[0] != 0x55 || image[1] != 0xaa)
		return 0;
	do {
		uint8* pds = image + *(uint16*)(image + 0x18);
		if (memcmp(pds, "PCIR", 4) != 0)
			break;
		lastImage = (*(pds + 0x15) & 0x80) != 0;
		length = *(uint16*)(pds + 0x10);
		image += length * 512;
		if (image >= end)
			break;
		if (!lastImage && (image[0] != 0x55 || image[1] != 0xaa))
			break;
	} while (length > 0 && !lastImage);

	return min_c((size_t)(image - rom), romSize);
}


static status_t
mapAtomBIOS(radeon_info &info, phys_addr_t romBase, uint32 romSize,
	bool findROMlength = false)
{
	TRACE("%s: seeking AtomBIOS @ 0x%" B_PRIXPHYSADDR " [size: 0x%" B_PRIX32 "]\n",
		__func__, romBase, romSize);

	uint8* rom;

	// attempt to access area specified
	area_id testArea = map_physical_memory("radeon hd rom probe",
		romBase, romSize, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA,
		(void**)&rom);

	if (testArea < 0) {
		ERROR("%s: couldn't map potential rom @ 0x%" B_PRIXPHYSADDR
			"\n", __func__, romBase);
		return B_NO_MEMORY;
	}

	// check for valid BIOS signature
	if (rom[0] != 0x55 || rom[1] != 0xAA) {
		uint16 id = rom[0] + (rom[1] << 8);
		TRACE("%s: BIOS signature incorrect @ 0x%" B_PRIXPHYSADDR " (%X)\n",
			__func__, romBase, id);
		delete_area(testArea);
		return B_ERROR;
	}

	// see if valid AtomBIOS rom
	uint16 romHeader = RADEON_BIOS16(rom, 0x48);
	bool romValid = !memcmp(&rom[romHeader + 4], "ATOM", 4)
		|| !memcmp(&rom[romHeader + 4], "MOTA", 4);

	if (romValid == false) {
		// FAIL : a PCI VGA bios but not AtomBIOS
		uint16 id = rom[0] + (rom[1] << 8);
		TRACE("%s: not AtomBIOS rom at 0x%" B_PRIXPHYSADDR "(%X)\n",
			__func__, romBase, id);
		delete_area(testArea);
		return B_ERROR;
	}

	info.rom_area = create_area("radeon hd AtomBIOS",
		(void**)&info.atom_buffer, B_ANY_KERNEL_ADDRESS,
		romSize, B_NO_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);

	if (info.rom_area < 0) {
		ERROR("%s: unable to map kernel AtomBIOS space!\n",
			__func__);
		delete_area(testArea);
		return B_NO_MEMORY;
	}

	memset((void*)info.atom_buffer, 0, romSize);
		// Prevent unknown code execution by AtomBIOS parser
	if (findROMlength) {
		romSize = radeon_get_rom_size(rom, romSize);
		if (romSize == 0) {
			TRACE("%s: rom size is zero\n", __func__);
			delete_area(testArea);
			return B_ERROR;
		}
	}
	memcpy(info.atom_buffer, (void*)rom, romSize);
		// Copy AtomBIOS to kernel area

	// validate copied rom is valid
	romHeader = RADEON_BIOS16(info.atom_buffer, 0x48);
	romValid = !memcmp(&info.atom_buffer[romHeader + 4], "ATOM", 4)
		|| !memcmp(&info.atom_buffer[romHeader + 4], "MOTA", 4);

	if (romValid == true) {
		// XXX: We should only KERNEL_READ_AREA + CLONE here, but AtomBIOS calls fail w/RO #19348
		//set_area_protection(info.rom_area,
		//	B_KERNEL_READ_AREA | B_CLONEABLE_AREA);
		ERROR("%s: AtomBIOS verified and locked (%" B_PRIu32 ")\n", __func__, romSize);
	} else
		ERROR("%s: AtomBIOS memcpy failed!\n", __func__);

	delete_area(testArea);
	return romValid ? B_OK : B_ERROR;
}


static status_t
radeon_hd_getbios(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);

	phys_addr_t romBase = 0;
	uint32 romSize = 0;
	uint32 romMethod = 0;

	status_t mapResult = B_ERROR;

	// first we try to find the AtomBIOS rom via various methods
	for (romMethod = 0; romMethod < 3; romMethod++) {
		switch(romMethod) {
			case 0:
				// *** ACPI method, VFCT table
				mapResult = mapAtomBIOSACPI(info, romSize);
				break;
			case 1:
				// *** Discreet card on IGP, check PCI BAR 0
				// On post, the bios puts a copy of the IGP
				// AtomBIOS at the start of the video ram
				romBase = info.pci->u.h0.base_registers[PCI_BAR_FB];
				if ((info.pci->u.h0.base_register_flags[PCI_BAR_FB] & PCI_address_type)
					== PCI_address_type_64) {
					romBase |= (uint64)info.pci->u.h0.base_registers[PCI_BAR_FB + 1] << 32;
				}
				romSize = 256 * 1024;

				if (romBase == 0 || romSize == 0) {
					ERROR("%s: No base found at PCI FB BAR\n", __func__);
				} else {
					mapResult = mapAtomBIOS(info, romBase, romSize);
				}
				break;
			case 2:
			{
				// *** PCI ROM BAR
				// Enable ROM decoding for PCI BAR rom
				uint32 pciConfig = get_pci_config(info.pci, PCI_rom_base, 4);
				pciConfig |= PCI_rom_enable;
				set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

				uint32 flags = get_pci_config(info.pci, PCI_rom_base, 4);
				if ((flags & PCI_rom_enable) != 0)
					TRACE("%s: PCI ROM decode enabled\n", __func__);

				romBase = info.pci->u.h0.rom_base;
				romSize = info.pci->u.h0.rom_size;

				if (romBase == 0 || romSize == 0) {
					ERROR("%s: No base found at PCI ROM BAR\n", __func__);
				} else {
					mapResult = mapAtomBIOS(info, romBase, romSize, true);
				}

				// Disable ROM decoding
				pciConfig &= ~PCI_rom_enable;
				set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);
				break;
			}
		}

		if (mapResult == B_OK) {
			ERROR("%s: AtomBIOS found using active method %" B_PRIu32
				" at 0x%" B_PRIXPHYSADDR "\n", __func__, romMethod, romBase);
			break;
		} else {
			ERROR("%s: AtomBIOS not found using active method %" B_PRIu32
				" at 0x%" B_PRIXPHYSADDR "\n", __func__, romMethod, romBase);
		}
	}

	if (mapResult == B_OK) {
		info.shared_info->rom_phys = romBase;
		info.shared_info->rom_size = romSize;
	} else
		ERROR("%s: Active AtomBIOS search failed.\n", __func__);

	return mapResult;
}


static status_t
radeon_hd_getbios_ni(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);
	uint32 bus_cntl = read32(info.registers + R600_BUS_CNTL);
	uint32 d1vga_control = read32(info.registers + AVIVO_D1VGA_CONTROL);
	uint32 d2vga_control = read32(info.registers + AVIVO_D2VGA_CONTROL);
	uint32 vga_render_control
		= read32(info.registers + AVIVO_VGA_RENDER_CONTROL);
	uint32 rom_cntl = read32(info.registers + R600_ROM_CNTL);

	// enable the rom
	write32(info.registers + R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	// disable VGA mode
	write32(info.registers + AVIVO_D1VGA_CONTROL, (d1vga_control
		& ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
			| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_D2VGA_CONTROL, (d2vga_control
		& ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
			| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL,
		(vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	write32(info.registers + R600_ROM_CNTL, (rom_cntl | R600_SCK_OVERWRITE));

	// try to grab the bios via PCI ROM bar
	// Enable ROM decoding for PCI BAR rom
	uint32 pciConfig = get_pci_config(info.pci, PCI_rom_base, 4);
	pciConfig |= PCI_rom_enable;
	set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

	uint32 flags = get_pci_config(info.pci, PCI_rom_base, 4);
	if (flags & PCI_rom_enable)
		TRACE("%s: PCI ROM decode enabled\n", __func__);

	uint32 romBase = info.pci->u.h0.rom_base;
	uint32 romSize = info.pci->u.h0.rom_size;

	status_t result = B_OK;
	if (romBase == 0 || romSize == 0) {
		ERROR("%s: No AtomBIOS location found at PCI ROM BAR\n", __func__);
		result = B_ERROR;
	} else {
		result = mapAtomBIOS(info, romBase, romSize, true);
	}

	if (result == B_OK) {
		ERROR("%s: AtomBIOS found using disabled method at 0x%" B_PRIX32
			" [size: 0x%" B_PRIX32 "]\n", __func__, romBase, romSize);
		info.shared_info->rom_phys = romBase;
		info.shared_info->rom_size = romSize;
	}

	// Disable ROM decoding
	pciConfig &= ~PCI_rom_enable;
	set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

	// restore regs
	write32(info.registers + R600_BUS_CNTL, bus_cntl);
	write32(info.registers + AVIVO_D1VGA_CONTROL, d1vga_control);
	write32(info.registers + AVIVO_D2VGA_CONTROL, d2vga_control);
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	write32(info.registers + R600_ROM_CNTL, rom_cntl);

	return result;
}


static status_t
radeon_hd_getbios_r700(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);
	uint32 viph_control = read32(info.registers + RADEON_VIPH_CONTROL);
	uint32 bus_cntl = read32(info.registers + R600_BUS_CNTL);
	uint32 d1vga_control = read32(info.registers + AVIVO_D1VGA_CONTROL);
	uint32 d2vga_control = read32(info.registers + AVIVO_D2VGA_CONTROL);
	uint32 vga_render_control
		= read32(info.registers + AVIVO_VGA_RENDER_CONTROL);
	uint32 rom_cntl = read32(info.registers + R600_ROM_CNTL);

	// disable VIP
	write32(info.registers + RADEON_VIPH_CONTROL,
		(viph_control & ~RADEON_VIPH_EN));
	// enable the rom
	write32(info.registers + R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	// disable VGA mode
	write32(info.registers + AVIVO_D1VGA_CONTROL, (d1vga_control
		& ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
			| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_D2VGA_CONTROL, (d2vga_control
		& ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
			| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL,
		(vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	write32(info.registers + R600_ROM_CNTL, (rom_cntl | R600_SCK_OVERWRITE));

	// try to grab the bios via PCI ROM bar
	// Enable ROM decoding for PCI BAR rom
	uint32 pciConfig = get_pci_config(info.pci, PCI_rom_base, 4);
	pciConfig |= PCI_rom_enable;
	set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

	uint32 flags = get_pci_config(info.pci, PCI_rom_base, 4);
	if (flags & PCI_rom_enable)
		TRACE("%s: PCI ROM decode enabled\n", __func__);

	uint32 romBase = info.pci->u.h0.rom_base;
	uint32 romSize = info.pci->u.h0.rom_size;

	status_t result = B_OK;
	if (romBase == 0 || romSize == 0) {
		ERROR("%s: No AtomBIOS location found at PCI ROM BAR\n", __func__);
		result = B_ERROR;
	} else {
		result = mapAtomBIOS(info, romBase, romSize);
	}

	if (result == B_OK) {
		ERROR("%s: AtomBIOS found using disabled method at 0x%" B_PRIX32
			" [size: 0x%" B_PRIX32 "]\n", __func__, romBase, romSize);
		info.shared_info->rom_phys = romBase;
		info.shared_info->rom_size = romSize;
	}

	// Disable ROM decoding
	pciConfig &= ~PCI_rom_enable;
	set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

	// restore regs
	write32(info.registers + RADEON_VIPH_CONTROL, viph_control);
	write32(info.registers + R600_BUS_CNTL, bus_cntl);
	write32(info.registers + AVIVO_D1VGA_CONTROL, d1vga_control);
	write32(info.registers + AVIVO_D2VGA_CONTROL, d2vga_control);
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	write32(info.registers + R600_ROM_CNTL, rom_cntl);

	return result;
}


static status_t
radeon_hd_getbios_r600(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);
	uint32 viph_control = read32(info.registers + RADEON_VIPH_CONTROL);
	uint32 bus_cntl = read32(info.registers + R600_BUS_CNTL);
	uint32 d1vga_control = read32(info.registers + AVIVO_D1VGA_CONTROL);
	uint32 d2vga_control = read32(info.registers + AVIVO_D2VGA_CONTROL);
	uint32 vga_render_control
		= read32(info.registers + AVIVO_VGA_RENDER_CONTROL);
	uint32 rom_cntl = read32(info.registers + R600_ROM_CNTL);
	uint32 general_pwrmgt = read32(info.registers + R600_GENERAL_PWRMGT);
	uint32 low_vid_lower_gpio_cntl
		= read32(info.registers + R600_LOW_VID_LOWER_GPIO_CNTL);
	uint32 medium_vid_lower_gpio_cntl
		= read32(info.registers + R600_MEDIUM_VID_LOWER_GPIO_CNTL);
	uint32 high_vid_lower_gpio_cntl
		= read32(info.registers + R600_HIGH_VID_LOWER_GPIO_CNTL);
	uint32 ctxsw_vid_lower_gpio_cntl
		= read32(info.registers + R600_CTXSW_VID_LOWER_GPIO_CNTL);
	uint32 lower_gpio_enable
		= read32(info.registers + R600_LOWER_GPIO_ENABLE);

	// disable VIP
	write32(info.registers + RADEON_VIPH_CONTROL,
		(viph_control & ~RADEON_VIPH_EN));
	// enable the rom
	write32(info.registers + R600_BUS_CNTL, (bus_cntl & ~R600_BIOS_ROM_DIS));
	// disable VGA mode
	write32(info.registers + AVIVO_D1VGA_CONTROL, (d1vga_control
		& ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
			| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_D2VGA_CONTROL, (d2vga_control
		& ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
			| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL,
		(vga_render_control & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	write32(info.registers + R600_ROM_CNTL,
		((rom_cntl & ~R600_SCK_PRESCALE_CRYSTAL_CLK_MASK)
		| (1 << R600_SCK_PRESCALE_CRYSTAL_CLK_SHIFT) | R600_SCK_OVERWRITE));

	write32(info.registers + R600_GENERAL_PWRMGT,
		(general_pwrmgt & ~R600_OPEN_DRAIN_PADS));
	write32(info.registers + R600_LOW_VID_LOWER_GPIO_CNTL,
		(low_vid_lower_gpio_cntl & ~0x400));
	write32(info.registers + R600_MEDIUM_VID_LOWER_GPIO_CNTL,
		(medium_vid_lower_gpio_cntl & ~0x400));
	write32(info.registers + R600_HIGH_VID_LOWER_GPIO_CNTL,
		(high_vid_lower_gpio_cntl & ~0x400));
	write32(info.registers + R600_CTXSW_VID_LOWER_GPIO_CNTL,
		(ctxsw_vid_lower_gpio_cntl & ~0x400));
	write32(info.registers + R600_LOWER_GPIO_ENABLE,
		(lower_gpio_enable | 0x400));

	// try to grab the bios via PCI ROM bar
	// Enable ROM decoding for PCI BAR rom
	uint32 pciConfig = get_pci_config(info.pci, PCI_rom_base, 4);
	pciConfig |= PCI_rom_enable;
	set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

	uint32 flags = get_pci_config(info.pci, PCI_rom_base, 4);
	if (flags & PCI_rom_enable)
		TRACE("%s: PCI ROM decode enabled\n", __func__);

	uint32 romBase = info.pci->u.h0.rom_base;
	uint32 romSize = info.pci->u.h0.rom_size;

	status_t result = B_OK;
	if (romBase == 0 || romSize == 0) {
		ERROR("%s: No AtomBIOS location found at PCI ROM BAR\n", __func__);
		result = B_ERROR;
	} else {
		result = mapAtomBIOS(info, romBase, romSize);
	}

	if (result == B_OK) {
		ERROR("%s: AtomBIOS found using disabled method at 0x%" B_PRIX32
			" [size: 0x%" B_PRIX32 "]\n", __func__, romBase, romSize);
		info.shared_info->rom_phys = romBase;
		info.shared_info->rom_size = romSize;
	}

	// Disable ROM decoding
	pciConfig &= ~PCI_rom_enable;
	set_pci_config(info.pci, PCI_rom_base, 4, pciConfig);

	// restore regs
	write32(info.registers + RADEON_VIPH_CONTROL, viph_control);
	write32(info.registers + R600_BUS_CNTL, bus_cntl);
	write32(info.registers + AVIVO_D1VGA_CONTROL, d1vga_control);
	write32(info.registers + AVIVO_D2VGA_CONTROL, d2vga_control);
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL, vga_render_control);
	write32(info.registers + R600_ROM_CNTL, rom_cntl);
	write32(info.registers + R600_GENERAL_PWRMGT, general_pwrmgt);
	write32(info.registers + R600_LOW_VID_LOWER_GPIO_CNTL,
		low_vid_lower_gpio_cntl);
	write32(info.registers + R600_MEDIUM_VID_LOWER_GPIO_CNTL,
		medium_vid_lower_gpio_cntl);
	write32(info.registers + R600_HIGH_VID_LOWER_GPIO_CNTL,
		high_vid_lower_gpio_cntl);
	write32(info.registers + R600_CTXSW_VID_LOWER_GPIO_CNTL,
		ctxsw_vid_lower_gpio_cntl);
	write32(info.registers + R600_LOWER_GPIO_ENABLE, lower_gpio_enable);

	return result;
}


static status_t
radeon_hd_getbios_avivo(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);
	uint32 sepromControl = read32(info.registers + RADEON_SEPROM_CNTL1);
	uint32 viphControl = read32(info.registers + RADEON_VIPH_CONTROL);
	uint32 busControl = read32(info.registers + RV370_BUS_CNTL);
	uint32 d1vgaControl = read32(info.registers + AVIVO_D1VGA_CONTROL);
	uint32 d2vgaControl = read32(info.registers + AVIVO_D2VGA_CONTROL);
	uint32 vgaRenderControl
		= read32(info.registers + AVIVO_VGA_RENDER_CONTROL);
	uint32 gpioPadA = read32(info.registers + RADEON_GPIOPAD_A);
	uint32 gpioPadEN = read32(info.registers + RADEON_GPIOPAD_EN);
	uint32 gpioPadMask = read32(info.registers + RADEON_GPIOPAD_MASK);

	write32(info.registers + RADEON_SEPROM_CNTL1,
		((sepromControl & ~RADEON_SCK_PRESCALE_MASK)
		| (0xc << RADEON_SCK_PRESCALE_SHIFT)));
	write32(info.registers + RADEON_GPIOPAD_A, 0);
	write32(info.registers + RADEON_GPIOPAD_EN, 0);
	write32(info.registers + RADEON_GPIOPAD_MASK, 0);

	// disable VIP
	write32(info.registers + RADEON_VIPH_CONTROL,
		(viphControl & ~RADEON_VIPH_EN));

	// enable the ROM
	write32(info.registers + RV370_BUS_CNTL,
		(busControl & ~RV370_BUS_BIOS_DIS_ROM));

	// disable VGA
	write32(info.registers + AVIVO_D1VGA_CONTROL,
		(d1vgaControl & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
		| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_D2VGA_CONTROL,
		(d2vgaControl & ~(AVIVO_DVGA_CONTROL_MODE_ENABLE
		| AVIVO_DVGA_CONTROL_TIMING_SELECT)));
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL,
		(vgaRenderControl & ~AVIVO_VGA_VSTATUS_CNTL_MASK));

	uint32 romBase = info.pci->u.h0.rom_base;
	uint32 romSize = info.pci->u.h0.rom_size;

	status_t result = B_OK;
	if (romBase == 0 || romSize == 0) {
		ERROR("%s: No AtomBIOS location found at PCI ROM BAR\n", __func__);
		result = B_ERROR;
	} else {
		result = mapAtomBIOS(info, romBase, romSize);
	}

	if (result == B_OK) {
		ERROR("%s: AtomBIOS found using disabled method at 0x%" B_PRIX32
			" [size: 0x%" B_PRIX32 "]\n", __func__, romBase, romSize);
		info.shared_info->rom_phys = romBase;
		info.shared_info->rom_size = romSize;
	}

	// restore registers
	write32(info.registers + RADEON_SEPROM_CNTL1, sepromControl);
	write32(info.registers + RADEON_VIPH_CONTROL, viphControl);
	write32(info.registers + RV370_BUS_CNTL, busControl);
	write32(info.registers + AVIVO_D1VGA_CONTROL, d1vgaControl);
	write32(info.registers + AVIVO_D2VGA_CONTROL, d2vgaControl);
	write32(info.registers + AVIVO_VGA_RENDER_CONTROL, vgaRenderControl);
	write32(info.registers + RADEON_GPIOPAD_A, gpioPadA);
	write32(info.registers + RADEON_GPIOPAD_EN, gpioPadEN);
	write32(info.registers + RADEON_GPIOPAD_MASK, gpioPadMask);

	return result;
}


static uint32 radeon_hd_pci_bar_mmio(uint16 chipsetID);


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// amdgpu_doorbell_init maps PCI BAR 2 for BONAIRE and later. MMIO on
// those parts is BAR 5. A size outside 0x460 bytes .. 16 MiB is left
// unmapped and the GFX ring still starts from the write-pointer register.
static void
radeon_map_doorbell(radeon_info& info)
{
	info.doorbell_area = -1;
	info.doorbell = NULL;
	if (info.shared_info == NULL)
		return;
	info.shared_info->doorbell_area = -1;
	info.shared_info->doorbell_phys = 0;
	info.shared_info->doorbell_bytes = 0;
	if (radeon_hd_uses_dce(info.chipsetID))
		return;
	if (radeon_hd_pci_bar_mmio(info.chipsetID) != 5)
		return;

	const uint32 bar = 2;
	uint32 flags = info.pci->u.h0.base_register_flags[bar];
	phys_addr_t addr = info.pci->u.h0.base_registers[bar];
	uint64 bytes = info.pci->u.h0.base_register_sizes[bar];
	if ((flags & PCI_address_type) == PCI_address_type_64) {
		addr |= (uint64)info.pci->u.h0.base_registers[bar + 1] << 32;
		bytes |= (uint64)info.pci->u.h0.base_register_sizes[bar + 1] << 32;
	}
	if (bytes < 0x460 || bytes > 16ull * 1024ull * 1024ull) {
		ERROR("%s: R5 radeon_hd: doorbell BAR2 size 0x%" B_PRIx64
			" left unmapped\n", __func__, bytes);
		return;
	}

	uint8* mapped = NULL;
	AreaKeeper keeper;
	area_id area = keeper.Map("radeon hd doorbell", addr, (size_t)bytes,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&mapped);
	if (keeper.InitCheck() < B_OK || mapped == NULL || area < 0) {
		ERROR("%s: R5 radeon_hd: doorbell BAR2 map failed\n", __func__);
		return;
	}
	keeper.Detach();
	info.doorbell_area = area;
	info.doorbell = mapped;
	info.shared_info->doorbell_area = area;
	info.shared_info->doorbell_phys = (uint64)addr;
	info.shared_info->doorbell_bytes = bytes;
	ERROR("%s: R5 radeon_hd: doorbell BAR2 0x%" B_PRIx64
		" bytes 0x%" B_PRIx64 "\n", __func__, (uint64)addr, bytes);
}
// SPDX-SnippetEnd


static uint32
radeon_hd_pci_bar_mmio(uint16 chipsetID)
{
	if (chipsetID < RADEON_BONAIRE)
		return 2;
	else
		return 5;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static uint64
radeon_align_down(uint64 value, uint64 align)
{
	return value & ~(align - 1);
}


static uint64
radeon_align_up(uint64 value, uint64 align)
{
	return (value + align - 1) & ~(align - 1);
}


static void
radeon_publish_vram_window(radeon_shared_info* info, uint64 barPhys,
	uint64 barBytes, uint64 windowPhys, uint64 windowBytes,
	uint64 reservedOffset, uint64 reservedBytes)
{
	const uint64 kAlign = 4096;

	if (reservedBytes > 0 && reservedOffset < windowBytes) {
		uint64 end = reservedOffset + reservedBytes;
		if (end > windowBytes)
			end = windowBytes;
		reservedOffset = radeon_align_down(reservedOffset, kAlign);
		end = radeon_align_up(end, kAlign);
		if (end > windowBytes)
			end = windowBytes;
		reservedBytes = end - reservedOffset;
	} else {
		reservedOffset = 0;
		reservedBytes = 0;
	}

	uint64 before = reservedOffset;
	uint64 afterOffset = reservedBytes == 0
		? 0 : reservedOffset + reservedBytes;
	uint64 windowEnd = radeon_align_down(windowBytes, kAlign);
	uint64 after = windowEnd > afterOffset ? windowEnd - afterOffset : 0;
	uint64 poolOffset;
	uint64 poolBytes;
	if (after >= before) {
		poolOffset = afterOffset;
		poolBytes = after;
	} else {
		poolOffset = 0;
		poolBytes = radeon_align_down(before, kAlign);
	}

	info->vram_bar_phys = barPhys;
	info->vram_bar_bytes = barBytes;
	info->vram_window_phys = windowPhys;
	info->vram_window_bytes = windowBytes;
	info->vram_reserved_offset = reservedOffset;
	info->vram_reserved_bytes = reservedBytes;
	info->vram_pool_offset = poolOffset;
	info->vram_pool_bytes = poolBytes;
	info->vram_pool_cursor = poolOffset;

	{
		r5_vram_range holes[4];
		uint64 vramBytes = info->graphics_memory_size * 1024ull;
		int count = r5_vram_reserve(vramBytes, windowBytes, 0, 0, holes, 4);
		int index;
		info->vram_hole_count = (uint32)count;
		for (index = 0; index < 4; index++) {
			info->vram_hole_offset[index] = 0;
			info->vram_hole_bytes[index] = 0;
		}
		for (index = 0; index < count; index++) {
			info->vram_hole_offset[index] = holes[index].offset;
			info->vram_hole_bytes[index] = holes[index].bytes;
		}
		ERROR("%s: R5 radeon_hd: pool holes %d, psp 0x%" B_PRIx64
			"+0x%" B_PRIx64 " %s, discovery 0x%" B_PRIx64 " %s, "
			"fw_vram_usage none\n", __func__, count,
			(uint64)R5_PSP_TMR_OFFSET, (uint64)R5_PSP_TMR_BYTES,
			R5_PSP_TMR_OFFSET < windowBytes ? "inside" : "outside",
			vramBytes > R5_DISCOVERY_TMR_TAIL
				? vramBytes - R5_DISCOVERY_TMR_TAIL : 0,
			vramBytes > R5_DISCOVERY_TMR_TAIL
				&& vramBytes - R5_DISCOVERY_TMR_TAIL < windowBytes
				? "inside" : "outside");
	}

	ERROR("%s: R5 radeon_hd: visible VRAM bar 0x%" B_PRIx64
		" size 0x%" B_PRIx64 ", window 0x%" B_PRIx64
		" size 0x%" B_PRIx64 ", reserved 0x%" B_PRIx64
		"+0x%" B_PRIx64 ", pool 0x%" B_PRIx64 "+0x%" B_PRIx64 "\n",
		__func__, barPhys, barBytes, windowPhys, windowBytes,
		reservedOffset, reservedBytes, poolOffset, poolBytes);
}
// SPDX-SnippetEnd


status_t
radeon_hd_init(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s: called\n", info.id, __func__);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	info.doorbell_area = -1;
// SPDX-SnippetEnd

	ERROR("%s: card(%" B_PRId32 "): "
		"Radeon %s 1002:%" B_PRIX32 "\n", __func__, info.id,
		radeon_chip_name[info.chipsetID], info.pciID);

	// Enable response in I/O, memory space. Enable bus mastering
	uint32 pciConfig = get_pci_config(info.pci, PCI_command, 2);
	pciConfig |= PCI_command_io | PCI_command_memory | PCI_command_master;
	set_pci_config(info.pci, PCI_command, 2, pciConfig);

	// *** Map shared info
	AreaKeeper sharedCreator;
	info.shared_area = sharedCreator.Create("radeon hd shared info",
		(void**)&info.shared_info, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sizeof(radeon_shared_info)), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (info.shared_area < B_OK) {
		ERROR("%s: card (%" B_PRId32 "): couldn't map shared area!\n",
			__func__, info.id);
		return info.shared_area;
	}

	memset((void*)info.shared_info, 0, sizeof(radeon_shared_info));
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	info.doorbell_area = -1;
	info.shared_info->doorbell_area = -1;
// SPDX-SnippetEnd
	sharedCreator.Detach();

	// *** Map Memory mapped IO
	const uint32 pciBarMmio = radeon_hd_pci_bar_mmio(info.chipsetID);
	phys_addr_t addr = info.pci->u.h0.base_registers[pciBarMmio];
	uint64 mmioSize = info.pci->u.h0.base_register_sizes[pciBarMmio];
	if (pciBarMmio < 5
		&& (info.pci->u.h0.base_register_flags[pciBarMmio] & PCI_address_type) == PCI_address_type_64) {
		addr |= (uint64)info.pci->u.h0.base_registers[pciBarMmio + 1] << 32;
		mmioSize |= (uint64)info.pci->u.h0.base_register_sizes[pciBarMmio + 1] << 32;
	}

	AreaKeeper mmioMapper;
	info.registers_area = mmioMapper.Map("radeon hd mmio", addr, mmioSize,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&info.registers);
	if (mmioMapper.InitCheck() < B_OK) {
		ERROR("%s: card (%" B_PRId32 "): couldn't map memory I/O!\n",
			__func__, info.id);
		return info.registers_area;
	}
	mmioMapper.Detach();

	// *** Populate frame buffer information
	if (info.chipsetID >= RADEON_TAHITI) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		// Tahiti+ stores this in MB. Raven leaves the register at
		// 0x3fffff, which prints as 4194303 MB. Treat that as unknown.
		uint32 memMb = read32(info.registers + CONFIG_MEMSIZE_TAHITI);
		if (memMb == 0 || memMb > 65536) {
			TRACE("%s: R5 radeon_hd: CONFIG_MEMSIZE %" B_PRIu32
				" MB ignored\n", __func__, memMb);
			info.shared_info->graphics_memory_size = 0;
		} else
			info.shared_info->graphics_memory_size = memMb * 1024;
// SPDX-SnippetEnd
	} else if (info.chipsetID >= RADEON_CEDAR) {
		switch (info.chipsetID) {
			default:
				// Evergreen+ has memory stored in MB
				info.shared_info->graphics_memory_size
					= read32(info.registers + CONFIG_MEMSIZE) * 1024;
				// Haiku #17664: Cedar sometimes leaves CONFIG_MEMSIZE at 0.
				// Retry as bytes (R700-style) before falling back to the BAR.
				if (info.shared_info->graphics_memory_size == 0) {
					uint32 memSize = read32(info.registers + CONFIG_MEMSIZE);
					if (memSize > 1024)
						info.shared_info->graphics_memory_size = memSize / 1024;
				}
				break;
			case RADEON_PALM:
			case RADEON_SUMO:
			case RADEON_SUMO2:
				// Fusion in bytes
				info.shared_info->graphics_memory_size
					= read32(info.registers + CONFIG_MEMSIZE) / 1024;
				break;
		}
	} else if (info.chipsetID >= RADEON_R600) {
		// R600-R700 has memory stored in bytes
		info.shared_info->graphics_memory_size
			= read32(info.registers + CONFIG_MEMSIZE) / 1024;
	} else {
		// R420 - R600 cards
		// older cards use RADEON_CONFIG_MEMSIZE vs CONFIG_MEMSIZE
		if ((info.chipsetFlags & CHIP_IGP) != 0) {
			// NB_TOM holds amount of ram stolen for GPU
			uint32 tom = read32(info.registers + RADEON_NB_TOM);
			info.shared_info->graphics_memory_size
				= (((tom >> 16) - (tom & 0xffff) + 1) << 16);
			write32(info.registers + RADEON_CONFIG_MEMSIZE,
				info.shared_info->graphics_memory_size);
		} else {
			info.shared_info->graphics_memory_size
				= read32(info.registers + RADEON_CONFIG_MEMSIZE);
			if (info.shared_info->graphics_memory_size == 0) {
				// known bug if video memory == 8MB
				info.shared_info->graphics_memory_size = 8192;
				write32(info.registers + RADEON_CONFIG_MEMSIZE,
					info.shared_info->graphics_memory_size * 1024);
			}
		}
	}

	phys_addr_t fbAddr = info.pci->u.h0.base_registers[PCI_BAR_FB];
	uint64 fbBarSize = info.pci->u.h0.base_register_sizes[PCI_BAR_FB];
	if ((info.pci->u.h0.base_register_flags[PCI_BAR_FB] & PCI_address_type)
			== PCI_address_type_64) {
		fbAddr |= (uint64)info.pci->u.h0.base_registers[PCI_BAR_FB + 1] << 32;
		fbBarSize |= (uint64)info.pci->u.h0.base_register_sizes[PCI_BAR_FB + 1] << 32;
	}

	// Make KiB
	fbBarSize /= 1024;

	// if graphics memory is larger then PCI bar, just map bar
	if (info.shared_info->graphics_memory_size == 0) {
		// we can recover as we have PCI FB bar, but this should be fixed
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		if (!radeon_hd_uses_dce(info.chipsetID)) {
			// Raven leaves CONFIG_MEMSIZE unused. The PCI aperture stays
			// the upper bound. The mapped picture is the boot framebuffer.
			ERROR("%s: R5 radeon_hd: CONFIG_MEMSIZE unused, PCI aperture %"
				B_PRIu64 " KiB remains the upper bound\n",
				__func__, fbBarSize);
		} else {
// SPDX-SnippetEnd
		ERROR("%s: Error: found 0MB video ram, using PCI bar size...\n",
			__func__);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		}
// SPDX-SnippetEnd
		info.shared_info->frame_buffer_size = fbBarSize;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		info.shared_info->graphics_memory_size = fbBarSize;
// SPDX-SnippetEnd
	} else if (info.shared_info->graphics_memory_size > fbBarSize) {
		TRACE("%s: shrinking frame buffer to PCI bar...\n",
			__func__);
		info.shared_info->frame_buffer_size = fbBarSize;
	} else {
		info.shared_info->frame_buffer_size
			= info.shared_info->graphics_memory_size;
	}

	if (info.shared_info->frame_buffer_size == 0) {
		ERROR("%s: Error: frame buffer size is 0. I give up.\n",
			__func__);
		return B_ERROR;
	}
	if (info.shared_info->frame_buffer_size < 8192) {
		// Haiku #17664: still map a small BAR rather than leaving app_server
		// with a NULL framebuffer.
		ERROR("%s: Warning: frame buffer is less than 8 MiB (%" B_PRIu32
			" KiB), continuing.\n",
			__func__, info.shared_info->frame_buffer_size);
	}

	TRACE("%s: mapping a frame buffer of %" B_PRIu32 "MB out of %" B_PRIu32
		"MB video ram\n", __func__, info.shared_info->frame_buffer_size / 1024,
		info.shared_info->graphics_memory_size / 1024);

	// *** Framebuffer mapping
	//
	// The PCI BAR is the CPU-visible VRAM window. Linux gmc_v9_0_mc_init
	// records that BAR as aper_base/aper_size and then keeps the firmware
	// picture out of the allocator (amdgpu_gmc_get_vbios_allocations).
	// The picture stays where the firmware put it. The pool is the rest
	// of the window. Nothing here writes a memory-controller register.

	phys_addr_t mapAddr = fbAddr;
	// frame_buffer_size is KiB. A 4 GiB BAR is 4194304 KiB, and that times
	// 1024 does not fit in uint32.
	uint64 mapBytes = (uint64)info.shared_info->frame_buffer_size * 1024;
	uint64 barPhys = (uint64)fbAddr;
	uint64 barBytes = (uint64)fbBarSize * 1024;
	uint64 reservedOffset = 0;
	uint64 reservedBytes = 0;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	frame_buffer_boot_info* bootFrame
		= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
	if (bootFrame != NULL && bootFrame->physical_frame_buffer != 0
		&& bootFrame->bytes_per_row > 0 && bootFrame->height > 0) {
		uint64 scanPhys = (uint64)bootFrame->physical_frame_buffer;
		uint64 scanBytes = (uint64)bootFrame->bytes_per_row
			* (uint64)bootFrame->height;
		if (scanPhys >= barPhys && scanBytes > 0
			&& scanPhys - barPhys < (uint64)mapBytes
			&& scanBytes <= (uint64)mapBytes - (scanPhys - barPhys)) {
			reservedOffset = scanPhys - barPhys;
			reservedBytes = scanBytes;
			info.shared_info->bytes_per_row = bootFrame->bytes_per_row;
			ERROR("%s: R5 radeon_hd: boot framebuffer %" B_PRId32
				"x%" B_PRId32 " pitch %" B_PRId32 " at window +0x%" B_PRIx64
				"\n", __func__, bootFrame->width, bootFrame->height,
				bootFrame->bytes_per_row, reservedOffset);
		} else {
			ERROR("%s: R5 radeon_hd: boot framebuffer is outside the "
				"visible window\n", __func__);
		}
	}
// SPDX-SnippetEnd

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (!radeon_hd_uses_dce(info.chipsetID) && reservedBytes == 0) {
		ERROR("%s: R5 radeon_hd: DCN without the boot framebuffer, "
			"not claimed\n", __func__);
		return B_ERROR;
	}
// SPDX-SnippetEnd

	TRACE("framebuffer paddr: %#" B_PRIxPHYSADDR "\n", mapAddr);
	AreaKeeper frambufferMapper;
	info.framebuffer_area = frambufferMapper.Map("radeon hd frame buffer",
		mapAddr, mapBytes,
		B_ANY_KERNEL_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		(void**)&info.shared_info->frame_buffer);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (frambufferMapper.InitCheck() < B_OK && reservedBytes > 0) {
		// The whole window did not map. Keep the picture, and publish
		// an empty pool, which is the previous DCN mapping.
		uint64 scanBytes = (reservedBytes + 4095) & ~4095ULL;
		mapAddr = (phys_addr_t)(barPhys + reservedOffset);
		mapBytes = scanBytes;
		info.shared_info->frame_buffer_size = (uint32)(mapBytes / 1024);
		reservedOffset = 0;
		reservedBytes = scanBytes;
		ERROR("%s: R5 radeon_hd: visible window map failed, mapping the "
			"boot framebuffer only\n", __func__);
		info.framebuffer_area = frambufferMapper.Map(
			"radeon hd frame buffer", mapAddr, mapBytes,
			B_ANY_KERNEL_ADDRESS, B_READ_AREA | B_WRITE_AREA,
			(void**)&info.shared_info->frame_buffer);
	}
// SPDX-SnippetEnd

	if (frambufferMapper.InitCheck() < B_OK) {
		ERROR("%s: card(%" B_PRId32 "): couldn't map frame buffer!\n",
			__func__, info.id);
		return info.framebuffer_area;
	}
	TRACE("frambuffer vaddr: %#" B_PRIxADDR "\n",
		(addr_t)info.shared_info->frame_buffer);
	TRACE("frambuffer size: %#" B_PRIxSIZE "\n",
		(size_t)info.shared_info->frame_buffer_size * 1024);

	// Turn on write combining for the frame buffer area
	vm_set_area_memory_type(info.framebuffer_area, mapAddr,
		B_WRITE_COMBINING_MEMORY);

	frambufferMapper.Detach();

	info.shared_info->frame_buffer_area = info.framebuffer_area;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	{
		uint8* window = info.shared_info->frame_buffer;
		// DCN keeps drawing into the firmware picture. DCE modeset still
		// uses the start of the BAR, which is the pointer it had before.
		if (!radeon_hd_uses_dce(info.chipsetID)) {
			info.shared_info->frame_buffer = window + reservedOffset;
			info.shared_info->frame_buffer_phys
				= (addr_t)((uint64)mapAddr + reservedOffset);
		} else
			info.shared_info->frame_buffer_phys = (addr_t)mapAddr;
		radeon_publish_vram_window(info.shared_info, barPhys, barBytes,
			(uint64)mapAddr, (uint64)mapBytes, reservedOffset, reservedBytes);
	}
// SPDX-SnippetEnd

	// Pass common information to accelerant
	info.shared_info->deviceIndex = info.id;
	info.shared_info->pciID = info.pciID;
	info.shared_info->pciRev = info.pci->revision;
	info.shared_info->chipsetID = info.chipsetID;
	info.shared_info->chipsetFlags = info.chipsetFlags;
	info.shared_info->dceMajor = info.dceMajor;
	info.shared_info->dceMinor = info.dceMinor;
	info.shared_info->displayClass = info.displayClass;
	info.shared_info->displayMajor = info.displayMajor;
	info.shared_info->displayMinor = info.displayMinor;
	info.shared_info->displayStepping = info.displayStepping;
	info.shared_info->registers_area = info.registers_area;
	strlcpy(info.shared_info->deviceName,
		info.deviceName, MAX_NAME_LENGTH);
	strlcpy(info.shared_info->chipsetName,
		radeon_chip_name[info.chipsetID], MAX_NAME_LENGTH);

	// *** AtomBIOS mapping
	// First we try an active bios read
	status_t biosStatus = radeon_hd_getbios(info);

	if (biosStatus != B_OK) {
		// If the active read fails, we try a disabled read
		if (info.chipsetID >= RADEON_CAICOS)
			biosStatus = radeon_hd_getbios_ni(info);
		else if (info.chipsetID >= RADEON_RV770)
			biosStatus = radeon_hd_getbios_r700(info);
		else if (info.chipsetID >= RADEON_R600)
			biosStatus = radeon_hd_getbios_r600(info);
		else if (info.chipsetID >= RADEON_RS600)
			biosStatus = radeon_hd_getbios_avivo(info);
		// else legacy_read_disabled_bios
	}

	if (biosStatus != B_OK) {
		// *** very last resort, shadow bios VGA rom
		ERROR("%s: Can't find an AtomBIOS rom! Trying shadow rom...\n",
			__func__);

		// This works as long as the primary card is what this driver
		// is loaded for. Multiple cards may pose the risk of loading
		// the wrong AtomBIOS for the wrong card.

		uint32 romBase = 0xC0000;
		uint32 romSize = 128 * 1024;
			// what happens when AtomBIOS goes over 128Kb?
			// A Radeon HD 6990 has a 128Kb AtomBIOS

		if (mapAtomBIOS(info, romBase, romSize) == B_OK) {
			ERROR("%s: Found AtomBIOS at VGA shadow rom\n", __func__);
			// Whew!
			info.shared_info->rom_phys = romBase;
			info.shared_info->rom_size = romSize;
			biosStatus = B_OK;
		}
	}

	// Check if a valid AtomBIOS image was found.
	if (biosStatus != B_OK) {
		ERROR("%s: card (%" B_PRId32 "): couldn't find AtomBIOS rom!\n",
			__func__, info.id);
		ERROR("%s: card (%" B_PRId32 "): exiting. Please open a bug ticket"
			" at haiku-os.org with your /var/log/syslog\n",
			__func__, info.id);
		// Fallback to VESA (more likely crash app_server)
		return B_ERROR;
	}

	info.shared_info->has_rom = (biosStatus == B_OK) ? true : false;
	info.shared_info->rom_area = (biosStatus == B_OK) ? info.rom_area : -1;

	// *** Pull active monitor VESA EDID from boot loader
	edid1_info* edidInfo
		= (edid1_info*)get_boot_item(EDID_BOOT_INFO, NULL);

	if (edidInfo != NULL) {
		TRACE("card(%" B_PRId32 "): %s found VESA EDID information.\n",
			info.id, __func__);
		info.shared_info->has_edid = true;
		memcpy(&info.shared_info->edid_info, edidInfo, sizeof(edid1_info));
	} else {
		TRACE("card(%" B_PRId32 "): %s didn't find VESA EDID modes.\n",
			info.id, __func__);
		info.shared_info->has_edid = false;
	}

	TRACE("card(%" B_PRId32 "): %s completed successfully!\n",
		info.id, __func__);

	TRACE("card(%" B_PRId32 "): GPU thermal status: %" B_PRId32 "C\n",
		info.id, radeon_thermal_query(info) / 1000);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	radeon_map_doorbell(info);
	void r5_gfx9_kernel_start(radeon_info& info);
	r5_gfx9_kernel_start(info);
// SPDX-SnippetEnd
	return B_OK;
}


void
radeon_hd_uninit(radeon_info &info)
{
	TRACE("card(%" B_PRId32 "): %s called\n", info.id, __func__);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	void r5_gfx9_kernel_stop();
	r5_gfx9_kernel_stop();
// SPDX-SnippetEnd
	delete_area(info.shared_area);
	delete_area(info.registers_area);
	delete_area(info.framebuffer_area);
	delete_area(info.rom_area);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	if (info.doorbell_area >= 0)
		delete_area(info.doorbell_area);
// SPDX-SnippetEnd
}

