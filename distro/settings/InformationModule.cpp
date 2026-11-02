/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Device and R5OS version. Nothing here is saved.
 */


#include "InformationModule.h"

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsModule.h"

#include <Accelerant.h>
#include <Box.h>
#include <Control.h>
#include <Button.h>
#include <Clipboard.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OS.h>
#include <Screen.h>
#include <String.h>
#include <StringView.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <system/r5_os_version.h>


#ifndef R5_OS_BUILD_DATE
#define R5_OS_BUILD_DATE ""
#endif


static const uint32 kMsgCopyDevice = 'r5id';
static const uint32 kMsgCopySystem = 'r5is';
static const uint32 kMsgCopyDone = 'r5ic';
static const char* kModuleId = "information";


static BString
format_bytes(uint64 bytes)
{
	double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
	BString text;
	if (gb >= 10.0)
		text.SetToFormat("%.0f GB", gb);
	else if (gb >= 1.0)
		text.SetToFormat("%.1f GB", gb);
	else
		text.SetToFormat("%.0f MB", (double)bytes / (1024.0 * 1024.0));
	return text;
}


static BString
cpu_brand()
{
#if defined(__i386__) || defined(__x86_64__)
	char buffer[49];
	memset(buffer, 0, sizeof(buffer));
	for (int32 i = 0; i < 3; i++) {
		cpuid_info nameInfo;
		if (get_cpuid(&nameInfo, 0x80000002 + i, 0) != B_OK)
			return BString();
		memcpy(buffer + (i * 16), &nameInfo.regs.eax, 4);
		memcpy(buffer + (i * 16) + 4, &nameInfo.regs.ebx, 4);
		memcpy(buffer + (i * 16) + 8, &nameInfo.regs.ecx, 4);
		memcpy(buffer + (i * 16) + 12, &nameInfo.regs.edx, 4);
	}
	const char* name = buffer;
	while (name[0] == ' ')
		name++;
	return BString(name);
#else
	return BString();
#endif
}


static BString
cpu_line()
{
	BString text = cpu_brand();
	if (text.Length() == 0)
		text = "Unknown";

	cpu_info cpu;
	if (get_cpu_info(0, 1, &cpu) == B_OK && cpu.current_frequency > 100000000) {
		double ghz = (double)cpu.current_frequency / 1000000000.0;
		char speed[32];
		snprintf(speed, sizeof(speed), " (%.2f GHz)", ghz);
		text << speed;
	}
	return text;
}


static BString
memory_line()
{
	system_info info;
	if (get_system_info(&info) != B_OK)
		return BString("Unknown");

	uint64 installed = (info.max_pages + info.ignored_pages) * B_PAGE_SIZE;
	uint64 usable = info.max_pages * B_PAGE_SIZE;
	BString text = format_bytes(installed);
	if (info.ignored_pages > 0) {
		text << " (";
		text << format_bytes(usable);
		text << " usable)";
	}
	return text;
}


static BString
storage_line()
{
	BVolume boot;
	if (BVolumeRoster().GetBootVolume(&boot) != B_OK)
		return BString("Unknown");

	off_t capacity = boot.Capacity();
	off_t freeBytes = boot.FreeBytes();
	if (capacity < 0)
		return BString("Unknown");
	if (freeBytes < 0)
		freeBytes = 0;
	off_t used = capacity - freeBytes;
	if (used < 0)
		used = 0;

	BString text = format_bytes((uint64)used);
	text << " of ";
	text << format_bytes((uint64)capacity);
	return text;
}


static BString
graphics_line()
{
	accelerant_device_info device;
	memset(&device, 0, sizeof(device));
	BScreen screen;
	if (!screen.IsValid() || screen.GetDeviceInfo(&device) != B_OK)
		return BString("Unknown");

	BString text;
	if (device.name[0] != '\0')
		text = device.name;
	if (device.chipset[0] != '\0'
		&& (text.Length() == 0 || text != device.chipset)) {
		if (text.Length() > 0)
			text << " (";
		text << device.chipset;
		if (text != device.chipset)
			text << ")";
	}
	if (text.Length() == 0)
		text = "Unknown";
	if (device.memory > 0) {
		text << ", ";
		text << format_bytes(device.memory);
	}
	return text;
}


static BString
architecture_name()
{
#if defined(__x86_64__)
	return BString("AMD64");
#elif defined(__aarch64__)
	return BString("ARM64");
#elif defined(__riscv)
	return BString("RISC-V 64");
#else
	return BString("64-bit");
#endif
}


static BString
version_line()
{
	BString version(R5_OS_VERSION);
	int32 dev = version.FindLast("-dev");
	bool development = dev >= 0 && dev == version.Length() - 4;
	if (development)
		version.Truncate(dev);
	if (development)
		version << ", Development";
	return version;
}


static BString
kernel_line()
{
	system_info info;
	if (get_system_info(&info) != B_OK)
		return BString("Unknown");

	BString text(info.kernel_name);
	if (info.kernel_build_date[0] != '\0') {
		text << ", ";
		text << info.kernel_build_date;
		if (info.kernel_build_time[0] != '\0') {
			text << " ";
			text << info.kernel_build_time;
		}
	}
	return text;
}


static BString
build_date_line()
{
	if (R5_OS_BUILD_DATE[0] != '\0')
		return BString(R5_OS_BUILD_DATE);

	system_info info;
	if (get_system_info(&info) == B_OK && info.kernel_build_date[0] != '\0') {
		BString text(info.kernel_build_date);
		if (info.kernel_build_time[0] != '\0') {
			text << " ";
			text << info.kernel_build_time;
		}
		return text;
	}
	return BString("Unknown");
}


static BStringView*
make_label(const char* text)
{
	BStringView* view = new BStringView("label", text);
	view->SetAlignment(B_ALIGN_LEFT);
	view->SetFont(r5_settings_plain_font());
	view->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));
	return view;
}


static BStringView*
make_value(const char* text)
{
	BStringView* view = new BStringView("value", text);
	view->SetFont(r5_settings_bold_font());
	view->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	return view;
}


class InformationView : public BGroupView {
public:
								InformationView();
	virtual						~InformationView();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Copy(const char* heading, const char* body,
									BButton* button);
			BString				_DeviceText() const;
			BString				_SystemText() const;

			BString				fDeviceName;
			BString				fProcessor;
			BString				fMemory;
			BString				fStorage;
			BString				fGraphics;
			BString				fSystemType;
			BString				fVersion;
			BString				fBuild;
			BString				fBuildDate;
			BString				fKernel;
			BButton*			fCopyDevice;
			BButton*			fCopySystem;
			BMessageRunner*		fCopied;
};


InformationView::InformationView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fCopyDevice(NULL),
	fCopySystem(NULL),
	fCopied(NULL)
{
	char host[256];
	host[0] = '\0';
	if (gethostname(host, sizeof(host) - 1) != 0)
		host[0] = '\0';
	host[sizeof(host) - 1] = '\0';
	fDeviceName = host[0] != '\0' ? host : "This computer";
	fProcessor = cpu_line();
	fMemory = memory_line();
	fStorage = storage_line();
	fGraphics = graphics_line();
	fSystemType = architecture_name();
	fVersion = version_line();
	fBuild.SetToFormat("%" B_PRId32, (int32)R5_OS_BUILD);
	fBuildDate = build_date_line();
	fKernel = kernel_line();

	fCopyDevice = new BButton("copy-device", "Copy",
		new BMessage(kMsgCopyDevice));
	fCopySystem = new BButton("copy-system", "Copy",
		new BMessage(kMsgCopySystem));

	BBox* device = new BBox("device");
	device->SetLabel(r5_settings_box_title("Device"));
	BGridView* deviceGrid = new BGridView(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	BLayoutBuilder::Grid<>(deviceGrid)
		.Add(make_label("Device name"), 0, 0)
		.Add(make_value(fDeviceName.String()), 1, 0)
		.Add(make_label("Processor"), 0, 1)
		.Add(make_value(fProcessor.String()), 1, 1)
		.Add(make_label("Installed memory"), 0, 2)
		.Add(make_value(fMemory.String()), 1, 2)
		.Add(make_label("Storage"), 0, 3)
		.Add(make_value(fStorage.String()), 1, 3)
		.Add(make_label("Graphics"), 0, 4)
		.Add(make_value(fGraphics.String()), 1, 4)
		.Add(make_label("System type"), 0, 5)
		.Add(make_value(fSystemType.String()), 1, 5);
	// A layout on the box draws under the label. One child sits below it.
	BGroupView* deviceInner = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(deviceInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(deviceGrid)
		.AddGroup(B_HORIZONTAL, 0)
			.AddGlue()
			.Add(fCopyDevice)
			.End();
	device->AddChild(deviceInner);
	device->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BBox* system = new BBox("os");
	system->SetLabel(r5_settings_box_title("R5 Operating System"));
	BGridView* systemGrid = new BGridView(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	BLayoutBuilder::Grid<>(systemGrid)
		.Add(make_label("Version"), 0, 0)
		.Add(make_value(fVersion.String()), 1, 0)
		.Add(make_label("Build"), 0, 1)
		.Add(make_value(fBuild.String()), 1, 1)
		.Add(make_label("Build date"), 0, 2)
		.Add(make_value(fBuildDate.String()), 1, 2)
		.Add(make_label("Kernel"), 0, 3)
		.Add(make_value(fKernel.String()), 1, 3);
	BGroupView* systemInner = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(systemInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(systemGrid)
		.AddGroup(B_HORIZONTAL, 0)
			.AddGlue()
			.Add(fCopySystem)
			.End();
	system->AddChild(systemInner);
	system->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_BIG_SPACING)
		.Add(device)
		.Add(system)
		.AddGlue();
	r5_settings_apply_plain(this);
}


InformationView::~InformationView()
{
	delete fCopied;
}


static void
target_controls(BView* view, BHandler* target)
{
	if (view == NULL)
		return;
	for (int32 i = 0; i < view->CountChildren(); i++) {
		BView* child = view->ChildAt(i);
		BControl* control = dynamic_cast<BControl*>(child);
		if (control != NULL)
			control->SetTarget(target);
		target_controls(child, target);
	}
}


void
InformationView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	// A button's message goes to its target. That starts out as the
	// window, so Copy never reached this view.
	target_controls(this, this);
}


void
InformationView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCopyDevice:
			_Copy("Device", _DeviceText().String(), fCopyDevice);
			break;
		case kMsgCopySystem:
			_Copy("R5 Operating System", _SystemText().String(), fCopySystem);
			break;
		case kMsgCopyDone:
			if (fCopyDevice != NULL)
				fCopyDevice->SetLabel("Copy");
			if (fCopySystem != NULL)
				fCopySystem->SetLabel("Copy");
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
InformationView::_Copy(const char* heading, const char* body, BButton* button)
{
	BString text(heading);
	text << "\n" << body;

	BClipboard clipboard("system");
	if (clipboard.Lock()) {
		clipboard.Clear();
		BMessage* clip = clipboard.Data();
		if (clip != NULL) {
			clip->AddData("text/plain", B_MIME_TYPE, text.String(),
				text.Length() + 1);
		}
		clipboard.Commit();
		clipboard.Unlock();
	}

	if (button != NULL)
		button->SetLabel("Copied");
	delete fCopied;
	BMessage done(kMsgCopyDone);
	fCopied = new BMessageRunner(BMessenger(this), &done, 1200000, 1);
}


BString
InformationView::_DeviceText() const
{
	BString text;
	text << "Device name: " << fDeviceName << "\n";
	text << "Processor: " << fProcessor << "\n";
	text << "Installed memory: " << fMemory << "\n";
	text << "Storage: " << fStorage << "\n";
	text << "Graphics: " << fGraphics << "\n";
	text << "System type: " << fSystemType;
	return text;
}


BString
InformationView::_SystemText() const
{
	BString text;
	text << "Version: " << fVersion << "\n";
	text << "Build: " << fBuild << "\n";
	text << "Build date: " << fBuildDate << "\n";
	text << "Kernel: " << fKernel;
	return text;
}


class InformationModule : public SettingsModule {
public:
								InformationModule();
	virtual						~InformationModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Information"; }
	virtual	const char*			Category() const { return "system"; }
	virtual	const char*			Description() const
		{ return "View system details"; }
	virtual	const char*			Keywords() const
		{ return "information device processor memory version build kernel"; }
	virtual	bool				CanSave() const { return false; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
};


InformationModule::InformationModule()
{
}


InformationModule::~InformationModule()
{
}


BView*
InformationModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)context;
	(void)frame;
	return new InformationView();
}


status_t
InformationModule::LoadSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
InformationModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
InformationModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


void
register_information_page(SettingsManager& manager)
{
	manager.Add(new InformationModule());
}
