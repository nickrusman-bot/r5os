/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "Report.h"

#include <Catalog.h>

#include <time.h>

#include "DeviceModel.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Report"


static bool
is_listed(const DeviceRecord* record, bool showHidden)
{
	return !record->internal && (showHidden || !record->hidden);
}


static const char*
status_mark(const DeviceRecord* record)
{
	switch (record->status) {
		case DM_STATUS_NO_DRIVER:
			return "[!] ";
		case DM_STATUS_OFF_AFTER_RESTART:
			return "[off] ";
	}
	return "";
}


static BString
driver_line(const DeviceRecord* record)
{
	BString line;
	if (!record->driverName.IsEmpty())
		line << record->driverName;
	if (!record->driverPath.IsEmpty()) {
		if (!line.IsEmpty())
			line << " ";
		line << "(" << record->driverPath << ")";
	}
	return line;
}


static void
write_details(const DeviceRecord* record, BString& out)
{
	out << record->name << "\n";
	out << "  " << B_TRANSLATE("Status") << ": "
		<< DeviceModel::StatusSentence(record) << "\n";
	if (!record->manufacturer.IsEmpty()) {
		out << "  " << B_TRANSLATE("Manufacturer") << ": "
			<< record->manufacturer << "\n";
	}
	if (!record->location.IsEmpty())
		out << "  " << B_TRANSLATE("Location") << ": " << record->location << "\n";
	BString driver = driver_line(record);
	if (!driver.IsEmpty())
		out << "  " << B_TRANSLATE("Driver") << ": " << driver << "\n";
	if (!record->devicePaths.IsEmpty()) {
		out << "  " << B_TRANSLATE("Device paths") << ": "
			<< record->devicePaths << "\n";
	}
	if (record->hidden)
		out << "  " << B_TRANSLATE("Hidden device") << "\n";

	Attributes attributes = record->device->GetAllAttributes();
	if (!attributes.empty()) {
		out << "  " << B_TRANSLATE("Properties") << ":\n";
		for (size_t i = 0; i < attributes.size(); i++) {
			out << "    " << attributes[i].fName << ": "
				<< attributes[i].fValue << "\n";
		}
	}
	out << "\n";
}


static void
write_tree(const DeviceRecord* record, int32 depth, bool showHidden,
	BString& out)
{
	int32 childDepth = depth;
	if (is_listed(record, showHidden)) {
		for (int32 i = 0; i < depth; i++)
			out << "  ";
		out << status_mark(record) << record->name;
		BString driver = record->driverName;
		if (!driver.IsEmpty())
			out << "  \xE2\x80\x94 " << driver;
		out << "\n";
		childDepth = depth + 1;
	}
	for (size_t i = 0; i < record->children.size(); i++)
		write_tree(record->children[i], childDepth, showHidden, out);
}


void
write_report(const DeviceModel& model, int32 format,
	const BString& computerName, bool showHidden, BString& out)
{
	out.Truncate(0);
	char date[64];
	time_t now = time(NULL);
	strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&now));

	out << B_TRANSLATE("Device Manager report") << "\n";
	out << B_TRANSLATE("Computer") << ": " << computerName << "\n";
	out << B_TRANSLATE("Date") << ": " << date << "\n";
	out << (format == DM_REPORT_TREE ? B_TRANSLATE("Layout: device tree")
		: B_TRANSLATE("Layout: full details")) << "\n";
	if (format == DM_REPORT_TREE) {
		out << B_TRANSLATE("[!] no driver, [off] driver turned off") << "\n";
	}
	out << "\n";

	DeviceRecord* root = model.Root();
	if (root == NULL)
		return;

	if (format == DM_REPORT_TREE) {
		out << computerName << "\n";
		for (size_t i = 0; i < root->children.size(); i++)
			write_tree(root->children[i], 1, showHidden, out);
		return;
	}

	// Groups in the order the window shows them: by name.
	for (int32 pass = 0; pass < DM_GROUP_COUNT; pass++) {
		int32 group = -1;
		BString best;
		for (int32 candidate = 0; candidate < DM_GROUP_COUNT; candidate++) {
			BString name = DeviceModel::GroupName(candidate);
			// The pass-th smallest name.
			int32 smaller = 0;
			for (int32 other = 0; other < DM_GROUP_COUNT; other++) {
				if (other != candidate
					&& BString(DeviceModel::GroupName(other)).ICompare(name) < 0)
					smaller++;
			}
			if (smaller == pass) {
				group = candidate;
				best = name;
				break;
			}
		}
		if (group < 0 || group == DM_GROUP_COMPUTER)
			continue;

		bool header = false;
		for (int32 i = 0; i < model.CountRecords(); i++) {
			DeviceRecord* record = model.RecordAt(i);
			if (record == root || record->group != group
				|| !is_listed(record, showHidden)) {
				continue;
			}
			if (!header) {
				out << "== " << best << " ==\n\n";
				header = true;
			}
			write_details(record, out);
		}
	}
}
