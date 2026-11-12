/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Login side of kMsgAdminSetDriverBlocked. Device Manager runs as the
 * signed-in person and cannot write /boot/system/settings/packages. Login
 * checks the administrator grant, refuses drivers the system needs to
 * start, and then writes the blocked entry with Haiku's DriverUtils.
 */


#include "DriverAdmin.h"

#include "AccountAdmin.h"
#include "BlockedDrivers.h"
#include "DriverUtils.h"

#include <Entry.h>
#include <Path.h>
#include <String.h>

#include <string.h>
#include <unistd.h>


static const char* kKernelAddOns = "/boot/system/add-ons/kernel/";
static const char* kSystemPackagesSettings = "/boot/system/settings/packages";

// The same lists as IsCriticalDriver() in src/apps/devices/DriverUtils.cpp.
// Login only has the path, not the device category, so it checks the path.
static const char* kCriticalSubsystems[] = {"bus_managers/", "busses/",
	"drivers/disk/", "drivers/power/", "drivers/timer/", "drivers/bus/",
	"drivers/common/", "drivers/input/", NULL};
static const char* kCriticalModules[] = {"acpi", "pci", "root", NULL};


static bool
is_critical_driver_path(BString driver)
{
	driver.ToLower();
	for (int32 i = 0; kCriticalSubsystems[i] != NULL; i++) {
		if (driver.FindFirst(kCriticalSubsystems[i]) >= 0)
			return true;
	}
	for (int32 i = 0; kCriticalModules[i] != NULL; i++) {
		BString leaf("/");
		leaf << kCriticalModules[i];
		if (driver == kCriticalModules[i] || driver.EndsWith(leaf))
			return true;
	}
	return false;
}


static status_t
fail(BMessage& reply, status_t status, const char* text)
{
	reply.AddString("text", text);
	reply.AddInt32("error", status);
	return status;
}


status_t
handle_driver_admin_message(BMessage* request, BMessage& reply)
{
	reply.MakeEmpty();
	if (request == NULL)
		return fail(reply, B_BAD_VALUE, "The request is empty.");
	if (geteuid() != 0) {
		return fail(reply, B_PERMISSION_DENIED,
			"Driver changes require the sign-in helper.");
	}
	if (!admin_sender_may_change(request)) {
		return fail(reply, B_PERMISSION_DENIED,
			"Unlock this with an administrator password.");
	}

	const char* driverString = NULL;
	bool blocked = true;
	if (request->FindString("driver", &driverString) != B_OK
		|| request->FindBool("blocked", &blocked) != B_OK
		|| driverString == NULL || driverString[0] == '\0') {
		return fail(reply, B_BAD_VALUE, "No driver was given.");
	}

	// Only a plain path under the system kernel add-ons, nothing in the
	// settings tree and no "." or ".." parts.
	BString driver(driverString);
	if (!driver.StartsWith(kKernelAddOns) || driver.FindFirst("/./") >= 0
		|| driver.FindFirst("/../") >= 0 || driver.FindFirst("//") >= 0
		|| driver.EndsWith("/.") || driver.EndsWith("/..")) {
		return fail(reply, B_NOT_ALLOWED,
			"Only system drivers can be turned off here.");
	}

	status_t status;
	if (blocked) {
		// The file must be there and the path must be its real one.
		BPath normalized(driverString, NULL, true);
		BEntry entry(driverString);
		if (normalized.InitCheck() != B_OK || driver != normalized.Path()
			|| !entry.Exists() || entry.IsDirectory()) {
			return fail(reply, B_ENTRY_NOT_FOUND, "The driver file was not found.");
		}
		if (is_critical_driver_path(driver)) {
			return fail(reply, B_NOT_ALLOWED, "This driver is needed to start "
				"the system and cannot be turned off.");
		}

		BString packageName;
		if (!DriverUtils::IsPackagedDriver(driver, &packageName)) {
			return fail(reply, B_NOT_SUPPORTED,
				"This driver is not part of a package.");
		}
		BPath settingsPath;
		BString relativePath;
		status = DriverUtils::GetDriverPackageSettings(driver, settingsPath,
			relativePath);
		if (status != B_OK
			|| strcmp(settingsPath.Path(), kSystemPackagesSettings) != 0) {
			return fail(reply, B_NOT_ALLOWED,
				"Only system drivers can be turned off here.");
		}
		status = DriverUtils::UpdatePackageBlockedEntry(kSystemPackagesSettings,
			packageName.String(), relativePath.String(), true);
	} else {
		// A blocked file is hidden by packagefs, so its package cannot be
		// read from the file. Take it from the blocked entry instead.
		std::vector<BlockedDriver> list;
		status = read_blocked_drivers(kSystemPackagesSettings, "/boot/system",
			list);
		if (status != B_OK)
			return fail(reply, status, "The settings file could not be read.");
		const BlockedDriver* found = NULL;
		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].path == driver) {
				found = &list[i];
				break;
			}
		}
		if (found == NULL) {
			reply.AddInt32("error", B_OK);
			return B_OK;
		}
		status = DriverUtils::UpdatePackageBlockedEntry(kSystemPackagesSettings,
			found->package.String(), found->relative.String(), false);
	}
	if (status != B_OK)
		return fail(reply, status, "The settings file could not be written.");

	reply.AddInt32("error", B_OK);
	return B_OK;
}
