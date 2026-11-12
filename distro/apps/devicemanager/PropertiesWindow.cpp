/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "PropertiesWindow.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GridLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <R5IconView.h>
#include <R5Icons.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringList.h>
#include <StringView.h>
#include <TabView.h>
#include <TextView.h>

#include <package/PackageInfo.h>

#include <stdio.h>
#include <time.h>

#include <vector>

#include "DeviceModel.h"
#include "Messages.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PropertiesWindow"


using BAppearanceKit::BIconView;


static const int32 kEventLines = 50;
static const off_t kSyslogTail = 2 * 1024 * 1024;


static BStringView*
value_view(const char* name)
{
	BStringView* view = new BStringView(name, "");
	view->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	view->SetTruncation(B_TRUNCATE_MIDDLE);
	return view;
}


static BStringView*
label_view(const char* text)
{
	BStringView* view = new BStringView(NULL, text);
	view->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	return view;
}


static BTextView*
read_only_text(const char* name)
{
	BTextView* text = new BTextView(name);
	text->MakeEditable(false);
	text->SetWordWrap(true);
	text->SetStylable(false);
	return text;
}


static BString
size_text(off_t size)
{
	BString text;
	if (size < 1024)
		text.SetToFormat(B_TRANSLATE("%lld bytes"), (long long)size);
	else if (size < 1024 * 1024)
		text.SetToFormat(B_TRANSLATE("%.1f KiB"), size / 1024.0);
	else
		text.SetToFormat(B_TRANSLATE("%.1f MiB"), size / (1024.0 * 1024.0));
	return text;
}


PropertiesWindow::PropertiesWindow(BMessenger mainWindow, const BMessage& device)
	:
	BWindow(BRect(0, 0, 439, 499), "", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fMainWindow(mainWindow),
	fCookie(0),
	fGone(false),
	fIcon(NULL),
	fShownProperty(0)
{
	BTabView* tabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);
	tabs->SetBorder(B_NO_BORDER);
	// 440 by 500 at a 12 point font, as Windows' Properties window.
	float unit = be_plain_font->Size() / 12.0f;
	tabs->SetExplicitMinSize(BSize(floorf(410 * unit), floorf(420 * unit)));

	BView* general = _GeneralTab();
	general->SetName(B_TRANSLATE("General"));
	tabs->AddTab(general);
	BView* driver = _DriverTab();
	driver->SetName(B_TRANSLATE("Driver"));
	tabs->AddTab(driver);
	BView* details = _DetailsTab();
	details->SetName(B_TRANSLATE("Details"));
	tabs->AddTab(details);
	BView* events = _EventsTab();
	events->SetName(B_TRANSLATE("Events"));
	tabs->AddTab(events);

	BButton* ok = new BButton("ok", B_TRANSLATE("OK"),
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(tabs)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(ok)
		.End();
	SetDefaultButton(ok);
	AddShortcut('C', B_COMMAND_KEY, new BMessage(B_COPY));

	_SetDevice(device);
}


PropertiesWindow::~PropertiesWindow()
{
	delete fIcon;
}


bool
PropertiesWindow::QuitRequested()
{
	BMessage closed(kMsgPropertiesClosed);
	closed.AddUInt32("cookie", fCookie);
	fMainWindow.SendMessage(&closed);
	return true;
}


void
PropertiesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDeviceUpdate:
		{
			BMessage device;
			if (message->FindMessage("device", &device) == B_OK)
				_SetDevice(device);
			break;
		}
		case kMsgDeviceGone:
			_SetGone();
			break;

		case kMsgPropertyChosen:
			_ShowProperty(message->GetInt32("index", 0));
			break;

		case kMsgDriverDetails:
		{
			BString text;
			text << B_TRANSLATE("Driver file") << ":\n"
				<< fDevice.GetString("driver path", "") << "\n\n"
				<< B_TRANSLATE("Device paths") << ":\n";
			BString paths = fDevice.GetString("device paths", "");
			if (paths.IsEmpty())
				text << B_TRANSLATE("None");
			else {
				paths.ReplaceAll(", ", "\n");
				text << paths;
			}
			(new BAlert(B_TRANSLATE("Driver details"), text, B_TRANSLATE("OK"),
				NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT))->Go(NULL);
			break;
		}

		case kMsgToggleDriver:
		{
			BMessage toggle(kMsgToggleDriver);
			toggle.AddUInt32("cookie", fCookie);
			fMainWindow.SendMessage(&toggle);
			break;
		}

		case B_COPY:
			_CopySelection();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


BView*
PropertiesWindow::_Header(BIconView*& icon, BStringView*& name)
{
	icon = new BIconView("icon");
	name = new BStringView("name", "");
	BFont bold(be_bold_font);
	name->SetFont(&bold);
	name->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
	name->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	name->SetTruncation(B_TRUNCATE_END);

	BGroupView* header = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(header)
		.Add(icon)
		.Add(name);
	return header;
}


BView*
PropertiesWindow::_GeneralTab()
{
	BView* header = _Header(fGeneralIcon, fGeneralName);
	fType = value_view("type");
	fManufacturer = value_view("manufacturer");
	fLocation = value_view("location");

	fStatus = read_only_text("status");
	BScrollView* statusScroll = new BScrollView("status-scroll", fStatus, 0,
		false, false);
	font_height height;
	be_plain_font->GetHeight(&height);
	float line = ceilf(height.ascent + height.descent + height.leading);
	statusScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, line * 4));
	statusScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, line * 4));
	BBox* box = new BBox("status-box");
	box->SetLabel(B_TRANSLATE("Device status"));
	BLayoutBuilder::Group<>(box, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(statusScroll);

	BGroupView* view = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(view)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(header)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(label_view(B_TRANSLATE("Device type:")), 0, 0)
			.Add(fType, 1, 0)
			.Add(label_view(B_TRANSLATE("Manufacturer:")), 0, 1)
			.Add(fManufacturer, 1, 1)
			.Add(label_view(B_TRANSLATE("Location:")), 0, 2)
			.Add(fLocation, 1, 2)
		.End()
		.Add(box)
		.AddGlue();
	return view;
}


BView*
PropertiesWindow::_DriverTab()
{
	BView* header = _Header(fDriverIcon, fDriverDeviceName);
	fDriverName = value_view("driver");
	fDriverFile = value_view("file");
	fDriverSize = value_view("size");
	fDriverDate = value_view("date");
	fDriverPackage = value_view("package");
	fDriverVersion = value_view("version");
	fDriverLicense = value_view("license");
	fDriverNote = new BStringView("note", "");
	fDriverNote->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fDetailsButton = new BButton("details",
		B_TRANSLATE("Driver details" B_UTF8_ELLIPSIS),
		new BMessage(kMsgDriverDetails));
	fToggleButton = new BButton("toggle", B_TRANSLATE("Turn off driver"),
		new BMessage(kMsgToggleDriver));

	BGroupView* view = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(view)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(header)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(label_view(B_TRANSLATE("Driver:")), 0, 0)
			.Add(fDriverName, 1, 0)
			.Add(label_view(B_TRANSLATE("Driver file:")), 0, 1)
			.Add(fDriverFile, 1, 1)
			.Add(label_view(B_TRANSLATE("File size:")), 0, 2)
			.Add(fDriverSize, 1, 2)
			.Add(label_view(B_TRANSLATE("Date modified:")), 0, 3)
			.Add(fDriverDate, 1, 3)
			.Add(label_view(B_TRANSLATE("Package:")), 0, 4)
			.Add(fDriverPackage, 1, 4)
			.Add(label_view(B_TRANSLATE("Version:")), 0, 5)
			.Add(fDriverVersion, 1, 5)
			.Add(label_view(B_TRANSLATE("License:")), 0, 6)
			.Add(fDriverLicense, 1, 6)
		.End()
		.AddGlue()
		.Add(fDriverNote)
		.AddGroup(B_HORIZONTAL)
			.Add(fDetailsButton)
			.AddGlue()
			.Add(fToggleButton)
		.End();
	return view;
}


BView*
PropertiesWindow::_DetailsTab()
{
	fPropertyMenu = new BPopUpMenu(B_TRANSLATE("Property"));
	fPropertyField = new BMenuField("property", B_TRANSLATE("Property:"),
		fPropertyMenu);
	fValues = new BListView("values", B_SINGLE_SELECTION_LIST);
	BScrollView* scroll = new BScrollView("values-scroll", fValues, 0, false,
		true);

	BGroupView* view = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(view)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fPropertyField)
		.Add(new BStringView(NULL, B_TRANSLATE("Value:")))
		.Add(scroll, 1.0f);
	return view;
}


BView*
PropertiesWindow::_EventsTab()
{
	fEvents = read_only_text("events");
	BFont fixed(be_fixed_font);
	fEvents->SetFontAndColor(&fixed);
	fEvents->SetWordWrap(false);
	BScrollView* scroll = new BScrollView("events-scroll", fEvents, 0, true,
		true);

	BGroupView* view = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(view)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(new BStringView(NULL,
			B_TRANSLATE("Recent system log lines about this device:")))
		.Add(scroll, 1.0f);
	return view;
}


void
PropertiesWindow::_SetDevice(const BMessage& device)
{
	fDevice = device;
	fGone = false;
	fCookie = device.GetUInt32("cookie", 0);
	BString name = device.GetString("name", "");
	SetTitle(name);

	delete fIcon;
	BString signature = DeviceModel::GroupIconSignature(device.GetInt32("group",
		DM_GROUP_OTHER));
	float side = ceilf(be_plain_font->Size() * 32.0f / 12.0f);
	fIcon = r5_mime_icon_bitmap(signature, side);
	if (fIcon == NULL)
		fIcon = r5_mime_icon_bitmap(kAppSignature, side);
	fGeneralIcon->SetIcon(fIcon);
	fDriverIcon->SetIcon(fIcon);

	fGeneralName->SetText(name);
	fDriverDeviceName->SetText(name);
	fType->SetText(device.GetString("group name", ""));
	BString manufacturer = device.GetString("manufacturer", "");
	fManufacturer->SetText(manufacturer.IsEmpty() ? B_TRANSLATE("Unknown")
		: manufacturer.String());
	BString location = device.GetString("location", "");
	fLocation->SetText(location.IsEmpty() ? B_TRANSLATE("Unknown")
		: location.String());
	fStatus->SetText(device.GetString("status text", ""));

	_FillDriver();
	_FillProperties();
	_FillEvents();
}


void
PropertiesWindow::_SetGone()
{
	fGone = true;
	fStatus->SetText(B_TRANSLATE("This device is no longer connected."));
	fToggleButton->SetEnabled(false);
	fDetailsButton->SetEnabled(false);
}


void
PropertiesWindow::_FillDriver()
{
	const char* unknown = B_TRANSLATE("Not available");
	BString path = fDevice.GetString("driver path", "");
	BString driverName = fDevice.GetString("driver name", "");
	int32 kind = fDevice.GetInt32("driver kind", DM_DRIVER_NONE);

	if (driverName.IsEmpty()) {
		driverName = kind == DM_DRIVER_NONE ? B_TRANSLATE("None reported")
			: unknown;
	}
	fDriverName->SetText(driverName);
	fDriverFile->SetText(path.IsEmpty() ? unknown : path.String());

	BString size = unknown;
	BString date = unknown;
	BString package = unknown;
	BString version = unknown;
	BString license = unknown;
	if (!path.IsEmpty()) {
		BEntry entry(path.String(), true);
		off_t bytes = 0;
		if (entry.GetSize(&bytes) == B_OK)
			size = size_text(bytes);
		time_t modified = 0;
		if (entry.GetModificationTime(&modified) == B_OK) {
			char text[64];
			strftime(text, sizeof(text), "%Y-%m-%d %H:%M", localtime(&modified));
			date = text;
		}
		char packagePath[B_PATH_NAME_LENGTH];
		if (find_path_for_path(path.String(), B_FIND_PATH_PACKAGE_PATH, NULL,
				packagePath, sizeof(packagePath)) == B_OK) {
			BPackageKit::BPackageInfo info;
			if (info.ReadFromPackageFile(packagePath) == B_OK) {
				package = info.Name();
				version = info.Version().ToString();
				const BStringList& licenses = info.LicenseList();
				if (!licenses.IsEmpty())
					license = licenses.Join(", ");
			}
		}
	}
	fDriverSize->SetText(size);
	fDriverDate->SetText(date);
	fDriverPackage->SetText(package);
	fDriverVersion->SetText(version);
	fDriverLicense->SetText(license);

	bool off = fDevice.GetInt32("status", DM_STATUS_WORKING)
		== DM_STATUS_OFF_AFTER_RESTART;
	fToggleButton->SetLabel(off ? B_TRANSLATE("Turn on driver")
		: B_TRANSLATE("Turn off driver"));
	fToggleButton->SetEnabled(!fGone && fDevice.GetBool("can toggle", false));
	fDetailsButton->SetEnabled(!fGone && !path.IsEmpty());

	if (fDevice.GetBool("critical", false) && !path.IsEmpty()) {
		fDriverNote->SetText(B_TRANSLATE(
			"This driver is needed to start the system."));
	} else if (kind == DM_DRIVER_SYSTEM) {
		fDriverNote->SetText(B_TRANSLATE("A part of the system serves this device."));
	} else
		fDriverNote->SetText("");
}


void
PropertiesWindow::_FillProperties()
{
	BString shown;
	if (BMenuItem* item = fPropertyMenu->FindMarked())
		shown = item->Label();

	while (BMenuItem* item = fPropertyMenu->RemoveItem((int32)0))
		delete item;

	int32 select = 0;
	const char* name = NULL;
	for (int32 i = 0; fDevice.FindString("attr name", i, &name) == B_OK; i++) {
		BMessage* message = new BMessage(kMsgPropertyChosen);
		message->AddInt32("index", i);
		fPropertyMenu->AddItem(new BMenuItem(name, message));
		if (shown == name)
			select = i;
	}
	if (BMenuItem* item = fPropertyMenu->ItemAt(select))
		item->SetMarked(true);
	_ShowProperty(select);
}


void
PropertiesWindow::_ShowProperty(int32 index)
{
	fShownProperty = index;
	while (BListItem* item = fValues->RemoveItem((int32)0))
		delete item;

	const char* value = NULL;
	if (fDevice.FindString("attr value", index, &value) != B_OK)
		return;
	// Device paths come as one comma-separated line; show one per row.
	BString all(value);
	int32 start = 0;
	while (start <= all.Length()) {
		int32 comma = all.FindFirst(", ", start);
		BString part;
		all.CopyInto(part, start, (comma < 0 ? all.Length() : comma) - start);
		fValues->AddItem(new BStringItem(part));
		if (comma < 0)
			break;
		start = comma + 2;
	}
}


void
PropertiesWindow::_FillEvents()
{
	std::vector<BString> needles;
	BString driverName = fDevice.GetString("driver name", "");
	if (driverName.Length() >= 3)
		needles.push_back(driverName);
	BString paths = fDevice.GetString("device paths", "");
	int32 start = 0;
	while (start < paths.Length()) {
		int32 comma = paths.FindFirst(", ", start);
		BString part;
		paths.CopyInto(part, start, (comma < 0 ? paths.Length() : comma) - start);
		if (part.Length() >= 3)
			needles.push_back(part);
		if (comma < 0)
			break;
		start = comma + 2;
	}

	BPath log;
	BFile file;
	if (find_directory(B_SYSTEM_LOG_DIRECTORY, &log) == B_OK) {
		log.Append("syslog");
		file.SetTo(log.Path(), B_READ_ONLY);
	}
	if (needles.empty() || file.InitCheck() != B_OK) {
		fEvents->SetText(file.InitCheck() != B_OK
			? B_TRANSLATE("Events are not available.")
			: B_TRANSLATE("No events for this device."));
		return;
	}

	off_t size = 0;
	file.GetSize(&size);
	off_t offset = size > kSyslogTail ? size - kSyslogTail : 0;
	BString text;
	char* buffer = text.LockBuffer(size - offset + 1);
	ssize_t read = file.ReadAt(offset, buffer, size - offset);
	text.UnlockBuffer(read > 0 ? read : 0);

	std::vector<BString> found;
	int32 end = text.Length();
	while (end > 0 && (int32)found.size() < kEventLines) {
		int32 lineStart = text.FindLast('\n', end - 1);
		lineStart = lineStart < 0 ? 0 : lineStart + 1;
		BString line;
		text.CopyInto(line, lineStart, end - lineStart);
		for (size_t i = 0; i < needles.size(); i++) {
			if (line.FindFirst(needles[i]) >= 0) {
				found.push_back(line);
				break;
			}
		}
		end = lineStart - 1;
	}

	BString events;
	for (size_t i = 0; i < found.size(); i++)
		events << found[i] << "\n";
	if (events.IsEmpty())
		events = B_TRANSLATE("No events for this device.");
	fEvents->SetText(events);
}


void
PropertiesWindow::_CopySelection()
{
	BString text;
	if (BStringItem* item = dynamic_cast<BStringItem*>(
			fValues->ItemAt(fValues->CurrentSelection()))) {
		text = item->Text();
	} else {
		int32 from;
		int32 to;
		fEvents->GetSelection(&from, &to);
		if (to > from) {
			char* buffer = text.LockBuffer(to - from + 1);
			fEvents->GetText(from, to - from, buffer);
			text.UnlockBuffer(to - from);
		}
	}
	if (text.IsEmpty() || !be_clipboard->Lock())
		return;
	be_clipboard->Clear();
	BMessage* clip = be_clipboard->Data();
	clip->AddData("text/plain", B_MIME_TYPE, text.String(), text.Length());
	be_clipboard->Commit();
	be_clipboard->Unlock();
}
