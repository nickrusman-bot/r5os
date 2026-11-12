/*
 * Copyright 2005-2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Augustin Cavalier <waddlesplash>
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		René Gollent
 *		John Scipione, jscipione@gmail.com
 *		Wim van der Meer <WPJvanderMeer@gmail.com>
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */



#include <ctype.h>
#include <stdio.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <string.h>
// SPDX-SnippetEnd
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>

#include <AboutWindow.h>
#include <AppDefs.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Card.h>
#include <ColorConversion.h>
#include <ControlLook.h>
#include <DateTimeFormat.h>
#include <Dragger.h>
#include <DurationFormat.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <GridView.h>
#include <GroupView.h>
#include <fs_attr.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <NumberFormat.h>
#include <ObjectList.h>
#include <OS.h>
#include <Path.h>
#include <PathFinder.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <ScrollView.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <TabView.h>
// SPDX-SnippetEnd
#include <String.h>
#include <StringFormat.h>
#include <StringList.h>
#include <StringView.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <View.h>
#include <ViewPrivate.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <Window.h>
#include <WindowPrivate.h>

#include <AppMisc.h>
#include <AutoDeleter.h>
#include <AutoDeleterPosix.h>
#include <cpu_type.h>
#include <parsedate.h>
#include <system_revision.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <r5_os_version.h>
// SPDX-SnippetEnd

#include <Catalog.h>
#include <Language.h>
#include <Locale.h>
#include <LocaleRoster.h>

#include "HyperTextActions.h"
#include "HyperTextView.h"
#include "Utilities.h"

#include "Credits.h"


#ifndef LINE_MAX
#define LINE_MAX 2048
#endif


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutWindow"


static const char* kSignature = "application/x-vnd.Haiku-About";

static const float kWindowWidth = 500.0f;
static const float kWindowHeight = 300.0f;

static const float kSysInfoMinWidth = 163.0f;
static const float kSysInfoMinHeight = 193.0f;

static const float kDraggerMargin = 6.0f;

static const int32 kMsgScrollCreditsView = 'mviv';

static int ignored_pages(system_info*);
static int max_pages(system_info*);
static int max_and_ignored_pages(system_info*);
static int used_pages(system_info*);

static const rgb_color kIdealHaikuGreen = { 42, 131, 36, 255 };
static const rgb_color kIdealHaikuOrange = { 255, 69, 0, 255 };
static const rgb_color kIdealHaikuYellow = { 255, 176, 0, 255 };
static const rgb_color kIdealBeOSBlue = { 0, 0, 200, 255 };
static const rgb_color kIdealBeOSRed = { 200, 0, 0, 255 };
static const rgb_color kBlack = { 0, 0, 0, 255 };
static const rgb_color kWhite = { 255, 255, 255, 255 };

static const char* kBSDTwoClause = B_TRANSLATE_MARK("BSD (2-clause)");
static const char* kBSDThreeClause = B_TRANSLATE_MARK("BSD (3-clause)");
static const char* kBSDFourClause = B_TRANSLATE_MARK("BSD (4-clause)");
static const char* kGPLv2 = B_TRANSLATE_MARK("GNU GPL v2");
static const char* kGPLv3 = B_TRANSLATE_MARK("GNU GPL v3");
static const char* kLGPLv2 = B_TRANSLATE_MARK("GNU LGPL v2");
static const char* kLGPLv21 = B_TRANSLATE_MARK("GNU LGPL v2.1");
#if 0
static const char* kPublicDomain = B_TRANSLATE_MARK("Public Domain");
#endif
#ifdef __i386__
static const char* kIntel2xxxFirmware = B_TRANSLATE_MARK("Intel (2xxx firmware)");
static const char* kIntelFirmware = B_TRANSLATE_MARK("Intel WiFi Firmware");
static const char* kMarvellFirmware = B_TRANSLATE_MARK("Marvell (firmware)");
static const char* kRalinkFirmware = B_TRANSLATE_MARK("Ralink WiFi Firmware");
#endif


//	#pragma mark - TranslationComparator function


static int
TranslationComparator(const void* left, const void* right)
{
	const Translation* leftTranslation = *(const Translation**)left;
	const Translation* rightTranslation = *(const Translation**)right;

	BLanguage* language;
	BString leftName;
	if (BLocaleRoster::Default()->GetLanguage(leftTranslation->languageCode,
			&language) == B_OK) {
		language->GetName(leftName);
		delete language;
	} else
		leftName = leftTranslation->languageCode;

	BString rightName;
	if (BLocaleRoster::Default()->GetLanguage(rightTranslation->languageCode,
			&language) == B_OK) {
		language->GetName(rightName);
		delete language;
	} else
		rightName = rightTranslation->languageCode;

	BCollator collator;
	BLocale::Default()->GetCollator(&collator);
	return collator.Compare(leftName.String(), rightName.String());
}


//	#pragma mark - class definitions


class AboutApp : public BApplication {
public:
							AboutApp();
			void			MessageReceived(BMessage* message);
};


class AboutView;

class AboutWindow : public BWindow {
public:
							AboutWindow();

	virtual	bool			QuitRequested();

			AboutView*		fAboutView;
};


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
class R5MarkView : public BView {
public:
							R5MarkView();
	virtual					~R5MarkView();

	virtual	void			Draw(BRect updateRect);
	virtual	void			AttachedToWindow();
			void			ApplySize();

private:
			BBitmap*		fLogo;
};


class R5Header : public BAppearanceKit::BCard {
public:
							R5Header();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

private:
			void			_ApplyFonts();

			R5MarkView*		fMark;
			BStringView*	fTitle;
			BStringView*	fOperating;
			BStringView*	fVersion;
};
// SPDX-SnippetEnd


class CropView : public BView {
public:
							CropView(BView* target, int32 left, int32 top,
								int32 right, int32 bottom);
	virtual					~CropView();

	virtual	BSize			MinSize();
	virtual	BSize			MaxSize();

	virtual void			DoLayout();

private:
			BView*			fTarget;
			int32			fCropLeft;
			int32			fCropTop;
			int32			fCropRight;
			int32			fCropBottom;
};


class SysInfoDragger : public BDragger {
public:
							SysInfoDragger(BView* target);

	virtual	void			MessageReceived(BMessage* message);

private:
			BView*			fTarget;
};


class SysInfoView : public BAppearanceKit::BCard {
public:
							SysInfoView();
	virtual					~SysInfoView();

	virtual	status_t		Archive(BMessage* archive, bool deep = true) const;
	static	BArchivable*	Instantiate(BMessage* archive);

	virtual	void			AllAttached();
	virtual	void			AttachedToWindow();
	virtual	void			Draw(BRect);
	virtual void			MessageReceived(BMessage* message);
	virtual void			Pulse();

			void			CacheInitialSize();

			float			MinWidth() const { return fCachedMinWidth; };
			float			MinHeight() const { return fCachedMinHeight; };

private:
			void			_UpdateColors();
			bool			_IsReplicant() const;
			bool			_OnDesktop() const;
			void			_ResizeBy(float, float);
			void			_ResizeTo(float, float);

			BStringView*	_CreateLabel(const char*, const char*);
			void			_UpdateLabel(BStringView*);
			BStringView*	_CreateSubtext(const char*, const char*);
			void			_UpdateSubtext(BStringView*);
			void			_UpdateText(BTextView*);
			void			_CreateDragger();

			float			_BaseWidth();
			float			_BaseHeight();

			BString			_GetOSVersion();
			BString			_GetABIVersion();
			BString			_GetCPUCount(system_info*);
			BString			_GetCPUInfo();
			BString			_GetCPUFrequency();
			BString			_GetRamSize(system_info*);
			BString			_GetRamUsage(system_info*);
			BString			_GetKernelDateTime(system_info*);
			BString			_GetUptime();

			float			_UptimeHeight();

private:
			BStringView*	fVersionLabelView;
			BStringView*	fVersionInfoView;
			BStringView*	fCPULabelView;
			BStringView*	fCPUInfoView;
			BStringView*	fMemSizeView;
			BStringView*	fMemUsageView;
			BStringView*	fKernelDateTimeView;
			BTextView*		fUptimeView;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			BStringView*	fBuildView;
// SPDX-SnippetEnd

			BDragger*		fDragger;

			BNumberFormat	fNumberFormat;

			float			fCachedBaseWidth;
			float			fCachedMinWidth;
			float			fCachedBaseHeight;
			float			fCachedMinHeight;

	static const uint8		kLabelCount = 5;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	static const uint8		kSubtextCount = 7;
// SPDX-SnippetEnd
};


class AboutView : public BView {
public:
							AboutView();
							~AboutView();

	virtual void			AttachedToWindow();
	virtual void			Pulse();
	virtual void			MessageReceived(BMessage* message);
	virtual void			MouseDown(BPoint where);

			void			AddCopyrightEntry(const char* name,
								const char* text,
								const StringVector& licenses,
								const StringVector& sources,
								const char* url);
			void			AddCopyrightEntry(const char* name,
								const char* text, const char* url = NULL);
			void			PickRandomHaiku();

private:
	typedef std::map<std::string, PackageCredit*> PackageCreditMap;

			void			_CreateScrollRunner();
			BView*			_CreateLogoView();
			SysInfoView*	_CreateSysInfoView();
			CropView*		_CreateCreditsView();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			BView*			_CreateInfoTabs();
			void			_FillWhatsNew();
			void			_FillLicenses();
// SPDX-SnippetEnd
			status_t		_GetLicensePath(const char* license,
								BPath& path);
			void			_AddCopyrightsFromAttribute();
			void			_AddPackageCredit(const PackageCredit& package);
			void			_AddPackageCreditEntries();


private:
			BView*			fLogoView;
			SysInfoView*	fSysInfoView;
			HyperTextView*	fCreditsView;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			HyperTextView*	fWhatsNewView;
			HyperTextView*	fLicensesView;
			BTabView*		fTabView;
// SPDX-SnippetEnd

			bigtime_t		fLastActionTime;
			BMessageRunner*	fScrollRunner;

			float			fCachedMinWidth;
			float			fCachedMinHeight;

			PackageCreditMap fPackageCredits;

private:
			rgb_color		fTextColor;
			rgb_color		fLinkColor;
			rgb_color		fHaikuOrangeColor;
			rgb_color		fHaikuGreenColor;
			rgb_color		fHaikuYellowColor;
			rgb_color		fBeOSRedColor;
			rgb_color		fBeOSBlueColor;
};


//	#pragma mark - AboutApp


AboutApp::AboutApp()
	:
	BApplication(kSignature)
{
	B_TRANSLATE_MARK_SYSTEM_NAME_VOID("AboutSystem");

	AboutWindow* window = new(std::nothrow) AboutWindow();
	if (window != NULL)
		window->Show();
}


void
AboutApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SILENT_RELAUNCH:
			WindowAt(0)->Activate();
			break;
	}

	BApplication::MessageReceived(message);
}


//	#pragma mark - AboutWindow


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static float r5_about_width();
// SPDX-SnippetEnd


AboutWindow::AboutWindow()
	:
	BWindow(BRect(0, 0, kWindowWidth, kWindowHeight),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		B_TRANSLATE("About R5"), B_TITLED_WINDOW,
// SPDX-SnippetEnd
		B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE)
{
	SetLayout(new BGroupLayout(B_VERTICAL, 0));

	fAboutView = new AboutView();
	AddChild(fAboutView);

	UpdateSizeLimits();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Open at the mockup width. The layout max stays unlimited, so the
	// window can grow and shrink on both axes.
	float mockupWidth = r5_about_width();
	float height = Bounds().Height();
	if (mockupWidth * 0.618f > height)
		height = mockupWidth * 0.618f;
	ResizeTo(mockupWidth, height);
// SPDX-SnippetEnd

	CenterOnScreen();
}


bool
AboutWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutView"


//	#pragma mark - LogoView


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static const char*
r5_arch_name()
{
	const char* archName = B_HAIKU_ABI_NAME;
	if (strcmp(archName, "x86_64") == 0)
		return "AMD64";
	if (strcmp(archName, "aarch64") == 0)
		return "ARM64";
	if (strcmp(archName, "riscv64") == 0)
		return "RISC-V 64";
	return archName;
}
// SPDX-SnippetEnd


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static void
r5_make_transparent(BView* view)
{
	if (view == NULL)
		return;
	view->SetViewColor(B_TRANSPARENT_COLOR);
	view->SetFlags(view->Flags() | B_TRANSPARENT_BACKGROUND);
}


static void
r5_allow_wide(BView* view)
{
	if (view == NULL)
		return;
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}


// The mockup window is 540 wide at 12 pt. That is the window, not the
// content inside the big insets.
static float
r5_about_width()
{
	return be_plain_font->Size() * 45.0f;
}


static float
r5_about_content_width()
{
	float inset = be_control_look->ComposeSpacing(B_USE_BIG_SPACING);
	return r5_about_width() - 2.0f * inset;
}


static void
r5_color_named(BView* root, const char* name, rgb_color color)
{
	BView* view = root->FindView(name);
	if (view != NULL)
		view->SetHighColor(color);
}


static void
r5_color_system_info(BView* root, rgb_color text)
{
	rgb_color muted = tint_color(text, B_DARKEN_2_TINT);
	const char* labels[] = {
		"cpulabel", "memlabel", "kernellabel", "uptimelabel", NULL
	};
	const char* values[] = {
		"cputext", "ramusagetext", "kerneltext", NULL
	};
	for (int32 index = 0; labels[index] != NULL; index++)
		r5_color_named(root, labels[index], text);
	for (int32 index = 0; values[index] != NULL; index++)
		r5_color_named(root, values[index], muted);
	BTextView* uptime = dynamic_cast<BTextView*>(root->FindView("uptimetext"));
	if (uptime != NULL)
		uptime->SetFontAndColor(NULL, 0, &muted);
}


static BGroupView*
r5_info_cell(BView* label, BView* value)
{
	BGroupView* cell = new BGroupView(B_VERTICAL, 0);
	r5_make_transparent(cell);
	r5_make_transparent(label);
	r5_make_transparent(value);
	BLayoutBuilder::Group<>(cell)
		.Add(label)
		.Add(value);
	return cell;
}


R5MarkView::R5MarkView()
	:
	BView("r5-mark", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fLogo(NULL)
{
	r5_make_transparent(this);
	SetDrawingMode(B_OP_ALPHA);
#ifdef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
	rgb_color bgColor = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	if (bgColor.IsLight())
		fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "logo.png");
	else
		fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "logo_dark.png");
#else
	fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "r5_symbol.png");
#endif
}


R5MarkView::~R5MarkView()
{
	delete fLogo;
}


void
R5MarkView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fLogo == NULL)
		return;
	DrawBitmap(fLogo, fLogo->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);
}


void
R5MarkView::AttachedToWindow()
{
	BView::AttachedToWindow();
	ApplySize();
}


void
R5MarkView::ApplySize()
{
	float side = be_plain_font->Size() * 4.0f;
	SetExplicitMinSize(BSize(side, side));
	SetExplicitMaxSize(BSize(side, side));
	Invalidate();
}


R5Header::R5Header()
	:
	BAppearanceKit::BCard("logo", BAppearanceKit::B_CARD_DOCUMENT,
		B_HORIZONTAL),
	fMark(NULL),
	fTitle(NULL),
	fOperating(NULL),
	fVersion(NULL)
{
	fMark = new R5MarkView();
	fTitle = new BStringView("r5-title", "R5");
	fOperating = new BStringView("r5-operating", "Operating System");
	BString version;
	version.SetToFormat("Version %s • %s", R5_OS_VERSION, r5_arch_name());
	fVersion = new BStringView("r5-version", version);
	r5_make_transparent(fTitle);
	r5_make_transparent(fOperating);
	r5_make_transparent(fVersion);
	r5_allow_wide(fTitle);
	r5_allow_wide(fOperating);
	r5_allow_wide(fVersion);

	BGroupView* line = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	r5_make_transparent(line);
	BLayoutBuilder::Group<>(line)
		.Add(fTitle, 0.0f)
		.Add(fOperating, 0.0f)
		.AddGlue();

	BGroupView* text = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	r5_make_transparent(text);
	BLayoutBuilder::Group<>(text)
		.Add(line)
		.Add(fVersion);

	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.Add(fMark)
		.Add(text);
	_ApplyFonts();
}


void
R5Header::AttachedToWindow()
{
	BView::AttachedToWindow();
	_ApplyFonts();
}


void
R5Header::MessageReceived(BMessage* message)
{
	if (message->what == B_FONTS_UPDATED || message->what == B_COLORS_UPDATED)
		_ApplyFonts();
	BCard::MessageReceived(message);
}


void
R5Header::_ApplyFonts()
{
	float size = be_plain_font->Size();
	BFont title(be_bold_font);
	title.SetSize(size * 2.0f);
	fTitle->SetFont(&title, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
	BFont operating(be_plain_font);
	operating.SetSize(size * 2.0f);
	fOperating->SetFont(&operating, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
	fVersion->SetFont(be_plain_font, B_FONT_FAMILY_AND_STYLE);

	rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
	fTitle->SetHighColor(text);
	fOperating->SetHighColor(text);
	fVersion->SetHighColor(tint_color(text, B_DARKEN_2_TINT));
	if (fMark != NULL)
		fMark->ApplySize();
}
// SPDX-SnippetEnd


//	#pragma mark - CropView


CropView::CropView(BView* target, int32 left, int32 top, int32 right,
	int32 bottom)
	:
	BView("crop view", 0),
	fTarget(target),
	fCropLeft(left),
	fCropTop(top),
	fCropRight(right),
	fCropBottom(bottom)
{
	AddChild(target);
}


CropView::~CropView()
{
}


BSize
CropView::MinSize()
{
	if (fTarget == NULL)
		return BSize();

	BSize size = fTarget->MinSize();
	if (size.width != B_SIZE_UNSET)
		size.width -= fCropLeft + fCropRight;
	if (size.height != B_SIZE_UNSET)
		size.height -= fCropTop + fCropBottom;

	return size;
}


BSize
CropView::MaxSize()
{
	if (fTarget == NULL)
		return BSize();

	BSize size = fTarget->MaxSize();
	if (size.width != B_SIZE_UNSET)
		size.width -= fCropLeft + fCropRight;
	if (size.height != B_SIZE_UNSET)
		size.height -= fCropTop + fCropBottom;

	return size;
}


void
CropView::DoLayout()
{
	BView::DoLayout();

	if (fTarget == NULL)
		return;

	fTarget->MoveTo(-fCropLeft, -fCropTop);
	fTarget->ResizeTo(Bounds().Width() + fCropLeft + fCropRight,
		Bounds().Height() + fCropTop + fCropBottom);
}


//	#pragma mark - SysInfoDragger


SysInfoDragger::SysInfoDragger(BView* target)
	:
	BDragger(BRect(0, 0, 7, 7), target, B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM),
	fTarget(target)
{
}


void
SysInfoDragger::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case _SHOW_DRAG_HANDLES_:
			if (fTarget != NULL)
				fTarget->Invalidate();
		default:
			BDragger::MessageReceived(message);
			break;
	}
}


//	#pragma mark - SysInfoView


SysInfoView::SysInfoView()
	:
	BAppearanceKit::BCard("AboutSystem", BAppearanceKit::B_CARD_PANEL),
	fVersionLabelView(NULL),
	fVersionInfoView(NULL),
	fCPULabelView(NULL),
	fCPUInfoView(NULL),
	fMemSizeView(NULL),
	fMemUsageView(NULL),
	fKernelDateTimeView(NULL),
	fUptimeView(NULL),
	fBuildView(NULL),
	fDragger(NULL),
	fCachedBaseWidth(kSysInfoMinWidth),
	fCachedMinWidth(kSysInfoMinWidth),
	fCachedBaseHeight(kSysInfoMinHeight),
	fCachedMinHeight(kSysInfoMinHeight)
{
	SetFlags(Flags() | B_PULSE_NEEDED);

	// Begin construction of system information controls.
	system_info sysInfo;
	get_system_info(&sysInfo);

	// Create all the various labels for system infomation.

	// OS Version / ABI
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fVersionLabelView = _CreateLabel("oslabel",
		B_TRANSLATE("R5 Operating System"));
	fVersionInfoView = _CreateSubtext("ostext", _GetOSVersion());
	BStringView* abiView = _CreateSubtext("abitext", _GetABIVersion());
// SPDX-SnippetEnd

	// CPU count, type and clock speed
	fCPULabelView = _CreateLabel("cpulabel", _GetCPUCount(&sysInfo));
	fCPUInfoView = _CreateSubtext("cputext", _GetCPUInfo());

	// Memory size and usage
	fMemSizeView = _CreateLabel("memlabel", _GetRamSize(&sysInfo));
	fMemUsageView = _CreateSubtext("ramusagetext", _GetRamUsage(&sysInfo));

	// Kernel build time/date
	BStringView* kernelLabel = _CreateLabel("kernellabel", B_TRANSLATE("Kernel:"));
	fKernelDateTimeView = _CreateSubtext("kerneltext", _GetKernelDateTime(&sysInfo));

	// Uptime
	BStringView* uptimeLabel = _CreateLabel("uptimelabel", B_TRANSLATE("Time running:"));
	fUptimeView = new BTextView("uptimetext");
	fUptimeView->SetText(_GetUptime());
	_UpdateText(fUptimeView);
	r5_allow_wide(fUptimeView);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BString buildLine;
	buildLine.SetToFormat(B_TRANSLATE_COMMENT("Build %" B_PRId32,
		"Build line under the system information, e.g. Build 10001"),
		(int32)R5_OS_BUILD);
	fBuildView = new BStringView("buildtext", buildLine);
	_UpdateSubtext(fBuildView);
	BFont buildFont(be_plain_font);
	buildFont.SetSize(be_plain_font->Size() * 0.85f);
	fBuildView->SetFont(&buildFont, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
// SPDX-SnippetEnd

	// Processors and kernel on the left, memory and time on the right.
	BGridView* grid = new BGridView(B_USE_BIG_SPACING, B_USE_BIG_SPACING);
	r5_make_transparent(grid);
	BLayoutBuilder::Grid<>((BGridLayout*)grid->GetLayout())
		.Add(r5_info_cell(fCPULabelView, fCPUInfoView), 0, 0)
		.Add(r5_info_cell(fMemSizeView, fMemUsageView), 1, 0)
		.Add(r5_info_cell(kernelLabel, fKernelDateTimeView), 0, 1)
		.Add(r5_info_cell(uptimeLabel, fUptimeView), 1, 1)
		.SetColumnWeight(0, 1.0f)
		.SetColumnWeight(1, 1.0f);

	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.Add(fVersionLabelView)
		.Add(fVersionInfoView)
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		.Add(abiView)
		.Add(grid)
		.Add(fBuildView);
// SPDX-SnippetEnd

	_CreateDragger();
}


SysInfoView::~SysInfoView()
{
}


status_t
SysInfoView::Archive(BMessage* archive, bool deep) const
{
	// DO NOT record inherited class members
	status_t result = B_OK;

	// record app signature for replicant add-on loading
	if (result == B_OK)
		result = archive->AddString("add_on", kSignature);

	// record class last
	if (result == B_OK)
		result = archive->AddString("class", "SysInfoView");

	return result;
}


BArchivable*
SysInfoView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "SysInfoView"))
		return NULL;

	return new SysInfoView();
}


void
SysInfoView::AllAttached()
{
	BView::AllAttached();

	_UpdateColors();
}


void
SysInfoView::AttachedToWindow()
{
	BView::AttachedToWindow();

	Window()->SetPulseRate(500000);
	if (!_IsReplicant()) {
		fVersionLabelView->Hide();
		fVersionInfoView->Hide();
		BView* abiView = FindView("abitext");
		if (abiView != NULL)
			abiView->Hide();
		if (fDragger != NULL)
			fDragger->Hide();
	}
	DoLayout();

	if (_IsReplicant()) {
		// if we are a replicant the parent view doesn't do this for us
		CacheInitialSize();
		// add extra height for dragger: fixed, and insets: font-dependent
		float draggerHeight = fDragger->Bounds().Height() + kDraggerMargin * 2;
		float insets = be_control_look->DefaultLabelSpacing() * 2;
		ResizeTo(fCachedMinWidth, fCachedMinHeight + draggerHeight + insets);
	}
}


void
SysInfoView::CacheInitialSize()
{
	fCachedBaseWidth = _BaseWidth();
	float insets = be_control_look->DefaultLabelSpacing() * 2;

	// increase min width based on some potentially wide string views
	fCachedMinWidth = ceilf(std::max(fCachedBaseWidth,
		fVersionLabelView->StringWidth(fVersionLabelView->Text()) + insets));
	fCachedMinWidth = ceilf(std::max(fCachedMinWidth,
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		fVersionInfoView->StringWidth(fVersionInfoView->Text()) + insets));
	fCachedMinWidth = ceilf(std::max(fCachedMinWidth,
// SPDX-SnippetEnd
		fCPUInfoView->StringWidth(fCPUInfoView->Text()) + insets));
	fCachedMinWidth = ceilf(std::max(fCachedMinWidth,
		fMemSizeView->StringWidth(fMemSizeView->Text()) + insets));

	// width is fixed, height can grow in Pulse()
	fCachedBaseHeight = _BaseHeight();

	// determine initial line count using current font
	float lineCount = ceilf(be_plain_font->StringWidth(fUptimeView->Text())
		/ (fCachedMinWidth - insets));
	float uptimeHeight = fUptimeView->LineHeight(0) * lineCount;
	fCachedMinHeight = fCachedBaseHeight + uptimeHeight;
	if (GetLayout() != NULL) {
		BSize layoutSize = GetLayout()->MinSize();
		if (layoutSize.width > fCachedMinWidth)
			fCachedMinWidth = layoutSize.width;
		if (layoutSize.height > 0)
			fCachedMinHeight = layoutSize.height;
	}

	// set view limits
	float maxWidth = _IsReplicant() ? fCachedMinWidth : B_SIZE_UNLIMITED;
	SetExplicitMinSize(BSize(fCachedMinWidth, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(maxWidth,
		_IsReplicant() ? fCachedMinHeight : B_SIZE_UNSET));
	fUptimeView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		fUptimeView->LineHeight(0)));
}


void
SysInfoView::Draw(BRect updateRect)
{
	BAppearanceKit::BCard::Draw(updateRect);

	// stroke a line around the view
	if (_IsReplicant() && fDragger != NULL && fDragger->AreDraggersDrawn())
		StrokeRect(Bounds());
}


void
SysInfoView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_COLORS_UPDATED:
			if (_OnDesktop())
				break;

			if (message->HasColor(ui_color_name(B_PANEL_BACKGROUND_COLOR))
				|| message->HasColor(ui_color_name(B_PANEL_TEXT_COLOR))) {
				_UpdateColors();
				Invalidate();
			}
			break;

		case B_WORKSPACE_ACTIVATED:
			if (_OnDesktop()) {
				_UpdateColors();
				Invalidate();
			}
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
SysInfoView::Pulse()
{
	system_info sysInfo;
	get_system_info(&sysInfo);

	fMemUsageView->SetText(_GetRamUsage(&sysInfo));
	fUptimeView->SetText(_GetUptime());

	float newHeight = fCachedBaseHeight + _UptimeHeight();
	_ResizeBy(0, newHeight - fCachedMinHeight);
	fCachedMinHeight = newHeight;

	float maxWidth = _IsReplicant() ? fCachedMinWidth : B_SIZE_UNLIMITED;
	SetExplicitMinSize(BSize(fCachedMinWidth, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(maxWidth, fCachedMinHeight));
}


void
SysInfoView::_UpdateColors()
{
	if (_OnDesktop()) {
		rgb_color textColor = (Parent()->ViewColor().Brightness()
			<= ui_color(B_DESKTOP_COLOR).Brightness() ? kWhite : kBlack);

		SetDrawingMode(B_OP_ALPHA);
		SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
		SetViewColor(B_TRANSPARENT_COLOR);
		SetLowColor(blend_color(B_TRANSPARENT_COLOR, textColor, 192));
		SetHighColor(textColor);

		BLayout* layout = GetLayout();
		int32 itemCount = layout->CountItems() - 2;
			// leave out dragger and uptime
		for (int32 index = 0; index < itemCount; index++) {
			BView* view = layout->ItemAt(index)->View();
			if (view == NULL)
				continue;

			view->SetDrawingMode(B_OP_ALPHA);
			view->SetFlags(view->Flags() | B_TRANSPARENT_BACKGROUND);
			view->SetViewColor(B_TRANSPARENT_COLOR);
			view->SetLowColor(blend_color(B_TRANSPARENT_COLOR, textColor, 192));
			view->SetHighColor(textColor);
		}

		fUptimeView->SetDrawingMode(B_OP_ALPHA);
		fUptimeView->SetFlags(fUptimeView->Flags() | B_TRANSPARENT_BACKGROUND);
		fUptimeView->SetViewColor(B_TRANSPARENT_COLOR);
		fUptimeView->SetLowColor(blend_color(B_TRANSPARENT_COLOR, textColor, 192));
		fUptimeView->SetFontAndColor(NULL, 0, &textColor);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		r5_color_system_info(this, textColor);
		if (fBuildView != NULL) {
			rgb_color buildColor = tint_color(textColor, B_LIGHTEN_1_TINT);
			fBuildView->SetDrawingMode(B_OP_ALPHA);
			fBuildView->SetFlags(fBuildView->Flags() | B_TRANSPARENT_BACKGROUND);
			fBuildView->SetViewColor(B_TRANSPARENT_COLOR);
			fBuildView->SetLowColor(blend_color(B_TRANSPARENT_COLOR,
				buildColor, 192));
			fBuildView->SetHighColor(buildColor);
		}
// SPDX-SnippetEnd
	} else {
		rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
		r5_make_transparent(fUptimeView);
		fUptimeView->SetDrawingMode(B_OP_ALPHA);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		r5_color_system_info(this, textColor);
		if (fBuildView != NULL)
			fBuildView->SetHighColor(tint_color(textColor, B_LIGHTEN_1_TINT));
// SPDX-SnippetEnd
	}
}


bool
SysInfoView::_IsReplicant() const
{
	return dynamic_cast<AboutWindow*>(Window()) == NULL;
}


bool
SysInfoView::_OnDesktop() const
{
	return Window() != NULL
		&& Window()->Look() == kDesktopWindowLook
		&& Window()->Feel() == kDesktopWindowFeel;
}


void
SysInfoView::_ResizeBy(float widthChange, float heightChange)
{
	if (!_IsReplicant() || (widthChange == 0 && heightChange == 0))
		return;

	ResizeBy(widthChange, heightChange);
	MoveBy(-widthChange, -heightChange);
}


void
SysInfoView::_ResizeTo(float width, float height)
{
	_ResizeBy(width - Bounds().Width(), height - Bounds().Height());
}


BStringView*
SysInfoView::_CreateLabel(const char* name, const char* text)
{
	BStringView* label = new BStringView(name, text);
	_UpdateLabel(label);

	return label;
}


void
SysInfoView::_UpdateLabel(BStringView* label)
{
	label->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	label->SetFont(be_bold_font, B_FONT_FAMILY_AND_STYLE);
	r5_make_transparent(label);
	r5_allow_wide(label);
}


BStringView*
SysInfoView::_CreateSubtext(const char* name, const char* text)
{
	BStringView* subtext = new BStringView(name, text);
	_UpdateSubtext(subtext);

	return subtext;
}


void
SysInfoView::_UpdateSubtext(BStringView* subtext)
{
	subtext->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_UNSET));
	subtext->SetFont(be_plain_font, B_FONT_FAMILY_AND_STYLE);
	r5_make_transparent(subtext);
	r5_allow_wide(subtext);
}


void
SysInfoView::_UpdateText(BTextView* textView)
{
	textView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	textView->SetFontAndColor(be_plain_font, B_FONT_FAMILY_AND_STYLE);
	textView->SetColorSpace(B_RGBA32);
	textView->MakeResizable(false);
	textView->MakeEditable(false);
	textView->MakeSelectable(false);
	textView->SetWordWrap(true);
	textView->SetDoesUndo(false);
	textView->SetInsets(0, 0, 0, 0);
}


void
SysInfoView::_CreateDragger()
{
	// create replicant dragger and add it as the new child 0
	fDragger = new SysInfoDragger(this);
	BPopUpMenu* popUp = new BPopUpMenu("Shelf", false, false, B_ITEMS_IN_COLUMN);
	popUp->AddItem(new BMenuItem(B_TRANSLATE("Remove replicant"),
		new BMessage(kDeleteReplicant)));
	fDragger->SetPopUp(popUp);
	AddChild(fDragger, ChildAt(0));
}


float
SysInfoView::_BaseWidth()
{
	// based on font size
	return be_plain_font->StringWidth("M") * 24;
}


float
SysInfoView::_BaseHeight()
{
	// based on line heights
	font_height plainFH;
	be_plain_font->GetHeight(&plainFH);
	font_height boldFH;
	be_bold_font->GetHeight(&boldFH);

	uint8 labels = kLabelCount;
	uint8 subtexts = kSubtextCount;
	if (Window() != NULL && !_IsReplicant()) {
		labels -= 1;
		subtexts -= 2;
	}
	return ceilf(((boldFH.ascent + boldFH.descent) * labels
		+ (plainFH.ascent + plainFH.descent) * subtexts
		+ be_control_look->DefaultLabelSpacing() * labels));
}


BString
SysInfoView::_GetOSVersion()
{
	BString revision;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#ifndef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
	revision.SetToFormat(
		B_TRANSLATE_COMMENT("Version %s • %s",
			"Version line, e.g. Version 2026.1-dev • AMD64"),
		R5_OS_VERSION, r5_arch_name());
	return revision;
#else
// SPDX-SnippetEnd
	const char* hrev = __get_haiku_revision();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (hrev != NULL) {
// SPDX-SnippetEnd
		revision.SetToFormat(B_TRANSLATE_COMMENT("Version: %s",
			"Version: R1 or hrev99999"), hrev);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	} else
// SPDX-SnippetEnd
		revision = B_TRANSLATE("Version:");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#endif
// SPDX-SnippetEnd

	return revision;
}


BString
SysInfoView::_GetABIVersion()
{
	BString abiVersion;

	// the version is stored in the BEOS:APP_VERSION attribute of libbe.so
	BPath path;
	if (find_directory(B_BEOS_LIB_DIRECTORY, &path) == B_OK) {
		path.Append("libbe.so");

		BAppFileInfo appFileInfo;
		version_info versionInfo;
		BFile file;
		if (file.SetTo(path.Path(), B_READ_ONLY) == B_OK
			&& appFileInfo.SetTo(&file) == B_OK
			&& appFileInfo.GetVersionInfo(&versionInfo,
				B_APP_VERSION_KIND) == B_OK
			&& versionInfo.short_info[0] != '\0')
			abiVersion = versionInfo.short_info;
	}

	if (abiVersion.IsEmpty())
		abiVersion = B_TRANSLATE("Unknown");

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Package ABI stays x86_64. About shows the name used for the target.
	const char* archName = B_HAIKU_ABI_NAME;
	if (strcmp(archName, "x86_64") == 0)
		archName = "AMD64";
	else if (strcmp(archName, "aarch64") == 0)
		archName = "ARM64";
	else if (strcmp(archName, "riscv64") == 0)
		archName = "RISC-V 64";
	abiVersion << " (" << archName << ")";
// SPDX-SnippetEnd

	return abiVersion;
}


BString
SysInfoView::_GetCPUCount(system_info* sysInfo)
{
	static BStringFormat format(B_TRANSLATE_COMMENT(
		"{0, plural, one{Processor:} other{# Processors:}}",
		"\"Processor:\" or \"2 Processors:\""));

	BString processorLabel;
	format.Format(processorLabel, sysInfo->cpu_count);
	return processorLabel;
}


BString
SysInfoView::_GetCPUInfo()
{
	uint32 topologyNodeCount = 0;
	cpu_topology_node_info* topology = NULL;

	get_cpu_topology_info(NULL, &topologyNodeCount);
	if (topologyNodeCount != 0)
		topology = new cpu_topology_node_info[topologyNodeCount];
	get_cpu_topology_info(topology, &topologyNodeCount);

	enum cpu_platform platform = B_CPU_UNKNOWN;
	enum cpu_vendor cpuVendor = B_CPU_VENDOR_UNKNOWN;
	uint32 cpuModel = 0;

	for (uint32 i = 0; i < topologyNodeCount; i++) {
		switch (topology[i].type) {
			case B_TOPOLOGY_ROOT:
				platform = topology[i].data.root.platform;
				break;

			case B_TOPOLOGY_PACKAGE:
				cpuVendor = topology[i].data.package.vendor;
				break;

			case B_TOPOLOGY_CORE:
				cpuModel = topology[i].data.core.model;
				break;

			default:
				break;
		}
	}

	delete[] topology;

	BString cpuType;
	cpuType << get_cpu_vendor_string(cpuVendor) << " "
		<< get_cpu_model_string(platform, cpuVendor, cpuModel)
		<< " @ " << _GetCPUFrequency();

	return cpuType;
}


BString
SysInfoView::_GetCPUFrequency()
{
	BString clockSpeed;

	long int frequency = get_rounded_cpu_speed();
	if (frequency < 1000) {
		clockSpeed.SetToFormat(B_TRANSLATE_COMMENT("%ld MHz",
			"750 Mhz (CPU clock speed)"), frequency);
	}
	else {
		clockSpeed.SetToFormat(B_TRANSLATE_COMMENT("%.2f GHz",
			"3.49 Ghz (CPU clock speed)"), frequency / 1000.0f);
	}

	return clockSpeed;
}


BString
SysInfoView::_GetRamSize(system_info* sysInfo)
{
	BString ramSize;
	ramSize.SetToFormat(B_TRANSLATE_COMMENT("%d MiB Memory:",
		"2048 MiB Memory:"), max_and_ignored_pages(sysInfo));

	return ramSize;
}


BString
SysInfoView::_GetRamUsage(system_info* sysInfo)
{
	BString ramUsage;
	BString data;
	double usedMemoryPercent = double(sysInfo->used_pages) / sysInfo->max_pages;
	status_t status = fNumberFormat.FormatPercent(data, usedMemoryPercent);

	if (status == B_OK) {
		ramUsage.SetToFormat(B_TRANSLATE_COMMENT("%d MiB used (%s)",
			"326 MiB used (16%)"), used_pages(sysInfo), data.String());
	} else {
		ramUsage.SetToFormat(B_TRANSLATE_COMMENT("%d MiB used (%d%%)",
			"326 MiB used (16%)"), used_pages(sysInfo), (int)(100 * usedMemoryPercent));
	}

	return ramUsage;
}


BString
SysInfoView::_GetKernelDateTime(system_info* sysInfo)
{
	BString kernelDateTime;

	BString buildDateTime;
	buildDateTime << sysInfo->kernel_build_date << " " << sysInfo->kernel_build_time;

	time_t buildDateTimeStamp = parsedate(buildDateTime, -1);

	if (buildDateTimeStamp > 0) {
		if (BDateTimeFormat().Format(kernelDateTime, buildDateTimeStamp,
			B_LONG_DATE_FORMAT, B_MEDIUM_TIME_FORMAT) != B_OK)
			kernelDateTime.SetTo(buildDateTime);
	} else
		kernelDateTime.SetTo(buildDateTime);

	return kernelDateTime;
}


BString
SysInfoView::_GetUptime()
{
	BDurationFormat formatter;
	BString uptimeText;

	bigtime_t uptime = system_time();
	bigtime_t now = (bigtime_t)time(NULL) * 1000000;
	formatter.Format(uptimeText, now - uptime, now);

	return uptimeText;
}


float
SysInfoView::_UptimeHeight()
{
	return fUptimeView->LineHeight(0) * fUptimeView->CountLines();
}


//	#pragma mark - AboutView


AboutView::AboutView()
	:
	BView("aboutview", B_WILL_DRAW | B_PULSE_NEEDED),
	fLogoView(NULL),
	fSysInfoView(NULL),
	fCreditsView(NULL),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fWhatsNewView(NULL),
	fLicensesView(NULL),
	fTabView(NULL),
// SPDX-SnippetEnd
	fLastActionTime(system_time()),
	fScrollRunner(NULL),
	fCachedMinWidth(kSysInfoMinWidth),
	fCachedMinHeight(kSysInfoMinHeight)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	// Assign the colors, sadly this does not respect live color updates
	fTextColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	fLinkColor = ui_color(B_LINK_TEXT_COLOR);
	fHaikuOrangeColor = mix_color(fTextColor, kIdealHaikuOrange, 191);
	fHaikuGreenColor = mix_color(fTextColor, kIdealHaikuGreen, 191);
	fHaikuYellowColor = mix_color(fTextColor, kIdealHaikuYellow, 191);
	fBeOSRedColor = mix_color(fTextColor, kIdealBeOSRed, 191);
	fBeOSBlueColor = mix_color(fTextColor, kIdealBeOSBlue, 191);

	SetLayout(new BGroupLayout(B_VERTICAL, B_USE_BIG_SPACING));
	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.SetInsets(B_USE_BIG_SPACING)
		.Add(_CreateLogoView(), 0.0f)
		.Add(_CreateSysInfoView(), 0.0f)
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		.Add(_CreateInfoTabs())
// SPDX-SnippetEnd
		.End();
}


AboutView::~AboutView()
{
	for (PackageCreditMap::iterator it = fPackageCredits.begin();
		it != fPackageCredits.end(); it++) {

		delete it->second;
	}

	delete fScrollRunner;
	delete fCreditsView;
	delete fSysInfoView;
	delete fLogoView;
}


void
AboutView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fSysInfoView->CacheInitialSize();

	float headerHeight = fLogoView->MinSize().height;
	float infoWidth = fSysInfoView->MinWidth();
	fCachedMinWidth = std::max(infoWidth, fLogoView->MinSize().width);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fCachedMinWidth = std::max(fCachedMinWidth, r5_about_content_width());
// SPDX-SnippetEnd
	float tabHeight = 180.0f;
	fCachedMinHeight = headerHeight + fSysInfoView->MinHeight() + tabHeight;
	if (fTabView != NULL)
		fTabView->SetExplicitMinSize(BSize(fCachedMinWidth, tabHeight));

	SetEventMask(B_POINTER_EVENTS);
	DoLayout();
}


void
AboutView::MouseDown(BPoint where)
{
	BRect rect(92, 26, 105, 31);
	if (rect.Contains(where))
		BMessenger(this).SendMessage('eegg');

	if (Bounds().Contains(where)) {
		fLastActionTime = system_time();
		delete fScrollRunner;
		fScrollRunner = NULL;
	}
}


void
AboutView::Pulse()
{
	float tabHeight = 180.0f;
	float newHeight = fLogoView->MinSize().height + fSysInfoView->MinHeight()
		+ tabHeight;
	if (newHeight != fCachedMinHeight) {
		if (fTabView != NULL)
			fTabView->SetExplicitMinSize(BSize(fCachedMinWidth, tabHeight));
		fCachedMinHeight = newHeight;
	}

	if (fScrollRunner == NULL && system_time() > fLastActionTime + 10000000)
		_CreateScrollRunner();
}


void
AboutView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_COLORS_UPDATED:
			if (message->HasColor(ui_color_name(B_DOCUMENT_TEXT_COLOR))
				|| message->HasColor(ui_color_name(B_LINK_TEXT_COLOR))) {
				rgb_color newTextColor = ui_color(B_DOCUMENT_TEXT_COLOR);
				rgb_color newLinkColor = ui_color(B_LINK_TEXT_COLOR);
				rgb_color newOrangeColor = mix_color(newTextColor, kIdealHaikuOrange, 191);
				rgb_color newGreenColor = mix_color(newTextColor, kIdealHaikuGreen, 191);
				rgb_color newYellowColor = mix_color(newTextColor, kIdealHaikuYellow, 191);
				rgb_color newRedColor = mix_color(newTextColor, kIdealBeOSRed, 191);
				rgb_color newBlueColor = mix_color(newTextColor, kIdealBeOSBlue, 191);

				text_run_array* runArray = fCreditsView->RunArray(0, INT32_MAX);
				for (int32 i = 0; i < runArray->count; i++) {
					if (runArray->runs[i].color == fTextColor)
						runArray->runs[i].color = newTextColor;
					else if (runArray->runs[i].color == fLinkColor)
						runArray->runs[i].color = newLinkColor;
					else if (runArray->runs[i].color == kIdealHaikuOrange)
						runArray->runs[i].color = newOrangeColor;
					else if (runArray->runs[i].color == kIdealHaikuGreen)
						runArray->runs[i].color = newGreenColor;
					else if (runArray->runs[i].color == kIdealHaikuYellow)
						runArray->runs[i].color = newYellowColor;
					else if (runArray->runs[i].color == kIdealBeOSRed)
						runArray->runs[i].color = newRedColor;
					else if (runArray->runs[i].color == kIdealBeOSBlue)
						runArray->runs[i].color = newBlueColor;
				}
				fCreditsView->SetRunArray(0, INT32_MAX, runArray);
				fCreditsView->FreeRunArray(runArray);

				fTextColor = newTextColor;
				fLinkColor = newLinkColor;
				fHaikuOrangeColor = newOrangeColor;
				fHaikuGreenColor = newGreenColor;
				fHaikuYellowColor = newYellowColor;
				fBeOSRedColor = newRedColor;
				fBeOSBlueColor = newBlueColor;
			}
			break;

		case kMsgScrollCreditsView:
		{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			if (fTabView != NULL && fTabView->Selection() != 0)
				break;
// SPDX-SnippetEnd
			BScrollBar* scrollBar = fCreditsView->ScrollBar(B_VERTICAL);
			if (scrollBar == NULL)
				break;
			float min;
			float max;
			scrollBar->GetRange(&min, &max);
			if (scrollBar->Value() < max)
				fCreditsView->ScrollBy(0, 1);

			break;
		}

		case 'eegg':
		{
			printf("Easter egg\n");
			PickRandomHaiku();
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
AboutView::AddCopyrightEntry(const char* name, const char* text,
	const char* url)
{
	AddCopyrightEntry(name, text, StringVector(), StringVector(), url);
}


void
AboutView::AddCopyrightEntry(const char* name, const char* text,
	const StringVector& licenses, const StringVector& sources,
	const char* url)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	HyperTextView* views[] = { fCreditsView, fLicensesView };
	int32 viewCount = fLicensesView != NULL ? 2 : 1;
	for (int32 viewIndex = 0; viewIndex < viewCount; viewIndex++) {
		HyperTextView* view = views[viewIndex];
		if (view == NULL)
			continue;
		BFont font(be_bold_font);
		//font.SetSize(be_bold_font->Size());
		font.SetFace(B_BOLD_FACE | B_ITALIC_FACE);
	
		view->SetFontAndColor(&font, B_FONT_ALL, &fHaikuYellowColor);
		view->Insert(name);
		view->Insert("\n");
		view->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
		view->Insert(text);
		view->Insert("\n");
	
		if (licenses.CountStrings() > 0) {
			if (licenses.CountStrings() > 1)
				view->Insert(B_TRANSLATE("Licenses: "));
			else
				view->Insert(B_TRANSLATE("License: "));
	
			for (int32 i = 0; i < licenses.CountStrings(); i++) {
				const char* license = licenses.StringAt(i);
	
				if (i > 0)
					view->Insert(", ");
	
				BString licenseName;
				BString licenseURL;
				parse_named_url(license, licenseName, licenseURL);
	
				BPath licensePath;
				if (_GetLicensePath(licenseURL, licensePath) == B_OK) {
					view->InsertHyperText(B_TRANSLATE_NOCOLLECT(licenseName),
						new OpenFileAction(licensePath.Path()));
				} else
					view->Insert(licenseName);
			}
	
			view->Insert("\n");
// SPDX-SnippetEnd
		}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	
		if (sources.CountStrings() > 0) {
			view->Insert(B_TRANSLATE("Source Code: "));
	
			for (int32 i = 0; i < sources.CountStrings(); i++) {
				const char* source = sources.StringAt(i);
	
				if (i > 0)
					view->Insert(", ");
	
				BString urlName;
				BString urlAddress;
				parse_named_url(source, urlName, urlAddress);
	
				view->SetFontAndColor(be_plain_font, B_FONT_ALL,
					&fLinkColor);
				view->InsertHyperText(urlName,
					new URLAction(urlAddress));
			}
	
			view->Insert("\n");
		}
	
		if (url) {
// SPDX-SnippetEnd
			BString urlName;
			BString urlAddress;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			parse_named_url(url, urlName, urlAddress);
	
			view->SetFontAndColor(be_plain_font, B_FONT_ALL,
// SPDX-SnippetEnd
				&fLinkColor);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			view->InsertHyperText(urlName,
// SPDX-SnippetEnd
				new URLAction(urlAddress));
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			view->Insert("\n");
// SPDX-SnippetEnd
		}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		view->Insert("\n");
		}
// SPDX-SnippetEnd
}


void
AboutView::PickRandomHaiku()
{
	BPath path;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &path) != B_OK)
		path = "/system/data";
	path.Append("fortunes");
	path.Append("Haiku");

	BFile fortunes(path.Path(), B_READ_ONLY);
	struct stat st;
	if (fortunes.InitCheck() < B_OK)
		return;
	if (fortunes.GetStat(&st) < B_OK)
		return;

	char* buff = (char*)malloc((size_t)st.st_size + 1);
	if (buff == NULL)
		return;

	buff[(size_t)st.st_size] = '\0';
	BList haikuList;
	if (fortunes.Read(buff, (size_t)st.st_size) == (ssize_t)st.st_size) {
		char* p = buff;
		while (p && *p) {
			char* e = strchr(p, '%');
			BString* s = new BString(p, e ? (e - p) : -1);
			haikuList.AddItem(s);
			p = e;
			if (p && (*p == '%'))
				p++;
			if (p && (*p == '\n'))
				p++;
		}
	}
	free(buff);

	if (haikuList.CountItems() < 1)
		return;

	BString* s = (BString*)haikuList.ItemAt(rand() % haikuList.CountItems());
	BFont font(be_bold_font);
	font.SetSize(be_bold_font->Size());
	font.SetFace(B_BOLD_FACE | B_ITALIC_FACE);
	fCreditsView->SelectAll();
	fCreditsView->Delete();
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(s->String());
	fCreditsView->Insert("\n");
	while ((s = (BString*)haikuList.RemoveItem((int32)0)))
		delete s;
}


void
AboutView::_CreateScrollRunner()
{
#if 0
	BMessage scroll(kMsgScrollCreditsView);
	fScrollRunner = new(std::nothrow) BMessageRunner(this, &scroll, 25000, -1);
#endif
}


BView*
AboutView::_CreateLogoView()
{
	fLogoView = new(std::nothrow) R5Header();

	return fLogoView;
}


SysInfoView*
AboutView::_CreateSysInfoView()
{
	fSysInfoView = new(std::nothrow) SysInfoView();

	return fSysInfoView;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
BView*
AboutView::_CreateInfoTabs()
{
	fLicensesView = new HyperTextView("licenses");
	fLicensesView->SetFlags(fLicensesView->Flags() | B_FRAME_EVENTS);
	fLicensesView->SetStylable(true);
	fLicensesView->MakeEditable(false);
	fLicensesView->SetWordWrap(true);
	fLicensesView->SetInsets(5, 5, 5, 5);
	fLicensesView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	_FillLicenses();

	CropView* credits = _CreateCreditsView();

	fWhatsNewView = new HyperTextView("whatsnew");
	fWhatsNewView->SetFlags(fWhatsNewView->Flags() | B_FRAME_EVENTS);
	fWhatsNewView->SetStylable(true);
	fWhatsNewView->MakeEditable(false);
	fWhatsNewView->SetWordWrap(true);
	fWhatsNewView->SetInsets(5, 5, 5, 5);
	fWhatsNewView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	_FillWhatsNew();

	BScrollView* newsScroller = new BScrollView("whatsNewScroller",
		fWhatsNewView, B_WILL_DRAW | B_FRAME_EVENTS, false, true,
		B_PLAIN_BORDER);

	fTabView = new BTabView("aboutTabs", B_WIDTH_FROM_LABEL);
	fTabView->AddTab(newsScroller);
	fTabView->TabAt(0)->SetLabel(B_TRANSLATE("What's new"));
	fTabView->AddTab(credits);
	fTabView->TabAt(1)->SetLabel(B_TRANSLATE("Credits"));

	BScrollView* licenseScroller = new BScrollView("licenseScroller",
		fLicensesView, B_WILL_DRAW | B_FRAME_EVENTS, false, true,
		B_PLAIN_BORDER);
	fTabView->AddTab(licenseScroller);
	fTabView->TabAt(2)->SetLabel(B_TRANSLATE("Licenses"));
	return fTabView;
}


void
AboutView::_FillWhatsNew()
{
	BString text;
	BResources* resources = BApplication::AppResources();
	if (resources != NULL) {
		size_t size = 0;
		const void* data = resources->LoadResource(B_STRING_TYPE,
			"r5_whatsnew", &size);
		if (data != NULL && size > 0)
			text.SetTo(static_cast<const char*>(data), (int32)size);
	}
	if (text.Length() > 0 && text[text.Length() - 1] == '\0')
		text.Truncate(text.Length() - 1);
	if (text.IsEmpty())
		text = B_TRANSLATE("No notes in this build.");

	BFont headingFont(be_bold_font);
	headingFont.SetSize(headingFont.Size() + 2);

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		if (line.FindFirst("R5 ") == 0) {
			fWhatsNewView->SetFontAndColor(&headingFont, B_FONT_ALL,
				&fHaikuGreenColor);
		} else {
			fWhatsNewView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fTextColor);
		}
		fWhatsNewView->Insert(line);
		fWhatsNewView->Insert("\n");
		if (end >= text.Length())
			break;
		start = end + 1;
	}
}


void
AboutView::_FillLicenses()
{
	BString text;
	BResources* resources = BApplication::AppResources();
	if (resources != NULL) {
		size_t size = 0;
		const void* data = resources->LoadResource(B_STRING_TYPE,
			"r5_licenses", &size);
		if (data != NULL && size > 0)
			text.SetTo(static_cast<const char*>(data), (int32)size);
	}
	if (text.Length() > 0 && text[text.Length() - 1] == '\0')
		text.Truncate(text.Length() - 1);

	BFont headingFont(be_bold_font);
	headingFont.SetSize(headingFont.Size() + 2);

	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			end = text.Length();
		BString line;
		text.CopyInto(line, start, end - start);
		if (line.StartsWith("# ")) {
			line.Remove(0, 2);
			fLicensesView->SetFontAndColor(&headingFont, B_FONT_ALL,
				&fHaikuGreenColor);
			fLicensesView->Insert(line);
		} else if (line.StartsWith("Source: http://")
			|| line.StartsWith("Source: https://")) {
			BString url(line);
			url.Remove(0, 8);
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fTextColor);
			fLicensesView->Insert("Source: ");
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fLinkColor);
			fLicensesView->InsertHyperText(url.String(),
				new URLAction(url.String()));
		} else if (line.StartsWith("http://") || line.StartsWith("https://")) {
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fLinkColor);
			fLicensesView->InsertHyperText(line, new URLAction(line.String()));
		} else {
			fLicensesView->SetFontAndColor(be_plain_font, B_FONT_ALL,
				&fTextColor);
			fLicensesView->Insert(line);
		}
		fLicensesView->Insert("\n");
		if (end >= text.Length())
			break;
		start = end + 1;
	}
}

// SPDX-SnippetEnd
CropView*
AboutView::_CreateCreditsView()
{
	// Begin construction of the credits view
	fCreditsView = new HyperTextView("credits");
	fCreditsView->SetFlags(fCreditsView->Flags() | B_FRAME_EVENTS);
	fCreditsView->SetStylable(true);
	fCreditsView->MakeEditable(false);
	fCreditsView->SetWordWrap(true);
	fCreditsView->SetInsets(5, 5, 5, 5);
	fCreditsView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	BScrollView* creditsScroller = new BScrollView("creditsScroller",
		fCreditsView, B_WILL_DRAW | B_FRAME_EVENTS, false, true,
		B_PLAIN_BORDER);

	// Haiku copyright
	BFont font(be_bold_font);
	font.SetSize(font.Size() + 4);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuGreenColor);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#ifndef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
	fCreditsView->Insert("R5 Operating System\n");
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(B_TRANSLATE(
		"R5 is based on Haiku, but it is not the official distribution "
		"from the Haiku Project. R5 is not associated with the Haiku "
		"Project.\n\n"));
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuGreenColor);
#endif
// SPDX-SnippetEnd
	fCreditsView->Insert("Haiku\n");

	time_t time = ::time(NULL);
	struct tm* tm = localtime(&time);
	int32 year = tm->tm_year + 1900;
	if (year < 2008)
		year = 2008;
	BString text;
	text.SetToFormat(
		B_TRANSLATE(COPYRIGHT_STRING "2001-%" B_PRId32 " The Haiku project. "),
		year);

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(text.String());

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(B_TRANSLATE("The copyright to the Haiku code is "
		"property of Haiku, Inc. or of the respective authors where expressly "
		"noted in the source. Haiku" B_UTF8_REGISTERED
		" and the HAIKU logo" B_UTF8_REGISTERED
		" are registered trademarks of Haiku, Inc."
		"\n\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fLinkColor);
	fCreditsView->InsertHyperText(B_TRANSLATE("Visit the Haiku website"),
		new URLAction("https://www.haiku-os.org"));
	fCreditsView->Insert("\n");
	fCreditsView->InsertHyperText(B_TRANSLATE("Make a donation"),
		new URLAction("https://www.haiku-inc.org/donate"));
	fCreditsView->Insert("\n\n");

	font.SetSize(be_bold_font->Size());
	font.SetFace(B_BOLD_FACE | B_ITALIC_FACE);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Current maintainers:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(kCurrentMaintainers);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Past maintainers:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(kPastMaintainers);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Website & marketing:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(kWebsiteTeam);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Past website & marketing:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(kPastWebsiteTeam);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Testing and bug triaging:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(kTestingTeam);

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Contributors:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(kContributors);
	fCreditsView->Insert(
		B_TRANSLATE("\n" B_UTF8_ELLIPSIS
			"and probably some more we forgot to mention (sorry!)"
			"\n\n"));

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("Translations:\n"));

	BLanguage* lang;
	BString langName;

	BList sortedTranslations;
	for (uint32 i = 0; i < kNumberOfTranslations; i ++) {
		const Translation* translation = &kTranslations[i];
		sortedTranslations.AddItem((void*)translation);
	}
	sortedTranslations.SortItems(TranslationComparator);

	for (uint32 i = 0; i < kNumberOfTranslations; i ++) {
		const Translation& translation
			= *(const Translation*)sortedTranslations.ItemAt(i);

		langName.Truncate(0);
		if (BLocaleRoster::Default()->GetLanguage(translation.languageCode,
				&lang) == B_OK) {
			lang->GetName(langName);
			delete lang;
		} else {
			// We failed to get the localized readable name,
			// go with what we have.
			langName.Append(translation.languageCode);
		}

		fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuGreenColor);
		fCreditsView->Insert("\n");
		fCreditsView->Insert(langName);
		fCreditsView->Insert("\n");
		fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
		fCreditsView->Insert(translation.names);
	}

	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuOrangeColor);
	fCreditsView->Insert(B_TRANSLATE("\n\nSpecial thanks to:\n"));

	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	BString beosCredits(B_TRANSLATE(
		"Be Inc. and its developer team, for having created BeOS!\n\n"));
	int32 beosOffset = beosCredits.FindFirst("BeOS");
	fCreditsView->Insert(beosCredits.String(),
		(beosOffset < 0) ? beosCredits.Length() : beosOffset);
	if (beosOffset > -1) {
		fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fBeOSBlueColor);
		fCreditsView->Insert("B");
		fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fBeOSRedColor);
		fCreditsView->Insert("e");
		fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
		beosCredits.Remove(0, beosOffset + 2);
		fCreditsView->Insert(beosCredits.String(), beosCredits.Length());
	}
	fCreditsView->Insert(
		B_TRANSLATE("Travis Geiselbrecht (and his NewOS kernel)\n"));
	fCreditsView->Insert(
		B_TRANSLATE("Michael Phipps (project founder)\n\n"));
	fCreditsView->Insert(
		B_TRANSLATE("The HaikuPorts team\n"));
	fCreditsView->Insert(
		B_TRANSLATE("The Haikuware team and their bounty program\n"));
	fCreditsView->Insert(
		B_TRANSLATE("The BeGeistert team\n"));
	fCreditsView->Insert(
		B_TRANSLATE("Google and their Google Summer of Code and Google Code In "
			"programs\n"));
	fCreditsView->Insert(
		B_TRANSLATE("The University of Auckland and Christof Lutteroth\n\n"));
	fCreditsView->Insert(
		B_TRANSLATE(B_UTF8_ELLIPSIS "and the many people making donations!\n\n"));

	// copyrights for various projects we use

	BPath mitPath;
	_GetLicensePath("MIT", mitPath);
	BPath lgplPath;
	_GetLicensePath("GNU LGPL v2.1", lgplPath);

	font.SetSize(be_bold_font->Size() + 4);
	font.SetFace(B_BOLD_FACE);
	fCreditsView->SetFontAndColor(&font, B_FONT_ALL, &fHaikuGreenColor);
	fCreditsView->Insert(B_TRANSLATE("\nCopyrights\n\n"));

	// Haiku license
	BString haikuLicense = B_TRANSLATE_COMMENT("The code that is unique to "
		"Haiku, especially the kernel and all code that applications may link "
		"against, is distributed under the terms of the <MIT license>. "
		"Some system libraries contain third party code distributed under the "
		"<LGPL license>. You can find the copyrights to third party code below."
		"\n\n", "<MIT license> and <LGPL license> aren't variables and can be "
		"translated. However, please, don't remove < and > as they're needed "
		"as placeholders for proper hypertext functionality.");
	int32 licensePart1 = haikuLicense.FindFirst("<");
	int32 licensePart2 = haikuLicense.FindFirst(">");
	int32 licensePart3 = haikuLicense.FindLast("<");
	int32 licensePart4 = haikuLicense.FindLast(">");
	BString part;
	haikuLicense.CopyInto(part, 0, licensePart1);
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(part);

	part.Truncate(0);
	haikuLicense.CopyInto(part, licensePart1 + 1, licensePart2 - 1
		- licensePart1);
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fLinkColor);
	fCreditsView->InsertHyperText(part, new OpenFileAction(mitPath.Path()));

	part.Truncate(0);
	haikuLicense.CopyInto(part, licensePart2 + 1, licensePart3 - 1
		- licensePart2);
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(part);

	part.Truncate(0);
	haikuLicense.CopyInto(part, licensePart3 + 1, licensePart4 - 1
		- licensePart3);
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fLinkColor);
	fCreditsView->InsertHyperText(part, new OpenFileAction(lgplPath.Path()));

	part.Truncate(0);
	haikuLicense.CopyInto(part, licensePart4 + 1, haikuLicense.Length() - 1
		- licensePart4);
	fCreditsView->SetFontAndColor(be_plain_font, B_FONT_ALL, &fTextColor);
	fCreditsView->Insert(part);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// VirtualBox guest additions are GPL-3.0-only. kGPLv3 opens the
	// license text already shipped under data/licenses.
	AddCopyrightEntry("VirtualBox Guest Additions",
		B_TRANSLATE("Guest software included with this build.\n"),
		StringVector(kGPLv3, NULL),
		StringVector(),
		"https://www.virtualbox.org/");

// SPDX-SnippetEnd
	// GNU copyrights
	AddCopyrightEntry("The GNU Project",
		B_TRANSLATE("Contains software from the GNU Project, "
		"released under the GPL and LGPL licenses:\n"
		"GNU C Library, "
		"GNU coretools, diffutils, findutils, "
		"sharutils, gawk, bison, m4, make, "
		"wget, ncurses, termcap, "
		"Bourne Again Shell.\n"
		COPYRIGHT_STRING "The Free Software Foundation."),
		StringVector(kLGPLv21, kGPLv2, kGPLv3, NULL),
		StringVector(),
		"https://www.gnu.org");

	// FreeBSD copyrights
	AddCopyrightEntry("The FreeBSD Project",
		B_TRANSLATE("Contains software from the FreeBSD Project, "
		"released under the BSD license:\n"
		"ftpd, ping, telnet, telnetd, traceroute, and various network drivers\n"
		COPYRIGHT_STRING "1994-2024 The FreeBSD Project. "
		"All rights reserved."),
		StringVector(kBSDTwoClause, kBSDThreeClause, kBSDFourClause,
			NULL),
		StringVector(),
		"https://www.freebsd.org");

	// NetBSD copyrights
	AddCopyrightEntry("The NetBSD Project",
		B_TRANSLATE("Contains software developed by the NetBSD "
		"Foundation, Inc. and its contributors:\n"
		"netresolv\n"
		COPYRIGHT_STRING "1998-2023 The NetBSD Project, "
		COPYRIGHT_STRING "2004-2009 by Internet Systems Consortium, Inc. (\"ISC\"), "
		COPYRIGHT_STRING "1996-2003 by Internet Software Consortium. "
		"All rights reserved."),
		StringVector("ISC", kBSDTwoClause, kBSDThreeClause, NULL),
		StringVector(),
		"https://www.netbsd.org");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT

#ifndef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
	AddCopyrightEntry("R5 AirPlay",
		B_TRANSLATE("AirPlay 1 RAOP audio receiver. Protocol flow and AirPort Express RSA key from Shairport (James Laird) and Shairport Sync (Mike Brady). ALAC decoder by David Hammerton. AES-128-CBC from tiny-AES-c (kokke). RSA via OpenSSL 3. AirPlay 2 is advertised (_airplay._tcp, /info); HomeKit pairing and PTP are not included.\n"
		COPYRIGHT_STRING "2011-2013 James Laird, "
		COPYRIGHT_STRING "2014-2025 Mike Brady, "
		COPYRIGHT_STRING "2005 David Hammerton, "
		COPYRIGHT_STRING "tiny-AES-c authors. All rights reserved."),
		StringVector("MIT", "Unlicense", NULL),
		StringVector(),
		"https://github.com/mikebrady/shairport-sync");
#endif
// SPDX-SnippetEnd

	// FFmpeg copyrights
	_AddPackageCredit(PackageCredit("FFmpeg")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2000-2019 Fabrice "
			"Bellard, et al."))
		.SetLicenses(kLGPLv21, kLGPLv2, NULL)
		.SetURL("https://www.ffmpeg.org"));

	// AGG copyrights
	_AddPackageCredit(PackageCredit("AntiGrain Geometry")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2002-2006 Maxim "
			"Shemanarev (McSeem)."))
		.SetLicenses("Anti-Grain Geometry", kBSDThreeClause, NULL)
		.SetURL("https://agg.sourceforge.net/antigrain.com"));

	// FreeType copyrights
	_AddPackageCredit(PackageCredit("FreeType2")
		.SetCopyrights(B_TRANSLATE(COPYRIGHT_STRING "1996-2002, 2006 "
			"David Turner, Robert Wilhelm and Werner Lemberg."),
			COPYRIGHT_STRING "2014 The FreeType Project. "
			"All rights reserved.",
			NULL)
		.SetLicense("FreeType")
		.SetURL("https://www.freetype.org"));

	// Mesa3D (http://www.mesa3d.org) copyrights
	_AddPackageCredit(PackageCredit("Mesa")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1999-2006 Brian Paul. "
			"Mesa3D Project. All rights reserved."))
		.SetLicense("MIT")
		.SetURL("https://www.mesa3d.org"));

	// GLUT implementation copyrights
	_AddPackageCredit(PackageCredit("GLUT")
		.SetCopyrights(B_TRANSLATE(COPYRIGHT_STRING "1994-1997 Mark Kilgard. "
			"All rights reserved."),
			COPYRIGHT_STRING "1997 Be Inc.",
			COPYRIGHT_STRING "1999 Jake Hamby.",
			NULL)
		.SetLicense("MIT")
		.SetURL("https://www.opengl.org/resources/libraries/glut"));

	// OpenGroup & DEC (BRegion backend) copyright
	_AddPackageCredit(PackageCredit("BRegion backend (XFree86)")
		.SetCopyrights(COPYRIGHT_STRING "1987-1988, 1998 The Open Group.",
			B_TRANSLATE(COPYRIGHT_STRING "1987-1988 Digital Equipment "
			"Corporation, Maynard, Massachusetts.\n"
			"All rights reserved."),
			NULL)
		.SetLicenses("OpenGroup", "DEC", NULL)
		.SetURL("https://xfree86.org"));

	// Bitstream Charter font
	_AddPackageCredit(PackageCredit("Bitstream Charter font")
		.SetCopyrights(COPYRIGHT_STRING "1989-1992 Bitstream Inc.,"
			"Cambridge, MA.",
			B_TRANSLATE("BITSTREAM CHARTER is a registered trademark of "
				"Bitstream Inc."),
			NULL)
		.SetLicense("Bitstream Charter"));

	// Noto fonts copyright
	_AddPackageCredit(PackageCredit("Noto fonts")
		.SetCopyrights(B_TRANSLATE(COPYRIGHT_STRING
			"2012-2016 Google Internationalization team."),
			NULL)
		.SetLicense("SIL Open Font Licence v1.1")
		.SetURL("https://fonts.google.com/noto"));

	// expat copyrights
	_AddPackageCredit(PackageCredit("expat")
		.SetCopyrights(B_TRANSLATE(COPYRIGHT_STRING "1998-2000 Thai "
			"Open Source Software Center Ltd and Clark Cooper."),
			B_TRANSLATE(COPYRIGHT_STRING "2001-2003 Expat maintainers."),
			NULL)
		.SetLicense("Expat")
		.SetURL("https://libexpat.github.io"));

	// zlib copyrights
	_AddPackageCredit(PackageCredit("zlib")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1995-2004 Jean-loup "
			"Gailly and Mark Adler."))
		.SetLicense("Zlib")
		.SetURL("https://www.zlib.net"));

	// zip copyrights
	_AddPackageCredit(PackageCredit("Info-ZIP")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1990-2002 Info-ZIP. "
			"All rights reserved."))
		.SetLicense("Info-ZIP")
		.SetURL("https://infozip.sourceforge.net"));

	// bzip2 copyrights
	_AddPackageCredit(PackageCredit("bzip2")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1996-2005 Julian R "
			"Seward. All rights reserved."))
		.SetLicense(kBSDFourClause)
		.SetURL("http://bzip.org"));

	// acpica copyrights
	_AddPackageCredit(PackageCredit("ACPI Component Architecture (ACPICA)")
		.SetCopyright(COPYRIGHT_STRING "1999-2026 Intel Corp.")
		.SetLicense("Intel (ACPICA)")
		.SetURL("https://www.intel.com/content/www/us/en/developer/topic-technology/open/acpica/overview.html"));

	// libpng copyrights
	_AddPackageCredit(PackageCredit("libpng")
		.SetCopyright(COPYRIGHT_STRING "1995-2017 libpng authors")
		.SetLicense("LibPNG")
		.SetURL("http://www.libpng.org"));

	// libjpeg copyrights
	_AddPackageCredit(PackageCredit("libjpeg")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1994-2009, Thomas G. "
			"Lane, Guido Vollbeding. This software is based in part on the "
			"work of the Independent JPEG Group."))
		.SetLicense("LibJPEG")
		.SetURL("https://www.ijg.org"));

	// libprint copyrights
	_AddPackageCredit(PackageCredit("libprint")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1999-2000 Y.Takagi. "
			"All rights reserved.")));
			// TODO: License!

	// cortex copyrights
	_AddPackageCredit(PackageCredit("Cortex")
		.SetCopyright(COPYRIGHT_STRING "1999-2000 Eric Moon.")
		.SetLicense(kBSDThreeClause)
		.SetURL("https://cortex.sourceforge.net/documentation"));

	// FluidSynth copyrights
	_AddPackageCredit(PackageCredit("FluidSynth")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2003 Peter Hanappe "
			"and others."))
		.SetLicense(kLGPLv2)
		.SetURL("https://www.fluidsynth.org"));

	// Xiph.org Foundation copyrights
	_AddPackageCredit(PackageCredit("Xiph.org Foundation")
		.SetCopyrights("libvorbis, libogg, libtheora, libspeex",
			B_TRANSLATE(COPYRIGHT_STRING "1994-2008 Xiph.Org. "
			"All rights reserved."), NULL)
		.SetLicense(kBSDThreeClause)
		.SetURL("https://www.xiph.org"));

	// Matroska
	_AddPackageCredit(PackageCredit("libmatroska")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2002-2003 Steve Lhomme. "
			"All rights reserved."))
		.SetLicense(kLGPLv21)
		.SetURL("https://www.matroska.org"));

	// BColorQuantizer (originally CQuantizer code)
	_AddPackageCredit(PackageCredit("CQuantizer")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1996-1997 Jeff Prosise. "
			"All rights reserved."))
		.SetLicense("CQuantizer")
		.SetURL("https://jacobfilipp.com/MSJ/wicked1097.html"));

	// MAPM (Mike's Arbitrary Precision Math Library) used by DeskCalc
	_AddPackageCredit(PackageCredit("MAPM")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1999-2007 Michael C. "
			"Ring. All rights reserved."))
		.SetLicense("MAPM")
		.SetURL("https://github.com/LuaDist/mapm"));

	// MkDepend 1.7 copyright (Makefile dependency generator)
	_AddPackageCredit(PackageCredit("MkDepend")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1995-2001 Lars Düning. "
			"All rights reserved."))
		.SetLicense("MIT")
		.SetURL("https://ftp.sunet.se/mirror/media/Oakvalley/soamc/001/Raw_Datafiles/04_LHA-STD/08/Part00442/6be4_8-04-08.dat"));

	// libhttpd copyright (used as Poorman backend)
	_AddPackageCredit(PackageCredit("libhttpd")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "1995, 1998-2001 "
			"Jef Poskanzer. All rights reserved."))
		.SetLicense(kBSDTwoClause)
		.SetURL("https://www.acme.com/software/thttpd"));

	// Zydis copyrights
	_AddPackageCredit(PackageCredit("Zydis")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2014-2024 Florian Bernd "
			"and Joel Höner. All rights reserved."))
		.SetLicense("MIT")
		.SetURL("https://zydis.re/"));

#ifdef __i386__
	// Intel PRO/Wireless 2100 & 2200BG firmwares
	_AddPackageCredit(PackageCredit("Intel PRO/Wireless 2100 & 2200BG firmwares")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2003-2006 "
			"Intel Corporation. All rights reserved."))
		.SetLicense(kIntel2xxxFirmware)
		.SetURL("http://www.intellinuxwireless.org"));

	// Intel wireless firmwares
	_AddPackageCredit(
		PackageCredit("Intel PRO/Wireless network adapter firmwares")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2006-2015 "
			"Intel Corporation. All rights reserved."))
		.SetLicense(kIntelFirmware)
		.SetURL("http://www.intellinuxwireless.org"));

	// Marvell 88w8363
	_AddPackageCredit(PackageCredit("Marvell 88w8363")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2007-2009 "
			"Marvell Semiconductor, Inc. All rights reserved."))
		.SetLicense(kMarvellFirmware)
		.SetURL("https://www.marvell.com"));

	// Ralink Firmware RT2501/RT2561/RT2661
	_AddPackageCredit(PackageCredit("Ralink Firmware RT2501/RT2561/RT2661")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2007 "
			"Ralink Technology Corporation. All rights reserved."))
		.SetLicense(kRalinkFirmware));
#endif

	// Gutenprint
	_AddPackageCredit(PackageCredit("Gutenprint")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING
			"1999-2010 by the authors of Gutenprint. All rights reserved."))
		.SetLicense(kGPLv2)
		.SetURL("https://gimp-print.sourceforge.io"));

	// libwebp
	_AddPackageCredit(PackageCredit("libwebp")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING
			"2010-2011 Google Inc. All rights reserved."))
		.SetLicense(kBSDThreeClause)
		.SetURL("https://www.webmproject.org/code/#webp-repositories"));

	// libavif
	_AddPackageCredit(PackageCredit("libavif")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING
			"2019 Joe Drago. All rights reserved."))
		.SetLicense(kBSDThreeClause)
		.SetURL("https://github.com/AOMediaCodec/libavif"));

	// GTF
	_AddPackageCredit(PackageCredit("GTF")
		.SetCopyright(B_TRANSLATE("2001 by Andy Ritger based on the "
			"Generalized Timing Formula"))
		.SetLicense(kBSDThreeClause)
		.SetURL("https://gtf.sourceforge.net"));

	// libqrencode
	_AddPackageCredit(PackageCredit("libqrencode")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2006-2012 Kentaro Fukuchi"))
		.SetLicense(kLGPLv21)
		.SetURL("https://fukuchi.org/works/qrencode"));

	// scrypt
	_AddPackageCredit(PackageCredit("scrypt")
		.SetCopyright(B_TRANSLATE(COPYRIGHT_STRING "2009 Colin Percival"))
		.SetLicense(kBSDTwoClause)
		.SetURL("https://www.tarsnap.com/scrypt.html"));

	_AddCopyrightsFromAttribute();
	_AddPackageCreditEntries();

	return new CropView(creditsScroller, 0, 1, 1, 1);
}


status_t
AboutView::_GetLicensePath(const char* license, BPath& path)
{
	BPathFinder pathFinder;
	BStringList paths;
	struct stat st;

	status_t error = pathFinder.FindPaths(B_FIND_PATH_DATA_DIRECTORY,
		"licenses", paths);

	for (int i = 0; i < paths.CountStrings(); ++i) {
		if (error == B_OK && path.SetTo(paths.StringAt(i)) == B_OK
			&& path.Append(license) == B_OK
			&& lstat(path.Path(), &st) == 0) {
			return B_OK;
		}
	}

	path.Unset();
	return B_ENTRY_NOT_FOUND;
}


void
AboutView::_AddCopyrightsFromAttribute()
{
#ifdef __HAIKU__
	// open the app executable file
	char appPath[B_PATH_NAME_LENGTH];
	int appFD;
	if (BPrivate::get_app_path(appPath) != B_OK
		|| (appFD = open(appPath, O_RDONLY)) < 0) {
		return;
	}

	// open the attribute
	int attrFD = fs_fopen_attr(appFD, "COPYRIGHTS", B_STRING_TYPE, O_RDONLY);
	close(appFD);
	if (attrFD < 0)
		return;

	// attach it to a FILE
	FileCloser attrFile(fdopen(attrFD, "r"));
	if (!attrFile.IsSet()) {
		close(attrFD);
		return;
	}

	// read and parse the copyrights
	BMessage package;
	BString fieldName;
	BString fieldValue;
	char lineBuffer[LINE_MAX];
	while (char* line
		= fgets(lineBuffer, sizeof(lineBuffer), attrFile.Get())) {
		// chop off line break
		size_t lineLen = strlen(line);
		if (lineLen > 0 && line[lineLen - 1] == '\n')
			line[--lineLen] = '\0';

		// flush previous field, if a new field begins, otherwise append
		if (lineLen == 0 || !isspace(line[0])) {
			// new field -- flush the previous one
			if (fieldName.Length() > 0) {
				fieldValue = trim_string(fieldValue.String(),
					fieldValue.Length());
				package.AddString(fieldName.String(), fieldValue);
				fieldName = "";
			}
		} else if (fieldName.Length() > 0) {
			// append to current field
			fieldValue += line;
			continue;
		} else {
			// bogus line -- ignore
			continue;
		}

		if (lineLen == 0)
			continue;

		// parse new field
		char* colon = strchr(line, ':');
		if (colon == NULL) {
			// bogus line -- ignore
			continue;
		}

		fieldName.SetTo(line, colon - line);
		fieldName = trim_string(line, colon - line);
		if (fieldName.Length() == 0) {
			// invalid field name
			continue;
		}

		fieldValue = colon + 1;

		if (fieldName == "Package") {
			// flush the current package
			_AddPackageCredit(PackageCredit(package));
			package.MakeEmpty();
		}
	}

	// flush current package
	_AddPackageCredit(PackageCredit(package));
#endif
}


void
AboutView::_AddPackageCreditEntries()
{
	// sort the packages case-insensitively
	PackageCredit* packages[fPackageCredits.size()];
	int32 count = 0;
	for (PackageCreditMap::iterator it = fPackageCredits.begin();
			it != fPackageCredits.end(); ++it) {
		packages[count++] = it->second;
	}

	if (count > 1) {
		std::sort(packages, packages + count,
			&PackageCredit::NameLessInsensitive);
	}

	// add the credits
	BString text;
	for (int32 i = 0; i < count; i++) {
		PackageCredit* package = packages[i];

		text.SetTo(package->CopyrightAt(0));
		int32 count = package->CountCopyrights();
		for (int32 i = 1; i < count; i++)
			text << "\n" << package->CopyrightAt(i);

		AddCopyrightEntry(package->PackageName(), text.String(),
			package->Licenses(), package->Sources(), package->URL());
	}
}


void
AboutView::_AddPackageCredit(const PackageCredit& package)
{
	if (!package.IsValid())
		return;

	PackageCreditMap::iterator it = fPackageCredits.find(package.PackageName());
	if (it != fPackageCredits.end()) {
		// If the new package credit isn't "better" than the old one, ignore it.
		PackageCredit* oldPackage = it->second;
		if (!package.IsBetterThan(*oldPackage))
			return;

		// replace the old credit
		fPackageCredits.erase(it);
		delete oldPackage;
	}

	fPackageCredits[package.PackageName()] = new PackageCredit(package);
}


//	#pragma mark - static functions


static int
ignored_pages(system_info* sysInfo)
{
	return (int)round(sysInfo->ignored_pages * B_PAGE_SIZE / 1048576.0);
}


static int
max_pages(system_info* sysInfo)
{
	return (int)round(sysInfo->max_pages * B_PAGE_SIZE / 1048576.0);
}


static int
max_and_ignored_pages(system_info* sysInfo)
{
	return max_pages(sysInfo) + ignored_pages(sysInfo);
}


static int
used_pages(system_info* sysInfo)
{
	return (int)round(sysInfo->used_pages * B_PAGE_SIZE / 1048576.0);
}


//	#pragma mark - main


int
main()
{
	AboutApp app;
	app.Run();

	return 0;
}
