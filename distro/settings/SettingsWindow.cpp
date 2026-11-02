/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "SettingsWindow.h"

#include "HomeView.h"
#include "SettingsFont.h"
#include "SettingsHost.h"
#include "SettingsIcon.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <CardLayout.h>
#include <ControlLook.h>
#include <FindDirectory.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <R5Icons.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <ListView.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <stdio.h>
#include <string.h>


struct SettingsCategory {
	const char* id;
	const char* name;
	const char* summary;
	const char* iconSignature;
};


static const char* module_icon(const char* moduleId);


static const SettingsCategory kCategories[] = {
	{ "personalization", "Personalization",
		"Appearance, the desktop, wallpaper, and the Deskbar.",
		"application/x-vnd.Haiku-Appearance" },
	{ "system", "System", "Display, sound, and this computer.",
		"application/x-vnd.Haiku-Screen" },
	{ "devices", "Devices", "Keyboard, mouse, Bluetooth, and printers.",
		"application/x-vnd.Haiku-Input" },
	{ "network", "Network", "How this computer joins the network.",
		"application/x-vnd.Haiku-Network" },
	{ "accounts", "Accounts", "People who can sign in.",
		"application/x-vnd.R5OS-Users" }
};


class NavItem : public BStringItem {
public:
								NavItem(const char* label, const char* id,
									bool header);
	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete);

			const char*			Id() const { return fId.String(); }
			bool				IsHeader() const { return fHeader; }

private:
			BString				fId;
			bool				fHeader;
};


NavItem::NavItem(const char* label, const char* id, bool header)
	:
	BStringItem(label),
	fId(id),
	fHeader(header)
{
}


void
NavItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	if (!fHeader) {
		BStringItem::DrawItem(owner, frame, complete);
		return;
	}
	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	owner->SetLowColor(background);
	if (complete)
		owner->FillRect(frame, B_SOLID_LOW);
	owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
	owner->SetFont(r5_settings_bold_font());
	font_height height;
	r5_settings_bold_font()->GetHeight(&height);
	owner->DrawString(Text(),
		BPoint(frame.left + 8, frame.bottom - height.descent - 2));
	owner->SetFont(r5_settings_plain_font());
}


class NavList : public BListView {
public:
								NavList();
	virtual	void				FrameResized(float width, float height);
			void				UpdateScrollBar();

private:
			bool				fBarShown;
			bool				fUpdatingBar;
};


NavList::NavList()
	:
	BListView("nav", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS),
	fBarShown(true),
	fUpdatingBar(false)
{
}


void
NavList::FrameResized(float width, float height)
{
	BListView::FrameResized(width, height);
	UpdateScrollBar();
}


void
NavList::UpdateScrollBar()
{
	if (fUpdatingBar)
		return;

	BScrollBar* bar = ScrollBar(B_VERTICAL);
	if (bar == NULL)
		return;

	float min = 0;
	float max = 0;
	bar->GetRange(&min, &max);
	bool need = max > 0;
	if (need == fBarShown)
		return;

	fUpdatingBar = true;
	fBarShown = need;
	if (need) {
		bar->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
		bar->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
		bar->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
		if (bar->IsHidden())
			bar->Show();
	} else {
		bar->SetExplicitMinSize(BSize(0, 0));
		bar->SetExplicitMaxSize(BSize(0, 0));
		bar->SetExplicitPreferredSize(BSize(0, 0));
		if (!bar->IsHidden())
			bar->Hide();
	}
	if (Parent() != NULL)
		Parent()->InvalidateLayout();
	fUpdatingBar = false;
}


class PageIcon : public BView {
public:
								PageIcon();
	virtual						~PageIcon();
	virtual	void				Draw(BRect update);
			void				SetSignature(const char* signature);
			void				UsePreferencesFolder();

private:
			void				_Clear();
			bool				_Load(BNode& node);

			BBitmap*			fBitmap;
};


PageIcon::PageIcon()
	:
	BView("page-icon", B_WILL_DRAW),
	fBitmap(NULL)
{
	BSize size = be_control_look->ComposeIconSize(B_LARGE_ICON);
	SetExplicitMinSize(size);
	SetExplicitMaxSize(size);
	SetExplicitPreferredSize(size);
	SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_VERTICAL_CENTER));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


PageIcon::~PageIcon()
{
	delete fBitmap;
}


void
PageIcon::Draw(BRect update)
{
	(void)update;
	SetLowColor(ViewColor());
	FillRect(Bounds(), B_SOLID_LOW);
	if (fBitmap == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	BPoint where(
		(Bounds().Width() - fBitmap->Bounds().Width()) / 2,
		(Bounds().Height() - fBitmap->Bounds().Height()) / 2);
	DrawBitmap(fBitmap, where);
	SetDrawingMode(B_OP_COPY);
}


void
PageIcon::_Clear()
{
	delete fBitmap;
	fBitmap = NULL;
}


bool
PageIcon::_Load(BNode& node)
{
	BRect bounds(BPoint(0, 0), be_control_look->ComposeIconSize(B_LARGE_ICON));
	BBitmap* bitmap = new BBitmap(bounds, B_RGBA32);
	BNodeInfo info(&node);
	if (info.GetIcon(bitmap, B_LARGE_ICON) != B_OK) {
		delete bitmap;
		return false;
	}
	r5_settings_apply_prefs_overlay(bitmap);
	fBitmap = bitmap;
	return true;
}


void
PageIcon::SetSignature(const char* signature)
{
	_Clear();
	if (signature != NULL && signature[0] != '\0') {
		BRect bounds(BPoint(0, 0),
			be_control_look->ComposeIconSize(B_LARGE_ICON));
		BBitmap* bitmap = new BBitmap(bounds, B_RGBA32);
		if (BMimeType(signature).GetIcon(bitmap, B_LARGE_ICON) == B_OK) {
			r5_settings_apply_prefs_overlay(bitmap);
			fBitmap = bitmap;
		} else
			delete bitmap;
	}
	Invalidate();
}


void
PageIcon::UsePreferencesFolder()
{
	_Clear();
	BPath path;
	BNode node;
	if (find_directory(B_SYSTEM_PREFERENCES_DIRECTORY, &path) == B_OK
		&& node.SetTo(path.Path()) == B_OK)
		_Load(node);
	Invalidate();
}


class PageBar;


// Shows the page at its real height. When that is taller than the area
// under the subtitle, the scrollbar appears. The list, title, and bottom
// row stay where they are.
class PageScroller : public BView {
public:
								PageScroller(BView* content);
	virtual						~PageScroller();
	virtual	void				DoLayout();
			void				ScrollToValue(float value);

private:
			BView*				fContent;
			PageBar*			fBar;
			bool				fScrolling;
};


class PageBar : public BScrollBar {
public:
								PageBar(PageScroller* owner);
	virtual	void				ValueChanged(float newValue);

private:
			PageScroller*		fOwner;
};


PageBar::PageBar(PageScroller* owner)
	:
	BScrollBar(BRect(0, 0, 14, 14), "page-bar", NULL, 0, 0, B_VERTICAL),
	fOwner(owner)
{
}


void
PageBar::ValueChanged(float newValue)
{
	if (fOwner != NULL)
		fOwner->ScrollToValue(newValue);
	SetValue(newValue);
}


PageScroller::PageScroller(BView* content)
	:
	BView("page-scroll", B_WILL_DRAW | B_FRAME_EVENTS),
	fContent(content),
	fBar(NULL),
	fScrolling(false)
{
	SetFlags(Flags() | B_SUPPORTS_LAYOUT);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fBar = new PageBar(this);
	if (fContent != NULL)
		AddChild(fContent);
	AddChild(fBar);
	fBar->Hide();
}


PageScroller::~PageScroller()
{
}


void
PageScroller::ScrollToValue(float value)
{
	if (fContent == NULL || fScrolling)
		return;
	fContent->MoveTo(0, -value);
}


void
PageScroller::DoLayout()
{
	if (fContent == NULL || fBar == NULL || fScrolling)
		return;

	fScrolling = true;
	float width = Bounds().Width();
	float height = Bounds().Height();
	BSize preferred = fContent->PreferredSize();
	float barWidth = fBar->PreferredSize().width;
	bool overflow = preferred.height > height + 1.0f;
	float contentWidth = overflow ? width - barWidth - 1.0f : width;
	float contentHeight = overflow ? preferred.height : height;
	if (contentWidth < 0)
		contentWidth = 0;
	if (contentHeight < 0)
		contentHeight = 0;

	float value = fBar->Value();
	float maxValue = 0;
	if (overflow)
		maxValue = preferred.height - height;
	if (value > maxValue)
		value = maxValue;
	if (value < 0)
		value = 0;

	fContent->MoveTo(0, overflow ? -value : 0);
	fContent->ResizeTo(contentWidth, contentHeight);
	if (overflow) {
		if (fBar->IsHidden())
			fBar->Show();
		fBar->MoveTo(width - barWidth, 0);
		fBar->ResizeTo(barWidth, height);
		fBar->SetRange(0, maxValue);
		fBar->SetSteps(20, height);
		if (preferred.height > 0)
			fBar->SetProportion(height / preferred.height);
		if (fBar->Value() != value)
			fBar->SetValue(value);
	} else {
		fBar->SetRange(0, 0);
		if (!fBar->IsHidden())
			fBar->Hide();
	}
	fScrolling = false;
}


static SettingsPlace*
make_place(const char* id, bool category)
{
	SettingsPlace* place = new SettingsPlace();
	place->id = id;
	place->category = category;
	return place;
}


SettingsWindow::SettingsWindow(SettingsManager* manager,
	SettingsStorage* storage)
	:
	BWindow(BRect(80, 60, 900, 640), "Settings", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fManager(manager),
	fStorage(storage),
	fContext(NULL),
	fSearch(NULL),
	fList(NULL),
	fTitle(NULL),
	fDescription(NULL),
	fIcon(NULL),
	fCards(NULL),
	fDeck(NULL),
	fHome(NULL),
	fBack(NULL),
	fRevert(NULL),
	fDefaults(NULL),
	fCurrent(NULL),
	fAdjusting(false),
	fLastApplyError(B_OK)
{
	fContext = new SettingsHost(fStorage, BMessenger(this));

	fSearch = new BTextControl("search", "Search:", "", NULL);
	fSearch->SetModificationMessage(new BMessage(kMsgSearch));

	fList = new NavList();
	fList->SetSelectionMessage(new BMessage(kMsgOpenPage));
	BScrollView* scroll = new BScrollView("nav-scroll", fList, 0, false, true);

	fBack = new BButton("back", NULL, new BMessage(kMsgGoBack));
	fHome = new BButton("home", NULL, new BMessage(kMsgOpenHome));
	fBack->SetToolTip("Back");
	fHome->SetToolTip("Home");
	fTitle = new BStringView("title", "Settings");
	fDescription = new BStringView("description",
		"One place for the computer's settings.");
	fTitle->SetFont(r5_settings_title_font());
	fIcon = new PageIcon();

	fDeck = new BView("deck", 0);
	fCards = new BCardLayout();
	fDeck->SetLayout(fCards);
	fDeck->SetExplicitMinSize(BSize(360, 240));

	fRevert = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fDefaults = new BButton("defaults", "Defaults", new BMessage(kMsgDefaults));

	// The list starts under the tab and runs down to Search. The title
	// and the subtitle sit beside the list, with this page's icon at the
	// right of that block. The page is under the subtitle. A page that is
	// taller than that area scrolls there. Back, Home, Revert, and
	// Defaults stay on the bottom row. Back and Home use
	// r5_button_set_icon(). See AGENTS.md.
	const float sidebarWidth = 248;
	scroll->SetExplicitMinSize(BSize(sidebarWidth, 160));
	scroll->SetExplicitMaxSize(BSize(sidebarWidth, B_SIZE_UNLIMITED));
	fSearch->SetExplicitMinSize(BSize(sidebarWidth, B_SIZE_UNSET));
	fSearch->SetExplicitMaxSize(BSize(sidebarWidth, B_SIZE_UNSET));

	fDeck->SetExplicitMinSize(BSize(760, 280));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 1)
			.Add(scroll, 0)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1)
				.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0)
					.AddGroup(B_VERTICAL, 0, 1)
						.Add(fTitle)
						.Add(fDescription)
						.End()
					.Add(fIcon)
					.End()
				.Add(fDeck, 1)
				.End()
			.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0)
			.Add(fSearch)
			.AddGlue()
			.Add(fBack)
			.Add(fHome)
			.Add(fRevert)
			.Add(fDefaults)
			.End();
	for (int32 i = 0; i < CountChildren(); i++)
		r5_settings_apply_plain(ChildAt(i));
	fTitle->SetFont(r5_settings_title_font());
	if (fSearch->TextView() != NULL)
		fSearch->TextView()->SetFontAndColor(r5_settings_plain_font());

	float buttonHeight = fRevert->PreferredSize().height;
	r5_button_set_icon(fBack, R5_ICON_BACK, buttonHeight);
	r5_button_set_icon(fHome, R5_ICON_HOME, buttonHeight);

	fBack->SetEnabled(false);
	fRevert->SetEnabled(false);
	fDefaults->SetEnabled(false);
	// 610 is 100 taller than 510, so the page still has room under
	// the title. 560 is the shortest the window may be.
	SetSizeLimits(980, 4096, 560, 4096);
	ResizeTo(1100, 610);
	CenterOnScreen();
}


SettingsWindow::~SettingsWindow()
{
	for (int32 i = 0; i < fHistory.CountItems(); i++)
		delete static_cast<SettingsPlace*>(fHistory.ItemAt(i));
	if (fList != NULL) {
		for (int32 i = fList->CountItems() - 1; i >= 0; i--)
			delete fList->RemoveItem(i);
	}
	delete fContext;
}


bool
SettingsWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
SettingsWindow::ModulesLoaded()
{
	SettingsHomeItem items[8];
	int32 count = 0;
	for (uint32 i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); i++) {
		bool any = false;
		for (int32 m = 0; m < fManager->Count(); m++) {
			SettingsModule* module = fManager->ModuleAt(m);
			if (module != NULL
				&& strcmp(module->Category(), kCategories[i].id) == 0) {
				any = true;
				break;
			}
		}
		if (!any || count >= 8)
			continue;
		items[count].id = kCategories[i].id;
		items[count].name = kCategories[i].name;
		items[count].summary = kCategories[i].summary;
		items[count].iconSignature = kCategories[i].iconSignature;
		items[count].command = kMsgOpenCategory;
		items[count].field = "category";
		count++;
	}
	if (count == 0) {
		items[0].id = "";
		items[0].name = "No pages";
		items[0].summary = "No settings modules were found.";
		items[0].iconSignature = NULL;
		items[0].command = kMsgOpenCategory;
		items[0].field = "category";
		count = 1;
	}
	fCards->AddView(new HomeView(items, count,
		"Choose a category. Existing preference programs open in their own window."));
	_BuildSidebar();
	_ShowHome();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSearch:
		{
			fQuery = fSearch->Text();
			_BuildSidebar();
			break;
		}
		case kMsgOpenHome:
			_ShowHome();
			break;
		case kMsgOpenCategory:
		{
			const char* category = NULL;
			if (message->FindString("category", &category) != B_OK)
				break;
			_ShowCategory(category);
			break;
		}
		case kMsgOpenPage:
		{
			const char* moduleId = NULL;
			if (message->FindString("module", &moduleId) != B_OK) {
				int32 index = fList->CurrentSelection();
				if (index < 0)
					break;
				NavItem* item = dynamic_cast<NavItem*>(fList->ItemAt(index));
				if (item == NULL || item->IsHeader()) {
					_SelectSidebar(fCurrent != NULL
						? fCurrent->Identifier() : NULL);
					break;
				}
				moduleId = item->Id();
			}
			if (moduleId != NULL)
				_ShowModule(moduleId);
			break;
		}
		case kMsgGoBack:
		{
			if (fHistory.CountItems() < 2) {
				_ShowHome();
				break;
			}
			delete static_cast<SettingsPlace*>(
				fHistory.RemoveItem(fHistory.CountItems() - 1));
			SettingsPlace* place = static_cast<SettingsPlace*>(
				fHistory.RemoveItem(fHistory.CountItems() - 1));
			if (place == NULL) {
				_ShowHome();
				break;
			}
			// Copy first. _ShowHome() stores a new place, and the old
			// one is freed once here. Freeing LastItem() and then
			// `place` was the double free on Back.
			bool category = place->category;
			BString id(place->id);
			delete place;
			if (category)
				_ShowCategory(id.String());
			else if (id == "home")
				_ShowHome();
			else
				_ShowModule(id.String());
			break;
		}
		case kMsgRevert:
			if (fCurrent != NULL && fCurrent->CanSave()) {
				fLastApplyError = B_OK;
				status_t status = fCurrent->RevertSettings(fContext);
				if (status != B_OK) {
					BString text("Could not revert ");
					text << fCurrent->Name() << ".\n" << strerror(status);
					BAlert* alert = new BAlert("Settings", text.String(), "OK",
						NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					alert->Go(NULL);
				}
				_UpdateActions();
			}
			break;
		case kMsgDefaults:
			if (fCurrent != NULL && fCurrent->CanApplyDefaults()) {
				fLastApplyError = B_OK;
				status_t status = fCurrent->ApplyDefaults(fContext);
				if (status != B_OK) {
					BString text("Could not restore defaults for ");
					text << fCurrent->Name() << ".\n" << strerror(status);
					BAlert* alert = new BAlert("Settings", text.String(), "OK",
						NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					alert->Go(NULL);
				}
				_UpdateActions();
			}
			break;
		case kMsgModuleChanged:
			_ApplyCurrent();
			_UpdateActions();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
SettingsWindow::_BuildSidebar()
{
	fAdjusting = true;
	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);
	fList->MakeEmpty();
	for (uint32 c = 0; c < sizeof(kCategories) / sizeof(kCategories[0]); c++) {
		bool header = false;
		for (int32 i = 0; i < fManager->Count(); i++) {
			SettingsModule* module = fManager->ModuleAt(i);
			if (module == NULL
				|| strcmp(module->Category(), kCategories[c].id) != 0)
				continue;
			if (!_Matches(module, fQuery.String()))
				continue;
			if (!header) {
				fList->AddItem(new NavItem(kCategories[c].name,
					kCategories[c].id, true));
				header = true;
			}
			fList->AddItem(new NavItem(module->Name(), module->Identifier(),
				false));
		}
	}
	fAdjusting = false;
	_SelectSidebar(fCurrent != NULL ? fCurrent->Identifier() : NULL);
	NavList* list = dynamic_cast<NavList*>(fList);
	if (list != NULL)
		list->UpdateScrollBar();
}


void
SettingsWindow::_ShowHome()
{
	fCurrent = NULL;
	fCards->SetVisibleItem((int32)0);
	_SetHeader("Settings", "One place for the computer's settings.", NULL);
	if (fIcon != NULL)
		fIcon->UsePreferencesFolder();
	_Remember("home", false);
	_SelectSidebar(NULL);
	_UpdateActions();
}


void
SettingsWindow::_ShowCategory(const char* categoryId)
{
	if (categoryId == NULL || categoryId[0] == '\0')
		return;
	fCurrent = NULL;
	BString name("category:");
	name << categoryId;
	BView* existing = fDeck->FindView(name.String());
	if (existing == NULL) {
		existing = _CategoryView(categoryId, _CategoryName(categoryId));
		existing->SetName(name.String());
		fCards->AddView(existing);
	}
	int32 index = fCards->IndexOfView(existing);
	if (index >= 0)
		fCards->SetVisibleItem(index);
	const char* icon = NULL;
	for (uint32 i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); i++) {
		if (strcmp(kCategories[i].id, categoryId) == 0)
			icon = kCategories[i].iconSignature;
	}
	_SetHeader(_CategoryName(categoryId), _CategorySummary(categoryId), icon);
	_Remember(categoryId, true);
	_SelectSidebar(NULL);
	_UpdateActions();
}


void
SettingsWindow::_ShowModule(const char* moduleId)
{
	SettingsModule* module = fManager->Find(moduleId);
	if (module == NULL)
		return;
	BView* view = _ModuleView(module);
	if (view == NULL)
		return;
	int32 index = fCards->IndexOfView(view);
	if (index >= 0)
		fCards->SetVisibleItem(index);
	fCurrent = module;
	_SetHeader(module->Name(), module->Description(),
		module_icon(module->Identifier()));
	_Remember(module->Identifier(), false);
	_SelectSidebar(module->Identifier());
	_UpdateActions();
}


void
SettingsWindow::_ApplyCurrent()
{
	if (fCurrent == NULL || !fCurrent->CanSave())
		return;
	if (fCurrent->RequiresAdmin() && !fContext->AdminChangeAllowed())
		return;
	status_t status = fCurrent->SaveSettings(fContext);
	if (status == B_OK) {
		fLastApplyError = B_OK;
		return;
	}
	if (status == fLastApplyError)
		return;
	fLastApplyError = status;
	BString text("Could not apply ");
	text << fCurrent->Name() << ".\n" << strerror(status);
	BAlert* alert = new BAlert("Settings", text.String(), "OK",
		NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go(NULL);
}


void
SettingsWindow::_Remember(const char* id, bool category)
{
	SettingsPlace* last = static_cast<SettingsPlace*>(fHistory.LastItem());
	if (last != NULL && last->category == category && last->id == id)
		return;
	fHistory.AddItem(make_place(id, category));
	fBack->SetEnabled(fHistory.CountItems() > 1);
}


void
SettingsWindow::_SetHeader(const char* title, const char* description,
	const char* iconSignature)
{
	fTitle->SetText(title != NULL ? title : "");
	fDescription->SetText(description != NULL ? description : "");
	if (fIcon != NULL)
		fIcon->SetSignature(iconSignature);
}


void
SettingsWindow::_UpdateActions()
{
	bool can = fCurrent != NULL && fCurrent->CanSave();
	fRevert->SetEnabled(can && fCurrent->IsModified());
	fDefaults->SetEnabled(fCurrent != NULL && fCurrent->CanApplyDefaults());
}


BView*
SettingsWindow::_ModuleView(SettingsModule* module)
{
	BString name("module:");
	name << module->Identifier();
	BView* existing = fDeck->FindView(name.String());
	if (existing != NULL)
		return existing;

	module->LoadSettings(fContext);
	BView* view = module->CreateView(fContext, BRect(0, 0, 0, 0));
	if (view == NULL) {
		view = new BStringView(name.String(),
			"This page could not be opened.");
	}
	r5_settings_apply_plain(view);
	PageScroller* scroller = new PageScroller(view);
	scroller->SetName(name.String());
	fCards->AddView(scroller);
	return scroller;
}


static const char*
module_icon(const char* moduleId)
{
	struct IconRow {
		const char* id;
		const char* signature;
	};
	static const IconRow rows[] = {
		{ "appearance", "application/x-vnd.Haiku-Appearance" },
		{ "colors", "application/x-vnd.Haiku-Appearance" },
		{ "deskbar", "application/x-vnd.Haiku-DeskbarPreferences" },
		{ "wallpaper", "application/x-vnd.Haiku-Backgrounds" },
		{ "information", "application/x-vnd.Haiku-About" },
		{ "display", "application/x-vnd.Haiku-Screen" },
		{ "sound", "application/x-vnd.Haiku-Sounds" },
		{ "keyboard", "application/x-vnd.Haiku-Keymap" },
		{ "mouse", "application/x-vnd.Haiku-Input" },
		{ "bluetooth", "application/x-vnd.Haiku-BluetoothPrefs" },
		{ "printers", "application/x-vnd.Be-PRNT" },
		{ "network", "application/x-vnd.Haiku-Network" },
		{ "accounts", "application/x-vnd.R5OS-Users" }
	};
	if (moduleId == NULL)
		return NULL;
	for (uint32 i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		if (strcmp(rows[i].id, moduleId) == 0)
			return rows[i].signature;
	}
	return NULL;
}


BView*
SettingsWindow::_CategoryView(const char* categoryId, const char* name)
{
	(void)name;
	SettingsHomeItem items[16];
	int32 count = 0;
	for (int32 i = 0; i < fManager->Count() && count < 16; i++) {
		SettingsModule* module = fManager->ModuleAt(i);
		if (module == NULL || strcmp(module->Category(), categoryId) != 0)
			continue;
		items[count].id = module->Identifier();
		items[count].name = module->Name();
		items[count].summary = module->Description();
		items[count].iconSignature = module_icon(module->Identifier());
		items[count].command = kMsgOpenPage;
		items[count].field = "module";
		count++;
	}
	return new HomeView(items, count, NULL);
}


bool
SettingsWindow::_Matches(SettingsModule* module, const char* query) const
{
	if (module == NULL)
		return false;
	if (query == NULL || query[0] == '\0')
		return true;
	BString hay(module->Name());
	hay << " " << module->Description() << " " << module->Keywords()
		<< " " << _CategoryName(module->Category());
	return hay.IFindFirst(query) >= 0;
}


const char*
SettingsWindow::_CategoryName(const char* categoryId) const
{
	if (categoryId == NULL)
		return "";
	for (uint32 i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); i++) {
		if (strcmp(kCategories[i].id, categoryId) == 0)
			return kCategories[i].name;
	}
	return categoryId;
}


const char*
SettingsWindow::_CategorySummary(const char* categoryId) const
{
	if (categoryId == NULL)
		return "";
	for (uint32 i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); i++) {
		if (strcmp(kCategories[i].id, categoryId) == 0)
			return kCategories[i].summary;
	}
	return "";
}


void
SettingsWindow::_SelectSidebar(const char* moduleId)
{
	fAdjusting = true;
	if (moduleId == NULL) {
		fList->DeselectAll();
		fAdjusting = false;
		return;
	}
	for (int32 i = 0; i < fList->CountItems(); i++) {
		NavItem* item = dynamic_cast<NavItem*>(fList->ItemAt(i));
		if (item != NULL && !item->IsHeader()
			&& strcmp(item->Id(), moduleId) == 0) {
			fList->Select(i);
			fAdjusting = false;
			return;
		}
	}
	fList->DeselectAll();
	fAdjusting = false;
}
