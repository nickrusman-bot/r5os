/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 *
 * Reads the driver entries a packages settings file blocks. Also built into
 * Login, which needs the package name to turn a hidden driver on again.
 */
#ifndef DM_BLOCKED_DRIVERS_H
#define DM_BLOCKED_DRIVERS_H


#include <String.h>

#include <vector>


struct BlockedDriver {
	BString		package;
	// Relative to the package root, for example add-ons/kernel/drivers/bin/hda.
	BString		relative;
	// base + "/" + relative.
	BString		path;
};


// The kernel driver entries (under add-ons/kernel/) that settingsPath
// blocks. base is the folder the entries are relative to, "/boot/system"
// for /boot/system/settings/packages. A missing file is an empty list.
status_t read_blocked_drivers(const char* settingsPath, const char* base,
	std::vector<BlockedDriver>& out);


#endif
