/*
 * Copyright 2003-2011, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Sikosis
 *		Jérôme Duval
 */


#include "MediaViews.h"

#include <Alert.h>
#include <AutoDeleter.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "MediaWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Media views"

#define MEDIA_DEFAULT_INPUT_CHANGE 'dich'
#define MEDIA_DEFAULT_OUTPUT_CHANGE 'doch'
#define ML_AUDIO_LATENCY 'alat'
#define MEDIA_SHOW_HIDE_VOLUME_CONTROL 'shvc'


SettingsView::SettingsView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fInputMenu(NULL),
	fOutputMenu(NULL)
{
	// input menu
	fInputMenu = new BPopUpMenu(B_TRANSLATE_ALL("<none>",
		"VideoInputMenu", "Used when no video input is available"));
	fInputMenu->SetLabelFromMarked(true);

	// output menu
	fOutputMenu = new BPopUpMenu(B_TRANSLATE_ALL("<none>",
		"VideoOutputMenu", "Used when no video output is available"));
	fOutputMenu->SetLabelFromMarked(true);
}


BButton*
SettingsView::MakeRestartButton()
{
	return new BButton("restartButton",
		B_TRANSLATE("Restart media services"),
		new BMessage(ML_RESTART_MEDIA_SERVER));
}



void
SettingsView::AddInputNodes(NodeList& list)
{
	_EmptyMenu(fInputMenu);

	BMessage message(MEDIA_DEFAULT_INPUT_CHANGE);
	_PopulateMenu(fInputMenu, list, message);
}


void
SettingsView::AddOutputNodes(NodeList& list)
{
	_EmptyMenu(fOutputMenu);

	BMessage message(MEDIA_DEFAULT_OUTPUT_CHANGE);
	_PopulateMenu(fOutputMenu, list, message);
}


void
SettingsView::SetDefaultInput(const dormant_node_info* info)
{
	_ClearMenuSelection(fInputMenu);
	NodeMenuItem* item = _FindNodeItem(fInputMenu, info);
	if (item)
		item->SetMarked(true);
}


void
SettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	_ClearMenuSelection(fOutputMenu);
	NodeMenuItem* item = _FindNodeItem(fOutputMenu, info);
	if (item)
		item->SetMarked(true);
}


void
SettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MEDIA_DEFAULT_INPUT_CHANGE:
		{
			int32 index;
			if (message->FindInt32("index", &index)!=B_OK)
				break;
			NodeMenuItem* item
				= static_cast<NodeMenuItem*>(fInputMenu->ItemAt(index));
			if (item == NULL)
				break;
			status_t status = _ChangeDefaultInput(item->NodeInfo());
			if (status != B_OK)
				_ChangeFailed(item->NodeInfo(), status);
			break;
		}
		case MEDIA_DEFAULT_OUTPUT_CHANGE:
		{
			int32 index;
			if (message->FindInt32("index", &index)!=B_OK)
				break;
			NodeMenuItem* item
				= static_cast<NodeMenuItem*>(fOutputMenu->ItemAt(index));
			if (item == NULL)
				break;
			status_t status = _ChangeDefaultOutput(item->NodeInfo());
			if (status != B_OK)
				_ChangeFailed(item->NodeInfo(), status);
			break;
		}
		default:
			BGroupView::MessageReceived(message);
	}
}


void
SettingsView::AttachedToWindow()
{
	BMessenger thisMessenger(this);
	fInputMenu->SetTargetForItems(thisMessenger);
	fOutputMenu->SetTargetForItems(thisMessenger);
}


MediaWindow*
SettingsView::_MediaWindow() const
{
	return static_cast<MediaWindow*>(Window());
}


status_t
SettingsView::_ChangeDefaultInput(const dormant_node_info* info)
{
	return B_NOT_SUPPORTED;
}


status_t
SettingsView::_ChangeDefaultOutput(const dormant_node_info* info)
{
	return B_NOT_SUPPORTED;
}


status_t
SettingsView::_GetNode(const dormant_node_info* info, media_node* node,
	bool* release)
{
	// A device that appeared after the media services started may have no node.
	BMediaRoster* roster = BMediaRoster::Roster();
	media_node_id nodeID;
	int32 instanceCount = 1;
	*release = false;

	status_t status = roster->GetInstancesFor(info->addon, info->flavor_id,
		&nodeID, &instanceCount);
	if (status != B_OK || instanceCount == 0) {
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: GPL-3.0-or-later
		// A node made here is kept, as BMediaRoster::SetAudioOutput() with a
		// dormant_node_info does: releasing it would delete the new default.
		// SPDX-SnippetEnd
		return roster->InstantiateDormantNode(*info, node, B_FLAVOR_IS_GLOBAL);
	}
	status = roster->GetNodeFor(nodeID, node);
	*release = status == B_OK;
	return status;
}


void
SettingsView::_ChangeFailed(const dormant_node_info* info, status_t status)
{
	BString text(B_TRANSLATE("Could not make \"%device%\" the default:\n"
		"%error%"));
	text.ReplaceFirst("%device%", info->name);
	text.ReplaceFirst("%error%", strerror(status));

	BAlert* alert = new BAlert(B_TRANSLATE("Error"), text, B_TRANSLATE("OK"),
		NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(NULL);

	// The menu has marked the item already. Show what is in use.
	Window()->PostMessage(ML_UPDATE_DEFAULTS);
}


void
SettingsView::_EmptyMenu(BMenu* menu)
{
	while (menu->CountItems() > 0)
		delete menu->RemoveItem((int32)0);
}


void
SettingsView::_PopulateMenu(BMenu* menu, NodeList& nodes,
	const BMessage& message)
{
	for (int32 i = 0; i < nodes.CountItems(); i++) {
		dormant_node_info* info = nodes.ItemAt(i);
		menu->AddItem(new NodeMenuItem(info, new BMessage(message)));
	}

	if (Window() != NULL)
		menu->SetTargetForItems(BMessenger(this));
}


NodeMenuItem*
SettingsView::_FindNodeItem(BMenu* menu, const dormant_node_info* nodeInfo)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		NodeMenuItem* item = static_cast<NodeMenuItem*>(menu->ItemAt(i));
		const dormant_node_info* itemInfo = item->NodeInfo();
		if (itemInfo && itemInfo->addon == nodeInfo->addon
			&& itemInfo->flavor_id == nodeInfo->flavor_id) {
			return item;
		}
	}
	return NULL;
}


void
SettingsView::_ClearMenuSelection(BMenu* menu)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		item->SetMarked(false);
	}
}


NodeMenuItem::NodeMenuItem(const dormant_node_info* info, BMessage* message,
	char shortcut, uint32 modifiers)
	:
	BMenuItem(info->name, message, shortcut, modifiers),
	fInfo(info)
{

}


status_t
NodeMenuItem::Invoke(BMessage* message)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: GPL-3.0-or-later
	// The marked device may only stand in for a saved one that is absent.
	// Choosing it again tells media_server to keep it, so it is sent too.
	// SPDX-SnippetEnd
	return BMenuItem::Invoke(message);
}


ChannelMenuItem::ChannelMenuItem(media_input* input, BMessage* message,
	char shortcut, uint32 modifiers)
	:
	BMenuItem(input->name, message, shortcut, modifiers),
	fInput(input)
{
}


ChannelMenuItem::~ChannelMenuItem()
{
	delete fInput;
}


int32
ChannelMenuItem::DestinationID()
{
	return fInput->destination.id;
}


media_input*
ChannelMenuItem::Input()
{
	return fInput;
}


status_t
ChannelMenuItem::Invoke(BMessage* message)
{
	if (IsMarked())
		return B_OK;
	return BMenuItem::Invoke(message);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The HDA and usb_audio drivers read their settings with
// load_driver_settings() in the kernel. There B_USER_SETTINGS_DIRECTORY is
// always kUserSettingsDirectory (headers/private/system/directories.h), not
// the home of the person running Media. The mixer runs in media_addon_server,
// a system service, and reads the same place. So these files are written
// there, and only someone who may write that directory can change them.
static const char* kKernelSettingsDirectory = "/boot/home/config/settings";
static const char* kDriverSettingsSubdirectory = "kernel/drivers";

// Larger than any settings file Media writes. A bigger file is left alone
// instead of being cut off.
static const off_t kMaxSettingsFileSize = 1024 * 1024;

static const char* kLatencyKeys[] = {
	"play_buffer_frames",
	"record_buffer_frames"
};
static const char* kLayoutKeys[] = { "channel_layout" };


static status_t
driver_settings_path(const char* fileName, char* path, size_t pathSize)
{
	if (snprintf(path, pathSize, "%s/%s/%s", kKernelSettingsDirectory,
			kDriverSettingsSubdirectory, fileName) >= (int)pathSize)
		return B_NAME_TOO_LONG;
	return B_OK;
}


static bool
can_write_driver_settings()
{
	// The directory may not exist yet. Then the nearest existing parent
	// decides whether it can be made.
	char path[B_PATH_NAME_LENGTH];
	if (snprintf(path, sizeof(path), "%s/%s", kKernelSettingsDirectory,
			kDriverSettingsSubdirectory) >= (int)sizeof(path))
		return false;

	while (true) {
		if (access(path, F_OK) == 0)
			return access(path, W_OK) == 0;
		char* slash = strrchr(path, '/');
		if (slash == NULL || slash == path)
			return false;
		*slash = '\0';
	}
}


static status_t
make_driver_settings_directory(char* failedPath, size_t pathSize)
{
	// BPath::SetTo and create_directory both walk the path with a BEntry.
	// That left kernel/drivers on disk and never created the file, so each
	// level is made with mkdir().
	char directory[B_PATH_NAME_LENGTH];
	const char* levels[] = { "kernel", kDriverSettingsSubdirectory };
	for (uint32 i = 0; i < B_COUNT_OF(levels); i++) {
		if (snprintf(directory, sizeof(directory), "%s/%s",
				kKernelSettingsDirectory, levels[i]) >= (int)sizeof(directory))
			return B_NAME_TOO_LONG;
		if (mkdir(directory, 0755) < 0 && errno != EEXIST) {
			status_t error = errno != 0 ? errno : B_ERROR;
			strlcpy(failedPath, directory, pathSize);
			return error;
		}
	}
	return B_OK;
}


static status_t
read_settings_file(const char* fileName, BString& text)
{
	text.Truncate(0);

	char path[B_PATH_NAME_LENGTH];
	status_t status = driver_settings_path(fileName, path, sizeof(path));
	if (status != B_OK)
		return status;

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;

	struct stat st;
	if (fstat(fd, &st) != 0) {
		status = errno != 0 ? errno : B_ERROR;
		close(fd);
		return status;
	}
	if (st.st_size > kMaxSettingsFileSize) {
		close(fd);
		return B_FILE_TOO_LARGE;
	}
	if (st.st_size == 0) {
		close(fd);
		return B_OK;
	}

	char* buffer = text.LockBuffer((int32)st.st_size);
	if (buffer == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}
	ssize_t length = read(fd, buffer, (size_t)st.st_size);
	status = length < 0 ? (errno != 0 ? errno : B_ERROR) : B_OK;
	close(fd);
	text.UnlockBuffer(length > 0 ? (int32)length : 0);
	return status;
}


static bool
is_settings_key(const BString& line, const char* key)
{
	int32 length = (int32)strlen(key);
	if (!line.StartsWith(key))
		return false;
	if (line.Length() == length)
		return true;
	char next = line.ByteAt(length);
	return next == ' ' || next == '\t' || next == '\r';
}


static bool
read_setting(const char* fileName, const char* key, BString& value)
{
	BString text;
	if (read_settings_file(fileName, text) != B_OK)
		return false;

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		line.RemoveAll("\r");
		if (is_settings_key(line, key)) {
			line.Remove(0, (int32)strlen(key));
			value = line.Trim();
			return true;
		}
		start = end + 1;
	}
	return false;
}


static status_t
write_settings_text(const char* path, const BString& text)
{
	// A temporary file and rename(), so a crash or a full disk never leaves
	// an empty file. rename() also replaces a file owned by root in a
	// directory this person may write.
	char tempPath[B_PATH_NAME_LENGTH];
	if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", path)
			>= (int)sizeof(tempPath))
		return B_NAME_TOO_LONG;

	int fd = open(tempPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;

	status_t status = B_OK;
	ssize_t written = write(fd, text.String(), (size_t)text.Length());
	if (written != (ssize_t)text.Length())
		status = written < 0 && errno != 0 ? errno : B_ERROR;
	else if (fsync(fd) != 0)
		status = errno != 0 ? errno : B_ERROR;
	close(fd);

	if (status == B_OK && rename(tempPath, path) != 0)
		status = errno != 0 ? errno : B_ERROR;
	if (status != B_OK)
		unlink(tempPath);
	return status;
}


// Replaces the lines that start with one of keys by lines. The other lines
// of the file stay.
static status_t
write_setting(const char* fileName, const char* const* keys, uint32 keyCount,
	const BString& lines, char* writtenPath, size_t pathSize)
{
	status_t status = driver_settings_path(fileName, writtenPath, pathSize);
	if (status != B_OK)
		return status;
	status = make_driver_settings_directory(writtenPath, pathSize);
	if (status != B_OK)
		return status;

	BString text;
	status = read_settings_file(fileName, text);
	if (status != B_OK && status != B_ENTRY_NOT_FOUND)
		return status;

	BString kept;
	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		bool replaced = false;
		for (uint32 i = 0; i < keyCount; i++) {
			if (is_settings_key(line, keys[i])) {
				replaced = true;
				break;
			}
		}
		if (!replaced)
			kept << line << "\n";
		start = end + 1;
	}
	kept << lines;

	return write_settings_text(writtenPath, kept);
}


static int32
read_latency_frames()
{
	BString value;
	if (!read_setting("hda.settings", "play_buffer_frames", value))
		return 0;
	int32 frames = atoi(value.String());
	return frames == 256 || frames == 128 ? frames : 0;
}


static status_t
write_latency_file(const char* fileName, int32 frames, char* writtenPath,
	size_t pathSize)
{
	BString lines;
	if (frames == 256 || frames == 128) {
		lines << "play_buffer_frames " << frames << "\n";
		lines << "record_buffer_frames " << frames << "\n";
	}
	return write_setting(fileName, kLatencyKeys, B_COUNT_OF(kLatencyKeys),
		lines, writtenPath, pathSize);
}


static int32
read_channel_layout()
{
	BString value;
	if (!read_setting("usb_audio", "channel_layout", value))
		return 0;
	int32 channels = atoi(value.String());
	return channels >= 1 && channels <= 16 ? channels : 0;
}


static status_t
write_channel_layout(int32 channels, char* writtenPath, size_t pathSize)
{
	BString lines;
	if (channels >= 1 && channels <= 16)
		lines << "channel_layout " << channels << "\n";
	else
		lines << "channel_layout auto\n";
	return write_setting("usb_audio", kLayoutKeys, B_COUNT_OF(kLayoutKeys),
		lines, writtenPath, pathSize);
}


static bool
read_virtual_surround()
{
	BString value;
	return read_setting("virtual_surround", "virtual_surround", value)
		&& value == "on";
}


static status_t
write_virtual_surround(bool enabled, char* writtenPath, size_t pathSize)
{
	status_t status = driver_settings_path("virtual_surround", writtenPath,
		pathSize);
	if (status != B_OK)
		return status;
	status = make_driver_settings_directory(writtenPath, pathSize);
	if (status != B_OK)
		return status;

	// The mixer only reads the first line of this file, so the whole file is
	// this one line.
	BString text(enabled ? "virtual_surround on\n" : "virtual_surround off\n");
	return write_settings_text(writtenPath, text);
}


static bool
has_usb_audio_device()
{
	// usb_audio publishes its cards as audio/hmulti/usb/<n> (sDeviceBaseName
	// in its Driver.cpp).
	BDirectory directory("/dev/audio/hmulti/usb");
	return directory.InitCheck() == B_OK && directory.CountEntries() > 0;
}


static void
mark_item(BMenu* menu, const char* field, int32 value)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		int32 itemValue;
		if (item->Message() != NULL
			&& item->Message()->FindInt32(field, &itemValue) == B_OK
			&& itemValue == value) {
			item->SetMarked(true);
			return;
		}
	}
}
// SPDX-SnippetEnd


AudioSettingsView::AudioSettingsView()
{
	BBox* defaultsBox = new BBox("defaults");
	defaultsBox->SetLabel(B_TRANSLATE("Defaults"));
	BGridView* defaultsGridView = new BGridView();

	BMenuField* inputMenuField = new BMenuField("inputMenuField",
		B_TRANSLATE("Audio input:"), InputMenu());

	BMenuField* outputMenuField = new BMenuField("outputMenuField",
		B_TRANSLATE("Audio output:"), OutputMenu());

	BLayoutBuilder::Grid<>(defaultsGridView)
		.SetInsets(B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING,
			B_USE_DEFAULT_SPACING)
		.AddMenuField(inputMenuField, 0, 0, B_ALIGN_HORIZONTAL_UNSET, 1, 3, 1)
		.AddMenuField(outputMenuField, 0, 1)
		.AddMenuField(_MakeChannelMenu(), 2, 1)
		.AddMenuField(_MakeLayoutMenu(), 0, 2, B_ALIGN_HORIZONTAL_UNSET, 1, 3, 1)
		.AddMenuField(_MakeLatencyMenu(), 0, 3, B_ALIGN_HORIZONTAL_UNSET, 1, 3, 1)
		.AddMenuField(_MakeSurroundMenu(), 0, 4, B_ALIGN_HORIZONTAL_UNSET, 1, 3, 1)
		.Add(fLatencyHint = new BStringView("latency hint", ""), 0, 5, 3);

	defaultsBox->AddChild(defaultsGridView);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: GPL-3.0-or-later
	// The drivers and the mixer read these files from the administrator's
	// settings. Someone who cannot write there sees the values but cannot
	// change them.
	BMenuField* layoutField
		= dynamic_cast<BMenuField*>(defaultsBox->FindView("layoutMenuField"));
	BMenuField* latencyField
		= dynamic_cast<BMenuField*>(defaultsBox->FindView("latencyMenuField"));
	BMenuField* surroundField
		= dynamic_cast<BMenuField*>(defaultsBox->FindView("surroundMenuField"));
	if (!can_write_driver_settings()) {
		if (layoutField != NULL)
			layoutField->SetEnabled(false);
		if (latencyField != NULL)
			latencyField->SetEnabled(false);
		if (surroundField != NULL)
			surroundField->SetEnabled(false);
		fLatencyHint->SetText(B_TRANSLATE(
			"Only an administrator can change the USB layout, latency and "
			"virtual surround."));
	} else if (!has_usb_audio_device() && layoutField != NULL)
		layoutField->SetEnabled(false);
	// SPDX-SnippetEnd

	BLayoutBuilder::Group<>(this)
		.SetInsets(0, 0, 0, 0)
		.Add(defaultsBox)
		.AddGroup(B_HORIZONTAL)
			.Add(_MakeVolumeCheckBox())
			.AddGlue()
			.Add(MakeRestartButton())
			.End()
		.AddGlue();
}


void
AudioSettingsView::SetDefaultChannel(int32 channelID)
{
	for (int32 i = 0; i < fChannelMenu->CountItems(); i++) {
		ChannelMenuItem* item = _ChannelMenuItemAt(i);
		item->SetMarked(item->DestinationID() == channelID);
	}
}


void
AudioSettingsView::AttachedToWindow()
{
	SettingsView::AttachedToWindow();

	BMessenger thisMessenger(this);
	fChannelMenu->SetTargetForItems(thisMessenger);
	fLayoutMenu->SetTargetForItems(thisMessenger);
	fLatencyMenu->SetTargetForItems(thisMessenger);
	fSurroundMenu->SetTargetForItems(thisMessenger);
	fVolumeCheckBox->SetTarget(thisMessenger);
}


void
AudioSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case ML_DEFAULT_CHANNEL_CHANGED:
			{
				int32 index;
				if (message->FindInt32("index", &index) != B_OK)
					break;
				ChannelMenuItem* item = _ChannelMenuItemAt(index);

				if (item) {
					BMediaRoster* roster = BMediaRoster::Roster();
					roster->SetAudioOutput(*item->Input());
				} else
					fprintf(stderr, "ChannelMenuItem not found\n");
			}
			break;
		case MEDIA_SHOW_HIDE_VOLUME_CONTROL:
		{
			if (fVolumeCheckBox->Value() == B_CONTROL_ON)
				_ShowDeskbarVolumeControl();
			else
				_HideDeskbarVolumeControl();
			break;
		}
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: GPL-3.0-or-later
		case ML_USB_CHANNEL_LAYOUT:
		{
			int32 channels = 0;
			if (message->FindInt32("channels", &channels) != B_OK)
				break;
			char path[B_PATH_NAME_LENGTH];
			path[0] = '\0';
			status_t status = write_channel_layout(channels, path, sizeof(path));
			if (status != B_OK) {
				BString text(B_TRANSLATE("Could not save the channel layout."));
				text << "\n" << path << "\n" << strerror(status);
				(new BAlert("layout", text.String(), B_TRANSLATE("OK")))->Go();
				// The menu marked the new item already. Show what is saved.
				mark_item(fLayoutMenu, "channels", read_channel_layout());
				break;
			}
			fLatencyHint->SetText(B_TRANSLATE(
				"Channel layout saved. Restart media services to apply."));
			break;
		}
		case ML_AUDIO_LATENCY:
		{
			int32 frames = 0;
			if (message->FindInt32("frames", &frames) != B_OK)
				break;
			char path[B_PATH_NAME_LENGTH];
			path[0] = '\0';
			const int32 previous = read_latency_frames();
			status_t status = write_latency_file("hda.settings", frames, path,
				sizeof(path));
			if (status == B_OK) {
				char usbPath[B_PATH_NAME_LENGTH];
				usbPath[0] = '\0';
				status = write_latency_file("usb_audio", frames, usbPath,
					sizeof(usbPath));
				if (status != B_OK) {
					if (usbPath[0] != '\0')
						strlcpy(path, usbPath, sizeof(path));
					// Both drivers keep the same choice.
					char ignored[B_PATH_NAME_LENGTH];
					write_latency_file("hda.settings", previous, ignored,
						sizeof(ignored));
				}
			}
			if (status != B_OK) {
				BString text(B_TRANSLATE("Could not save the latency choice."));
				text << "\n" << path << "\n" << strerror(status);
				(new BAlert("latency", text.String(), B_TRANSLATE("OK")))->Go();
				mark_item(fLatencyMenu, "frames", read_latency_frames());
				break;
			}
			if (frames == 256 || frames == 128) {
				fLatencyHint->SetText(B_TRANSLATE(
					"Saved. Restart media services to apply."));
			} else {
				fLatencyHint->SetText(B_TRANSLATE(
					"Normal buffer saved. Restart media services to apply."));
			}
			break;
		}
		case ML_VIRTUAL_SURROUND:
		{
			int32 enabled = 0;
			if (message->FindInt32("enabled", &enabled) != B_OK)
				break;
			char path[B_PATH_NAME_LENGTH];
			path[0] = '\0';
			status_t status = write_virtual_surround(enabled != 0, path,
				sizeof(path));
			if (status != B_OK) {
				BString text(B_TRANSLATE("Could not save virtual surround."));
				text << "\n" << path << "\n" << strerror(status);
				(new BAlert("surround", text.String(), B_TRANSLATE("OK")))->Go();
				mark_item(fSurroundMenu, "enabled",
					read_virtual_surround() ? 1 : 0);
				break;
			}
			fLatencyHint->SetText(B_TRANSLATE(
				"Virtual surround saved. Restart media services to apply."));
			break;
		}

		// SPDX-SnippetEnd
		default:
			SettingsView::MessageReceived(message);
	}
}


void
AudioSettingsView::SetDefaultInput(const dormant_node_info* info)
{
	SettingsView::SetDefaultInput(info);
	_MediaWindow()->UpdateInputListItem(MediaListItem::AUDIO_TYPE, info);
}


status_t
AudioSettingsView::_ChangeDefaultInput(const dormant_node_info* info)
{
	media_node node;
	bool release;
	status_t status = _GetNode(info, &node, &release);
	if (status == B_OK)
		status = BMediaRoster::Roster()->SetAudioInput(node);
	if (release)
		BMediaRoster::Roster()->ReleaseNode(node);
	if (status == B_OK)
		SetDefaultInput(info);
	return status;
}


void
AudioSettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	SettingsView::SetDefaultOutput(info);
	_MediaWindow()->UpdateOutputListItem(MediaListItem::AUDIO_TYPE, info);
	_FillChannelMenu(info);
}


status_t
AudioSettingsView::_ChangeDefaultOutput(const dormant_node_info* info)
{
	media_node node;
	bool release;
	status_t status = _GetNode(info, &node, &release);
	if (status == B_OK)
		status = BMediaRoster::Roster()->SetAudioOutput(node);
	if (release)
		BMediaRoster::Roster()->ReleaseNode(node);
	if (status == B_OK)
		SetDefaultOutput(info);
	return status;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
BMenuField*
AudioSettingsView::_MakeLatencyMenu()
{
	fLatencyMenu = new BPopUpMenu(B_TRANSLATE("Normal"));
	fLatencyMenu->SetLabelFromMarked(true);

	const int32 current = read_latency_frames();
	struct latency_choice {
		int32 frames;
		const char* label;
	};
	const latency_choice choices[] = {
		{ 0, B_TRANSLATE_COMMENT("Normal",
			"Keeps the sound card's usual buffer") },
		{ 256, B_TRANSLATE("Low latency (256 frames)") },
		{ 128, B_TRANSLATE("Pro audio (128 frames)") },
	};
	for (uint32 i = 0; i < B_COUNT_OF(choices); i++) {
		BMessage* message = new BMessage(ML_AUDIO_LATENCY);
		message->AddInt32("frames", choices[i].frames);
		BMenuItem* item = new BMenuItem(choices[i].label, message);
		fLatencyMenu->AddItem(item);
		if (choices[i].frames == current)
			item->SetMarked(true);
	}

	return new BMenuField("latencyMenuField", B_TRANSLATE("Latency:"),
		fLatencyMenu);
}


BMenuField*
AudioSettingsView::_MakeSurroundMenu()
{
	fSurroundMenu = new BPopUpMenu(B_TRANSLATE("Off"));
	fSurroundMenu->SetLabelFromMarked(true);

	const bool current = read_virtual_surround();
	struct surround_choice {
		int32 enabled;
		const char* label;
	};
	const surround_choice choices[] = {
		{ 0, B_TRANSLATE("Off") },
		{ 1, B_TRANSLATE("On") },
	};
	for (uint32 i = 0; i < B_COUNT_OF(choices); i++) {
		BMessage* message = new BMessage(ML_VIRTUAL_SURROUND);
		message->AddInt32("enabled", choices[i].enabled);
		BMenuItem* item = new BMenuItem(choices[i].label, message);
		fSurroundMenu->AddItem(item);
		if ((choices[i].enabled != 0) == current)
			item->SetMarked(true);
	}

	return new BMenuField("surroundMenuField", B_TRANSLATE("Virtual surround:"),
		fSurroundMenu);
}


BMenuField*
AudioSettingsView::_MakeLayoutMenu()
{
	fLayoutMenu = new BPopUpMenu(B_TRANSLATE("Automatic"));
	fLayoutMenu->SetLabelFromMarked(true);

	const int32 current = read_channel_layout();
	struct layout_choice {
		int32 channels;
		const char* label;
	};
	const layout_choice choices[] = {
		{ 0, B_TRANSLATE("Automatic") },
		{ 2, B_TRANSLATE("Stereo (2.0)") },
		{ 4, B_TRANSLATE("4 channels") },
		{ 6, B_TRANSLATE("5.1") },
		{ 8, B_TRANSLATE("7.1") },
	};
	for (uint32 i = 0; i < B_COUNT_OF(choices); i++) {
		BMessage* message = new BMessage(ML_USB_CHANNEL_LAYOUT);
		message->AddInt32("channels", choices[i].channels);
		BMenuItem* item = new BMenuItem(choices[i].label, message);
		fLayoutMenu->AddItem(item);
		if (choices[i].channels == current)
			item->SetMarked(true);
	}

	return new BMenuField("layoutMenuField", B_TRANSLATE("USB layout:"),
		fLayoutMenu);
}


// SPDX-SnippetEnd


BMenuField*
AudioSettingsView::_MakeChannelMenu()
{
	fChannelMenu = new BPopUpMenu(B_TRANSLATE("<none>"));
	fChannelMenu->SetLabelFromMarked(true);
	BMenuField* channelMenuField = new BMenuField("channelMenuField",
		B_TRANSLATE("Channel:"), fChannelMenu);
	return channelMenuField;
}


BCheckBox*
AudioSettingsView::_MakeVolumeCheckBox()
{
	fVolumeCheckBox = new BCheckBox("volumeCheckBox",
		B_TRANSLATE("Show volume control on Deskbar"),
		new BMessage(MEDIA_SHOW_HIDE_VOLUME_CONTROL));

	if (BDeskbar().HasItem("MediaReplicant"))
		fVolumeCheckBox->SetValue(B_CONTROL_ON);

	return fVolumeCheckBox;
}


void
AudioSettingsView::_FillChannelMenu(const dormant_node_info* nodeInfo)
{
	_EmptyMenu(fChannelMenu);

	BMediaRoster* roster = BMediaRoster::Roster();
	media_node node;
	bool release;
	status_t err = _GetNode(nodeInfo, &node, &release);

	if (err == B_OK) {
		int32 inputCount = 4;
		media_input* inputs = new media_input[inputCount];
		BPrivate::ArrayDeleter<media_input> inputDeleter(inputs);

		while (true) {
			int32 realInputCount = 0;
			err = roster->GetAllInputsFor(node, inputs,
				inputCount, &realInputCount);
			if (realInputCount > inputCount) {
				inputCount *= 2;
				inputs = new media_input[inputCount];
				inputDeleter.SetTo(inputs);
			} else {
				inputCount = realInputCount;
				break;
			}
		}

		if (err == B_OK) {
			BMessage message(ML_DEFAULT_CHANNEL_CHANGED);

			for (int32 i = 0; i < inputCount; i++) {
				media_input* input = new media_input();
				*input = inputs[i];
				ChannelMenuItem* channelItem = new ChannelMenuItem(input,
					new BMessage(message));
				fChannelMenu->AddItem(channelItem);

				if (channelItem->DestinationID() == 0)
					channelItem->SetMarked(true);
			}
		}

		// This runs on every refresh of the window; the reference was never
		// released.
		if (release)
			roster->ReleaseNode(node);
	}

	if (Window())
		fChannelMenu->SetTargetForItems(BMessenger(this));
}


void
AudioSettingsView::_ShowDeskbarVolumeControl()
{
	BDeskbar deskbar;
	BEntry entry("/bin/desklink", true);
	int32 id;
	entry_ref ref;
	status_t status = entry.GetRef(&ref);
	if (status == B_OK)
		status = deskbar.AddItem(&ref, &id);

	if (status != B_OK) {
		fprintf(stderr, B_TRANSLATE(
			"Couldn't add volume control in Deskbar: %s\n"),
			strerror(status));
	}
}


void
AudioSettingsView::_HideDeskbarVolumeControl()
{
	BDeskbar deskbar;
	status_t status = deskbar.RemoveItem("MediaReplicant");
	if (status != B_OK) {
		fprintf(stderr, B_TRANSLATE(
			"Couldn't remove volume control in Deskbar: %s\n"),
			strerror(status));
	}
}


ChannelMenuItem*
AudioSettingsView::_ChannelMenuItemAt(int32 index)
{
	return static_cast<ChannelMenuItem*>(fChannelMenu->ItemAt(index));
}


VideoSettingsView::VideoSettingsView()
{
	BBox* defaultsBox = new BBox("defaults");
	defaultsBox->SetLabel(B_TRANSLATE("Defaults"));
	BGridView* defaultsGridView = new BGridView();

	BMenuField* inputMenuField = new BMenuField("inputMenuField",
		B_TRANSLATE("Video input:"), InputMenu());

	BMenuField* outputMenuField = new BMenuField("outputMenuField",
		B_TRANSLATE("Video output:"), OutputMenu());

	BLayoutBuilder::Grid<>(defaultsGridView)
		.SetInsets(B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING,
			B_USE_DEFAULT_SPACING)
		.AddMenuField(inputMenuField, 0, 0)
		.AddMenuField(outputMenuField, 0, 1);

	defaultsBox->AddChild(defaultsGridView);

	BLayoutBuilder::Group<>(this)
		.SetInsets(0, 0, 0, 0)
		.Add(defaultsBox)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(MakeRestartButton())
			.End()
		.AddGlue();
}


void
VideoSettingsView::SetDefaultInput(const dormant_node_info* info)
{
	SettingsView::SetDefaultInput(info);
	_MediaWindow()->UpdateInputListItem(MediaListItem::VIDEO_TYPE, info);
}


status_t
VideoSettingsView::_ChangeDefaultInput(const dormant_node_info* info)
{
	media_node node;
	bool release;
	status_t status = _GetNode(info, &node, &release);
	if (status == B_OK)
		status = BMediaRoster::Roster()->SetVideoInput(node);
	if (release)
		BMediaRoster::Roster()->ReleaseNode(node);
	if (status == B_OK)
		SetDefaultInput(info);
	return status;
}


void
VideoSettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	SettingsView::SetDefaultOutput(info);
	_MediaWindow()->UpdateOutputListItem(MediaListItem::VIDEO_TYPE, info);
}


status_t
VideoSettingsView::_ChangeDefaultOutput(const dormant_node_info* info)
{
	media_node node;
	bool release;
	status_t status = _GetNode(info, &node, &release);
	if (status == B_OK)
		status = BMediaRoster::Roster()->SetVideoOutput(node);
	if (release)
		BMediaRoster::Roster()->ReleaseNode(node);
	if (status == B_OK)
		SetDefaultOutput(info);
	return status;
}
