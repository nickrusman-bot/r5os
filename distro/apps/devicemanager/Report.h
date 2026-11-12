/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_REPORT_H
#define DM_REPORT_H


#include <String.h>


class DeviceModel;


enum {
	// Every device under its type, with all its details.
	DM_REPORT_DETAILS = 0,
	// The devices as they are connected, indented one step per level.
	DM_REPORT_TREE = 1
};


// Plain UTF-8 text. Hidden devices are included when showHidden is true;
// internal driver nodes never are.
void write_report(const DeviceModel& model, int32 format,
	const BString& computerName, bool showHidden, BString& out);


#endif
