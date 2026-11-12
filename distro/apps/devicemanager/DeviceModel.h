/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 *
 * The device tree as Device Manager shows it. The kernel side is read with
 * Haiku's Devices code (dm_wrapper, Device*, DriverUtils).
 */
#ifndef DM_DEVICE_MODEL_H
#define DM_DEVICE_MODEL_H


#include <String.h>

#include <vector>

#include "Device.h"


// Groups of "Devices by type". The order is not the shown order; groups are
// sorted by their name.
enum dm_group {
	DM_GROUP_COMPUTER = 0,
	DM_GROUP_CAMERAS,
	DM_GROUP_DISK_DRIVES,
	DM_GROUP_DISPLAY,
	DM_GROUP_HID,
	DM_GROUP_NETWORK,
	DM_GROUP_OTHER,
	DM_GROUP_PROCESSORS,
	DM_GROUP_SOUND,
	DM_GROUP_STORAGE,
	DM_GROUP_SYSTEM,
	DM_GROUP_OTHER_SYSTEM,
	DM_GROUP_USB,
	DM_GROUP_COUNT
};


enum dm_status {
	// A driver is attached to this device.
	DM_STATUS_WORKING = 0,
	// The driver still runs, but it is blocked from the next start.
	DM_STATUS_OFF_AFTER_RESTART,
	// A device of a kind that needs a driver, and none is attached.
	DM_STATUS_NO_DRIVER,
	// The device manager does not say which driver serves this device.
	DM_STATUS_NOT_REPORTED
};


enum dm_driver_kind {
	DM_DRIVER_NONE = 0,
	// A driver node under this device (new-style driver).
	DM_DRIVER_NODE,
	// A legacy driver that reserved this PCI device.
	DM_DRIVER_LEGACY,
	// A graphics driver whose /dev/graphics entry names this PCI location.
	DM_DRIVER_GRAPHICS,
	// The device is a bus or controller served by a system module.
	DM_DRIVER_SYSTEM
};


struct DeviceRecord {
								DeviceRecord();
								~DeviceRecord();

			Device*				device;
			DeviceRecord*		parent;
			std::vector<DeviceRecord*> children;

			// Node attributes exactly as the kernel reported them.
			Attributes			raw;
			uint32				cookie;

			BString				name;
			BString				manufacturer;
			BString				location;
			BString				devicePaths;

			int32				group;
			int32				status;
			int32				driverKind;
			BString				driverPath;
			BString				driverName;

			bool				hidden;
			// A node that only carries a driver (driver node, legacy
			// reservation). Its data is shown on the parent.
			bool				internal;
			bool				critical;

			BString				RawValue(const char* name) const;
			BString				modulePath;
			bool				HasDriver() const;
			bool				CanToggleDriver() const;
};


class DeviceModel {
public:
								DeviceModel();
								~DeviceModel();

			status_t			Scan();

			int32				CountRecords() const;
			DeviceRecord*		RecordAt(int32 index) const;
			DeviceRecord*		Root() const;
			DeviceRecord*		FindByCookie(uint32 cookie) const;

	static	const char*			GroupName(int32 group);
	static	const char*			GroupIconSignature(int32 group);
	static	const char*			StatusSentence(const DeviceRecord* record);

private:
			void				_Clear();
			void				_AddNode(device_node_cookie* node,
									DeviceRecord* parent);
			void				_Classify(DeviceRecord* record);
			void				_FindDrivers();
			void				_MatchGraphics();
	static	BString				_DriverFile(const BString& name);
			void				_SetStatus(DeviceRecord* record);

			std::vector<DeviceRecord*> fRecords;
			DeviceRecord*		fRoot;
};


#endif
