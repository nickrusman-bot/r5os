/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * The computer, on one page. Rename uses the hostname file the
 * system already reads.
 */


#include "InformationModule.h"

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <Font.h>
#include <GridView.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OS.h>
#include <R5Icons.h>
#include <Roster.h>
#include <Screen.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <Window.h>

#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <system/r5_os_version.h>


#ifndef R5_OS_BUILD_DATE
#define R5_OS_BUILD_DATE ""
#endif


static const uint32 kMsgCopy = 'r5cp';
static const uint32 kMsgRename = 'r5rn';
static const uint32 kMsgApplyName = 'r5ra';
static const uint32 kMsgTick = 'r5tk';
static const char* kAboutSignature = "application/x-vnd.Haiku-About";
static const char* kModuleId = "information";


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


static rgb_color
quiet_color(uint8 amount)
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), amount);
}


// The mockup's tile (#e4e4e4) sits between the window grey and the white
// card. A darkened panel tint is far darker than that on the Haiku panel
// grey, so the tile mixes the panel toward the document background.
static rgb_color
tile_fill()
{
	return r5_settings_tile_fill();
}


// The usage track (#c9cfd6) is the tile fill with a little document text
// mixed in, so it stays visible on light and dark panels.
static rgb_color
track_color()
{
	return mix_color(tile_fill(), ui_color(B_DOCUMENT_TEXT_COLOR), 40);
}


static void
underline(BView* view, float left, float right, float baseline, float u)
{
	float y = baseline + u * 2.0f;
	view->StrokeLine(BPoint(left, y), BPoint(right, y));
}


static void
semi_bold(BFont* font)
{
	font_family family;
	font_style style;
	font->GetFamilyAndStyle(&family, &style);
	int32 styles = count_font_styles(family);
	for (int32 index = 0; index < styles; index++) {
		font_style name;
		if (get_font_style(family, index, &name) == B_OK
			&& strstr(name, "Semi") != NULL) {
			font->SetFamilyAndStyle(family, name);
			return;
		}
	}
	font->SetFace(B_BOLD_FACE);
}


static void
extra_bold(BFont* font)
{
	font_family family;
	font_style style;
	font->GetFamilyAndStyle(&family, &style);
	int32 styles = count_font_styles(family);
	for (int32 index = 0; index < styles; index++) {
		font_style name;
		if (get_font_style(family, index, &name) == B_OK
			&& (strcasecmp(name, "ExtraBold") == 0
				|| strcasecmp(name, "Black") == 0)) {
			font->SetFamilyAndStyle(family, name);
			return;
		}
	}
	font->SetFace(B_BOLD_FACE);
}


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


static void
cpu_counts(int32& cores, int32& threads)
{
	threads = 0;
	system_info info;
	if (get_system_info(&info) == B_OK)
		threads = (int32)info.cpu_count;
	cores = 0;
	uint32 count = 0;
	if (get_cpu_topology_info(NULL, &count) != B_OK || count == 0)
		return;
	cpu_topology_node_info* nodes = new(std::nothrow) cpu_topology_node_info[
		count];
	if (nodes == NULL)
		return;
	if (get_cpu_topology_info(nodes, &count) == B_OK) {
		for (uint32 i = 0; i < count; i++) {
			if (nodes[i].type == B_TOPOLOGY_CORE)
				cores++;
		}
	}
	delete[] nodes;
}


static BString
frequency_text()
{
	cpu_info cpu;
	uint64 hz = 0;
	if (get_cpu_info(0, 1, &cpu) == B_OK)
		hz = cpu.current_frequency;
	if (hz < 100000000)
		return BString();
	double ghz = (double)hz / 1000000000.0;
	BString text;
	if (ghz < 10.0)
		text.SetToFormat("%.1f GHz", ghz);
	else
		text.SetToFormat("%.0f GHz", ghz);
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
version_text()
{
	BString version(R5_OS_VERSION);
	int32 dev = version.FindLast("-dev");
	if (dev >= 0 && dev == version.Length() - 4)
		version.Truncate(dev);
	BString text("R5 Operating System ");
	text << version << " · Build " << (int32)R5_OS_BUILD << " · ";
	text << architecture_name();
	return text;
}


static BString
host_name()
{
	char host[64];
	memset(host, 0, sizeof(host));
	if (gethostname(host, sizeof(host) - 1) != 0 || host[0] == '\0')
		return BString("This computer");
	return BString(host);
}


static bool
name_ok(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return false;
	size_t length = strlen(name);
	if (length > 63 || name[0] == '-' || name[length - 1] == '-')
		return false;
	for (size_t i = 0; i < length; i++) {
		char c = name[i];
		bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		bool digit = c >= '0' && c <= '9';
		if (!letter && !digit && c != '-')
			return false;
	}
	return true;
}


static void
memory_numbers(uint64& installed, uint64& used)
{
	installed = 0;
	used = 0;
	system_info info;
	if (get_system_info(&info) != B_OK)
		return;
	installed = (info.max_pages + info.ignored_pages) * B_PAGE_SIZE;
	used = info.used_pages * B_PAGE_SIZE;
	if (used > installed && installed > 0)
		used = installed;
}


static void
storage_numbers(BString& name, uint64& capacity, uint64& used)
{
	name = "";
	capacity = 0;
	used = 0;
	BVolume boot;
	if (BVolumeRoster().GetBootVolume(&boot) != B_OK)
		return;
	char label[B_FILE_NAME_LENGTH];
	if (boot.GetName(label) == B_OK)
		name = label;
	off_t bytes = boot.Capacity();
	off_t freeBytes = boot.FreeBytes();
	if (bytes < 0)
		return;
	if (freeBytes < 0)
		freeBytes = 0;
	if (freeBytes > bytes)
		freeBytes = bytes;
	capacity = (uint64)bytes;
	used = (uint64)(bytes - freeBytes);
}


static BString
graphics_name()
{
	accelerant_device_info device;
	memset(&device, 0, sizeof(device));
	BScreen screen;
	if (!screen.IsValid() || screen.GetDeviceInfo(&device) != B_OK
		|| device.name[0] == '\0')
		return BString("Unknown");
	return BString(device.name);
}


static BString
display_value()
{
	BScreen screen;
	if (!screen.IsValid())
		return BString("Unknown");
	display_mode mode;
	if (screen.GetMode(&mode) != B_OK)
		return BString("Unknown");
	BString text;
	text.SetToFormat("%" B_PRIu16 " × %" B_PRIu16, mode.virtual_width,
		mode.virtual_height);
	return text;
}


static BString
display_detail()
{
	BScreen screen;
	display_mode mode;
	if (!screen.IsValid() || screen.GetMode(&mode) != B_OK)
		return BString();
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0
		|| mode.timing.pixel_clock == 0)
		return BString();
	float hz = (float)mode.timing.pixel_clock * 1000.0f
		/ ((float)mode.timing.h_total * (float)mode.timing.v_total);
	BString text;
	text.SetToFormat("%.0f Hz", hz);
	return text;
}


static BString
running_value()
{
	int64 seconds = system_time() / 1000000;
	if (seconds < 60)
		return BString("Less than a minute");
	int64 minutes = seconds / 60;
	int64 hours = minutes / 60;
	minutes %= 60;
	int64 days = hours / 24;
	hours %= 24;
	BString text;
	if (days > 0) {
		text.SetToFormat("%" B_PRId64 " day%s", days, days == 1 ? "" : "s");
		if (hours > 0) {
			text << ", " << hours << (hours == 1 ? " hour" : " hours");
		}
		return text;
	}
	if (hours > 0) {
		text.SetToFormat("%" B_PRId64 " hour%s", hours, hours == 1 ? "" : "s");
		if (minutes > 0) {
			text << ", " << minutes
				<< (minutes == 1 ? " minute" : " minutes");
		}
		return text;
	}
	text.SetToFormat("%" B_PRId64 " minute%s", minutes,
		minutes == 1 ? "" : "s");
	return text;
}


static BString
running_since()
{
	time_t now = time(NULL);
	time_t boot = now - (time_t)(system_time() / 1000000);
	struct tm localTime;
	struct tm today;
	localtime_r(&boot, &localTime);
	localtime_r(&now, &today);
	char clock[16];
	strftime(clock, sizeof(clock), "%H:%M", &localTime);
	BString text("Since ");
	text << clock;
	if (localTime.tm_year == today.tm_year && localTime.tm_yday == today.tm_yday)
		text << " today";
	else {
		char day[40];
		strftime(day, sizeof(day), "%d %b %Y", &localTime);
		text << ", " << day;
	}
	return text;
}


static BString
kernel_text()
{
	system_info info;
	if (get_system_info(&info) != B_OK)
		return BString("Kernel");
	const char* name = info.kernel_name;
	if (strncmp(name, "kernel_", 7) == 0)
		name += 7;
	BString text("Kernel ");
	text << name;
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
image_text()
{
	BString text("Image built ");
	if (R5_OS_BUILD_DATE[0] != '\0')
		text << R5_OS_BUILD_DATE;
	else
		text << "unknown";
	return text;
}


class InfoTile : public BAppearanceKit::BCard {
public:
								InfoTile(const char* name, const char* label,
									const char* signature);
	virtual						~InfoTile();
	virtual	void				Draw(BRect update);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();
			void				SetLines(const char* value, const char* detail,
									float bar);

private:
			float				_TextHeight() const;
			BString				fLabel;
			BString				fValue;
			BString				fDetail;
			float				fBar;
			BBitmap*			fIcon;
};


InfoTile::InfoTile(const char* name, const char* label, const char* signature)
	:
	BCard(name, BAppearanceKit::B_CARD_PANEL),
	fLabel(label),
	fBar(-1.0f),
	fIcon(NULL)
{
	SetLayout(NULL);
	float u = unit();
	fIcon = r5_mime_icon_bitmap(signature, u * 34.0f);
	SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
}


InfoTile::~InfoTile()
{
	delete fIcon;
}


void
InfoTile::SetLines(const char* value, const char* detail, float bar)
{
	fValue = value != NULL ? value : "";
	fDetail = detail != NULL ? detail : "";
	fBar = bar;
	Invalidate();
}


float
InfoTile::_TextHeight() const
{
	float u = unit();
	BFont label(be_plain_font);
	label.SetSize(u * 11.5f);
	BFont value(be_bold_font);
	value.SetSize(u * 13.5f);
	BFont detail(be_plain_font);
	detail.SetSize(u * 11.5f);
	font_height labelHeight;
	font_height valueHeight;
	font_height detailHeight;
	label.GetHeight(&labelHeight);
	value.GetHeight(&valueHeight);
	detail.GetHeight(&detailHeight);
	float text = labelHeight.ascent + labelHeight.descent + u * 2.0f
		+ valueHeight.ascent + valueHeight.descent;
	if (fBar >= 0.0f)
		text += u * 3.0f + u * 6.0f;
	if (fDetail.Length() > 0)
		text += u * 2.0f + detailHeight.ascent + detailHeight.descent;
	return text;
}


BSize
InfoTile::MinSize()
{
	float u = unit();
	float icon = u * 34.0f;
	float text = _TextHeight();
	float body = text > icon ? text : icon;
	return BSize(u * 12.0f * 2 + icon + u * 10.0f, body + u * 10.0f * 2);
}


BSize
InfoTile::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


BSize
InfoTile::PreferredSize()
{
	return MinSize();
}


void
InfoTile::Draw(BRect update)
{
	(void)update;
	// Same shape and border as BCard::Draw(), with the lighter tile fill.
	BRect bounds = Bounds();
	float radius = be_plain_font->Size() * 0.25f;
	SetHighColor(tile_fill());
	FillRoundRect(bounds, radius, radius);
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	StrokeRoundRect(bounds, radius, radius);

	float u = unit();
	float padX = u * 12.0f;
	float padY = u * 10.0f;
	float iconSide = u * 34.0f;
	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BRect dest(padX, padY, padX + iconSide, padY + iconSide);
		DrawBitmap(fIcon, fIcon->Bounds(), dest);
		SetDrawingMode(B_OP_COPY);
	}
	float left = padX + iconSide + u * 10.0f;
	float right = Bounds().right - padX;
	float room = right - left;
	if (room < u * 8.0f)
		room = u * 8.0f;

	BFont labelFont(be_plain_font);
	labelFont.SetSize(u * 11.5f);
	semi_bold(&labelFont);
	BFont valueFont(be_bold_font);
	valueFont.SetSize(u * 13.5f);
	BFont detailFont(be_plain_font);
	detailFont.SetSize(u * 11.5f);
	font_height labelHeight;
	font_height valueHeight;
	font_height detailHeight;
	labelFont.GetHeight(&labelHeight);
	valueFont.GetHeight(&valueHeight);
	detailFont.GetHeight(&detailHeight);

	float y = padY + labelHeight.ascent;
	BString label(fLabel);
	labelFont.TruncateString(&label, B_TRUNCATE_END, room);
	SetFont(&labelFont);
	SetHighColor(quiet_color(160));
	DrawString(label.String(), BPoint(left, y));

	y += labelHeight.descent + u * 2.0f + valueHeight.ascent;
	BString value(fValue);
	valueFont.TruncateString(&value, B_TRUNCATE_END, room);
	SetFont(&valueFont);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	DrawString(value.String(), BPoint(left, y));
	y += valueHeight.descent;

	if (fBar >= 0.0f) {
		y += u * 3.0f;
		float span = fBar;
		if (span < 0.0f)
			span = 0.0f;
		if (span > 1.0f)
			span = 1.0f;
		BRect bar(left, y, right, y + u * 6.0f);
		SetHighColor(track_color());
		FillRoundRect(bar, u * 3.0f, u * 3.0f);
		if (span > 0.0f) {
			BRect fill = bar;
			fill.right = fill.left + bar.Width() * span;
			if (fill.right < fill.left + u * 6.0f && span > 0.0f)
				fill.right = fill.left + u * 6.0f;
			if (fill.right > bar.right)
				fill.right = bar.right;
			SetHighColor(ui_color(B_STATUS_BAR_COLOR));
			FillRoundRect(fill, u * 3.0f, u * 3.0f);
		}
		y += u * 6.0f;
	}

	if (fDetail.Length() > 0) {
		y += u * 2.0f + detailHeight.ascent;
		BString detail(fDetail);
		detailFont.TruncateString(&detail, B_TRUNCATE_END, room);
		SetFont(&detailFont);
		SetHighColor(quiet_color(160));
		DrawString(detail.String(), BPoint(left, y));
	}
	SetFont(be_plain_font);
}


class DeviceCard : public BAppearanceKit::BCard {
public:
								DeviceCard(BButton* copy);
	virtual						~DeviceCard();
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();
			void				SetName(const char* name, bool canRename);
			float				HeightFor() const;

private:
			void				_PlaceButton();
			BString				fName;
			bool				fCanRename;
			BRect				fRename;
			BBitmap*			fIcon;
			BButton*			fCopy;
};


DeviceCard::DeviceCard(BButton* copy)
	:
	BCard("device", BAppearanceKit::B_CARD_DOCUMENT),
	fCanRename(false),
	fIcon(NULL),
	fCopy(copy)
{
	SetLayout(NULL);
	float u = unit();
	fIcon = r5_mime_icon_bitmap("application/x-vnd.Haiku-Screen", u * 56.0f);
	if (fCopy != NULL) {
		AddChild(fCopy);
		fCopy->SetExplicitMinSize(BSize(B_SIZE_UNSET, u * 32.0f));
		fCopy->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, u * 32.0f));
	}
	SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP));
}


DeviceCard::~DeviceCard()
{
	delete fIcon;
}


void
DeviceCard::SetName(const char* name, bool canRename)
{
	fName = name != NULL ? name : "";
	fCanRename = canRename;
	Invalidate();
}


float
DeviceCard::HeightFor() const
{
	float u = unit();
	BFont name(be_bold_font);
	name.SetSize(u * 20.0f);
	extra_bold(&name);
	BFont quiet(be_plain_font);
	quiet.SetSize(u * 12.0f);
	font_height nameHeight;
	font_height quietHeight;
	name.GetHeight(&nameHeight);
	quiet.GetHeight(&quietHeight);
	float text = nameHeight.ascent + nameHeight.descent + u * 2.0f
		+ quietHeight.ascent + quietHeight.descent;
	float icon = u * 56.0f;
	float body = text > icon ? text : icon;
	return body + u * 12.0f * 2;
}


BSize
DeviceCard::MinSize()
{
	return BSize(280, HeightFor());
}


BSize
DeviceCard::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, HeightFor());
}


BSize
DeviceCard::PreferredSize()
{
	return MinSize();
}


void
DeviceCard::FrameResized(float width, float height)
{
	BCard::FrameResized(width, height);
	_PlaceButton();
}


void
DeviceCard::_PlaceButton()
{
	if (fCopy == NULL)
		return;
	float u = unit();
	float height = u * 32.0f;
	float width = fCopy->PreferredSize().width;
	if (width < u * 32.0f)
		width = u * 32.0f;
	float x = Bounds().right - u * 16.0f - width;
	float y = (Bounds().Height() - height) / 2.0f;
	fCopy->MoveTo(x, y);
	fCopy->ResizeTo(width, height);
}


void
DeviceCard::Draw(BRect update)
{
	BCard::Draw(update);
	_PlaceButton();
	float u = unit();
	float padX = u * 16.0f;
	float iconSide = u * 56.0f;
	float top = (Bounds().Height() - iconSide) / 2.0f;
	if (top < u * 12.0f)
		top = u * 12.0f;
	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BRect dest(padX, top, padX + iconSide, top + iconSide);
		DrawBitmap(fIcon, fIcon->Bounds(), dest);
		SetDrawingMode(B_OP_COPY);
	}
	float left = padX + iconSide + u * 16.0f;
	float right = Bounds().right - padX;
	if (fCopy != NULL)
		right = fCopy->Frame().left - u * 16.0f;
	float room = right - left;
	if (room < u * 8.0f)
		room = u * 8.0f;

	BFont nameFont(be_bold_font);
	nameFont.SetSize(u * 20.0f);
	extra_bold(&nameFont);
	BFont linkFont(be_plain_font);
	linkFont.SetSize(u * 12.0f);
	semi_bold(&linkFont);
	BFont lineFont(be_plain_font);
	lineFont.SetSize(u * 12.0f);
	font_height nameHeight;
	font_height linkHeight;
	font_height lineHeight;
	nameFont.GetHeight(&nameHeight);
	linkFont.GetHeight(&linkHeight);
	lineFont.GetHeight(&lineHeight);

	BString link("Rename…");
	float linkWidth = linkFont.StringWidth(link.String());
	float nameRoom = room - u * 10.0f - linkWidth;
	if (nameRoom < u * 8.0f)
		nameRoom = u * 8.0f;
	BString name(fName);
	nameFont.TruncateString(&name, B_TRUNCATE_END, nameRoom);

	float block = nameHeight.ascent + nameHeight.descent + u * 2.0f
		+ lineHeight.ascent + lineHeight.descent;
	float y = (Bounds().Height() - block) / 2.0f + nameHeight.ascent;
	SetFont(&nameFont);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	DrawString(name.String(), BPoint(left, y));
	float linkX = left + nameFont.StringWidth(name.String()) + u * 10.0f;
	float linkY = y - nameHeight.ascent + linkHeight.ascent
		+ (nameHeight.ascent + nameHeight.descent
			- (linkHeight.ascent + linkHeight.descent)) / 2.0f;
	SetFont(&linkFont);
	if (fCanRename)
		SetHighColor(ui_color(B_LINK_TEXT_COLOR));
	else
		SetHighColor(quiet_color(80));
	DrawString(link.String(), BPoint(linkX, linkY));
	underline(this, linkX, linkX + linkWidth, linkY, u);
	fRename.Set(linkX, linkY - linkHeight.ascent, linkX + linkWidth,
		linkY + linkHeight.descent);

	y += nameHeight.descent + u * 2.0f + lineHeight.ascent;
	BString version = version_text();
	lineFont.TruncateString(&version, B_TRUNCATE_END, room);
	SetFont(&lineFont);
	SetHighColor(quiet_color(140));
	DrawString(version.String(), BPoint(left, y));
	SetFont(be_plain_font);
}


void
DeviceCard::MouseDown(BPoint where)
{
	if (fCanRename && fRename.Contains(where)) {
		BMessage message(kMsgRename);
		if (Window() != NULL)
			Window()->PostMessage(&message, Parent());
		return;
	}
	BCard::MouseDown(where);
}


class FooterLine : public BView {
public:
								FooterLine();
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			BRect				fLink;
};


FooterLine::FooterLine()
	:
	BView("footer", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP));
}


void
FooterLine::Draw(BRect update)
{
	(void)update;
	SetLowColor(ViewColor());
	FillRect(Bounds(), B_SOLID_LOW);
	float u = unit();
	BFont font(be_plain_font);
	font.SetSize(u * 11.5f);
	BFont link(font);
	semi_bold(&link);
	font_height height;
	font.GetHeight(&height);
	float y = height.ascent;
	SetFont(&font);
	SetHighColor(quiet_color(140));
	BString leftText(kernel_text());
	leftText << " · " << image_text();
	const char* linkText = "What's new in this build";
	float linkWidth = link.StringWidth(linkText);
	float room = Bounds().Width() - linkWidth - u * 12.0f;
	if (room < u * 8.0f)
		room = u * 8.0f;
	font.TruncateString(&leftText, B_TRUNCATE_END, room);
	DrawString(leftText.String(), BPoint(0, y));
	float x = Bounds().right - linkWidth;
	SetFont(&link);
	SetHighColor(ui_color(B_LINK_TEXT_COLOR));
	DrawString(linkText, BPoint(x, y));
	underline(this, x, x + linkWidth, y, u);
	fLink.Set(x, 0, Bounds().right, height.ascent + height.descent);
	SetFont(be_plain_font);
}


void
FooterLine::MouseDown(BPoint where)
{
	if (fLink.Contains(where))
		be_roster->Launch(kAboutSignature);
}


BSize
FooterLine::MinSize()
{
	float u = unit();
	BFont font(be_plain_font);
	font.SetSize(u * 11.5f);
	font_height height;
	font.GetHeight(&height);
	return BSize(0, height.ascent + height.descent);
}


BSize
FooterLine::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
FooterLine::PreferredSize()
{
	return MinSize();
}


class RenameWindow : public BWindow {
public:
								RenameWindow(const char* name,
									const BMessenger& target);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Submit();
			BTextControl*		fName;
			BStringView*		fError;
			BMessenger			fTarget;
};


RenameWindow::RenameWindow(const char* name, const BMessenger& target)
	:
	BWindow(BRect(0, 0, 360, 120), "Rename", B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fName(NULL),
	fError(NULL),
	fTarget(target)
{
	fName = new BTextControl("name", "Name:", name, NULL);
	fError = new BStringView("error", "");
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	BButton* rename = new BButton("rename", "Rename",
		new BMessage(kMsgApplyName));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fName)
		.Add(fError)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(cancel)
			.Add(rename)
			.End();
	fName->SetTarget(this);
	rename->SetTarget(this);
	cancel->SetTarget(this);
	SetDefaultButton(rename);
	fName->MakeFocus(true);
	CenterOnScreen();
}


void
RenameWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgApplyName) {
		_Submit();
		return;
	}
	BWindow::MessageReceived(message);
}


void
RenameWindow::_Submit()
{
	const char* name = fName->Text();
	if (!name_ok(name)) {
		fError->SetText("Use letters, digits and -, up to 63, "
			"not starting or ending with -.");
		return;
	}
	BMessage apply(kMsgApplyName);
	apply.AddString("name", name);
	fTarget.SendMessage(&apply);
	PostMessage(B_QUIT_REQUESTED);
}


class InformationView : public BView {
public:
								InformationView(SettingsContext* context);
	virtual						~InformationView();
	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Show();
	virtual	void				Hide();
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);

private:
			void				_Start();
			void				_Stop();
			void				_Reload();
			void				_ApplyName(const char* name);
			BString				_CopyText() const;
			float				_NaturalHeight() const;
			SettingsContext*	fContext;
			DeviceCard*			fDevice;
			InfoTile*			fProcessor;
			InfoTile*			fMemory;
			InfoTile*			fStorage;
			InfoTile*			fGraphics;
			InfoTile*			fDisplay;
			InfoTile*			fRunning;
			BString				fMemoryValue;
			BString				fMemoryDetail;
			BString				fRunningValue;
			BString				fRunningDetail;
			BMessageRunner*		fRunner;
			bool				fCanRename;
};


InformationView::InformationView(SettingsContext* context)
	:
	BView("information", 0),
	fContext(context),
	fDevice(NULL),
	fProcessor(NULL),
	fMemory(NULL),
	fStorage(NULL),
	fGraphics(NULL),
	fDisplay(NULL),
	fRunning(NULL),
	fRunner(NULL),
	fCanRename(false)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	float u = unit();
	fCanRename = fContext != NULL && fContext->AdminChangeAllowed();
	BButton* copy = new BButton("copy", "Copy details", new BMessage(kMsgCopy));
	fDevice = new DeviceCard(copy);
	fDevice->SetName(host_name().String(), fCanRename);

	fProcessor = new InfoTile("processor", "Processor",
		"application/x-vnd.Haiku-ActivityMonitor");
	fMemory = new InfoTile("memory", "Memory",
		"application/x-vnd.Haiku-VirtualMemory");
	fStorage = new InfoTile("storage", "Storage",
		"application/x-vnd.Haiku-DriveSetup");
	fGraphics = new InfoTile("graphics", "Graphics",
		"application/x-vnd.Haiku-Devices");
	fDisplay = new InfoTile("display", "Display",
		"application/x-vnd.Haiku-Screen");
	fRunning = new InfoTile("running", "Running for",
		"application/x-vnd.Haiku-Clock");

	int32 cores = 0;
	int32 threads = 0;
	cpu_counts(cores, threads);
	BString processorDetail;
	if (cores > 0) {
		processorDetail.SetToFormat("%" B_PRId32 " core%s, %" B_PRId32
			" thread%s", cores, cores == 1 ? "" : "s", threads,
			threads == 1 ? "" : "s");
	} else if (threads > 0) {
		processorDetail.SetToFormat("%" B_PRId32 " thread%s", threads,
			threads == 1 ? "" : "s");
	}
	BString speed = frequency_text();
	if (speed.Length() > 0) {
		if (processorDetail.Length() > 0)
			processorDetail << " · ";
		processorDetail << speed;
	}
	BString brand = cpu_brand();
	if (brand.Length() == 0)
		brand = "Unknown";
	fProcessor->SetLines(brand.String(), processorDetail.String(), -1.0f);

	uint64 installed = 0;
	uint64 used = 0;
	memory_numbers(installed, used);
	fMemoryValue = installed > 0 ? format_bytes(installed) : "Unknown";
	fMemoryDetail = used > 0 ? format_bytes(used) : "0 MB";
	fMemoryDetail << " in use";
	float memoryBar = installed > 0 ? (float)used / (float)installed : 0.0f;
	fMemory->SetLines(fMemoryValue.String(), fMemoryDetail.String(),
		memoryBar);

	BString volume;
	uint64 capacity = 0;
	uint64 stored = 0;
	storage_numbers(volume, capacity, stored);
	BString storageValue = format_bytes(capacity);
	if (volume.Length() > 0) {
		BString named(volume);
		named << " · " << storageValue;
		storageValue = named;
	}
	BString storageDetail(format_bytes(stored));
	storageDetail << " used · ";
	storageDetail << format_bytes(capacity > stored ? capacity - stored : 0);
	storageDetail << " free";
	float storageBar = capacity > 0 ? (float)stored / (float)capacity : 0.0f;
	fStorage->SetLines(storageValue.String(), storageDetail.String(),
		storageBar);

	fGraphics->SetLines(graphics_name().String(),
		"Drawn by the processor (software)", -1.0f);
	fDisplay->SetLines(display_value().String(), display_detail().String(),
		-1.0f);
	fRunningValue = running_value();
	fRunningDetail = running_since();
	fRunning->SetLines(fRunningValue.String(), fRunningDetail.String(), -1.0f);

	BGridView* grid = new BGridView(u * 10.0f, u * 10.0f);
	grid->GridLayout()->SetRowWeight(0, 1.0f);
	grid->GridLayout()->SetRowWeight(1, 1.0f);
	grid->GridLayout()->AddView(fProcessor, 0, 0);
	grid->GridLayout()->AddView(fMemory, 1, 0);
	grid->GridLayout()->AddView(fStorage, 2, 0);
	grid->GridLayout()->AddView(fGraphics, 0, 1);
	grid->GridLayout()->AddView(fDisplay, 1, 1);
	grid->GridLayout()->AddView(fRunning, 2, 1);

	BLayoutBuilder::Group<>(this, B_VERTICAL, u * 10.0f)
		.Add(fDevice, 0)
		.Add(grid, 1)
		.Add(new FooterLine(), 0);
}


InformationView::~InformationView()
{
	delete fRunner;
}


void
InformationView::AttachedToWindow()
{
	BView::AttachedToWindow();
	// Without a target the button messages the window, which does not
	// know kMsgCopy, and nothing reaches the clipboard.
	BButton* copy = dynamic_cast<BButton*>(FindView("copy"));
	if (copy != NULL)
		copy->SetTarget(this);
	if (!IsHidden())
		_Start();
}


void
InformationView::DetachedFromWindow()
{
	_Stop();
	BView::DetachedFromWindow();
}


void
InformationView::Show()
{
	BView::Show();
	_Start();
}


void
InformationView::Hide()
{
	_Stop();
	BView::Hide();
}


void
InformationView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCopy: {
			BString text = _CopyText();
			BClipboard clipboard("system");
			if (clipboard.Lock()) {
				clipboard.Clear();
				BMessage* clip = clipboard.Data();
				if (clip != NULL) {
					clip->AddData("text/plain", B_MIME_TYPE, text.String(),
						text.Length());
				}
				clipboard.Commit();
				clipboard.Unlock();
			}
			break;
		}
		case kMsgRename:
			if (fCanRename) {
				RenameWindow* dialog = new RenameWindow(host_name().String(),
					BMessenger(this));
				dialog->Show();
			}
			break;
		case kMsgApplyName: {
			const char* name = NULL;
			if (message->FindString("name", &name) == B_OK)
				_ApplyName(name);
			break;
		}
		case kMsgTick:
		case kMsgRefreshPage:
			_Reload();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


bool
InformationView::HasHeightForWidth()
{
	return true;
}


void
InformationView::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	(void)width;
	float height = _NaturalHeight();
	if (min != NULL)
		*min = height;
	if (preferred != NULL)
		*preferred = height;
	if (max != NULL)
		*max = B_SIZE_UNLIMITED;
}


void
InformationView::_Start()
{
	if (fRunner != NULL)
		return;
	BMessage tick(kMsgTick);
	fRunner = new BMessageRunner(BMessenger(this), &tick, 2000000);
}


void
InformationView::_Stop()
{
	delete fRunner;
	fRunner = NULL;
}


void
InformationView::_Reload()
{
	fCanRename = fContext != NULL && fContext->AdminChangeAllowed();
	if (fDevice != NULL)
		fDevice->SetName(host_name().String(), fCanRename);
	uint64 installed = 0;
	uint64 used = 0;
	memory_numbers(installed, used);
	fMemoryValue = installed > 0 ? format_bytes(installed) : "Unknown";
	fMemoryDetail = format_bytes(used);
	fMemoryDetail << " in use";
	float memoryBar = installed > 0 ? (float)used / (float)installed : 0.0f;
	if (fMemory != NULL) {
		fMemory->SetLines(fMemoryValue.String(), fMemoryDetail.String(),
			memoryBar);
	}
	fRunningValue = running_value();
	fRunningDetail = running_since();
	if (fRunning != NULL) {
		fRunning->SetLines(fRunningValue.String(), fRunningDetail.String(),
			-1.0f);
	}
}


void
InformationView::_ApplyName(const char* name)
{
	if (!fCanRename || !name_ok(name))
		return;
	if (sethostname(name, strlen(name)) != 0) {
		BAlert* alert = new BAlert("Rename", "The name could not be saved.",
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		return;
	}
	_Reload();
}


BString
InformationView::_CopyText() const
{
	BString text;
	text << "Computer: " << host_name() << "\n";
	text << "System: " << version_text() << "\n";
	text << "Processor: " << cpu_brand() << "\n";
	int32 cores = 0;
	int32 threads = 0;
	cpu_counts(cores, threads);
	text << "Processor detail: ";
	if (cores > 0)
		text << cores << (cores == 1 ? " core, " : " cores, ");
	text << threads << (threads == 1 ? " thread" : " threads");
	BString speed = frequency_text();
	if (speed.Length() > 0)
		text << " · " << speed;
	text << "\n";
	text << "Memory: " << fMemoryValue << "\n";
	text << "Memory in use: " << fMemoryDetail << "\n";
	BString volume;
	uint64 capacity = 0;
	uint64 stored = 0;
	storage_numbers(volume, capacity, stored);
	text << "Storage: ";
	if (volume.Length() > 0)
		text << volume << " · ";
	text << format_bytes(capacity) << "\n";
	text << "Storage used: " << format_bytes(stored) << " used · ";
	text << format_bytes(capacity > stored ? capacity - stored : 0);
	text << " free\n";
	text << "Graphics: " << graphics_name() << "\n";
	text << "Graphics detail: Drawn by the processor (software)\n";
	text << "Display: " << display_value() << "\n";
	text << "Display detail: " << display_detail() << "\n";
	text << "Running for: " << fRunningValue << "\n";
	text << "Running since: " << fRunningDetail << "\n";
	text << kernel_text() << "\n";
	text << image_text();
	return text;
}


float
InformationView::_NaturalHeight() const
{
	float u = unit();
	float tiles = 0.0f;
	if (fProcessor != NULL)
		tiles = fProcessor->MinSize().height * 2.0f + u * 10.0f;
	float device = fDevice != NULL ? fDevice->HeightFor() : 0.0f;
	float footer = u * 11.5f + u * 4.0f;
	return device + u * 10.0f + tiles + u * 10.0f + footer;
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
	(void)frame;
	return new InformationView(context);
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
