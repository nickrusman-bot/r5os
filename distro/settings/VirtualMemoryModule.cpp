/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Virtual memory page. Writes the kernel swap file. The separate
 * VirtualMemory program stays installed.
 */


#include "VirtualMemoryModule.h"

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Box.h>
#include <CheckBox.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StatusBar.h>
#include <String.h>
#include <StringForSize.h>
#include <StringView.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_info.h>
#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const uint32 kMsgEnable = 'vmEn';
static const uint32 kMsgAuto = 'vmAu';
static const uint32 kMsgSize = 'vmSz';
static const uint32 kMsgVolume = 'vmVo';
static const off_t kMegaByte = 1024 * 1024;
static const off_t kGigaByte = kMegaByte * 1024;
static const char* kSwapPath =
	"/boot/system/settings/kernel/drivers/virtual_memory";


struct SwapState {
	bool	enabled;
	bool	automatic;
	off_t	size;
	dev_t	volume;
};


static bool
same_swap(const SwapState& a, const SwapState& b)
{
	return a.enabled == b.enabled && a.automatic == b.automatic
		&& a.size == b.size && a.volume == b.volume;
}


class VirtualMemoryModule;


class SizeSlider : public BSlider {
public:
								SizeSlider(const char* name, const char* label,
									BMessage* message, int32 min, int32 max);
	virtual	const char*			UpdateText() const;

private:
	mutable	char				fText[128];
};


class VirtualMemoryView : public BGroupView {
public:
								VirtualMemoryView(VirtualMemoryModule* module);
	virtual						~VirtualMemoryView();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				ApplyState(const SwapState& state);

private:
			void				_RebuildVolumes();
			void				_AddVolume(dev_t device);
			void				_RemoveVolume(dev_t device);
			void				_Show(const SwapState& state);
			void				_Edited();
			dev_t				_MarkedVolume() const;

			VirtualMemoryModule* fModule;
			bool				fUpdating;
			BCheckBox*			fEnable;
			BStatusBar*			fUsage;
			BCheckBox*			fAutomatic;
			BMenuField*			fVolume;
			BPopUpMenu*			fVolumeMenu;
			SizeSlider*			fSize;
};


class VirtualMemoryModule : public SettingsModule {
public:
								VirtualMemoryModule();
	virtual						~VirtualMemoryModule();

	virtual	const char*			Identifier() const { return "virtualmemory"; }
	virtual	const char*			Name() const { return "Virtual memory"; }
	virtual	const char*			Category() const { return "system"; }
	virtual	const char*			Description() const;
	virtual	const char*			Keywords() const;
	virtual	bool				RequiresAdmin() const { return true; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			const SwapState&	Current() const { return fCurrent; }
			void				ClearView(VirtualMemoryView* view);
			void				UserEdited(const SwapState& state);

private:
			void				_Push();
			status_t			_Write(const SwapState& state);
			void				_LoadFile();

			VirtualMemoryView*	fView;
			SettingsContext*	fContext;
			SwapState			fCurrent;
			SwapState			fOpened;
			SwapState			fFactory;
};


SizeSlider::SizeSlider(const char* name, const char* label,
	BMessage* message, int32 min, int32 max)
	:
	BSlider(name, label, message, min, max, B_HORIZONTAL, B_BLOCK_THUMB,
		B_WILL_DRAW | B_FRAME_EVENTS)
{
	rgb_color color = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
	UseFillColor(true, &color);
}


const char*
SizeSlider::UpdateText() const
{
	return string_for_size((off_t)Value() * kMegaByte, fText, sizeof(fText));
}


static SwapState
factory_swap()
{
	SwapState state;
	state.enabled = true;
	state.automatic = true;
	system_info info;
	get_system_info(&info);
	state.size = (off_t)info.max_pages * B_PAGE_SIZE;
	if (state.size <= kGigaByte)
		state.size *= 2;
	state.volume = dev_for_path("/boot");
	return state;
}


static bool
truth(const char* text)
{
	if (text == NULL)
		return false;
	return strcmp(text, "1") == 0 || strcmp(text, "on") == 0
		|| strcmp(text, "yes") == 0 || strcmp(text, "true") == 0
		|| strcmp(text, "enabled") == 0;
}


static bool
field(const BString& file, const char* key, BString& value)
{
	int32 start = 0;
	while (start < file.Length()) {
		int32 end = file.FindFirst('\n', start);
		if (end < 0)
			end = file.Length();
		BString line;
		file.CopyInto(line, start, end - start);
		start = end + 1;
		if (line.Length() == 0)
			continue;
		int32 space = line.FindFirst(' ');
		BString name;
		if (space < 0)
			name = line;
		else
			line.CopyInto(name, 0, space);
		if (name != key)
			continue;
		if (space < 0)
			value = "";
		else
			line.CopyInto(value, space + 1, line.Length() - space - 1);
		return true;
	}
	return false;
}


static dev_t
match_volume(const char* name, const char* device, const char* filesystem,
	off_t capacity)
{
	int32 bestScore = -1;
	dev_t best = -1;
	BVolume volume;
	BVolumeRoster roster;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.IsPersistent() || volume.IsReadOnly()
			|| volume.IsRemovable() || volume.IsShared())
			continue;
		fs_info info;
		if (fs_stat_dev(volume.Device(), &info) != 0)
			continue;
		int32 score = 0;
		if (name != NULL && strcmp(name, info.volume_name) == 0)
			score += 4;
		if (device != NULL && strcmp(device, info.device_name) == 0)
			score += 3;
		if (capacity == (off_t)info.total_blocks * info.block_size)
			score += 2;
		if (filesystem != NULL && strcmp(filesystem, info.fsh_name) == 0)
			score += 1;
		if (score >= 4 && score > bestScore) {
			best = volume.Device();
			bestScore = score;
		}
	}
	return best;
}


VirtualMemoryView::VirtualMemoryView(VirtualMemoryModule* module)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fUpdating(false),
	fEnable(NULL),
	fUsage(NULL),
	fAutomatic(NULL),
	fVolume(NULL),
	fVolumeMenu(NULL),
	fSize(NULL)
{
	fEnable = new BCheckBox("enable", "Enable virtual memory",
		new BMessage(kMsgEnable));
	fEnable->SetFont(r5_settings_bold_font());
	fUsage = new BStatusBar("usage");
	fAutomatic = new BCheckBox("automatic", "Automatic swap management",
		new BMessage(kMsgAuto));
	fVolumeMenu = new BPopUpMenu("volume");
	fVolume = new BMenuField("volume", "Use volume:", fVolumeMenu);
	fSize = new SizeSlider("size", "Requested swap file size:",
		new BMessage(kMsgSize), 1, 2);

	BBox* box = new BBox("swap");
	box->SetLabel(fEnable);
	BGroupView* inner = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(inner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fUsage)
		.Add(fAutomatic)
		.Add(fVolume)
		.Add(fSize)
		.Add(new BStringView("reboot",
			"Changes take effect on the next start."));
	box->AddChild(inner);

	BLayoutBuilder::Group<>(this)
		.Add(box)
		.AddGlue();

	_RebuildVolumes();
	if (fModule != NULL)
		ApplyState(fModule->Current());
}


VirtualMemoryView::~VirtualMemoryView()
{
	stop_watching(this);
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
VirtualMemoryView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fEnable->SetTarget(this);
	fAutomatic->SetTarget(this);
	fSize->SetTarget(this);
	fVolumeMenu->SetTargetForItems(this);
	watch_node(NULL, B_WATCH_MOUNT, this);
	_Show(fModule != NULL ? fModule->Current() : factory_swap());
}


void
VirtualMemoryView::_RebuildVolumes()
{
	while (fVolumeMenu->CountItems() > 0)
		delete fVolumeMenu->RemoveItem((int32)0);
	BVolume volume;
	BVolumeRoster roster;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.IsPersistent() || volume.IsReadOnly()
			|| volume.IsRemovable() || volume.IsShared())
			continue;
		_AddVolume(volume.Device());
	}
}


void
VirtualMemoryView::_AddVolume(dev_t device)
{
	for (int32 i = 0; i < fVolumeMenu->CountItems(); i++) {
		BMenuItem* item = fVolumeMenu->ItemAt(i);
		if (item != NULL && item->Message() != NULL
			&& item->Message()->GetInt32("device", -1) == (int32)device)
			return;
	}
	BVolume volume(device);
	char name[B_FILE_NAME_LENGTH];
	if (volume.GetName(name) != B_OK)
		strlcpy(name, "volume", sizeof(name));
	BString label(name);
	BDirectory root;
	BEntry entry;
	BPath path;
	if (volume.GetRootDirectory(&root) == B_OK && root.GetEntry(&entry) == B_OK
		&& entry.GetPath(&path) == B_OK) {
		label << " (" << path.Path() << ")";
	}
	BMessage* message = new BMessage(kMsgVolume);
	message->AddInt32("device", (int32)device);
	fVolumeMenu->AddItem(new BMenuItem(label.String(), message));
}


void
VirtualMemoryView::_RemoveVolume(dev_t device)
{
	for (int32 i = 0; i < fVolumeMenu->CountItems(); i++) {
		BMenuItem* item = fVolumeMenu->ItemAt(i);
		if (item == NULL || item->Message() == NULL)
			continue;
		if (item->Message()->GetInt32("device", -1) != (int32)device)
			continue;
		delete fVolumeMenu->RemoveItem(i);
		return;
	}
}


dev_t
VirtualMemoryView::_MarkedVolume() const
{
	BMenuItem* item = fVolumeMenu->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return -1;
	return (dev_t)item->Message()->GetInt32("device", -1);
}


void
VirtualMemoryView::_Show(const SwapState& state)
{
	bool gray = !fUpdating && fEnable->IsEnabled() == false;
	fUpdating = true;
	fEnable->SetValue(state.enabled ? B_CONTROL_ON : B_CONTROL_OFF);
	fAutomatic->SetValue(state.automatic ? B_CONTROL_ON : B_CONTROL_OFF);
	fAutomatic->SetEnabled(state.enabled);

	bool manual = state.enabled && !state.automatic;
	fVolume->SetEnabled(manual);

	BMenuItem* marked = NULL;
	for (int32 i = 0; i < fVolumeMenu->CountItems(); i++) {
		BMenuItem* item = fVolumeMenu->ItemAt(i);
		if (item == NULL || item->Message() == NULL)
			continue;
		bool match = item->Message()->GetInt32("device", -1)
			== (int32)state.volume;
		item->SetMarked(match);
		if (match)
			marked = item;
	}

	if (marked != NULL && manual) {
		BVolume volume(state.volume);
		off_t swapFileSize = 0;
		BEntry swapFile;
		if (state.volume == dev_for_path("/boot"))
			swapFile.SetTo("/var/swap");
		else {
			BDirectory root;
			if (volume.GetRootDirectory(&root) == B_OK)
				swapFile.SetTo(&root, "swap");
		}
		swapFile.GetSize(&swapFileSize);
		off_t freeSpace = volume.FreeBytes() + swapFileSize;
		off_t safeSpace = freeSpace - (off_t)(0.15 * (double)freeSpace);
		(safeSpace >>= 20) <<= 20;
		off_t minSize = B_PAGE_SIZE + kMegaByte;
		(minSize >>= 20) <<= 20;
		if (safeSpace < minSize)
			safeSpace = minSize;
		char buffer[16];
		BString minLabel(string_for_size(minSize, buffer, sizeof(buffer)));
		BString maxLabel(string_for_size(safeSpace, buffer, sizeof(buffer)));
		fSize->SetLimitLabels(minLabel.String(), maxLabel.String());
		fSize->SetLimits((int32)(minSize / kMegaByte),
			(int32)(safeSpace / kMegaByte));
		int32 minimum = (int32)(minSize / kMegaByte);
		int32 maximum = (int32)(safeSpace / kMegaByte);
		int32 value = (int32)(state.size / kMegaByte);
		if (value < minimum)
			value = minimum;
		if (value > maximum)
			value = maximum;
		fSize->SetValue(value);
		fSize->SetEnabled(true);
	} else
		fSize->SetEnabled(false);

	system_info info;
	get_system_info(&info);
	off_t swapSize = (off_t)info.max_swap_pages * B_PAGE_SIZE;
	off_t swapUsed = (off_t)(info.max_swap_pages - info.free_swap_pages)
		* B_PAGE_SIZE;
	char sizeBuffer[16];
	char usedBuffer[16];
	BString trailing;
	trailing << string_for_size(swapUsed, usedBuffer, sizeof(usedBuffer))
		<< " / " << string_for_size(swapSize, sizeBuffer, sizeof(sizeBuffer));
	fUsage->SetMaxValue(swapSize / (float)kMegaByte);
	fUsage->SetTo(swapUsed / (float)kMegaByte, "Current swap:",
		trailing.String());
	if (gray) {
		fEnable->SetEnabled(false);
		fAutomatic->SetEnabled(false);
		fVolume->SetEnabled(false);
		fSize->SetEnabled(false);
	}
	fUpdating = false;
}


void
VirtualMemoryView::ApplyState(const SwapState& state)
{
	_Show(state);
}


void
VirtualMemoryView::_Edited()
{
	if (fUpdating || fModule == NULL)
		return;
	SwapState state = fModule->Current();
	state.enabled = fEnable->Value() == B_CONTROL_ON;
	state.automatic = fAutomatic->Value() == B_CONTROL_ON;
	dev_t volume = _MarkedVolume();
	if (volume >= 0)
		state.volume = volume;
	if (fSize->IsEnabled())
		state.size = (off_t)fSize->Value() * kMegaByte;
	fModule->UserEdited(state);
}


void
VirtualMemoryView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshPage:
			if (fModule != NULL)
				_Show(fModule->Current());
			break;
		case B_NODE_MONITOR:
		{
			int32 opcode = 0;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				break;
			dev_t device = -1;
			if (opcode == B_DEVICE_MOUNTED
				&& message->FindInt32("new device", &device) == B_OK) {
				BVolume volume(device);
				if (volume.IsPersistent() && !volume.IsReadOnly()
					&& !volume.IsRemovable() && !volume.IsShared())
					_AddVolume(device);
			} else if (opcode == B_DEVICE_UNMOUNTED
				&& message->FindInt32("device", &device) == B_OK) {
				_RemoveVolume(device);
			}
			if (fModule != NULL)
				_Show(fModule->Current());
			break;
		}
		case kMsgEnable:
			if (fUpdating)
				break;
			if (fEnable->Value() == B_CONTROL_OFF) {
				BAlert* alert = new BAlert("Virtual memory",
					"Disabling virtual memory will have unwanted effects on "
					"system stability once the memory is used up.\n"
					"Virtual memory does not affect system performance "
					"until this point is reached.\n\n"
					"Are you really sure you want to turn it off?",
					"Turn off", "Keep enabled", NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->SetShortcut(1, B_ESCAPE);
				if (alert->Go() == 1) {
					fUpdating = true;
					fEnable->SetValue(B_CONTROL_ON);
					fUpdating = false;
					break;
				}
			}
			_Edited();
			break;
		case kMsgAuto:
		case kMsgSize:
		case kMsgVolume:
			_Edited();
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


const char*
VirtualMemoryModule::Description() const
{
	return "Swap space for when memory is full.";
}


const char*
VirtualMemoryModule::Keywords() const
{
	return "swap virtual memory disk";
}


VirtualMemoryModule::VirtualMemoryModule()
	:
	fView(NULL),
	fContext(NULL),
	fCurrent(factory_swap()),
	fOpened(fCurrent),
	fFactory(fCurrent)
{
}


VirtualMemoryModule::~VirtualMemoryModule()
{
}


BView*
VirtualMemoryModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new VirtualMemoryView(this);
	return fView;
}


void
VirtualMemoryModule::ClearView(VirtualMemoryView* view)
{
	if (fView == view)
		fView = NULL;
}


void
VirtualMemoryModule::_Push()
{
	if (fView != NULL)
		fView->ApplyState(fCurrent);
}


void
VirtualMemoryModule::_LoadFile()
{
	fFactory = factory_swap();
	fCurrent = fFactory;
	BFile file;
	if (file.SetTo(kSwapPath, B_READ_ONLY) != B_OK) {
		fOpened = fCurrent;
		return;
	}
	off_t length = 0;
	if (file.GetSize(&length) != B_OK || length <= 0 || length > 64 * 1024) {
		fOpened = fCurrent;
		return;
	}
	BString text;
	char* buffer = text.LockBuffer((int32)length);
	if (buffer == NULL) {
		fOpened = fCurrent;
		return;
	}
	ssize_t got = file.Read(buffer, (size_t)length);
	text.UnlockBuffer(got > 0 ? (int32)got : 0);
	BString enabled;
	BString automatic;
	BString size;
	BString name;
	BString device;
	BString filesystem;
	BString capacity;
	if (!field(text, "vm", enabled) || !field(text, "swap_auto", automatic)
		|| !field(text, "swap_size", size)
		|| !field(text, "swap_volume_name", name)
		|| !field(text, "swap_volume_device", device)
		|| !field(text, "swap_volume_filesystem", filesystem)
		|| !field(text, "swap_volume_capacity", capacity)) {
		fOpened = fCurrent;
		return;
	}
	fCurrent.enabled = truth(enabled.String());
	fCurrent.automatic = truth(automatic.String());
	fCurrent.size = (off_t)atoll(size.String());
	dev_t volume = match_volume(name.String(), device.String(),
		filesystem.String(), (off_t)atoll(capacity.String()));
	if (volume < 0)
		volume = fFactory.volume;
	fCurrent.volume = volume;
	fOpened = fCurrent;
}


status_t
VirtualMemoryModule::_Write(const SwapState& state)
{
	fs_info info;
	if (fs_stat_dev(state.volume, &info) != 0)
		return B_ERROR;
	BString text;
	text.SetToFormat("vm %s\nswap_auto %s\nswap_size %" B_PRIdOFF "\n"
		"swap_volume_name %s\nswap_volume_device %s\n"
		"swap_volume_filesystem %s\nswap_volume_capacity %" B_PRIdOFF "\n",
		state.enabled ? "on" : "off",
		state.automatic ? "yes" : "no",
		state.size, info.volume_name, info.device_name, info.fsh_name,
		(off_t)info.total_blocks * (off_t)info.block_size);
	BFile file;
	status_t status = file.SetTo(kSwapPath,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status != B_OK)
		return status;
	ssize_t wrote = file.Write(text.String(), text.Length());
	if (wrote < 0)
		return (status_t)wrote;
	if ((int32)wrote != text.Length())
		return B_ERROR;
	return B_OK;
}


void
VirtualMemoryModule::UserEdited(const SwapState& state)
{
	fCurrent = state;
	_Push();
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
VirtualMemoryModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	_LoadFile();
	_Push();
	return B_OK;
}


status_t
VirtualMemoryModule::SaveSettings(SettingsContext* context)
{
	fContext = context;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;
	return _Write(fCurrent);
}


status_t
VirtualMemoryModule::RevertSettings(SettingsContext* context)
{
	fCurrent = fOpened;
	_Push();
	return SaveSettings(context);
}


bool
VirtualMemoryModule::IsModified() const
{
	return !same_swap(fCurrent, fOpened);
}


bool
VirtualMemoryModule::CanApplyDefaults() const
{
	return !same_swap(fCurrent, fFactory);
}


status_t
VirtualMemoryModule::ApplyDefaults(SettingsContext* context)
{
	fCurrent = fFactory;
	_Push();
	return SaveSettings(context);
}


void
register_virtual_memory_page(SettingsManager& manager)
{
	manager.Add(new VirtualMemoryModule());
}
