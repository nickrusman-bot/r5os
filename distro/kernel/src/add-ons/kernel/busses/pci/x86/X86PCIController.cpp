/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

/*
 * The R5 change in this file is the SPDX snippet. Copyright 2026 R5OS
 * Contributors. The Haiku copyright and the license above still cover the
 * original work. The snippet is MIT.
 */


#include "X86PCIController.h"

#include <ioapic.h>

#include <AutoDeleterDrivers.h>
#include <util/AutoLock.h>

#include <string.h>
#include <new>

#include "PCI_x86.h"


// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors

struct r5_mcfg_window {
	uint64	address;
	uint16	segment;
	uint8	startBus;
	uint8	endBus;
};

static r5_mcfg_window sExtraSegments[8];
static int sExtraSegmentCount = 0;


void
r5_pci_note_mcfg_segment(uint64 address, uint16 segment, uint8 startBus,
	uint8 endBus)
{
	if (segment == 0 || sExtraSegmentCount >= 8)
		return;

	sExtraSegments[sExtraSegmentCount].address = address;
	sExtraSegments[sExtraSegmentCount].segment = segment;
	sExtraSegments[sExtraSegmentCount].startBus = startBus;
	sExtraSegments[sExtraSegmentCount].endBus = endBus;
	sExtraSegmentCount++;
}


class X86PCIControllerSegment : public X86PCIController {
public:
	explicit X86PCIControllerSegment(int windowIndex)
		:
		fWindow(windowIndex)
	{
	}

	status_t InitDriverInt(device_node* node) override
	{
		CHECK_RET(X86PCIController::InitDriverInt(node));
		if (fWindow < 0 || fWindow >= sExtraSegmentCount)
			return B_BAD_INDEX;

		const r5_mcfg_window& window = sExtraSegments[fWindow];
		fStartBus = window.startBus;
		fEndBus = window.endBus;
		fRegsLen = (uint64(fEndBus) - fStartBus + 1) << 20;
		fRegsArea.SetTo(map_physical_memory("PCI Config MMIO",
			window.address, fRegsLen, B_ANY_KERNEL_ADDRESS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
		return fRegsArea.Get();
	}

	status_t ReadConfig(uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32& value) override
	{
		addr_t address = _Address(bus, device, function, offset);
		if (address == 0)
			return ERANGE;

		switch (size) {
			case 1: value = *(volatile uint8*)address; break;
			case 2: value = *(volatile uint16*)address; break;
			case 4: value = *(volatile uint32*)address; break;
			default:
				return B_BAD_VALUE;
		}
		return B_OK;
	}

	status_t WriteConfig(uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) override
	{
		addr_t address = _Address(bus, device, function, offset);
		if (address == 0)
			return ERANGE;

		switch (size) {
			case 1: *(volatile uint8*)address = value; break;
			case 2: *(volatile uint16*)address = value; break;
			case 4: *(volatile uint32*)address = value; break;
			default:
				return B_BAD_VALUE;
		}
		return B_OK;
	}

	status_t GetMaxBusDevices(int32& count) override
	{
		count = 32;
		return B_OK;
	}

private:
	addr_t _Address(uint8 bus, uint8 device, uint8 function, uint16 offset) const
	{
		if (bus < fStartBus || bus > fEndBus || fRegs == NULL)
			return 0;

		uint32 index = (uint32(bus - fStartBus) << 20)
			| (uint32(device) << 15)
			| (uint32(function) << 12)
			| (offset & 0xfff);
		if ((index & ~3u) + 4 > fRegsLen)
			return 0;
		return (addr_t)fRegs + index;
	}

	int					fWindow;
	AreaDeleter			fRegsArea;
	volatile uint8*		fRegs = NULL;
	uint64				fRegsLen = 0;
	uint8				fStartBus = 0;
	uint8				fEndBus = 0;
};

// SPDX-SnippetEnd


// #pragma mark - driver


float
X86PCIController::SupportsDevice(device_node* parent)
{
	const char* bus;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) < B_OK)
		return -1.0f;

	if (strcmp(bus, "root") == 0)
		return 1.0f;

	return 0.0;
}


status_t
X86PCIController::RegisterDevice(device_node* parent)
{
	// I/O port for PCI config space address
#define PCI_CONFIG_ADDRESS 0xcf8

	io_resource resources[2] = {
		{B_IO_PORT, PCI_CONFIG_ADDRESS, 8},
		{}
	};
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "X86 PCI Host Controller"} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/pci/root/driver_v1"} },
		{}
	};

	return gDeviceManager->register_node(parent, PCI_X86_DRIVER_MODULE_NAME, attrs, resources, NULL);
}


status_t
X86PCIController::InitDriver(device_node* node, X86PCIController*& outDriver)
{
	bool search_mech1 = true;
	bool search_mech2 = true;
	bool search_mechpcie = true;
	void *config = NULL;

	config = load_driver_settings("pci");
	if (config) {
		const char *mech = get_driver_parameter(config, "mechanism", NULL, NULL);
		if (mech) {
			search_mech1 = search_mech2 = search_mechpcie = false;
			if (strcmp(mech, "1") == 0)
				search_mech1 = true;
			else if (strcmp(mech, "2") == 0)
				search_mech2 = true;
			else if (strcmp(mech, "pcie") == 0)
				search_mechpcie = true;
			else
				panic("Unknown pci config mechanism setting %s\n", mech);
		}
		unload_driver_settings(config);
	}

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	uint64 window = 0;
	if (gDeviceManager->get_attr_uint64(node, "r5_pci_segment_window", &window,
			false) == B_OK) {
		return CreateDriver(node, new(std::nothrow) X86PCIControllerSegment(
			(int)window), outDriver);
	}
	// SPDX-SnippetEnd

	// PCI configuration mechanism PCIe is the preferred one.
	// If it doesn't work, try mechanism 1.
	// If it doesn't work, try mechanism 2.

	if (search_mechpcie) {
		if (CreateDriver(node, new(std::nothrow) X86PCIControllerMethPcie(), outDriver) >= B_OK)
			return B_OK;
	}
	if (search_mech1) {
		if (CreateDriver(node, new(std::nothrow) X86PCIControllerMeth1(), outDriver) >= B_OK)
			return B_OK;
	}
	if (search_mech2) {
		if (CreateDriver(node, new(std::nothrow) X86PCIControllerMeth2(), outDriver) >= B_OK)
			return B_OK;
	}

	dprintf("PCI: no configuration mechanism found\n");
	return B_ERROR;
}


status_t
X86PCIController::CreateDriver(device_node* node, X86PCIController* driverIn,
	X86PCIController*& driverOut)
{
	ObjectDeleter<X86PCIController> driver(driverIn);
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	driverOut = driver.Detach();
	return B_OK;
}


status_t
X86PCIController::InitDriverInt(device_node* node)
{
	fNode = node;
	return B_OK;
}


void
X86PCIController::UninitDriver()
{
	delete this;
}


//#pragma mark - PCI controller


status_t
X86PCIController::ReadIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8& irq)
{
	return B_UNSUPPORTED;
}


status_t
X86PCIController::WriteIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}


status_t
X86PCIController::GetRange(uint32 index, pci_resource_range* range)
{
	return B_BAD_INDEX;
}


status_t
X86PCIController::Finalize()
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	static bool sSegmentsRegistered = false;
	if (sSegmentsRegistered)
		return B_OK;
	sSegmentsRegistered = true;
	// SPDX-SnippetEnd

	ioapic_routing_init();

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	RegisterExtraSegments();
	// SPDX-SnippetEnd
	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors

void
X86PCIController::RegisterExtraSegments()
{
	if (sExtraSegmentCount == 0 || fNode == NULL)
		return;

	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == NULL)
		return;

	for (int i = 0; i < sExtraSegmentCount; i++) {
		device_attr attrs[] = {
			{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
				{.string = "X86 PCI Host Controller"} },
			{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE,
				{.string = "bus_managers/pci/root/driver_v1"} },
			{ "r5_pci_segment_window", B_UINT64_TYPE, {.ui64 = (uint64)i} },
			{}
		};
		gDeviceManager->register_node(parent, PCI_X86_DRIVER_MODULE_NAME,
			attrs, NULL, NULL);
	}

	gDeviceManager->put_node(parent);
}

// SPDX-SnippetEnd


//#pragma mark - X86PCIControllerMeth1


status_t
X86PCIControllerMeth1::InitDriverInt(device_node* node)
{
	CHECK_RET(X86PCIController::InitDriverInt(node));

	// check for mechanism 1
	out32(0x80000000, PCI_MECH1_REQ_PORT);
	if (0x80000000 == in32(PCI_MECH1_REQ_PORT)) {
		dprintf("PCI: mechanism 1 controller found\n");
		return B_OK;
	}
	return B_ERROR;
}


status_t
X86PCIControllerMeth1::ReadConfig(
	uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 &value)
{
	if (offset > 0xff)
		return ERANGE;

	InterruptsSpinLocker lock(fLock);
	out32(PCI_MECH1_REQ_DATA(bus, device, function, offset), PCI_MECH1_REQ_PORT);
	switch (size) {
		case 1:
			value = in8(PCI_MECH1_DATA_PORT + (offset & 3));
			break;
		case 2:
			value = in16(PCI_MECH1_DATA_PORT + (offset & 3));
			break;
		case 4:
			value = in32(PCI_MECH1_DATA_PORT);
			break;
		default:
			return B_BAD_VALUE;
	}

	return B_OK;
}


status_t
X86PCIControllerMeth1::WriteConfig(
	uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	if (offset > 0xff)
		return ERANGE;

	InterruptsSpinLocker lock(fLock);
	out32(PCI_MECH1_REQ_DATA(bus, device, function, offset), PCI_MECH1_REQ_PORT);
	switch (size) {
		case 1:
			out8(value, PCI_MECH1_DATA_PORT + (offset & 3));
			break;
		case 2:
			out16(value, PCI_MECH1_DATA_PORT + (offset & 3));
			break;
		case 4:
			out32(value, PCI_MECH1_DATA_PORT);
			break;
		default:
			return B_BAD_VALUE;
	}

	return B_OK;
}


status_t
X86PCIControllerMeth1::GetMaxBusDevices(int32& count)
{
	count = 32;
	return B_OK;
}


// #pragma mark - X86PCIControllerMeth2


status_t
X86PCIControllerMeth2::InitDriverInt(device_node* node)
{
	CHECK_RET(X86PCIController::InitDriverInt(node));

	// check for mechanism 2
	out8(0x00, 0xCFB);
	out8(0x00, 0xCF8);
	out8(0x00, 0xCFA);
	if (in8(0xCF8) == 0x00 && in8(0xCFA) == 0x00) {
		dprintf("PCI: mechanism 2 controller found\n");
		return B_OK;
	}
	return B_ERROR;
}


status_t
X86PCIControllerMeth2::ReadConfig(
	uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 &value)
{
	if (offset > 0xff)
		return ERANGE;

	InterruptsSpinLocker lock(fLock);
	out8((uint8)(0xf0 | (function << 1)), PCI_MECH2_ENABLE_PORT);
	out8(bus, PCI_MECH2_FORWARD_PORT);
	switch (size) {
		case 1:
			value = in8(PCI_MECH2_CONFIG_PORT(device, offset));
			break;
		case 2:
			value = in16(PCI_MECH2_CONFIG_PORT(device, offset));
			break;
		case 4:
			value = in32(PCI_MECH2_CONFIG_PORT(device, offset));
			break;
		default:
			return B_BAD_VALUE;
	}
	out8(0, PCI_MECH2_ENABLE_PORT);

	return B_OK;
}


status_t
X86PCIControllerMeth2::WriteConfig(
	uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	if (offset > 0xff)
		return ERANGE;

	InterruptsSpinLocker lock(fLock);
	out8((uint8)(0xf0 | (function << 1)), PCI_MECH2_ENABLE_PORT);
	out8(bus, PCI_MECH2_FORWARD_PORT);
	switch (size) {
		case 1:
			out8(value, PCI_MECH2_CONFIG_PORT(device, offset));
			break;
		case 2:
			out16(value, PCI_MECH2_CONFIG_PORT(device, offset));
			break;
		case 4:
			out32(value, PCI_MECH2_CONFIG_PORT(device, offset));
			break;
		default:
			return B_BAD_VALUE;
	}
	out8(0, PCI_MECH2_ENABLE_PORT);

	return B_OK;
}


status_t
X86PCIControllerMeth2::GetMaxBusDevices(int32& count)
{
	count = 16;
	return B_OK;
}


// #pragma mark - X86PCIControllerMethPcie


status_t
X86PCIControllerMethPcie::InitDriverInt(device_node* node)
{
	status_t status = X86PCIController::InitDriverInt(node);
	if (status != B_OK)
		return status;

	// search ACPI
	device_node *acpiNode = NULL;
	{
		device_node* deviceRoot = gDeviceManager->get_root_node();
		device_attr acpiAttrs[] = {
			{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" }},
			{ ACPI_DEVICE_HID_ITEM, B_STRING_TYPE, { .string = "PNP0A08" }},
			{ NULL }
		};
		if (gDeviceManager->find_child_node(deviceRoot, acpiAttrs, &acpiNode) != B_OK)
			return ENODEV;
	}

	status = fECAMPCIController.ReadResourceInfo(acpiNode);

	for (int i = 0;; i++) {
		pci_resource_range resource;
		if (fECAMPCIController.GetRange(i, &resource) != B_OK)
			break;

		// Areas in the lower part of the address space are special
		// (BIOS ROM, VGA, etc.) and should not be touched.
		if (resource.type == B_IO_MEMORY && resource.pci_address < 0x100000)
			continue;

		fResourceRanges.Add(resource);
	}

	gDeviceManager->put_node(acpiNode);
	return status;
}


status_t
X86PCIControllerMethPcie::ReadConfig(
	uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 &value)
{
	// fallback to mechanism 1 for out of range busses
	if (bus < fECAMPCIController.fStartBusNumber || bus > fECAMPCIController.fEndBusNumber) {
		return X86PCIControllerMeth1::ReadConfig(bus, device, function, offset,
			size, value);
	}

	return fECAMPCIController.ReadConfig(bus, device, function, offset, size, value);
}


status_t
X86PCIControllerMethPcie::WriteConfig(
	uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	// fallback to mechanism 1 for out of range busses
	if (bus < fECAMPCIController.fStartBusNumber || bus > fECAMPCIController.fEndBusNumber) {
		return X86PCIControllerMeth1::WriteConfig(bus, device, function, offset,
			size, value);
	}

	return fECAMPCIController.WriteConfig(bus, device, function, offset, size, value);
}


status_t
X86PCIControllerMethPcie::GetMaxBusDevices(int32& count)
{
	return fECAMPCIController.GetMaxBusDevices(count);
}


status_t
X86PCIControllerMethPcie::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= (uint32)fResourceRanges.Count())
		return B_BAD_INDEX;

	*range = fResourceRanges[index];
	return B_OK;
}
