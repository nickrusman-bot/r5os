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
			SetDefaultInput(item->NodeInfo());
			break;
		}
		case MEDIA_DEFAULT_OUTPUT_CHANGE:
		{
			int32 index;
			if (message->FindInt32("index", &index)!=B_OK)
				break;
			NodeMenuItem* item
				= static_cast<NodeMenuItem*>(fOutputMenu->ItemAt(index));
			SetDefaultOutput(item->NodeInfo());
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
	if (IsMarked())
		return B_OK;
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


static bool
is_latency_key(const BString& line, const char* key)
{
	int32 length = (int32)strlen(key);
	if (!line.StartsWith(key))
		return false;
	if (line.Length() == length)
		return true;
	char next = line.ByteAt(length);
	return next == ' ' || next == '\t' || next == '\r';
}


static status_t
driver_settings_file(const char* fileName, char* path, size_t pathSize)
{
	// The HDA driver reads this through find_directory(), then open().
	// BPath::SetTo and create_directory both walk the path with a BEntry.
	// That left kernel/drivers on disk and never created the file.
	char settings[B_PATH_NAME_LENGTH];
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, (dev_t)-1,
		true, settings, (int32)sizeof(settings));
	if (status != B_OK)
		strlcpy(settings, "/boot/home/config/settings", sizeof(settings));

	char directory[B_PATH_NAME_LENGTH];
	if (snprintf(directory, sizeof(directory), "%s/kernel", settings)
			>= (int)sizeof(directory))
		return B_NAME_TOO_LONG;
	if (mkdir(directory, 0755) < 0 && errno != EEXIST)
		return errno != 0 ? errno : B_ERROR;
	chmod(directory, 0755);

	if (snprintf(directory, sizeof(directory), "%s/kernel/drivers", settings)
			>= (int)sizeof(directory))
		return B_NAME_TOO_LONG;
	if (mkdir(directory, 0755) < 0 && errno != EEXIST)
		return errno != 0 ? errno : B_ERROR;
	chmod(directory, 0755);

	if (snprintf(path, pathSize, "%s/%s", directory, fileName) >= (int)pathSize)
		return B_NAME_TOO_LONG;
	return B_OK;
}


static int32
read_latency_frames()
{
	char path[B_PATH_NAME_LENGTH];
	if (driver_settings_file("hda.settings", path, sizeof(path)) != B_OK)
		return 0;

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;

	char bytes[32768];
	ssize_t length = read(fd, bytes, sizeof(bytes) - 1);
	close(fd);
	if (length <= 0)
		return 0;

	BString text;
	char* buffer = text.LockBuffer((int32)length);
	if (buffer == NULL)
		return 0;
	memcpy(buffer, bytes, (size_t)length);
	text.UnlockBuffer((int32)length);

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		line.RemoveAll("\r");
		if (is_latency_key(line, "play_buffer_frames")) {
			int32 value = 0;
			if (sscanf(line.String(), "play_buffer_frames %" B_PRId32, &value)
					== 1 && (value == 256 || value == 128))
				return value;
			return 0;
		}
		start = end + 1;
	}
	return 0;
}


static status_t
write_latency_file(const char* fileName, int32 frames, char* writtenPath,
	size_t pathSize)
{
	status_t status = driver_settings_file(fileName, writtenPath, pathSize);
	if (status != B_OK)
		return status;

	BString kept;
	int existingFd = open(writtenPath, O_RDONLY);
	if (existingFd >= 0) {
		char bytes[32768];
		ssize_t length = read(existingFd, bytes, sizeof(bytes) - 1);
		close(existingFd);
		if (length > 0) {
			BString text;
			char* buffer = text.LockBuffer((int32)length);
			if (buffer != NULL) {
				memcpy(buffer, bytes, (size_t)length);
				text.UnlockBuffer((int32)length);
				int32 start = 0;
				while (start < text.Length()) {
					int32 end = text.FindFirst('\n', start);
					if (end < 0)
						end = text.Length();
					BString line;
					text.CopyInto(line, start, end - start);
					if (!is_latency_key(line, "play_buffer_frames")
						&& !is_latency_key(line, "record_buffer_frames")) {
						kept << line << "\n";
					}
					start = end + 1;
				}
			}
		}
	}

	if (frames == 256 || frames == 128) {
		kept << "play_buffer_frames " << frames << "\n";
		kept << "record_buffer_frames " << frames << "\n";
	}

	int fd = open(writtenPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		// The directory can exist without write permission for this user.
		char directory[B_PATH_NAME_LENGTH];
		strlcpy(directory, writtenPath, sizeof(directory));
		char* slash = strrchr(directory, '/');
		if (slash != NULL) {
			*slash = '\0';
			chmod(directory, 0755);
		}
		fd = open(writtenPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	}
	if (fd < 0)
		return errno != 0 ? errno : B_ERROR;

	ssize_t written = write(fd, kept.String(), (size_t)kept.Length());
	if (written == (ssize_t)kept.Length())
		fsync(fd);
	status = written == (ssize_t)kept.Length() ? B_OK
		: (errno != 0 ? errno : B_ERROR);
	close(fd);
	return status;
}


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
		.AddMenuField(_MakeLatencyMenu(), 0, 2, B_ALIGN_HORIZONTAL_UNSET, 1, 3, 1)
		.Add(fLatencyHint = new BStringView("latency hint",
			B_TRANSLATE("Restart media services to apply.")), 0, 3, 3);

	defaultsBox->AddChild(defaultsGridView);

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
	fLatencyMenu->SetTargetForItems(thisMessenger);
	fVolumeCheckBox->SetTarget(thisMessenger);
}


void
AudioSettingsView::AllAttached()
{
	BGroupView::AllAttached();
	fLatencyMenu->SetTargetForItems(BMessenger(this));
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
		case ML_AUDIO_LATENCY:
		{
			int32 frames = 0;
			if (message->FindInt32("frames", &frames) != B_OK)
				break;
			char path[B_PATH_NAME_LENGTH];
			path[0] = '\0';
			status_t status = write_latency_file("hda.settings", frames, path,
				sizeof(path));
			if (status == B_OK) {
				char usbPath[B_PATH_NAME_LENGTH];
				usbPath[0] = '\0';
				status = write_latency_file("usb_audio", frames, usbPath,
					sizeof(usbPath));
				if (status != B_OK && usbPath[0] != '\0')
					strlcpy(path, usbPath, sizeof(path));
			}
			if (status != B_OK) {
				BString text(B_TRANSLATE("Could not save the latency choice."));
				text << "\n" << path << "\n" << strerror(status);
				(new BAlert("latency", text.String(), B_TRANSLATE("OK")))->Go();
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

		default:
			SettingsView::MessageReceived(message);
	}
}


void
AudioSettingsView::SetDefaultInput(const dormant_node_info* info)
{
	SettingsView::SetDefaultInput(info);
	_MediaWindow()->UpdateInputListItem(MediaListItem::AUDIO_TYPE, info);
	BMediaRoster::Roster()->SetAudioInput(*info);
}


void
AudioSettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	SettingsView::SetDefaultOutput(info);
	_MediaWindow()->UpdateOutputListItem(MediaListItem::AUDIO_TYPE, info);
	_FillChannelMenu(info);
	BMediaRoster::Roster()->SetAudioOutput(*info);
}


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
	media_node_id node_id;
	int32 instanceCount = 1;

	status_t err
		= roster->GetInstancesFor(nodeInfo->addon, nodeInfo->flavor_id, &node_id, &instanceCount);
	if (err != B_OK || instanceCount == 0)
		err = roster->InstantiateDormantNode(*nodeInfo, &node,
			B_FLAVOR_IS_GLOBAL);
	else
		err = roster->GetNodeFor(node_id, &node);

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
	BMediaRoster::Roster()->SetVideoInput(*info);
}


void
VideoSettingsView::SetDefaultOutput(const dormant_node_info* info)
{
	SettingsView::SetDefaultOutput(info);
	_MediaWindow()->UpdateOutputListItem(MediaListItem::VIDEO_TYPE, info);
	BMediaRoster::Roster()->SetVideoOutput(*info);
}
