/*
 * Copyright 2008-2026 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Pieter Panman
 *		Leo Rouleau
 *
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * DeviceModel::_AddNode() follows DevicesView::AddDeviceAndChildren() from
 * Haiku's src/apps/devices/DevicesView.cpp. The grouping, driver lookup and
 * status are Device Manager (R5), also MIT.
 */


#include "DeviceModel.h"

#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <USBKit.h>

#include <bus/PCI.h>
#include <bus/USB.h>
#include <device_manager.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DeviceACPI.h"
#include "DevicePCI.h"
#include "DeviceSCSI.h"
#include "DeviceUSB.h"
#include "DriverUtils.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeviceModel"


static const char* kDriverUsed = "Driver used";
static const char* kDevicePaths = "Device paths";


// #pragma mark - DeviceRecord


DeviceRecord::DeviceRecord()
	:
	device(NULL),
	parent(NULL),
	cookie(0),
	group(DM_GROUP_OTHER),
	status(DM_STATUS_NOT_REPORTED),
	driverKind(DM_DRIVER_NONE),
	hidden(false),
	internal(false),
	critical(false),
	plumbing(false)
{
}


DeviceRecord::~DeviceRecord()
{
	delete device;
}


BString
DeviceRecord::RawValue(const char* attrName) const
{
	for (size_t i = 0; i < raw.size(); i++) {
		if (raw[i].fName == attrName)
			return raw[i].fValue;
	}
	return BString();
}


bool
DeviceRecord::HasDriver() const
{
	return driverKind == DM_DRIVER_NODE || driverKind == DM_DRIVER_LEGACY
		|| driverKind == DM_DRIVER_GRAPHICS || driverKind == DM_DRIVER_SYSTEM;
}


bool
DeviceRecord::CanToggleDriver() const
{
	if (driverPath.IsEmpty() || critical || driverKind == DM_DRIVER_SYSTEM)
		return false;
	return DriverUtils::IsPackagedDriver(driverPath);
}


// #pragma mark - helpers


static bool
is_driver_module_path(const BString& path)
{
	return path.FindFirst("/add-ons/kernel/drivers/") >= 0;
}


static bool
is_kernel_module_path(const BString& path)
{
	return path.FindFirst("/add-ons/kernel/") >= 0;
}


// From DeviceUSB.cpp (Haiku, MIT): the /dev/bus/usb entry of a device.
static BString
find_usb_device_path(const char* currentPath, uint16 vendor, uint16 product)
{
	BDirectory dir(currentPath);
	if (dir.InitCheck() != B_OK)
		return "";

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath pathObj;
		entry.GetPath(&pathObj);

		if (BString(pathObj.Leaf()) == "hub")
			continue;

		if (entry.IsDirectory()) {
			BString result = find_usb_device_path(pathObj.Path(), vendor,
				product);
			if (result != "")
				return result;
		} else {
			BUSBDevice testDevice(pathObj.Path());
			if (testDevice.InitCheck() == B_OK
				&& testDevice.VendorID() == vendor
				&& testDevice.ProductID() == product) {
				return BString(pathObj.Path());
			}
		}
	}
	return "";
}


// A USB device with class 0 says what it is per interface. Returns the
// first interface class of the first configuration, or 0.
static uint8
usb_interface_class(uint16 vendor, uint16 product)
{
	BString path = find_usb_device_path("/dev/bus/usb", vendor, product);
	if (path.IsEmpty())
		return 0;
	BUSBDevice device(path.String());
	if (device.InitCheck() != B_OK || device.CountConfigurations() == 0)
		return 0;
	const BUSBConfiguration* config = device.ConfigurationAt(0);
	if (config == NULL)
		return 0;
	for (uint32 i = 0; i < config->CountInterfaces(); i++) {
		const BUSBInterface* interface = config->InterfaceAt(i);
		if (interface != NULL && interface->Class() != 0)
			return interface->Class();
	}
	return 0;
}


static int32
group_for_usb_class(uint8 usbClass, bool hasDriver)
{
	switch (usbClass) {
		case 0x01:
			return DM_GROUP_SOUND;
		case 0x02:
		case 0x0a:
		case 0xe0:
			return DM_GROUP_NETWORK;
		case 0x03:
			return DM_GROUP_HID;
		case 0x08:
			return DM_GROUP_STORAGE;
		case 0x09:
			return DM_GROUP_USB;
		case 0x0e:
			return DM_GROUP_CAMERAS;
	}
	return hasDriver ? DM_GROUP_OTHER_SYSTEM : DM_GROUP_OTHER;
}


static int32
group_for_category(Category category, uint32 subType)
{
	switch (category) {
		case CAT_MASS:
			return DM_GROUP_STORAGE;
		case CAT_NETWORK:
		case CAT_WIRELESS:
			return DM_GROUP_NETWORK;
		case CAT_DISPLAY:
			return DM_GROUP_DISPLAY;
		case CAT_MULTIMEDIA:
			return DM_GROUP_SOUND;
		case CAT_INPUT:
			return DM_GROUP_HID;
		case CAT_SERIAL:
			// PCI serial-bus subclass 0x03 is a USB controller.
			return subType == 0x03 ? DM_GROUP_USB : DM_GROUP_OTHER_SYSTEM;
		case CAT_CPU:
			return DM_GROUP_PROCESSORS;
		case CAT_BUS:
		case CAT_MEMORY:
		case CAT_GENERIC:
		case CAT_ACPI:
		case CAT_COMPUTER:
		case CAT_INTEL:
		case CAT_DOCK:
			return DM_GROUP_SYSTEM;
		case CAT_COMM:
		case CAT_SATELLITE:
		case CAT_CRYPTO:
		case CAT_SIGNAL:
			return DM_GROUP_OTHER_SYSTEM;
		case CAT_NONE:
		default:
			return DM_GROUP_OTHER;
	}
}


// Groups where a device without a driver does not work. Those get the
// warning, as in Windows; everything else stays neutral.
static bool
group_needs_driver(int32 group)
{
	return group == DM_GROUP_DISPLAY || group == DM_GROUP_NETWORK
		|| group == DM_GROUP_SOUND || group == DM_GROUP_STORAGE
		|| group == DM_GROUP_USB || group == DM_GROUP_HID;
}


// #pragma mark - DeviceModel


DeviceModel::DeviceModel()
	:
	fRoot(NULL)
{
}


DeviceModel::~DeviceModel()
{
	_Clear();
}


void
DeviceModel::_Clear()
{
	for (size_t i = 0; i < fRecords.size(); i++)
		delete fRecords[i];
	fRecords.clear();
	fRoot = NULL;
}


status_t
DeviceModel::Scan()
{
	_Clear();

	device_node_cookie rootCookie;
	status_t status = get_root(&rootCookie);
	if (status != B_OK)
		return status;

	_AddNode(&rootCookie, NULL);
	if (fRecords.empty())
		return B_ERROR;
	fRoot = fRecords[0];

	_FindDrivers();
	_FindBusDrivers();
	_MatchGraphics();
	for (size_t i = 0; i < fRecords.size(); i++) {
		_Classify(fRecords[i]);
		_SetStatus(fRecords[i]);
	}
	return B_OK;
}


int32
DeviceModel::CountRecords() const
{
	return (int32)fRecords.size();
}


DeviceRecord*
DeviceModel::RecordAt(int32 index) const
{
	if (index < 0 || index >= (int32)fRecords.size())
		return NULL;
	return fRecords[index];
}


DeviceRecord*
DeviceModel::Root() const
{
	return fRoot;
}


DeviceRecord*
DeviceModel::FindByCookie(uint32 cookie) const
{
	for (size_t i = 0; i < fRecords.size(); i++) {
		if (fRecords[i]->cookie == cookie)
			return fRecords[i];
	}
	return NULL;
}


void
DeviceModel::_AddNode(device_node_cookie* node, DeviceRecord* parent)
{
	DeviceRecord* record = new DeviceRecord;
	record->parent = parent;
	record->cookie = (uint32)*node;
	if (parent != NULL)
		parent->children.push_back(record);
	fRecords.push_back(record);

	// Copy all attributes; the device manager hands them out only once.
	char data[256];
	struct device_attr_info attr;
	attr.cookie = 0;
	attr.node_cookie = *node;
	attr.value.raw.data = data;
	attr.value.raw.length = sizeof(data);

	Attributes& attributes = record->raw;
	while (dm_get_next_attr(&attr) == B_OK) {
		BString value;
		switch (attr.type) {
			case B_STRING_TYPE:
				value << attr.value.string;
				break;
			case B_UINT8_TYPE:
				value << attr.value.ui8;
				break;
			case B_UINT16_TYPE:
				value << attr.value.ui16;
				break;
			case B_UINT32_TYPE:
				value << attr.value.ui32;
				break;
			case B_UINT64_TYPE:
				value << attr.value.ui64;
				break;
			case B_RAW_TYPE:
			{
				const uint8* bytes = (const uint8*)attr.value.raw.data;
				for (size_t i = 0; i < attr.value.raw.length; i++) {
					char hex[4];
					snprintf(hex, sizeof(hex), "%02x ", bytes[i]);
					value << hex;
				}
				value.Trim();
				break;
			}
			default:
				value << "Unknown type";
		}
		attributes.push_back(Attribute(attr.name, value));

		attr.value.raw.data = data;
		attr.value.raw.length = sizeof(data);
	}

	Device* parentDevice = parent != NULL ? parent->device : NULL;
	Device* newDevice = NULL;

	// Same detection order as Haiku's Devices.
	for (unsigned int i = 0; i < attributes.size(); i++) {
		const BString& name = attributes[i].fName;
		const BString& value = attributes[i].fValue;
		if (name == B_DEVICE_PRETTY_NAME && value == "Devices Root") {
			newDevice = new Device(parentDevice, BUS_NONE, CAT_COMPUTER,
				B_TRANSLATE("Computer"));
			break;
		}
		if (name == B_DEVICE_PRETTY_NAME && value == "ACPI") {
			newDevice = new Device(parentDevice, BUS_ACPI, CAT_BUS,
				B_TRANSLATE("ACPI bus"));
			break;
		}
		if (name == B_DEVICE_PRETTY_NAME && value == "PCI") {
			newDevice = new Device(parentDevice, BUS_PCI, CAT_BUS,
				B_TRANSLATE("PCI bus"));
			break;
		}
		if (name == B_DEVICE_BUS && value == "isa") {
			newDevice = new Device(parentDevice, BUS_ISA, CAT_BUS,
				B_TRANSLATE("ISA bus"));
			break;
		}
		if (name == B_DEVICE_PRETTY_NAME && value == "USB") {
			newDevice = new Device(parentDevice, BUS_USB, CAT_BUS,
				B_TRANSLATE("USB bus"));
			break;
		}
		if (name == B_DEVICE_BUS && value == "pci") {
			newDevice = new DevicePCI(parentDevice);
			break;
		}
		if (name == B_DEVICE_BUS && value == "acpi") {
			newDevice = new DeviceACPI(parentDevice);
			break;
		}
		if (name == B_DEVICE_BUS && value == "usb") {
			newDevice = new DeviceUSB(parentDevice);
			break;
		}
		if (name == "controller_name") {
			newDevice = new Device(parentDevice, BUS_PCI, CAT_MASS, value);
			break;
		}
		if (name == B_DEVICE_BUS && value == "scsi") {
			newDevice = new DeviceSCSI(parentDevice);
			break;
		}
		if (name == B_DEVICE_PRETTY_NAME) {
			newDevice = new Device(parentDevice, BUS_NONE, CAT_NONE, value);
			break;
		}
	}
	if (newDevice == NULL) {
		newDevice = new Device(parentDevice, BUS_NONE, CAT_NONE,
			B_TRANSLATE("Unknown device"));
	}
	newDevice->SetNodeCookie(*node);
	record->device = newDevice;

	struct device_attr_info driverAttr;
	memset(&driverAttr, 0, sizeof(driverAttr));
	driverAttr.node_cookie = *node;
	if (dm_get_driver_path(&driverAttr) == B_OK)
		record->modulePath = driverAttr.value.string;

	BString paths;
	for (unsigned int i = 0; i < attributes.size(); i++) {
		if (attributes[i].fName == B_DEVICE_PUBLISHED_PATH) {
			if (!paths.IsEmpty())
				paths << ", ";
			paths << attributes[i].fValue;
			continue;
		}
		newDevice->SetAttribute(attributes[i].fName, attributes[i].fValue);
	}
	record->devicePaths = paths;
	newDevice->SetAttribute(kDevicePaths, paths.IsEmpty() ? "none" : paths.String());
	newDevice->SetAttribute(kDriverUsed, record->modulePath.IsEmpty()
		? "unknown" : record->modulePath.String());
	newDevice->InitFromAttributes();

	record->name = newDevice->GetName();
	if (record->name.IsEmpty() || record->name == "unknown")
		record->name = newDevice->Text();
	// A device missing from the ID lists: say what kind it is instead.
	if (record->name.IsEmpty() || record->name.StartsWith("unknown")) {
		BString kind = newDevice->GetAttribute("Class info").fValue;
		if (kind.IsEmpty() || kind.StartsWith("unknown"))
			kind = B_TRANSLATE("Unknown device");
		record->name = kind;
	}
	record->manufacturer = newDevice->GetManufacturer();
	if (record->manufacturer == "unknown")
		record->manufacturer = "";

	device_node_cookie child = *node;
	if (get_child(&child) != B_OK)
		return;
	do {
		_AddNode(&child, record);
	} while (get_next_child(&child) == B_OK);
}


void
DeviceModel::_FindDrivers()
{
	for (size_t i = 0; i < fRecords.size(); i++) {
		DeviceRecord* record = fRecords[i];
		BString bus = record->RawValue(B_DEVICE_BUS);

		// A legacy driver reserved the parent PCI device.
		if (bus == "legacy_driver") {
			record->internal = true;
			record->hidden = true;
			BString legacy = record->RawValue("legacy_driver");
			DeviceRecord* owner = record->parent;
			// Two nodes: a reservation, and under it the driver's own node.
			while (owner != NULL && owner->RawValue(B_DEVICE_BUS) == "legacy_driver")
				owner = owner->parent;
			if (!legacy.IsEmpty() && owner != NULL
				&& owner->driverKind == DM_DRIVER_NONE) {
				owner->driverKind = DM_DRIVER_LEGACY;
				owner->driverName = legacy;
				owner->driverPath = _DriverFile(legacy);
			}
			continue;
		}

		if (is_driver_module_path(record->modulePath)) {
			// The parent reports the driver of its child; a node whose
			// path matches its parent is that driver, not a device of its own.
			if (record->parent != NULL
				&& record->parent->modulePath == record->modulePath) {
				if (record->modulePath.FindFirst("/drivers/disk/") >= 0)
					record->group = DM_GROUP_DISK_DRIVES;
				else {
					record->internal = true;
					record->hidden = true;
				}
			}
			if (record->driverKind == DM_DRIVER_NONE) {
				record->driverKind = DM_DRIVER_NODE;
				record->driverPath = record->modulePath;
				BPath path(record->modulePath.String());
				record->driverName = path.Leaf();
			}
			continue;
		}

		Category category = record->device->GetCategory();
		if (is_kernel_module_path(record->modulePath)
			&& (category == CAT_BUS || category == CAT_COMPUTER
				|| record->device->GetBusType() == BUS_ACPI
				|| dynamic_cast<DeviceACPI*>(record->device) != NULL)) {
			// A bus, the root, or an ACPI node: the system module serves it.
			if (record->driverKind == DM_DRIVER_NONE) {
				record->driverKind = DM_DRIVER_SYSTEM;
				record->driverPath = record->modulePath;
				BPath path(record->modulePath.String());
				record->driverName = path.Leaf();
			}
		}
	}
}


// A node that is not a device of its own: a plain Device with no category,
// such as a bus driver's node, an ATA channel or a virtio device.
static bool
is_plumbing(const DeviceRecord* record)
{
	Device* device = record->device;
	if (dynamic_cast<DevicePCI*>(device) != NULL
		|| dynamic_cast<DeviceUSB*>(device) != NULL
		|| dynamic_cast<DeviceACPI*>(device) != NULL
		|| dynamic_cast<DeviceSCSI*>(device) != NULL)
		return false;
	return device->GetCategory() == CAT_NONE
		&& record->RawValue(B_DEVICE_BUS) != "legacy_driver"
		&& record->group != DM_GROUP_DISK_DRIVES;
}


// Searches the plumbing under a device for its driver: the first module
// under drivers/, else the first other kernel module. Stops at real devices.
static void
find_bus_driver(DeviceRecord* record, BString& driverPath, BString& busPath)
{
	for (size_t i = 0; i < record->children.size(); i++) {
		DeviceRecord* child = record->children[i];
		if (!is_plumbing(child))
			continue;
		child->plumbing = true;
		child->hidden = true;
		if (driverPath.IsEmpty() && is_driver_module_path(child->modulePath))
			driverPath = child->modulePath;
		if (busPath.IsEmpty() && is_kernel_module_path(child->modulePath)
			&& child->modulePath != record->modulePath)
			busPath = child->modulePath;
		find_bus_driver(child, driverPath, busPath);
	}
}


// Many controllers are served by a bus driver (busses/ata, busses/usb,
// busses/virtio) with its own node below the PCI device, and the function
// driver further down. The kernel's driver path only looks one level down
// for drivers/, so look through those nodes here.
void
DeviceModel::_FindBusDrivers()
{
	for (size_t i = 0; i < fRecords.size(); i++) {
		DeviceRecord* record = fRecords[i];
		if (dynamic_cast<DevicePCI*>(record->device) == NULL
			&& dynamic_cast<DeviceUSB*>(record->device) == NULL)
			continue;
		BString driverPath;
		BString busPath;
		find_bus_driver(record, driverPath, busPath);
		if (record->driverKind != DM_DRIVER_NONE)
			continue;
		BString path = driverPath.IsEmpty() ? busPath : driverPath;
		if (path.IsEmpty())
			continue;
		record->driverKind = driverPath.IsEmpty() ? DM_DRIVER_SYSTEM : DM_DRIVER_NODE;
		record->driverPath = path;
		BPath leaf(path.String());
		record->driverName = leaf.Leaf();
	}
}


// Graphics drivers do not reserve their PCI device. Most name their
// /dev/graphics entry "<driver>_BBDDFF" (bus, device, function in hex).
void
DeviceModel::_MatchGraphics()
{
	BDirectory dir("/dev/graphics");
	if (dir.InitCheck() != B_OK)
		return;

	BString genericDriver;
	char name[B_FILE_NAME_LENGTH];
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.GetName(name) != B_OK)
			continue;
		BString entryName(name);
		if (entryName == "vesa" || entryName == "framebuffer") {
			genericDriver = entryName;
			continue;
		}
		int32 underscore = entryName.FindLast('_');
		if (underscore <= 0 || entryName.Length() - underscore - 1 != 6)
			continue;
		BString prefix;
		entryName.CopyInto(prefix, 0, underscore);
		unsigned int bus = 0, device = 0, function = 0;
		if (sscanf(entryName.String() + underscore + 1, "%2x%2x%2x", &bus,
				&device, &function) != 3) {
			continue;
		}
		for (size_t i = 0; i < fRecords.size(); i++) {
			DeviceRecord* record = fRecords[i];
			if (dynamic_cast<DevicePCI*>(record->device) == NULL
				|| record->driverKind != DM_DRIVER_NONE)
				continue;
			if ((unsigned int)atoi(record->RawValue(B_PCI_DEVICE_BUS)) != bus
				|| (unsigned int)atoi(record->RawValue(B_PCI_DEVICE_DEVICE)) != device
				|| (unsigned int)atoi(record->RawValue(B_PCI_DEVICE_FUNCTION))
					!= function) {
				continue;
			}
			record->driverKind = DM_DRIVER_GRAPHICS;
			// "%04X_%04X_..." names carry IDs, not a driver name.
			bool hexPrefix = prefix.Length() == 9 && prefix[4] == '_';
			if (!hexPrefix) {
				record->driverName = prefix;
				record->driverPath = _DriverFile(prefix);
			}
		}
	}

	if (genericDriver.IsEmpty())
		return;

	// The boot display driver serves one display controller.
	DeviceRecord* only = NULL;
	int32 count = 0;
	for (size_t i = 0; i < fRecords.size(); i++) {
		DeviceRecord* record = fRecords[i];
		if (dynamic_cast<DevicePCI*>(record->device) == NULL
			|| record->device->GetCategory() != CAT_DISPLAY)
			continue;
		if (record->driverKind != DM_DRIVER_NONE)
			return;
		only = record;
		count++;
	}
	if (count == 1) {
		only->driverKind = DM_DRIVER_GRAPHICS;
		only->driverName = genericDriver;
		only->driverPath = _DriverFile(genericDriver);
	}
}


/*static*/ BString
DeviceModel::_DriverFile(const BString& name)
{
	static const directory_which kWhere[] = {
		B_SYSTEM_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_USER_NONPACKAGED_ADDONS_DIRECTORY
	};
	for (size_t i = 0; i < sizeof(kWhere) / sizeof(kWhere[0]); i++) {
		BPath path;
		if (find_directory(kWhere[i], &path) != B_OK)
			continue;
		path.Append("kernel/drivers/bin");
		path.Append(name.String());
		BEntry entry(path.Path());
		if (entry.Exists())
			return path.Path();
	}
	return BString();
}


void
DeviceModel::_Classify(DeviceRecord* record)
{
	Device* device = record->device;
	Category category = device->GetCategory();

	if (record == fRoot) {
		record->group = DM_GROUP_COMPUTER;
		return;
	}
	if (record->group == DM_GROUP_DISK_DRIVES)
		return;

	// A plain node with no category that the bus search did not reach (for
	// example the SCSI bus under an ATA channel) is plumbing of its parent.
	// Directly under the computer it is a system device.
	if (!record->plumbing && is_plumbing(record) && record->parent != NULL) {
		if (record->parent == fRoot) {
			record->group = DM_GROUP_SYSTEM;
			return;
		}
		record->plumbing = true;
		record->hidden = true;
	}

	if (record->plumbing) {
		DeviceRecord* owner = record->parent;
		while (owner != NULL && owner->plumbing)
			owner = owner->parent;
		record->group = owner != NULL ? owner->group : DM_GROUP_SYSTEM;
		record->location.SetToFormat(B_TRANSLATE("On %s"),
			record->parent != NULL ? record->parent->name.String() : "");
		return;
	}

	if (dynamic_cast<DeviceSCSI*>(device) != NULL) {
		record->group = DM_GROUP_DISK_DRIVES;
	} else if (dynamic_cast<DeviceUSB*>(device) != NULL) {
		uint8 usbClass = (uint8)atoi(record->RawValue(USB_DEVICE_CLASS));
		if (usbClass == 0) {
			usbClass = usb_interface_class(
				(uint16)atoi(record->RawValue(B_DEVICE_VENDOR_ID)),
				(uint16)atoi(record->RawValue(B_DEVICE_ID)));
		}
		record->group = group_for_usb_class(usbClass, record->HasDriver());
	} else if (dynamic_cast<DeviceACPI*>(device) != NULL) {
		record->group = record->name.StartsWith("ACPI processor")
			? DM_GROUP_PROCESSORS : DM_GROUP_SYSTEM;
		// Unnamed ACPI namespace nodes are noise next to the real devices.
		if (record->name.StartsWith("ACPI node '")
			|| record->name.StartsWith("ACPI <unknown>")) {
			record->hidden = true;
		}
	} else {
		uint32 subType = (uint32)atoi(record->RawValue(B_DEVICE_SUB_TYPE));
		record->group = group_for_category(category, subType);
	}

	if (record->name == "Unknown device")
		record->hidden = true;

	// A device with no class that has a driver is a working system part
	// (for example a random number generator), not an unknown device.
	if (record->group == DM_GROUP_OTHER && record->HasDriver())
		record->group = DM_GROUP_OTHER_SYSTEM;

	// Location, in words.
	BString pciBus = record->RawValue(B_PCI_DEVICE_BUS);
	if (dynamic_cast<DevicePCI*>(device) != NULL && !pciBus.IsEmpty()) {
		record->location.SetToFormat(B_TRANSLATE("PCI bus %s, device %s, function %s"),
			pciBus.String(), record->RawValue(B_PCI_DEVICE_DEVICE).String(),
			record->RawValue(B_PCI_DEVICE_FUNCTION).String());
	} else if (!record->RawValue("acpi/path").IsEmpty()) {
		record->location = record->RawValue("acpi/path");
	} else if (record->parent != NULL) {
		record->location.SetToFormat(B_TRANSLATE("On %s"),
			record->parent->name.String());
	}

	// Same check as Haiku's Devices, with the driver Device Manager found.
	if (!record->driverPath.IsEmpty())
		device->SetAttribute(kDriverUsed, record->driverPath);
	record->critical = DriverUtils::IsCriticalDriver(device);
}


void
DeviceModel::_SetStatus(DeviceRecord* record)
{
	if (record->HasDriver()) {
		record->status = DM_STATUS_WORKING;
		if (!record->driverPath.IsEmpty()
			&& !DriverUtils::IsDriverEnabled(record->driverPath)) {
			record->status = DM_STATUS_OFF_AFTER_RESTART;
		}
		return;
	}

	if (dynamic_cast<DevicePCI*>(record->device) != NULL
		&& group_needs_driver(record->group)) {
		record->status = DM_STATUS_NO_DRIVER;
		record->group = DM_GROUP_OTHER;
		return;
	}
	record->status = DM_STATUS_NOT_REPORTED;
}


static void
add_signature(device_node_cookie* node, uint32& hash, int32 depth)
{
	hash = hash * 31 + (uint32)*node + (uint32)depth;
	device_node_cookie child = *node;
	if (get_child(&child) != B_OK)
		return;
	do {
		add_signature(&child, hash, depth + 1);
	} while (get_next_child(&child) == B_OK);
}


/*static*/ uint32
DeviceModel::TreeSignature()
{
	device_node_cookie root;
	if (get_root(&root) != B_OK)
		return 0;
	uint32 hash = 17;
	add_signature(&root, hash, 0);
	return hash;
}


/*static*/ const char*
DeviceModel::GroupName(int32 group)
{
	switch (group) {
		case DM_GROUP_COMPUTER:
			return B_TRANSLATE("Computer");
		case DM_GROUP_CAMERAS:
			return B_TRANSLATE("Cameras");
		case DM_GROUP_DISK_DRIVES:
			return B_TRANSLATE("Disk drives");
		case DM_GROUP_DISPLAY:
			return B_TRANSLATE("Display adapters");
		case DM_GROUP_HID:
			return B_TRANSLATE("Human interface devices");
		case DM_GROUP_NETWORK:
			return B_TRANSLATE("Network adapters");
		case DM_GROUP_OTHER:
			return B_TRANSLATE("Other devices");
		case DM_GROUP_PROCESSORS:
			return B_TRANSLATE("Processors");
		case DM_GROUP_SOUND:
			return B_TRANSLATE("Sound, video and game controllers");
		case DM_GROUP_STORAGE:
			return B_TRANSLATE("Storage controllers");
		case DM_GROUP_SYSTEM:
			return B_TRANSLATE("System devices");
		case DM_GROUP_OTHER_SYSTEM:
			return B_TRANSLATE("Other system devices");
		case DM_GROUP_USB:
			return B_TRANSLATE("Universal Serial Bus controllers");
	}
	return B_TRANSLATE("Other devices");
}


/*static*/ const char*
DeviceModel::GroupIconSignature(int32 group)
{
	switch (group) {
		case DM_GROUP_CAMERAS:
			return "application/x-vnd.Haiku-CodyCam";
		case DM_GROUP_DISK_DRIVES:
			return "application/x-vnd.Be-volume";
		case DM_GROUP_DISPLAY:
			return "application/x-vnd.Haiku-Screen";
		case DM_GROUP_HID:
			return "application/x-vnd.Haiku-Input";
		case DM_GROUP_NETWORK:
			return "application/x-vnd.Haiku-Network";
		case DM_GROUP_PROCESSORS:
			return "application/x-vnd.Haiku-ProcessController";
		case DM_GROUP_SOUND:
			return "application/x-vnd.Haiku-Media";
		case DM_GROUP_STORAGE:
			return "application/x-vnd.Haiku-DriveSetup";
	}
	return "application/x-vnd.R5-DeviceManager";
}


/*static*/ const char*
DeviceModel::StatusSentence(const DeviceRecord* record)
{
	switch (record->status) {
		case DM_STATUS_WORKING:
			return B_TRANSLATE("This device is working properly.");
		case DM_STATUS_OFF_AFTER_RESTART:
			return B_TRANSLATE("The driver for this device is turned off. "
				"It stops after a restart.");
		case DM_STATUS_NO_DRIVER:
			return B_TRANSLATE("No driver is installed for this device. If you "
				"turned its driver off, Action â¸ Turned-off drivers turns it on "
				"again.");
	}
	return B_TRANSLATE("The system does not report which driver serves "
		"this device. It may still work.");
}
