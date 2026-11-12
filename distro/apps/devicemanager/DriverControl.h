/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 *
 * Turning a driver off and on. A driver under /boot/system goes through
 * Login (r5_admin_set_driver_blocked in libr5ui); a driver in the person's
 * own packages is written here.
 */
#ifndef DM_DRIVER_CONTROL_H
#define DM_DRIVER_CONTROL_H


#include <String.h>

#include <vector>

#include "BlockedDrivers.h"


bool dm_is_system_driver(const BString& driverPath);

// Blocks (enable false) or unblocks the driver for the next start.
status_t dm_set_driver_enabled(const BString& driverPath, bool enable,
	BString& error);

// Every driver blocked in the system and the person's packages settings.
void dm_turned_off_drivers(std::vector<BlockedDriver>& out);


#endif
