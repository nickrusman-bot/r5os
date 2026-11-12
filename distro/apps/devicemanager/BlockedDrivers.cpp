/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "BlockedDrivers.h"

#include <driver_settings.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>


static void
add_entries(const driver_parameter& list, const char* package,
	const char* base, std::vector<BlockedDriver>& out)
{
	for (int i = 0; i < list.parameter_count; i++) {
		const char* entry = list.parameters[i].name;
		if (entry == NULL || strncmp(entry, "add-ons/kernel/", 15) != 0)
			continue;
		BlockedDriver driver;
		driver.package = package;
		driver.relative = entry;
		driver.path << base << "/" << entry;
		out.push_back(driver);
	}
}


status_t
read_blocked_drivers(const char* settingsPath, const char* base,
	std::vector<BlockedDriver>& out)
{
	out.clear();
	struct stat st;
	if (stat(settingsPath, &st) != 0)
		return errno == ENOENT ? B_OK : errno;

	void* handle = load_driver_settings(settingsPath);
	if (handle == NULL)
		return B_ERROR;
	const driver_settings* settings = get_driver_settings(handle);
	if (settings == NULL) {
		unload_driver_settings(handle);
		return B_ERROR;
	}

	for (int i = 0; i < settings->parameter_count; i++) {
		const driver_parameter& package = settings->parameters[i];
		if (strcmp(package.name, "Package") != 0)
			continue;
		// "Package haiku { ... }": the name is the value (the main field of
		// DriverUtils' template). A "name" field is read as well.
		const char* name = package.value_count > 0 ? package.values[0] : NULL;
		for (int j = 0; name == NULL && j < package.parameter_count; j++) {
			const driver_parameter& field = package.parameters[j];
			if (strcmp(field.name, "name") == 0 && field.value_count > 0)
				name = field.values[0];
		}
		if (name == NULL)
			continue;
		for (int j = 0; j < package.parameter_count; j++) {
			const driver_parameter& field = package.parameters[j];
			// "EntryBlacklist" is the older spelling packagefs still reads.
			if (strcmp(field.name, "BlockedEntries") == 0
				|| strcmp(field.name, "EntryBlacklist") == 0) {
				add_entries(field, name, base, out);
			}
		}
	}

	unload_driver_settings(handle);
	return B_OK;
}
