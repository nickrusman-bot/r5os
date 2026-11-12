/*
 * Copyright 2003-2011, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Sikosis
 *		Jérôme Duval
 */


#include "MediaViews.h"

#include <AdminLock.h>
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
#include <MessageRunner.h>
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

#include <R5Icons.h>


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

// The HDA and usb_audio drivers read these files with load_driver_settings()
// in the kernel, and the mixer reads virtual_surround in media_addon_server.
// Both read the administrator's kernel/drivers directory, not the home of the
// person running Media (libr5ui, r5_driver_settings_*()). A standard account
// writes them through Login after unlocking.

static const char* kLatencyKeys[] = {
	"play_buffer_frames",
	"record_buffer_frames"
};
static const char* kLayoutKeys[] = { "channel_layout" };
static const char* kSurroundKeys[] = { "virtual_surround" };

static const uint32 kMsgMediaLock = 'mlck';
static const uint32 kMsgMediaLockRefresh = 'mlcr';
// The grant runs out after a few minutes; the lock follows it.
static const bigtime_t kLockRefreshInterval = 15000000;


static status_t
save_setting(const char* fileName, const char* const* keys, int32 keyCount,
	const BString& lines, bool replaceFile, BString& error)
{
	status_t status;
	if (r5_driver_settings_writable()) {
		status = r5_driver_settings_write(fileName, keys, keyCount,
			lines.String(), replaceFile);
		if (status != B_OK)
			error = strerror(status);
	} else {
		status = r5_admin_set_driver_setting(fileName, keys, keyCount,
			lines.String(), replaceFile, error);
	}
	return status;
}


static int32
read_latency_frames()
{
	BString value;
	if (r5_driver_settings_read("hda.settings", "play_buffer_frames", value)
			!= B_OK)
		return 0;
	int32 frames = atoi(value.String());
	return frames == 256 || frames == 128 ? frames : 0;
}


static status_t
write_latency_file(const char* fileName, int32 frames, BString& error)
{
	BString lines;
	if (frames == 256 || frames == 128) {
		lines << "play_buffer_frames " << frames << "\n";
		lines << "record_buffer_frames " << frames << "\n";
	}
	return save_setting(fileName, kLatencyKeys, B_COUNT_OF(kLatencyKeys),
		lines, false, error);
}


static int32
read_channel_layout()
{
	BString value;
	if (r5_driver_settings_read("usb_audio", "channel_layout", value) != B_OK)
		return 0;
	int32 channels = atoi(value.String());
	return channels >= 1 && channels <= 16 ? channels : 0;
}


static status_t
write_channel_layout(int32 channels, BString& error)
{
	BString lines;
	if (channels >= 1 && channels <= 16)
		lines << "channel_layout " << channels << "\n";
	else
		lines << "channel_layout auto\n";
	return save_setting("usb_audio", kLayoutKeys, B_COUNT_OF(kLayoutKeys),
		lines, false, error);
}


static bool
read_virtual_surround()
{
	BString value;
	return r5_driver_settings_read("virtual_surround", "virtual_surround",
			value) == B_OK
		&& value == "on";
}


static status_t
write_virtual_surround(bool enabled, BString& error)
{
	// The mixer only reads the first line of this file, so the whole file is
	// this one line.
	BString lines(enabled ? "virtual_surround on\n" : "virtual_surround off\n");
	return save_setting("virtual_surround", kSurroundKeys,
		B_COUNT_OF(kSurroundKeys), lines, true, error);
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
		.Add(fLatencyHint = new BStringView("latency hint", ""), 0, 5, 2)
		.Add(fLockButton = new BButton("lock", "", new BMessage(kMsgMediaLock)),
			2, 5);

	defaultsBox->AddChild(defaultsGridView);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: GPL-3.0-or-later
	// The drivers and the mixer read these files from the administrator's
	// settings. Someone who cannot write there unlocks them with an
	// administrator password, as in Settings; Login then writes them.
	fLayoutField
		= dynamic_cast<BMenuField*>(defaultsBox->FindView("layoutMenuField"));
	fLatencyField
		= dynamic_cast<BMenuField*>(defaultsBox->FindView("latencyMenuField"));
	fSurroundField
		= dynamic_cast<BMenuField*>(defaultsBox->FindView("surroundMenuField"));
	fLockRunner = NULL;
	fNeedsLock = !r5_driver_settings_writable();
	fUnlocked = !fNeedsLock;
	fHasUsbAudio = has_usb_audio_device();
	float lockHeight = fLockButton->PreferredSize().Height();
	r5_button_set_lock(fLockButton, true, lockHeight);
	if (!fNeedsLock)
		fLockButton->Hide();
	_UpdateLock();
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

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: GPL-3.0-or-later
	fLockButton->SetTarget(thisMessenger);
	if (fNeedsLock && fLockRunner == NULL) {
		BMessage refresh(kMsgMediaLockRefresh);
		fLockRunner = new BMessageRunner(thisMessenger, &refresh,
			kLockRefreshInterval);
		_RefreshLock();
	}
	// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
void
AudioSettingsView::DetachedFromWindow()
{
	delete fLockRunner;
	fLockRunner = NULL;
	SettingsView::DetachedFromWindow();
}


void
AudioSettingsView::_RefreshLock()
{
	if (!fNeedsLock)
		return;
	bool unlocked = r5_admin_change_allowed();
	if (unlocked != fUnlocked) {
		fUnlocked = unlocked;
		_UpdateLock();
	}
}


void
AudioSettingsView::_UpdateLock()
{
	if (fNeedsLock) {
		r5_button_set_lock(fLockButton, !fUnlocked,
			fLockButton->Frame().IsValid() ? fLockButton->Frame().Height()
				: fLockButton->PreferredSize().Height());
	}
	if (fLayoutField != NULL)
		fLayoutField->SetEnabled(fUnlocked && fHasUsbAudio);
	if (fLatencyField != NULL)
		fLatencyField->SetEnabled(fUnlocked);
	if (fSurroundField != NULL)
		fSurroundField->SetEnabled(fUnlocked);

	if (fNeedsLock && !fUnlocked) {
		fLatencyHint->SetText(B_TRANSLATE(
			"Unlock to change the USB layout, latency and virtual surround."));
	} else if (fNeedsLock)
		fLatencyHint->SetText("");
}


void
AudioSettingsView::_SaveFailed(const char* what, const BString& error,
	status_t status)
{
	BString text(what);
	text << "\n" << (error.Length() > 0 ? error.String() : strerror(status));
	(new BAlert("media settings", text.String(), B_TRANSLATE("OK")))->Go();
	// A grant that ran out shows the lock closed again.
	_RefreshLock();
}
// SPDX-SnippetEnd


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
			BString error;
			status_t status = write_channel_layout(channels, error);
			if (status != B_OK) {
				_SaveFailed(B_TRANSLATE("Could not save the channel layout."),
					error, status);
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
			BString error;
			const int32 previous = read_latency_frames();
			status_t status = write_latency_file("hda.settings", frames, error);
			if (status == B_OK) {
				status = write_latency_file("usb_audio", frames, error);
				if (status != B_OK) {
					// Both drivers keep the same choice.
					BString ignored;
					write_latency_file("hda.settings", previous, ignored);
				}
			}
			if (status != B_OK) {
				_SaveFailed(B_TRANSLATE("Could not save the latency choice."),
					error, status);
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
			BString error;
			status_t status = write_virtual_surround(enabled != 0, error);
			if (status != B_OK) {
				_SaveFailed(B_TRANSLATE("Could not save virtual surround."),
					error, status);
				mark_item(fSurroundMenu, "enabled",
					read_virtual_surround() ? 1 : 0);
				break;
			}
			fLatencyHint->SetText(B_TRANSLATE(
				"Virtual surround saved. Restart media services to apply."));
			break;
		}
		case kMsgMediaLock:
			if (fUnlocked)
				r5_admin_relock();
			else
				r5_admin_prompt_unlock(BMessenger(this));
			_RefreshLock();
			break;
		case kMsgAdminLockChanged:
		case kMsgMediaLockRefresh:
			_RefreshLock();
			break;

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
