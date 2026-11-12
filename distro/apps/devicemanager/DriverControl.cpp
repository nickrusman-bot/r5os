/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "DriverControl.h"

#include <AdminLock.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <Path.h>

#include "DriverUtils.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DriverControl"


static const char* kSystemPackagesSettings = "/boot/system/settings/packages";


static BString
user_packages_settings(BString& base)
{
	BPath user;
	BPath settings;
	if (find_directory(B_USER_DIRECTORY, &user) != B_OK
		|| find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK)
		return BString();
	base = user.Path();
	settings.Append("global/packages");
	return settings.Path();
}


bool
dm_is_system_driver(const BString& driverPath)
{
	return driverPath.StartsWith("/boot/system/");
}


status_t
dm_set_driver_enabled(const BString& driverPath, bool enable, BString& error)
{
	if (driverPath.IsEmpty()) {
		error = B_TRANSLATE("No driver file is known for this device.");
		return B_BAD_VALUE;
	}

	if (dm_is_system_driver(driverPath)) {
		status_t status = r5_admin_set_driver_blocked(driverPath.String(),
			!enable, error);
		if (status != B_OK)
			return status;
		// An older Login answers an unknown request with an empty reply,
		// which reads as success. Check the file.
		std::vector<BlockedDriver> list;
		read_blocked_drivers(kSystemPackagesSettings, "/boot/system", list);
		bool blocked = false;
		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].path == driverPath)
				blocked = true;
		}
		if (blocked == enable) {
			error = B_TRANSLATE("The change was not saved.");
			return B_ERROR;
		}
		return B_OK;
	}

	// A driver in the person's own packages: their own settings file.
	BPath settingsPath;
	BString relativePath;
	status_t status = DriverUtils::GetDriverPackageSettings(driverPath,
		settingsPath, relativePath);
	if (status != B_OK) {
		error = B_TRANSLATE("This driver is not part of a package.");
		return status;
	}

	BString packageName;
	if (!enable) {
		if (!DriverUtils::IsPackagedDriver(driverPath, &packageName)) {
			error = B_TRANSLATE("This driver is not part of a package.");
			return B_NOT_SUPPORTED;
		}
	} else {
		BString base;
		user_packages_settings(base);
		std::vector<BlockedDriver> list;
		read_blocked_drivers(settingsPath.Path(), base.String(), list);
		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].path == driverPath)
				packageName = list[i].package;
		}
		if (packageName.IsEmpty())
			return B_OK;
	}

	status = DriverUtils::UpdatePackageBlockedEntry(settingsPath.Path(),
		packageName.String(), relativePath.String(), !enable);
	if (status != B_OK)
		error = B_TRANSLATE("The settings file could not be written.");
	return status;
}


void
dm_turned_off_drivers(std::vector<BlockedDriver>& out)
{
	read_blocked_drivers(kSystemPackagesSettings, "/boot/system", out);

	BString base;
	BString userSettings = user_packages_settings(base);
	if (userSettings.IsEmpty())
		return;
	std::vector<BlockedDriver> user;
	read_blocked_drivers(userSettings.String(), base.String(), user);
	out.insert(out.end(), user.begin(), user.end());
}
