/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Appearance page. Fonts, colors, the window look, and antialiasing.
 * The separate Appearance program stays installed.
 */


#include "SettingsModule.h"

#include <Alert.h>
#include <AppFileInfo.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <ControlLook.h>
#include <DecorInfo.h>
#include <DefaultColors.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <GroupView.h>
#include <HSL.h>
#include <InterfacePrivate.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PathFinder.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <Slider.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>
#include <TabView.h>
#include <TextView.h>
#include <View.h>
#include <app/ServerReadOnlyMemory.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>


static const uint32 kMsgFont = 'afnt';
static const uint32 kMsgSize = 'afsz';
static const uint32 kMsgFontsChanged = 'afch';
static const uint32 kMsgColor = 'acol';
static const uint32 kMsgPick = 'apck';
static const uint32 kMsgAuto = 'aaut';
static const uint32 kMsgDecor = 'adec';
static const uint32 kMsgDecorAbout = 'adab';
static const uint32 kMsgLook = 'alok';
static const uint32 kMsgLookAbout = 'alab';
static const uint32 kMsgArrows = 'aarr';
static const uint32 kMsgAntialias = 'aant';
static const uint32 kMsgHint = 'ahnt';
static const uint32 kMsgWeight = 'awgt';

static const char* kWhichPlain = "plain";
static const char* kWhichBold = "bold";
static const char* kWhichFixed = "fixed";
static const char* kWhichMenu = "menu";

static const float kMinFontSize = 8.0f;
static const float kMaxFontSize = 72.0f;
static const char* kPreviewText
	= "The quick brown fox jumps over the lazy dog.";

enum {
	kHintOff = 0,
	kHintOn,
	kHintMonospace
};

static const uint8 kDefaultHint = kHintOn;
static const uint8 kDefaultWeight = 120;
static const bool kDefaultSubpixel = true;


extern void _set_system_font_(const char* which, font_family family,
	font_style style, float size);
extern status_t _get_system_default_font_(const char* which,
	font_family family, font_style style, float* _size);
extern void set_subpixel_antialiasing(bool subpix);
extern status_t get_subpixel_antialiasing(bool* subpix);
extern void set_hinting_mode(uint8 hinting);
extern status_t get_hinting_mode(uint8* hinting);
extern void set_average_weight(unsigned char averageWeight);
extern status_t get_average_weight(unsigned char* averageWeight);


static bool
same_color(rgb_color left, rgb_color right)
{
	return left.red == right.red && left.green == right.green
		&& left.blue == right.blue && left.alpha == right.alpha;
}


static void
put_color(BMessage& colors, color_which which, rgb_color color)
{
	const char* name = ui_color_name(which);
	if (name == NULL)
		return;
	if (colors.ReplaceColor(name, color) != B_OK)
		colors.AddColor(name, color);
}


static void
read_current_colors(BMessage& colors)
{
	colors.MakeEmpty();
	for (int32 index = 0; index < kColorWhichCount; index++) {
		color_which which = index_to_color_which(index);
		put_color(colors, which, ui_color(which));
	}
}


static void
read_default_colors(BMessage& colors)
{
	colors.MakeEmpty();
	for (int32 index = 0; index < kColorWhichCount; index++) {
		color_which which = index_to_color_which(index);
		put_color(colors, which, BPrivate::kDefaultColors[index]);
	}
}


static bool
colors_differ(const BMessage& left, const BMessage& right)
{
	for (int32 index = 0; index < kColorWhichCount; index++) {
		color_which which = index_to_color_which(index);
		const char* name = ui_color_name(which);
		rgb_color a = left.GetColor(name, make_color(0, 0, 0));
		rgb_color b = right.GetColor(name, make_color(0, 0, 0));
		if (!same_color(a, b))
			return true;
	}
	return false;
}


static void
apply_colors(const BMessage& colors)
{
	set_ui_colors(&colors);
}


struct FontSnap {
	font_family	family;
	font_style	style;
	float		size;

	bool		operator==(const FontSnap& other) const
	{
		return size == other.size
			&& strcmp(family, other.family) == 0
			&& strcmp(style, other.style) == 0;
	}
};


static FontSnap
current_font(const char* which)
{
	FontSnap snap;
	memset(&snap, 0, sizeof(snap));
	if (strcmp(which, kWhichMenu) == 0) {
		menu_info info;
		get_menu_info(&info);
		strlcpy(snap.family, info.f_family, sizeof(snap.family));
		strlcpy(snap.style, info.f_style, sizeof(snap.style));
		snap.size = info.font_size;
		return snap;
	}
	BFont font;
	if (strcmp(which, kWhichBold) == 0)
		font = *be_bold_font;
	else if (strcmp(which, kWhichFixed) == 0)
		font = *be_fixed_font;
	else
		font = *be_plain_font;
	font.GetFamilyAndStyle(&snap.family, &snap.style);
	snap.size = font.Size();
	return snap;
}


static FontSnap
default_font(const char* which)
{
	const char* ask = strcmp(which, kWhichMenu) == 0 ? kWhichPlain : which;
	FontSnap snap = current_font(which);
	font_family family;
	font_style style;
	float size = snap.size;
	if (_get_system_default_font_(ask, family, style, &size) == B_OK) {
		strlcpy(snap.family, family, sizeof(snap.family));
		strlcpy(snap.style, style, sizeof(snap.style));
		snap.size = size;
	}
	return snap;
}


static void
apply_font(const char* which, const FontSnap& snap)
{
	if (strcmp(which, kWhichMenu) == 0) {
		menu_info info;
		get_menu_info(&info);
		strlcpy(info.f_family, snap.family, sizeof(info.f_family));
		strlcpy(info.f_style, snap.style, sizeof(info.f_style));
		info.font_size = snap.size;
		set_menu_info(&info);
		return;
	}
	font_family family;
	font_style style;
	strlcpy(family, snap.family, sizeof(family));
	strlcpy(style, snap.style, sizeof(style));
	_set_system_font_(which, family, style, snap.size);
}


class AppearanceModule;
class AppearancePage;


class FontRow : public BGroupView {
public:
								FontRow(const char* which, const char* label);
			void				Reload();
			void				RefreshFamilies();
			void				SetTargets(BHandler* target);
			void				Select(const char* family, const char* style,
									float size);
			FontSnap			Value() const;
			float				SizeValue() const;

private:
			void				_Fill();
			void				_Retarget();
			void				_ShowPreview();

			BString				fWhich;
			BHandler*			fTarget;
			BPopUpMenu*			fMenu;
			BMenuField*			fField;
			BSpinner*			fSize;
			BTextView*			fPreview;
			BBox*				fPreviewBox;
};


class FontsTab : public BGroupView {
public:
								FontsTab();
	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				Reload();
			void				Use(const FontSnap plain, const FontSnap bold,
									const FontSnap fixed, const FontSnap menu);
			bool				Differs(const FontSnap plain,
									const FontSnap bold, const FontSnap fixed,
									const FontSnap menu) const;

private:
			FontRow*			_Row(const char* which);

			FontRow*			fPlain;
			FontRow*			fBold;
			FontRow*			fFixed;
			FontRow*			fMenu;
			BMessageRunner*		fRunner;
};


class ColorEntry : public BStringItem {
public:
								ColorEntry(const char* label,
									color_which which);
	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete);
			color_which			Which() const { return fWhich; }
			void				SetSwatch(rgb_color color) { fColor = color; }

private:
			color_which			fWhich;
			rgb_color			fColor;
};


class ColorWell : public BView {
public:
								ColorWell();
	virtual	void				Draw(BRect update);
			void				SetSwatch(rgb_color color);

private:
			rgb_color			fColor;
};


class ColorsTab : public BGroupView {
public:
								ColorsTab();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				Reload();

private:
			void				_Fill();
			void				_Select(int32 index);
			void				_Apply(color_which which, rgb_color color);
			void				_Derive(color_which which, rgb_color color,
									BMessage& colors);

			BCheckBox*			fAuto;
			BListView*			fList;
			ColorWell*			fWell;
			BColorControl*		fPicker;
			color_which			fWhich;
			bool				fUpdating;
};


class ArrowSample : public BControl {
public:
								ArrowSample(const char* name, bool doubled,
									BMessage* message);
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	BSize				MinSize();

private:
			bool				fDoubled;
};


class LookTab : public BGroupView {
public:
								LookTab();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				Reload();
			void				Remember(BString& decor, BString& look,
									bool& arrows) const;

private:
			void				_BuildDecor();
			void				_BuildLooks();
			void				_MarkArrows(bool doubled);
			void				_AboutDecor();
			void				_AboutLook();

			BPrivate::DecorInfoUtility fDecors;
			BPopUpMenu*			fDecorMenu;
			BPopUpMenu*			fLookMenu;
			ArrowSample*		fSingle;
			ArrowSample*		fDouble;
			BString				fLookPath;
};


class AntialiasTab : public BGroupView {
public:
								AntialiasTab();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				Reload();
			void				Remember(bool& subpixel, uint8& hint,
									uint8& weight) const;

private:
			BPopUpMenu*			fTypeMenu;
			BMenuItem*			fGray;
			BMenuItem*			fLcd;
			BPopUpMenu*			fHintMenu;
			BSlider*			fWeight;
			bool				fSubpixel;
			uint8				fHint;
			uint8				fWeightValue;
			bool				fUpdating;
};


class AppearancePage : public BGroupView {
public:
								AppearancePage(AppearanceModule* module);
	virtual						~AppearancePage();
			void				Revert();
			void				Defaults();
			bool				Modified() const;
			bool				CanDefaults() const;
			void				Touch();

private:
			AppearanceModule*	fModule;
			FontsTab*			fFonts;
			ColorsTab*			fColors;
			LookTab*			fLook;
			AntialiasTab*		fAntialias;
			FontSnap			fOpenPlain;
			FontSnap			fOpenBold;
			FontSnap			fOpenFixed;
			FontSnap			fOpenMenu;
			BMessage			fOpenColors;
			BString				fOpenDecor;
			BString				fOpenLook;
			bool				fOpenArrows;
			bool				fOpenSubpixel;
			uint8				fOpenHint;
			uint8				fOpenWeight;
};


class AppearanceModule : public SettingsModule {
public:
								AppearanceModule();
	virtual	const char*			Identifier() const { return "appearance"; }
	virtual	const char*			Name() const { return "Appearance"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
									{ return "Personalize how the system looks"; }
	virtual	const char*			Keywords() const
		{ return "appearance colors fonts antialiasing decorator"; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);
			void				ClearPage(AppearancePage* page);
			void				Touch();

private:
			AppearancePage*		fPage;
			SettingsContext*	fContext;
};


FontRow::FontRow(const char* which, const char* label)
	:
	BGroupView(B_VERTICAL, 0),
	fWhich(which),
	fTarget(NULL),
	fMenu(NULL),
	fField(NULL),
	fSize(NULL),
	fPreview(NULL),
	fPreviewBox(NULL)
{
	SetName(which);
	fMenu = new BPopUpMenu("font");
	fField = new BMenuField("family", label, fMenu);
	fField->SetAlignment(B_ALIGN_RIGHT);
	BMessage* sizeMessage = new BMessage(kMsgSize);
	sizeMessage->AddString("which", which);
	fSize = new BSpinner("size", "Size:", sizeMessage);
	fSize->SetRange(kMinFontSize, kMaxFontSize);
	fSize->SetExplicitMaxSize(BSize(72, B_SIZE_UNSET));
	fPreview = new BTextView("preview");
	fPreview->SetText(kPreviewText);
	fPreview->MakeEditable(false);
	fPreview->MakeSelectable(false);
	fPreview->SetWordWrap(true);
	fPreviewBox = new BBox("preview");
	fPreviewBox->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL, 0)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fPreview)
		.View());
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fField)
			.AddGlue()
			.Add(fSize)
			.End()
		.Add(fPreviewBox);
	_Fill();
	Reload();
}


void
FontRow::_Fill()
{
	while (fMenu->CountItems() > 0)
		delete fMenu->RemoveItem((int32)0);
	int32 families = count_font_families();
	for (int32 i = 0; i < families; i++) {
		font_family family;
		if (get_font_family(i, &family, NULL) != B_OK)
			continue;
		BMenu* styles = new BMenu(family);
		int32 count = count_font_styles(family);
		for (int32 s = 0; s < count; s++) {
			font_style style;
			if (get_font_style(family, s, &style, (uint32*)NULL) != B_OK)
				continue;
			BMessage* message = new BMessage(kMsgFont);
			message->AddString("which", fWhich);
			message->AddString("family", family);
			message->AddString("style", style);
			styles->AddItem(new BMenuItem(style, message));
		}
		fMenu->AddItem(new BMenuItem(styles));
	}
	_Retarget();
}


void
FontRow::RefreshFamilies()
{
	_Fill();
	Reload();
}


void
FontRow::SetTargets(BHandler* target)
{
	fTarget = target;
	_Retarget();
}


void
FontRow::_Retarget()
{
	if (fTarget == NULL)
		return;
	fMenu->SetTargetForItems(fTarget);
	for (int32 i = 0; i < fMenu->CountItems(); i++) {
		BMenu* styles = fMenu->ItemAt(i)->Submenu();
		if (styles != NULL)
			styles->SetTargetForItems(fTarget);
	}
	fSize->SetTarget(fTarget);
}


void
FontRow::Reload()
{
	FontSnap snap = current_font(fWhich.String());
	Select(snap.family, snap.style, snap.size);
}


void
FontRow::Select(const char* family, const char* style, float size)
{
	for (int32 i = 0; i < fMenu->CountItems(); i++) {
		BMenuItem* familyItem = fMenu->ItemAt(i);
		BMenu* styles = familyItem->Submenu();
		bool thisFamily = styles != NULL && family != NULL
			&& strcmp(styles->Name(), family) == 0;
		familyItem->SetMarked(thisFamily);
		if (styles == NULL)
			continue;
		for (int32 s = 0; s < styles->CountItems(); s++) {
			BMenuItem* item = styles->ItemAt(s);
			item->SetMarked(thisFamily && style != NULL
				&& strcmp(item->Label(), style) == 0);
		}
	}
	if (fMenu->Superitem() != NULL && family != NULL && family[0] != '\0')
		fMenu->Superitem()->SetLabel(family);
	fSize->SetValue((int32)(size + 0.5f));
	_ShowPreview();
}


FontSnap
FontRow::Value() const
{
	return current_font(fWhich.String());
}


float
FontRow::SizeValue() const
{
	return (float)fSize->Value();
}


void
FontRow::_ShowPreview()
{
	FontSnap snap = current_font(fWhich.String());
	BFont font;
	font.SetFamilyAndStyle(snap.family, snap.style);
	font.SetSize(snap.size);
	fPreview->SetFontAndColor(&font);
	float lines = 2.0f;
	float width = fPreview->Bounds().Width();
	if (width > 20.0f) {
		float textWidth = font.StringWidth(kPreviewText);
		lines = textWidth / width;
		if (lines < 1.0f)
			lines = 1.0f;
		if (lines > 3.0f)
			lines = 3.0f;
	}
	float height = font.Size() * 1.4f * lines + 8.0f;
	fPreview->SetExplicitMinSize(BSize(180, height));
	fPreview->Invalidate();
}


FontsTab::FontsTab()
	:
	BGroupView("Fonts", B_VERTICAL, 0),
	fPlain(NULL),
	fBold(NULL),
	fFixed(NULL),
	fMenu(NULL),
	fRunner(NULL)
{
	fPlain = new FontRow(kWhichPlain, "Plain font:");
	fBold = new FontRow(kWhichBold, "Bold font:");
	fFixed = new FontRow(kWhichFixed, "Fixed font:");
	fMenu = new FontRow(kWhichMenu, "Menu font:");
	BLayoutBuilder::Group<>(this)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fPlain)
		.Add(fBold)
		.Add(fFixed)
		.Add(fMenu)
		.AddGlue();
}


void
FontsTab::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fPlain->SetTargets(this);
	fBold->SetTargets(this);
	fFixed->SetTargets(this);
	fMenu->SetTargets(this);
	fRunner = new BMessageRunner(this, new BMessage(kMsgFontsChanged),
		3000000);
	Reload();
}


void
FontsTab::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	BGroupView::DetachedFromWindow();
}


FontRow*
FontsTab::_Row(const char* which)
{
	if (which == NULL)
		return NULL;
	if (strcmp(which, kWhichBold) == 0)
		return fBold;
	if (strcmp(which, kWhichFixed) == 0)
		return fFixed;
	if (strcmp(which, kWhichMenu) == 0)
		return fMenu;
	return fPlain;
}


static AppearancePage*
page_of(BView* view)
{
	for (BView* current = view; current != NULL; current = current->Parent()) {
		AppearancePage* page = dynamic_cast<AppearancePage*>(current);
		if (page != NULL)
			return page;
	}
	return NULL;
}


void
FontsTab::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgFont:
		{
			const char* which = NULL;
			const char* family = NULL;
			const char* style = NULL;
			message->FindString("which", &which);
			message->FindString("family", &family);
			message->FindString("style", &style);
			FontRow* row = _Row(which);
			if (row == NULL || family == NULL || style == NULL)
				break;
			FontSnap snap = row->Value();
			strlcpy(snap.family, family, sizeof(snap.family));
			strlcpy(snap.style, style, sizeof(snap.style));
			apply_font(which, snap);
			row->Reload();
			AppearancePage* page = page_of(this);
			if (page != NULL)
				page->Touch();
			break;
		}
		case kMsgSize:
		{
			const char* which = NULL;
			message->FindString("which", &which);
			FontRow* row = _Row(which);
			if (row == NULL)
				break;
			FontSnap snap = row->Value();
			snap.size = row->SizeValue();
			apply_font(which, snap);
			row->Reload();
			AppearancePage* page = page_of(this);
			if (page != NULL)
				page->Touch();
			break;
		}
		case kMsgFontsChanged:
			if (update_font_families(true)) {
				fPlain->RefreshFamilies();
				fBold->RefreshFamilies();
				fFixed->RefreshFamilies();
				fMenu->RefreshFamilies();
			}
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
FontsTab::Reload()
{
	fPlain->Reload();
	fBold->Reload();
	fFixed->Reload();
	fMenu->Reload();
}


void
FontsTab::Use(const FontSnap plain, const FontSnap bold,
	const FontSnap fixed, const FontSnap menu)
{
	apply_font(kWhichPlain, plain);
	apply_font(kWhichBold, bold);
	apply_font(kWhichFixed, fixed);
	apply_font(kWhichMenu, menu);
	Reload();
}


bool
FontsTab::Differs(const FontSnap plain, const FontSnap bold,
	const FontSnap fixed, const FontSnap menu) const
{
	return !(current_font(kWhichPlain) == plain
		&& current_font(kWhichBold) == bold
		&& current_font(kWhichFixed) == fixed
		&& current_font(kWhichMenu) == menu);
}


ColorEntry::ColorEntry(const char* label, color_which which)
	:
	BStringItem(label),
	fWhich(which),
	fColor(ui_color(which))
{
}


void
ColorEntry::DrawItem(BView* owner, BRect frame, bool complete)
{
	BStringItem::DrawItem(owner, frame, complete);
	BRect swatch = frame;
	swatch.left = swatch.right - 28.0f;
	swatch.InsetBy(4.0f, 3.0f);
	owner->SetHighColor(fColor);
	owner->FillRect(swatch);
	owner->SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
	owner->StrokeRect(swatch);
}


static const struct {
	color_which	which;
	const char*	label;
} kColorRows[] = {
	{ B_PANEL_BACKGROUND_COLOR, "Panel background" },
	{ B_PANEL_TEXT_COLOR, "Panel text" },
	{ B_DOCUMENT_BACKGROUND_COLOR, "Document background" },
	{ B_DOCUMENT_TEXT_COLOR, "Document text" },
	{ B_CONTROL_BACKGROUND_COLOR, "Control background" },
	{ B_CONTROL_TEXT_COLOR, "Control text" },
	{ B_CONTROL_BORDER_COLOR, "Control border" },
	{ B_CONTROL_HIGHLIGHT_COLOR, "Control highlight" },
	{ B_CONTROL_MARK_COLOR, "Control mark" },
	{ B_NAVIGATION_BASE_COLOR, "Navigation base" },
	{ B_NAVIGATION_PULSE_COLOR, "Navigation pulse" },
	{ B_SHINE_COLOR, "Shine" },
	{ B_SHADOW_COLOR, "Shadow" },
	{ B_LINK_TEXT_COLOR, "Link text" },
	{ B_LINK_HOVER_COLOR, "Link hover" },
	{ B_LINK_VISITED_COLOR, "Link visited" },
	{ B_LINK_ACTIVE_COLOR, "Link active" },
	{ B_MENU_BACKGROUND_COLOR, "Menu background" },
	{ B_MENU_SELECTED_BACKGROUND_COLOR, "Selected menu item background" },
	{ B_MENU_ITEM_TEXT_COLOR, "Menu item text" },
	{ B_MENU_SELECTED_ITEM_TEXT_COLOR, "Selected menu item text" },
	{ B_MENU_SELECTED_BORDER_COLOR, "Selected menu item border" },
	{ B_LIST_BACKGROUND_COLOR, "List background" },
	{ B_LIST_SELECTED_BACKGROUND_COLOR, "Selected list item background" },
	{ B_LIST_ITEM_TEXT_COLOR, "List item text" },
	{ B_LIST_SELECTED_ITEM_TEXT_COLOR, "Selected list item text" },
	{ B_SCROLL_BAR_THUMB_COLOR, "Scroll bar thumb" },
	{ B_TOOL_TIP_BACKGROUND_COLOR, "Tooltip background" },
	{ B_TOOL_TIP_TEXT_COLOR, "Tooltip text" },
	{ B_STATUS_BAR_COLOR, "Progress bar" },
	{ B_SUCCESS_COLOR, "Success" },
	{ B_FAILURE_COLOR, "Failure" },
	{ B_WINDOW_TAB_COLOR, "Window tab" },
	{ B_WINDOW_TEXT_COLOR, "Window tab text" },
	{ B_WINDOW_INACTIVE_TAB_COLOR, "Inactive window tab" },
	{ B_WINDOW_INACTIVE_TEXT_COLOR, "Inactive window tab text" },
	{ B_WINDOW_BORDER_COLOR, "Window border" },
	{ B_WINDOW_INACTIVE_BORDER_COLOR, "Inactive window border" }
};


ColorsTab::ColorsTab()
	:
	BGroupView("Colors", B_VERTICAL, B_USE_SMALL_SPACING),
	fAuto(NULL),
	fList(NULL),
	fWell(NULL),
	fPicker(NULL),
	fWhich(B_PANEL_BACKGROUND_COLOR),
	fUpdating(false)
{
	fAuto = new BCheckBox("auto", "Automatically pick secondary colors",
		new BMessage(kMsgAuto));
	fAuto->SetValue(B_CONTROL_ON);
	fList = new BListView("colors");
	fList->SetSelectionMessage(new BMessage(kMsgColor));
	BScrollView* scroll = new BScrollView("color scroll", fList, 0, false,
		true);
	scroll->SetExplicitMinSize(BSize(220, 160));
	fWell = new ColorWell();
	fPicker = new BColorControl(B_ORIGIN, B_CELLS_32x8, 8.0f, "picker",
		new BMessage(kMsgPick));
	BLayoutBuilder::Group<>(this)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fAuto)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fWell)
			.AddGlue()
			.Add(fPicker)
			.End();
	_Fill();
}


void
ColorsTab::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fAuto->SetTarget(this);
	fList->SetTarget(this);
	fPicker->SetTarget(this);
	if (fList->CurrentSelection() < 0 && fList->CountItems() > 0)
		fList->Select(0);
}


void
ColorsTab::_Fill()
{
	int32 selected = fWhich;
	while (fList->CountItems() > 0)
		delete fList->RemoveItem((int32)0);
	bool automatic = fAuto->Value() == B_CONTROL_ON;
	for (uint32 i = 0; i < sizeof(kColorRows) / sizeof(kColorRows[0]); i++) {
		color_which which = kColorRows[i].which;
		if (automatic && which != B_PANEL_BACKGROUND_COLOR
			&& which != B_STATUS_BAR_COLOR
			&& which != B_WINDOW_TAB_COLOR)
			continue;
		fList->AddItem(new ColorEntry(kColorRows[i].label, which));
	}
	int32 match = 0;
	for (int32 i = 0; i < fList->CountItems(); i++) {
		ColorEntry* entry = static_cast<ColorEntry*>(fList->ItemAt(i));
		if (entry->Which() == (color_which)selected)
			match = i;
	}
	fList->Select(match);
	_Select(match);
}


void
ColorsTab::_Select(int32 index)
{
	ColorEntry* entry = static_cast<ColorEntry*>(fList->ItemAt(index));
	if (entry == NULL)
		return;
	fWhich = entry->Which();
	rgb_color color = ui_color(fWhich);
	entry->SetSwatch(color);
	fUpdating = true;
	fPicker->SetValue(color);
	fWell->SetSwatch(color);
	fUpdating = false;
}


ColorWell::ColorWell()
	:
	BView("well", B_WILL_DRAW),
	fColor(ui_color(B_PANEL_BACKGROUND_COLOR))
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetExplicitMinSize(BSize(36, 36));
}


void
ColorWell::Draw(BRect update)
{
	(void)update;
	SetHighColor(fColor);
	FillRect(Bounds());
	SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
	StrokeRect(Bounds());
}


void
ColorWell::SetSwatch(rgb_color color)
{
	fColor = color;
	Invalidate();
}


void
ColorsTab::_Derive(color_which which, rgb_color color, BMessage& colors)
{
	read_current_colors(colors);
	put_color(colors, which, color);
	if (fAuto->Value() != B_CONTROL_ON)
		return;
	if (which == B_PANEL_BACKGROUND_COLOR) {
		bool dark = color.IsDark();
		put_color(colors, B_MENU_BACKGROUND_COLOR, color);
		put_color(colors, B_MENU_SELECTED_BACKGROUND_COLOR,
			tint_color(color, dark ? 0.8f : B_DARKEN_2_TINT));
		rgb_color control = tint_color(color, 0.84f);
		put_color(colors, B_CONTROL_BACKGROUND_COLOR, control);
		put_color(colors, B_SCROLL_BAR_THUMB_COLOR, control);
		put_color(colors, B_CONTROL_BORDER_COLOR,
			tint_color(color, dark ? 0.4875f : 1.20f));
		put_color(colors, B_WINDOW_BORDER_COLOR, tint_color(color, 0.75f));
		rgb_color inactive = tint_color(color, B_LIGHTEN_1_TINT);
		put_color(colors, B_WINDOW_INACTIVE_TAB_COLOR, inactive);
		put_color(colors, B_WINDOW_INACTIVE_BORDER_COLOR, inactive);
		put_color(colors, B_LIST_SELECTED_BACKGROUND_COLOR,
			tint_color(color, dark ? 0.77f : 1.12f));
		color_which fromDefaults[] = {
			B_MENU_ITEM_TEXT_COLOR, B_MENU_SELECTED_ITEM_TEXT_COLOR,
			B_MENU_SELECTED_BORDER_COLOR, B_PANEL_TEXT_COLOR,
			B_DOCUMENT_BACKGROUND_COLOR, B_DOCUMENT_TEXT_COLOR,
			B_CONTROL_TEXT_COLOR, B_NAVIGATION_PULSE_COLOR,
			B_WINDOW_INACTIVE_TEXT_COLOR, B_LIST_BACKGROUND_COLOR,
			B_LIST_ITEM_TEXT_COLOR, B_LIST_SELECTED_ITEM_TEXT_COLOR,
			B_SHINE_COLOR, B_SHADOW_COLOR, B_LINK_TEXT_COLOR,
			B_LINK_HOVER_COLOR, B_LINK_ACTIVE_COLOR, B_LINK_VISITED_COLOR
		};
		for (uint32 i = 0; i < sizeof(fromDefaults) / sizeof(fromDefaults[0]);
			i++) {
			put_color(colors, fromDefaults[i],
				BPrivate::GetSystemColor(fromDefaults[i], dark));
		}
	} else if (which == B_STATUS_BAR_COLOR) {
		hsl_color hsl = hsl_color::from_rgb(color);
		hsl_color highlight = hsl;
		highlight.saturation = std::max(0.2f, highlight.saturation / 2.0f);
		put_color(colors, B_CONTROL_HIGHLIGHT_COLOR, highlight.to_rgb());
		hsl_color mark = hsl;
		mark.saturation = std::max(0.2f, mark.saturation * 0.67f);
		mark.lightness = std::max(0.25f, mark.lightness * 0.55f);
		put_color(colors, B_CONTROL_MARK_COLOR, mark.to_rgb());
		hsl_color keys = hsl;
		keys.lightness = std::max(0.2f, keys.lightness * 0.75f);
		rgb_color keyboard = keys.to_rgb();
		if (keyboard.blue >= std::max(keyboard.red, keyboard.green))
			keyboard.red = keyboard.green = 0;
		else if (keyboard.red >= std::max(keyboard.green, keyboard.blue))
			keyboard.green = keyboard.blue = 0;
		else
			keyboard.red = keyboard.blue = 0;
		put_color(colors, B_NAVIGATION_BASE_COLOR, keyboard);
	} else if (which == B_WINDOW_TAB_COLOR) {
		bool dark = color.IsDark();
		hsl_color hsl = hsl_color::from_rgb(color);
		float saturation = hsl.saturation != 0.0f
			? hsl.saturation : hsl.lightness;
		put_color(colors, B_WINDOW_TEXT_COLOR,
			BPrivate::GetSystemColor(B_WINDOW_TEXT_COLOR, dark));
		put_color(colors, B_TOOL_TIP_TEXT_COLOR,
			BPrivate::GetSystemColor(B_TOOL_TIP_TEXT_COLOR, dark));
		put_color(colors, B_TOOL_TIP_BACKGROUND_COLOR,
			tint_color(color, dark ? 1.7f : 0.15f));
		hsl_color success = hsl_color::from_rgb(
			BPrivate::GetSystemColor(B_SUCCESS_COLOR, dark));
		success.saturation = std::max(0.25f, saturation * 0.68f);
		put_color(colors, B_SUCCESS_COLOR, success.to_rgb());
		hsl_color failure = hsl_color::from_rgb(
			BPrivate::GetSystemColor(B_FAILURE_COLOR, dark));
		failure.saturation = std::max(0.25f, saturation);
		put_color(colors, B_FAILURE_COLOR, failure.to_rgb());
	}
}


void
ColorsTab::_Apply(color_which which, rgb_color color)
{
	BMessage colors;
	_Derive(which, color, colors);
	apply_colors(colors);
	for (int32 i = 0; i < fList->CountItems(); i++) {
		ColorEntry* entry = static_cast<ColorEntry*>(fList->ItemAt(i));
		entry->SetSwatch(ui_color(entry->Which()));
		fList->InvalidateItem(i);
	}
	_Select(fList->CurrentSelection());
}


void
ColorsTab::Reload()
{
	_Fill();
}


void
ColorsTab::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAuto:
			_Fill();
			break;
		case kMsgColor:
			_Select(fList->CurrentSelection());
			break;
		case kMsgPick:
			if (fUpdating)
				break;
			_Apply(fWhich, fPicker->ValueAsColor());
			{
				AppearancePage* page = page_of(this);
				if (page != NULL)
					page->Touch();
			}
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


ArrowSample::ArrowSample(const char* name, bool doubled, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW),
	fDoubled(doubled)
{
	SetExplicitMinSize(MinSize());
}


BSize
ArrowSample::MinSize()
{
	float height = be_control_look->GetScrollBarWidth(B_HORIZONTAL);
	return BSize(fDoubled ? 180.0f : 160.0f, height);
}


void
ArrowSample::Draw(BRect update)
{
	(void)update;
	BRect bounds = Bounds();
	rgb_color bar = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color mark = Value() == B_CONTROL_ON
		? ui_color(B_CONTROL_HIGHLIGHT_COLOR) : bar;
	SetHighColor(mark);
	FillRect(bounds);
	SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
	StrokeRect(bounds);
	float side = bounds.Height();
	BRect left(bounds.left, bounds.top, bounds.left + side, bounds.bottom);
	BRect arrow = left.InsetByCopy(3, 3);
	be_control_look->DrawArrowShape(this, arrow, bounds,
		ui_color(B_CONTROL_TEXT_COLOR), BControlLook::B_LEFT_ARROW, 0,
		B_DARKEN_MAX_TINT);
	if (fDoubled) {
		left.OffsetBy(side + 1.0f, 0.0f);
		arrow = left.InsetByCopy(3, 3);
		be_control_look->DrawArrowShape(this, arrow, bounds,
			ui_color(B_CONTROL_TEXT_COLOR), BControlLook::B_LEFT_ARROW, 0,
			B_DARKEN_MAX_TINT);
	}
	BRect right(bounds.right - side, bounds.top, bounds.right, bounds.bottom);
	arrow = right.InsetByCopy(3, 3);
	be_control_look->DrawArrowShape(this, arrow, bounds,
		ui_color(B_CONTROL_TEXT_COLOR), BControlLook::B_RIGHT_ARROW, 0,
		B_DARKEN_MAX_TINT);
	if (fDoubled) {
		right.OffsetBy(-(side + 1.0f), 0.0f);
		arrow = right.InsetByCopy(3, 3);
		be_control_look->DrawArrowShape(this, arrow, bounds,
			ui_color(B_CONTROL_TEXT_COLOR), BControlLook::B_RIGHT_ARROW, 0,
			B_DARKEN_MAX_TINT);
	}
}


void
ArrowSample::MouseDown(BPoint where)
{
	(void)where;
	Invoke();
}


static BString
trimmed_label(const char* name, const char* suffix)
{
	BString label(name);
	if (label.EndsWith(suffix))
		label.Truncate(label.Length() - (int32)strlen(suffix));
	label.Trim();
	if (label.Length() == 0)
		label = name;
	return label;
}


LookTab::LookTab()
	:
	BGroupView("Look and feel", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fDecorMenu(NULL),
	fLookMenu(NULL),
	fSingle(NULL),
	fDouble(NULL)
{
	_BuildDecor();
	BMenuField* decorField = new BMenuField("decorator", "Decorator:",
		fDecorMenu);
	decorField->SetAlignment(B_ALIGN_RIGHT);
	BButton* decorAbout = new BButton("decor about", "About",
		new BMessage(kMsgDecorAbout));
	_BuildLooks();
	BMenuField* lookField = new BMenuField("control look", "ControlLook:",
		fLookMenu);
	lookField->SetAlignment(B_ALIGN_RIGHT);
	lookField->SetToolTip("No effect on running applications");
	BButton* lookAbout = new BButton("look about", "About",
		new BMessage(kMsgLookAbout));
	fSingle = new ArrowSample("single", false, new BMessage(kMsgArrows));
	fDouble = new ArrowSample("double", true, new BMessage(kMsgArrows));
	fDouble->Message()->AddBool("double", true);
	fSingle->Message()->AddBool("double", false);
	BBox* arrows = new BBox("arrow style");
	arrows->SetLabel("Arrow style");
	BGroupView* inner = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(inner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(new BStringView("single label", "Single:"))
		.Add(fSingle)
		.Add(new BStringView("double label", "Double:"))
		.Add(fDouble);
	arrows->AddChild(inner);
	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(decorField->CreateLabelLayoutItem(), 0, 0)
		.Add(decorField->CreateMenuBarLayoutItem(), 1, 0)
		.Add(decorAbout, 2, 0)
		.Add(lookField->CreateLabelLayoutItem(), 0, 1)
		.Add(lookField->CreateMenuBarLayoutItem(), 1, 1)
		.Add(lookAbout, 2, 1)
		.Add(new BStringView("scroll", "Scroll bar:"), 0, 2)
		.Add(arrows, 1, 2, 2)
		.AddGlue(0, 3);
	Reload();
}


void
LookTab::_BuildDecor()
{
	fDecorMenu = new BPopUpMenu("decorator");
	BPrivate::DecorInfo* current = fDecors.CurrentDecorator();
	BString currentName = current != NULL ? current->ShortcutName() : "";
	int32 count = fDecors.CountDecorators();
	for (int32 i = 0; i < count; i++) {
		BPrivate::DecorInfo* decor = fDecors.DecoratorAt(i);
		if (decor == NULL)
			continue;
		BMessage* message = new BMessage(kMsgDecor);
		message->AddString("decor", decor->ShortcutName());
		BString label = trimmed_label(decor->Name(), "Decorator");
		BMenuItem* item = new BMenuItem(label.String(), message);
		fDecorMenu->AddItem(item);
		if (decor->ShortcutName() == currentName)
			item->SetMarked(true);
	}
}


void
LookTab::_BuildLooks()
{
	fLookMenu = new BPopUpMenu("control look");
	BPrivate::get_control_look(fLookPath);
	BMessage* def = new BMessage(kMsgLook);
	def->AddString("path", "");
	BMenuItem* item = new BMenuItem("Default", def);
	fLookMenu->AddItem(item);
	if (fLookPath.Length() == 0)
		item->SetMarked(true);
	BPathFinder finder;
	BStringList paths;
	finder.FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "control_look", paths);
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BDirectory dir(paths.StringAt(i));
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			BPath path(paths.StringAt(i), entry.Name());
			BMessage* message = new BMessage(kMsgLook);
			message->AddString("path", path.Path());
			char name[B_FILE_NAME_LENGTH];
			entry.GetName(name);
			BString label = trimmed_label(name, "ControlLook");
			BMenuItem* look = new BMenuItem(label.String(), message);
			fLookMenu->AddItem(look);
			if (fLookPath == path.Path())
				look->SetMarked(true);
		}
	}
}


void
LookTab::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fDecorMenu->SetTargetForItems(this);
	fLookMenu->SetTargetForItems(this);
	fSingle->SetTarget(this);
	fDouble->SetTarget(this);
	BControl* decorAbout = dynamic_cast<BControl*>(FindView("decor about"));
	BControl* lookAbout = dynamic_cast<BControl*>(FindView("look about"));
	if (decorAbout != NULL)
		decorAbout->SetTarget(this);
	if (lookAbout != NULL)
		lookAbout->SetTarget(this);
	Reload();
}


void
LookTab::_MarkArrows(bool doubled)
{
	fSingle->SetValue(doubled ? B_CONTROL_OFF : B_CONTROL_ON);
	fDouble->SetValue(doubled ? B_CONTROL_ON : B_CONTROL_OFF);
	fSingle->Invalidate();
	fDouble->Invalidate();
}


static void
mark_menu(BMenu* menu, const char* field, const char* value)
{
	if (menu == NULL)
		return;
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		const char* stored = NULL;
		if (item->Message() != NULL)
			item->Message()->FindString(field, &stored);
		bool match = stored != NULL && value != NULL
			&& strcmp(stored, value) == 0;
		item->SetMarked(match);
	}
	if (menu->Superitem() == NULL)
		return;
	BMenuItem* marked = menu->FindMarked();
	if (marked != NULL)
		menu->Superitem()->SetLabel(marked->Label());
}


void
LookTab::Reload()
{
	BPrivate::DecorInfo* current = fDecors.CurrentDecorator();
	BString decor = current != NULL ? current->ShortcutName() : "";
	mark_menu(fDecorMenu, "decor", decor.String());
	BPrivate::get_control_look(fLookPath);
	mark_menu(fLookMenu, "path", fLookPath.String());
	scroll_bar_info info;
	get_scroll_bar_info(&info);
	_MarkArrows(info.double_arrows);
}


void
LookTab::Remember(BString& decor, BString& look, bool& arrows) const
{
	BPrivate::DecorInfo* current
		= const_cast<BPrivate::DecorInfoUtility&>(fDecors).CurrentDecorator();
	decor = current != NULL ? current->ShortcutName() : "";
	BPrivate::get_control_look(look);
	scroll_bar_info info;
	get_scroll_bar_info(&info);
	arrows = info.double_arrows;
}


void
LookTab::_AboutDecor()
{
	BPrivate::DecorInfo* current = fDecors.CurrentDecorator();
	BString text(current != NULL ? current->Name() : "Decorator");
	if (current != NULL && current->ShortDescription().Length() > 0)
		text << "\n\n" << current->ShortDescription();
	if (current != NULL && current->LongDescription().Length() > 0)
		text << "\n\n" << current->LongDescription();
	BAlert* alert = new BAlert("Decorator", text.String(), "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


void
LookTab::_AboutLook()
{
	BString text("Default");
	if (fLookPath.Length() > 0) {
		BEntry entry(fLookPath.String());
		char name[B_FILE_NAME_LENGTH];
		entry.GetName(name);
		text = trimmed_label(name, "ControlLook");
		BFile file(fLookPath.String(), B_READ_ONLY);
		BAppFileInfo info(&file);
		version_info version;
		if (info.GetVersionInfo(&version, B_APP_VERSION_KIND) == B_OK) {
			if (version.short_info[0] != '\0')
				text << "\n\n" << version.short_info;
			if (version.long_info[0] != '\0')
				text << "\n\n" << version.long_info;
		}
	}
	BAlert* alert = new BAlert("Control look", text.String(), "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


void
LookTab::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDecor:
		{
			const char* name = NULL;
			if (message->FindString("decor", &name) != B_OK)
				break;
			fDecors.SetDecorator(fDecors.FindDecorator(name));
			AppearancePage* page = page_of(this);
			if (page != NULL)
				page->Touch();
			break;
		}
		case kMsgLook:
		{
			const char* path = NULL;
			if (message->FindString("path", &path) != B_OK)
				break;
			fLookPath = path;
			BPrivate::set_control_look(fLookPath);
			AppearancePage* page = page_of(this);
			if (page != NULL)
				page->Touch();
			break;
		}
		case kMsgArrows:
		{
			bool doubled = false;
			message->FindBool("double", &doubled);
			scroll_bar_info info;
			get_scroll_bar_info(&info);
			info.double_arrows = doubled;
			set_scroll_bar_info(&info);
			_MarkArrows(doubled);
			AppearancePage* page = page_of(this);
			if (page != NULL)
				page->Touch();
			break;
		}
		case kMsgDecorAbout:
			_AboutDecor();
			break;
		case kMsgLookAbout:
			_AboutLook();
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


AntialiasTab::AntialiasTab()
	:
	BGroupView("Antialiasing", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fTypeMenu(NULL),
	fGray(NULL),
	fLcd(NULL),
	fHintMenu(NULL),
	fWeight(NULL),
	fSubpixel(kDefaultSubpixel),
	fHint(kDefaultHint),
	fWeightValue(kDefaultWeight),
	fUpdating(false)
{
	if (get_subpixel_antialiasing(&fSubpixel) != B_OK)
		fSubpixel = kDefaultSubpixel;
	if (get_hinting_mode(&fHint) != B_OK)
		fHint = kDefaultHint;
	if (get_average_weight(&fWeightValue) != B_OK)
		fWeightValue = kDefaultWeight;
	fTypeMenu = new BPopUpMenu("antialiasing");
	BMessage* gray = new BMessage(kMsgAntialias);
	gray->AddBool("subpixel", false);
	fGray = new BMenuItem("Grayscale", gray);
	BMessage* lcd = new BMessage(kMsgAntialias);
	lcd->AddBool("subpixel", true);
	fLcd = new BMenuItem("LCD subpixel", lcd);
	fTypeMenu->AddItem(fGray);
	fTypeMenu->AddItem(fLcd);
	BMenuField* typeField = new BMenuField("type", "Antialiasing type:",
		fTypeMenu);
	typeField->SetAlignment(B_ALIGN_RIGHT);
	fHintMenu = new BPopUpMenu("hinting");
	struct hint { const char* label; uint8 mode; };
	const hint hints[] = {
		{ "Off", kHintOff },
		{ "On", kHintOn },
		{ "Monospaced fonts only", kHintMonospace }
	};
	for (uint32 i = 0; i < sizeof(hints) / sizeof(hints[0]); i++) {
		BMessage* message = new BMessage(kMsgHint);
		message->AddInt8("hint", hints[i].mode);
		fHintMenu->AddItem(new BMenuItem(hints[i].label, message));
	}
	BMenuField* hintField = new BMenuField("hint", "Glyph hinting:",
		fHintMenu);
	hintField->SetAlignment(B_ALIGN_RIGHT);
	fWeight = new BSlider("weight", "Reduce colored edges filter strength:",
		new BMessage(kMsgWeight), 0, 255, B_HORIZONTAL);
	fWeight->SetLimitLabels("Off", "Strong");
	fWeight->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fWeight->SetHashMarkCount(255 / 15);
	BLayoutBuilder::Group<>(this)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(hintField->CreateLabelLayoutItem(), 0, 0)
			.Add(hintField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(typeField->CreateLabelLayoutItem(), 0, 1)
			.Add(typeField->CreateMenuBarLayoutItem(), 1, 1)
			.End()
		.Add(fWeight)
		.AddGlue();
	Reload();
}


void
AntialiasTab::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fTypeMenu->SetTargetForItems(this);
	fHintMenu->SetTargetForItems(this);
	fWeight->SetTarget(this);
}


void
AntialiasTab::Reload()
{
	fUpdating = true;
	bool subpixel = fSubpixel;
	if (get_subpixel_antialiasing(&subpixel) == B_OK)
		fSubpixel = subpixel;
	uint8 hint = fHint;
	if (get_hinting_mode(&hint) == B_OK)
		fHint = hint;
	unsigned char weight = fWeightValue;
	if (get_average_weight(&weight) == B_OK)
		fWeightValue = weight;
	fGray->SetMarked(!fSubpixel);
	fLcd->SetMarked(fSubpixel);
	for (int32 i = 0; i < fHintMenu->CountItems(); i++) {
		BMenuItem* item = fHintMenu->ItemAt(i);
		int8 mode = 0;
		item->Message()->FindInt8("hint", &mode);
		item->SetMarked((uint8)mode == fHint);
	}
	fWeight->SetValue(fWeightValue);
	fWeight->SetEnabled(fSubpixel);
	fUpdating = false;
}


void
AntialiasTab::Remember(bool& subpixel, uint8& hint, uint8& weight) const
{
	subpixel = fSubpixel;
	hint = fHint;
	weight = fWeightValue;
}


void
AntialiasTab::MessageReceived(BMessage* message)
{
	if (fUpdating) {
		BGroupView::MessageReceived(message);
		return;
	}
	switch (message->what) {
		case kMsgAntialias:
		{
			bool subpixel = fSubpixel;
			if (message->FindBool("subpixel", &subpixel) != B_OK)
				break;
			fSubpixel = subpixel;
			set_subpixel_antialiasing(fSubpixel);
			Reload();
			break;
		}
		case kMsgHint:
		{
			int8 hint = fHint;
			if (message->FindInt8("hint", &hint) != B_OK)
				break;
			fHint = (uint8)hint;
			set_hinting_mode(fHint);
			Reload();
			break;
		}
		case kMsgWeight:
			fWeightValue = (uint8)fWeight->Value();
			set_average_weight(fWeightValue);
			break;
		default:
			BGroupView::MessageReceived(message);
			return;
	}
	AppearancePage* page = page_of(this);
	if (page != NULL)
		page->Touch();
}


AppearancePage::AppearancePage(AppearanceModule* module)
	:
	BGroupView(B_VERTICAL, 0),
	fModule(module),
	fFonts(NULL),
	fColors(NULL),
	fLook(NULL),
	fAntialias(NULL),
	fOpenArrows(false),
	fOpenSubpixel(kDefaultSubpixel),
	fOpenHint(kDefaultHint),
	fOpenWeight(kDefaultWeight)
{
	fOpenPlain = current_font(kWhichPlain);
	fOpenBold = current_font(kWhichBold);
	fOpenFixed = current_font(kWhichFixed);
	fOpenMenu = current_font(kWhichMenu);
	read_current_colors(fOpenColors);
	fFonts = new FontsTab();
	fColors = new ColorsTab();
	fLook = new LookTab();
	fLook->Remember(fOpenDecor, fOpenLook, fOpenArrows);
	fAntialias = new AntialiasTab();
	fAntialias->Remember(fOpenSubpixel, fOpenHint, fOpenWeight);
	BTabView* tabs = new BTabView("appearance tabs", B_WIDTH_FROM_LABEL);
	tabs->SetBorder(B_NO_BORDER);
	tabs->AddTab(fFonts);
	tabs->AddTab(fColors);
	tabs->AddTab(fLook);
	tabs->AddTab(fAntialias);
	BLayoutBuilder::Group<>(this)
		.Add(tabs);
}


AppearancePage::~AppearancePage()
{
	if (fModule != NULL)
		fModule->ClearPage(this);
}


void
AppearancePage::Revert()
{
	fFonts->Use(fOpenPlain, fOpenBold, fOpenFixed, fOpenMenu);
	apply_colors(fOpenColors);
	fColors->Reload();
	BPrivate::DecorInfoUtility decors;
	if (fOpenDecor.Length() > 0)
		decors.SetDecorator(decors.FindDecorator(fOpenDecor));
	BPrivate::set_control_look(fOpenLook);
	scroll_bar_info info;
	get_scroll_bar_info(&info);
	info.double_arrows = fOpenArrows;
	set_scroll_bar_info(&info);
	fLook->Reload();
	set_subpixel_antialiasing(fOpenSubpixel);
	set_hinting_mode(fOpenHint);
	set_average_weight(fOpenWeight);
	fAntialias->Reload();
}


void
AppearancePage::Defaults()
{
	fFonts->Use(default_font(kWhichPlain), default_font(kWhichBold),
		default_font(kWhichFixed), default_font(kWhichMenu));
	BMessage colors;
	read_default_colors(colors);
	apply_colors(colors);
	fColors->Reload();
	BPrivate::DecorInfoUtility decors;
	BPrivate::DecorInfo* fallback = decors.DefaultDecorator();
	if (fallback != NULL)
		decors.SetDecorator(fallback);
	BPrivate::set_control_look(BString());
	scroll_bar_info info;
	get_scroll_bar_info(&info);
	info.double_arrows = false;
	set_scroll_bar_info(&info);
	fLook->Reload();
	set_subpixel_antialiasing(kDefaultSubpixel);
	set_hinting_mode(kDefaultHint);
	set_average_weight(kDefaultWeight);
	fAntialias->Reload();
}


bool
AppearancePage::Modified() const
{
	BMessage colors;
	read_current_colors(colors);
	BString decor;
	BString look;
	bool arrows = false;
	fLook->Remember(decor, look, arrows);
	bool subpixel = false;
	uint8 hint = 0;
	uint8 weight = 0;
	fAntialias->Remember(subpixel, hint, weight);
	return fFonts->Differs(fOpenPlain, fOpenBold, fOpenFixed, fOpenMenu)
		|| colors_differ(colors, fOpenColors)
		|| decor != fOpenDecor || look != fOpenLook || arrows != fOpenArrows
		|| subpixel != fOpenSubpixel || hint != fOpenHint
		|| weight != fOpenWeight;
}


bool
AppearancePage::CanDefaults() const
{
	BMessage colors;
	BMessage defaults;
	read_current_colors(colors);
	read_default_colors(defaults);
	BString decor;
	BString look;
	bool arrows = false;
	fLook->Remember(decor, look, arrows);
	bool subpixel = false;
	uint8 hint = 0;
	uint8 weight = 0;
	fAntialias->Remember(subpixel, hint, weight);
	BPrivate::DecorInfoUtility decors;
	BPrivate::DecorInfo* fallback = decors.DefaultDecorator();
	BString defaultDecor = fallback != NULL ? fallback->ShortcutName() : "";
	return fFonts->Differs(default_font(kWhichPlain), default_font(kWhichBold),
			default_font(kWhichFixed), default_font(kWhichMenu))
		|| colors_differ(colors, defaults)
		|| decor != defaultDecor || look.Length() > 0 || arrows
		|| subpixel != kDefaultSubpixel || hint != kDefaultHint
		|| weight != kDefaultWeight;
}


void
AppearancePage::Touch()
{
	if (fModule != NULL)
		fModule->Touch();
}


AppearanceModule::AppearanceModule()
	:
	fPage(NULL),
	fContext(NULL)
{
}


BView*
AppearanceModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fPage = new AppearancePage(this);
	return fPage;
}


void
AppearanceModule::ClearPage(AppearancePage* page)
{
	if (fPage == page)
		fPage = NULL;
}


void
AppearanceModule::Touch()
{
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
AppearanceModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	return B_OK;
}


status_t
AppearanceModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
AppearanceModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Revert();
	return B_OK;
}


bool
AppearanceModule::IsModified() const
{
	return fPage != NULL && fPage->Modified();
}


bool
AppearanceModule::CanApplyDefaults() const
{
	return fPage != NULL && fPage->CanDefaults();
}


status_t
AppearanceModule::ApplyDefaults(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Defaults();
	return B_OK;
}


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new AppearanceModule();
}
