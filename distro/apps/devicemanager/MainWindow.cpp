/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "MainWindow.h"

#include <AdminLock.h>
#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <OutlineListView.h>
#include <Path.h>
#include <R5Icons.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "DeviceItem.h"
#include "DriverControl.h"
#include "Messages.h"
#include "PropertiesWindow.h"
#include "Report.h"
#include "TurnedOffWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


static const char* kSettingsName = "DeviceManager";
static const float kIconSide = 16;


static BString
computer_name()
{
	char name[256];
	if (gethostname(name, sizeof(name)) == 0 && name[0] != '\0')
		return BString(name);
	return BString(B_TRANSLATE("Computer"));
}


static BString
expand_key(const DeviceItem* item)
{
	BString key;
	if (item->IsGroup())
		key.SetToFormat("g%" B_PRId32, item->Group());
	else
		key.SetToFormat("d%" B_PRIu32, item->Cookie());
	return key;
}


MainWindow::MainWindow()
	:
	BWindow(BRect(0, 0, 719, 539), B_TRANSLATE_SYSTEM_NAME("Device Manager"),
		B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fTree(NULL),
	fByConnection(false),
	fShowHidden(false),
	fSavePanel(NULL),
	fReloadRunner(NULL),
	fPollRunner(NULL),
	fSignature(0),
	fHasPending(false),
	fPendingEnable(false)
{
	BRect frame = Frame();
	_LoadSettings(frame);

	BMenuBar* menuBar = new BMenuBar("menu");

	BMenu* file = new BMenu(B_TRANSLATE("File"));
	BMenu* exportMenu = new BMenu(B_TRANSLATE("Export"));
	BMessage* details = new BMessage(kMsgExport);
	details->AddInt32("format", DM_REPORT_DETAILS);
	exportMenu->AddItem(new BMenuItem(B_TRANSLATE("Full details" B_UTF8_ELLIPSIS),
		details));
	BMessage* tree = new BMessage(kMsgExport);
	tree->AddInt32("format", DM_REPORT_TREE);
	exportMenu->AddItem(new BMenuItem(B_TRANSLATE("Device tree" B_UTF8_ELLIPSIS),
		tree));
	file->AddItem(exportMenu);
	file->AddSeparatorItem();
	file->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(file);

	BMenu* action = new BMenu(B_TRANSLATE("Action"));
	fPropertiesItem = new BMenuItem(B_TRANSLATE("Properties"),
		new BMessage(kMsgProperties), 'I');
	action->AddItem(fPropertiesItem);
	fToggleItem = new BMenuItem(B_TRANSLATE("Turn off driver"),
		new BMessage(kMsgToggleDriver));
	action->AddItem(fToggleItem);
	action->AddItem(new BMenuItem(B_TRANSLATE("Scan for hardware changes"),
		new BMessage(kMsgScan), 'R'));
	action->AddSeparatorItem();
	action->AddItem(new BMenuItem(
		B_TRANSLATE("Turned-off drivers" B_UTF8_ELLIPSIS),
		new BMessage(kMsgTurnedOffWindow)));
	menuBar->AddItem(action);

	BMenu* view = new BMenu(B_TRANSLATE("View"));
	view->SetRadioMode(false);
	fByTypeItem = new BMenuItem(B_TRANSLATE("Devices by type"),
		new BMessage(kMsgViewByType));
	fByConnectionItem = new BMenuItem(B_TRANSLATE("Devices by connection"),
		new BMessage(kMsgViewByConnection));
	fShowHiddenItem = new BMenuItem(B_TRANSLATE("Show hidden devices"),
		new BMessage(kMsgShowHidden));
	view->AddItem(fByTypeItem);
	view->AddItem(fByConnectionItem);
	view->AddSeparatorItem();
	view->AddItem(fShowHiddenItem);
	menuBar->AddItem(view);

	BMenu* help = new BMenu(B_TRANSLATE("Help"));
	help->AddItem(new BMenuItem(
		B_TRANSLATE("About Device Manager" B_UTF8_ELLIPSIS),
		new BMessage(B_ABOUT_REQUESTED)));
	help->SetTargetForItems(be_app);
	menuBar->AddItem(help);

	fPropertiesButton = new BButton("properties", B_TRANSLATE("Properties"),
		new BMessage(kMsgProperties));
	fScanButton = new BButton("scan", B_TRANSLATE("Scan for hardware changes"),
		new BMessage(kMsgScan));
	r5_button_set_glyph(fScanButton, R5_GLYPH_RESTART);
	fToggleButton = new BButton("toggle", B_TRANSLATE("Turn off driver"),
		new BMessage(kMsgToggleDriver));

	fTree = new BOutlineListView("devices", B_SINGLE_SELECTION_LIST);
	fTree->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	fTree->SetInvocationMessage(new BMessage(kMsgProperties));
	BScrollView* scroll = new BScrollView("devices-scroll", fTree, 0, false,
		true, B_NO_BORDER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(fPropertiesButton)
			.Add(fScanButton)
			.Add(fToggleButton)
			.AddGlue()
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(scroll);

	SetSizeLimits(480, B_SIZE_UNLIMITED, 360, B_SIZE_UNLIMITED);
	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
	if (!BScreen(this).Frame().Contains(Frame().LeftTop()))
		CenterOnScreen();

	// The device manager sends no notifications, and devfs does not report
	// every plug-in. Compare the node ids every two seconds instead.
	BMessage poll(kMsgDevChanged);
	fPollRunner = new BMessageRunner(BMessenger(this), &poll, 2000000);

	_Reload();
}


MainWindow::~MainWindow()
{
	delete fPollRunner;
	delete fReloadRunner;
	delete fSavePanel;
	for (std::map<int32, BBitmap*>::iterator it = fIcons.begin();
			it != fIcons.end(); ++it) {
		delete it->second;
	}
}


bool
MainWindow::QuitRequested()
{
	_SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelectionChanged:
			_UpdateActions();
			break;

		case kMsgProperties:
		{
			uint32 cookie = 0;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				DeviceRecord* record = _Selected();
				if (record == NULL) {
					// Invoking a group row folds or unfolds it.
					int32 index = fTree->CurrentSelection();
					BListItem* item = fTree->ItemAt(index);
					if (item != NULL) {
						if (item->IsExpanded())
							fTree->Collapse(item);
						else
							fTree->Expand(item);
					}
					break;
				}
				cookie = record->cookie;
			}
			_OpenProperties(cookie);
			break;
		}

		case kMsgPropertiesClosed:
		{
			uint32 cookie = 0;
			if (message->FindUInt32("cookie", &cookie) == B_OK)
				fPropertiesWindows.erase(cookie);
			break;
		}

		case kMsgToggleDriver:
		{
			uint32 cookie = 0;
			DeviceRecord* record = message->FindUInt32("cookie", &cookie) == B_OK
				? fModel.FindByCookie(cookie) : _Selected();
			if (record == NULL || !record->CanToggleDriver())
				break;
			_RequestToggle(record->driverPath,
				record->status == DM_STATUS_OFF_AFTER_RESTART);
			break;
		}

		case kMsgTurnOnPath:
		{
			const char* path = NULL;
			if (message->FindString("path", &path) == B_OK)
				_RequestToggle(path, true);
			break;
		}

		case kMsgAdminLockChanged:
			if (fHasPending) {
				fHasPending = false;
				_ApplyToggle(fPendingPath, fPendingEnable);
			}
			break;

		case kMsgScan:
			_Reload();
			break;

		case kMsgDevChanged:
			if (DeviceModel::TreeSignature() == fSignature)
				break;
			// Wait half a second for the rest of a plug-in or unplug.
			if (fReloadRunner == NULL) {
				BMessage reload(kMsgReloadNow);
				fReloadRunner = new BMessageRunner(BMessenger(this), &reload,
					500000, 1);
			}
			break;

		case kMsgReloadNow:
			delete fReloadRunner;
			fReloadRunner = NULL;
			_Reload();
			break;

		case kMsgViewByType:
		case kMsgViewByConnection:
			fByConnection = message->what == kMsgViewByConnection;
			_Rebuild();
			break;

		case kMsgShowHidden:
			fShowHidden = !fShowHidden;
			_Rebuild();
			break;

		case kMsgTurnedOffWindow:
			_ShowTurnedOff();
			break;

		case kMsgExport:
			_Export(message->GetInt32("format", DM_REPORT_DETAILS));
			break;

		case B_SAVE_REQUESTED:
			_SaveReport(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/*static*/ void
MainWindow::DescribeDevice(const DeviceRecord* record, BMessage& out)
{
	out.MakeEmpty();
	out.AddUInt32("cookie", record->cookie);
	out.AddString("name", record->name);
	out.AddString("manufacturer", record->manufacturer);
	out.AddString("location", record->location);
	out.AddInt32("group", record->group);
	out.AddString("group name", DeviceModel::GroupName(record->group));
	out.AddInt32("status", record->status);
	out.AddString("status text", DeviceModel::StatusSentence(record));
	out.AddInt32("driver kind", record->driverKind);
	out.AddString("driver name", record->driverName);
	out.AddString("driver path", record->driverPath);
	out.AddString("device paths", record->devicePaths);
	out.AddBool("critical", record->critical);
	out.AddBool("can toggle", record->CanToggleDriver());
	Attributes attributes = record->device->GetAllAttributes();
	for (size_t i = 0; i < attributes.size(); i++) {
		out.AddString("attr name", attributes[i].fName);
		out.AddString("attr value", attributes[i].fValue);
	}
}


void
MainWindow::_Reload()
{
	fSignature = DeviceModel::TreeSignature();
	fModel.Scan();
	_Rebuild();
	_UpdatePropertiesWindows();
	if (fTurnedOffWindow.IsValid())
		fTurnedOffWindow.SendMessage(kMsgTurnedOffRefresh);
}


bool
MainWindow::_IsVisible(const DeviceRecord* record) const
{
	if (record == NULL || record->internal)
		return false;
	return fShowHidden || !record->hidden;
}


DeviceItem*
MainWindow::_NewItem(const DeviceRecord* record)
{
	return new DeviceItem(record->name, _Icon(record->group), false,
		record->group, record->cookie, record->status, record->hidden);
}


void
MainWindow::_Rebuild()
{
	fByTypeItem->SetMarked(!fByConnection);
	fByConnectionItem->SetMarked(fByConnection);
	fShowHiddenItem->SetMarked(fShowHidden);

	// Remember what was open and selected.
	uint32 selectedCookie = 0;
	int32 selectedGroup = -1;
	int32 selected = fTree->CurrentSelection();
	if (DeviceItem* item = dynamic_cast<DeviceItem*>(fTree->ItemAt(selected))) {
		if (item->IsGroup())
			selectedGroup = item->Group();
		else
			selectedCookie = item->Cookie();
	}
	for (int32 i = 0; i < fTree->FullListCountItems(); i++) {
		DeviceItem* item = dynamic_cast<DeviceItem*>(fTree->FullListItemAt(i));
		if (item != NULL && fTree->CountItemsUnder(item, true) > 0)
			fExpanded[expand_key(item)] = item->IsExpanded();
	}

	BList old;
	for (int32 i = 0; i < fTree->FullListCountItems(); i++)
		old.AddItem(fTree->FullListItemAt(i));
	fTree->MakeEmpty();
	for (int32 i = 0; i < old.CountItems(); i++)
		delete (BListItem*)old.ItemAt(i);

	DeviceRecord* root = fModel.Root();
	if (root == NULL)
		return;

	DeviceItem* top = new DeviceItem(computer_name(), _Icon(DM_GROUP_COMPUTER),
		false, DM_GROUP_COMPUTER, root->cookie, DM_STATUS_WORKING, false);
	fTree->AddItem(top);

	if (fByConnection) {
		for (size_t i = 0; i < root->children.size(); i++)
			_AddConnection(root->children[i], top);
	} else {
		for (int32 group = 0; group < DM_GROUP_COUNT; group++) {
			if (group == DM_GROUP_COMPUTER)
				continue;
			DeviceItem* groupItem = NULL;
			for (int32 i = 0; i < fModel.CountRecords(); i++) {
				DeviceRecord* record = fModel.RecordAt(i);
				if (record == root || record->group != group
					|| !_IsVisible(record)) {
					continue;
				}
				if (groupItem == NULL) {
					groupItem = new DeviceItem(DeviceModel::GroupName(group),
						_Icon(group), true, group, 0, DM_STATUS_WORKING, false);
					fTree->AddUnder(groupItem, top);
				}
				fTree->AddUnder(_NewItem(record), groupItem);
			}
		}
	}
	fTree->FullListSortItems(&DeviceItem::Compare);

	// Groups start folded, as in Windows. A group with a problem device
	// starts open, so the warning is seen.
	for (int32 i = 0; i < fTree->FullListCountItems(); i++) {
		DeviceItem* item = dynamic_cast<DeviceItem*>(fTree->FullListItemAt(i));
		if (item == NULL || fTree->CountItemsUnder(item, true) == 0)
			continue;
		bool open = item == top;
		if (!open && item->IsGroup() && item->Group() == DM_GROUP_OTHER)
			open = true;
		std::map<BString, bool>::iterator it = fExpanded.find(expand_key(item));
		if (it != fExpanded.end())
			open = it->second;
		if (open)
			fTree->Expand(item);
		else
			fTree->Collapse(item);
	}

	for (int32 i = 0; i < fTree->CountItems(); i++) {
		DeviceItem* item = dynamic_cast<DeviceItem*>(fTree->ItemAt(i));
		if (item == NULL)
			continue;
		if ((item->IsGroup() && item->Group() == selectedGroup)
			|| (!item->IsGroup() && selectedCookie != 0
				&& item->Cookie() == selectedCookie)) {
			fTree->Select(i);
			fTree->ScrollToSelection();
			break;
		}
	}
	_UpdateActions();
}


// Devices by connection. A row that is not shown hands its children to
// the nearest shown parent.
void
MainWindow::_AddConnection(DeviceRecord* record, DeviceItem* under)
{
	DeviceItem* parent = under;
	if (_IsVisible(record)) {
		parent = _NewItem(record);
		fTree->AddUnder(parent, under);
	}
	for (size_t i = 0; i < record->children.size(); i++)
		_AddConnection(record->children[i], parent);
}


DeviceRecord*
MainWindow::_Selected() const
{
	DeviceItem* item = dynamic_cast<DeviceItem*>(
		fTree->ItemAt(fTree->CurrentSelection()));
	if (item == NULL || item->IsGroup())
		return NULL;
	return fModel.FindByCookie(item->Cookie());
}


void
MainWindow::_UpdateActions()
{
	DeviceRecord* record = _Selected();
	bool canToggle = record != NULL && record->CanToggleDriver();
	const char* label = record != NULL
			&& record->status == DM_STATUS_OFF_AFTER_RESTART
		? B_TRANSLATE("Turn on driver") : B_TRANSLATE("Turn off driver");

	fPropertiesItem->SetEnabled(record != NULL);
	fPropertiesButton->SetEnabled(record != NULL);
	fToggleItem->SetLabel(label);
	fToggleItem->SetEnabled(canToggle);
	fToggleButton->SetLabel(label);
	fToggleButton->SetEnabled(canToggle);
}


void
MainWindow::_OpenProperties(uint32 cookie)
{
	std::map<uint32, BMessenger>::iterator it = fPropertiesWindows.find(cookie);
	if (it != fPropertiesWindows.end() && it->second.IsValid()) {
		BLooper* looper = NULL;
		it->second.Target(&looper);
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL && window->Lock()) {
			window->Activate();
			window->Unlock();
		}
		return;
	}

	DeviceRecord* record = fModel.FindByCookie(cookie);
	if (record == NULL)
		return;
	BMessage device;
	DescribeDevice(record, device);
	PropertiesWindow* window = new PropertiesWindow(BMessenger(this), device);
	BRect frame = Frame();
	window->MoveTo(frame.left + 40 + 20 * (fPropertiesWindows.size() % 8),
		frame.top + 40 + 20 * (fPropertiesWindows.size() % 8));
	fPropertiesWindows[cookie] = BMessenger(window);
	window->Show();
}


void
MainWindow::_UpdatePropertiesWindows()
{
	for (std::map<uint32, BMessenger>::iterator it = fPropertiesWindows.begin();
			it != fPropertiesWindows.end(); ++it) {
		DeviceRecord* record = fModel.FindByCookie(it->first);
		if (record == NULL) {
			it->second.SendMessage(kMsgDeviceGone);
			continue;
		}
		BMessage update(kMsgDeviceUpdate);
		BMessage device;
		DescribeDevice(record, device);
		update.AddMessage("device", &device);
		it->second.SendMessage(&update);
	}
}


void
MainWindow::_RequestToggle(const BString& driverPath, bool enable)
{
	if (!enable) {
		BAlert* confirm = new BAlert(B_TRANSLATE("Turn off driver"),
			B_TRANSLATE("Turning off this driver turns off all devices that "
				"use it. The change takes effect after a restart."),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Turn off"), NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		confirm->SetShortcut(0, B_ESCAPE);
		if (confirm->Go() != 1)
			return;
	}

	if (dm_is_system_driver(driverPath)) {
		bool administrator = false;
		bool unlocked = false;
		if (r5_admin_query(administrator, unlocked) != B_OK) {
			(new BAlert(B_TRANSLATE("Device Manager"),
				B_TRANSLATE("Sign in must be running to change a driver."),
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_STOP_ALERT))->Go(NULL);
			return;
		}
		if (!unlocked) {
			// Ask for an administrator; kMsgAdminLockChanged continues.
			fHasPending = true;
			fPendingPath = driverPath;
			fPendingEnable = enable;
			r5_admin_prompt_unlock(BMessenger(this));
			return;
		}
	}
	_ApplyToggle(driverPath, enable);
}


void
MainWindow::_ApplyToggle(const BString& driverPath, bool enable)
{
	BString error;
	status_t status = dm_set_driver_enabled(driverPath, enable, error);
	if (status != B_OK) {
		if (error.IsEmpty())
			error = strerror(status);
		(new BAlert(B_TRANSLATE("Device Manager"), error, B_TRANSLATE("OK"),
			NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
		return;
	}

	const char* text = enable
		? B_TRANSLATE("The driver is turned on. It loads after a restart.")
		: B_TRANSLATE("The driver is turned off. It stops after a restart.\n\n"
			"The device may then no longer appear in the list. To turn the "
			"driver on again, choose Action \xE2\x96\xB8 Turned-off drivers.");
	(new BAlert(B_TRANSLATE("Device Manager"), text, B_TRANSLATE("OK"), NULL,
		NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT))->Go(NULL);
	_Reload();
}


void
MainWindow::_ShowTurnedOff()
{
	if (fTurnedOffWindow.IsValid()) {
		BLooper* looper = NULL;
		fTurnedOffWindow.Target(&looper);
		BWindow* window = dynamic_cast<BWindow*>(looper);
		if (window != NULL && window->Lock()) {
			window->Activate();
			window->Unlock();
			return;
		}
	}
	TurnedOffWindow* window = new TurnedOffWindow(BMessenger(this));
	fTurnedOffWindow = BMessenger(window);
	window->Show();
}


void
MainWindow::_Export(int32 format)
{
	if (fSavePanel == NULL) {
		fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
			0, false);
	}
	BMessage message(B_SAVE_REQUESTED);
	message.AddInt32("format", format);
	fSavePanel->SetMessage(&message);
	fSavePanel->SetSaveText(format == DM_REPORT_TREE
		? B_TRANSLATE("Device tree.txt") : B_TRANSLATE("Devices.txt"));
	fSavePanel->Window()->SetTitle(format == DM_REPORT_TREE
		? B_TRANSLATE("Export device tree") : B_TRANSLATE("Export full details"));
	fSavePanel->Show();
}


void
MainWindow::_SaveReport(BMessage* message)
{
	entry_ref directory;
	const char* name = NULL;
	if (message->FindRef("directory", &directory) != B_OK
		|| message->FindString("name", &name) != B_OK)
		return;

	BString text;
	write_report(fModel, message->GetInt32("format", DM_REPORT_DETAILS),
		computer_name(), fShowHidden, text);

	BPath path(&directory);
	path.Append(name);
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status == B_OK) {
		ssize_t written = file.Write(text.String(), text.Length());
		if (written != text.Length())
			status = written < 0 ? (status_t)written : B_IO_ERROR;
		BNodeInfo(&file).SetType("text/plain");
	}
	if (status != B_OK) {
		BString error(B_TRANSLATE("The report could not be saved: %error%"));
		error.ReplaceFirst("%error%", strerror(status));
		(new BAlert(B_TRANSLATE("Device Manager"), error, B_TRANSLATE("OK"),
			NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
	}
}


const BBitmap*
MainWindow::_Icon(int32 group)
{
	std::map<int32, BBitmap*>::iterator it = fIcons.find(group);
	if (it != fIcons.end())
		return it->second;
	BBitmap* icon = r5_mime_icon_bitmap(DeviceModel::GroupIconSignature(group),
		kIconSide);
	if (icon == NULL)
		icon = r5_mime_icon_bitmap(kAppSignature, kIconSide);
	fIcons[group] = icon;
	return icon;
}


void
MainWindow::_LoadSettings(BRect& frame)
{
	frame.Set(0, 0, 719, 539);
	frame.OffsetTo(80, 80);

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSettingsName);
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return;

	BRect saved;
	if (settings.FindRect("frame", &saved) == B_OK && saved.IsValid()
		&& saved.Width() >= 479 && saved.Height() >= 359) {
		frame = saved;
	}
	fByConnection = settings.GetBool("by connection", false);
	fShowHidden = settings.GetBool("show hidden", false);
}


void
MainWindow::_SaveSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kSettingsName);

	BMessage settings;
	settings.AddRect("frame", Frame());
	settings.AddBool("by connection", fByConnection);
	settings.AddBool("show hidden", fShowHidden);
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}
