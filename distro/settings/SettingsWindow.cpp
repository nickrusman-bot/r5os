/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "SettingsWindow.h"

#include "AdminLock.h"
#include "HomeView.h"
#include "SettingsFont.h"
#include "SettingsHost.h"
#include "SettingsIcon.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"
#include "SettingsStorage.h"

#include <ActionCard.h>
#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <CardLayout.h>
#include <Control.h>
#include <ControlLook.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Mime.h>
#include <PageHeader.h>
#include <Path.h>
#include <R5Icons.h>
#include <Resources.h>
#include <Roster.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "r5_pebble.inc"

#include <stdio.h>
#include <string.h>


struct SettingsCategory {
	const char* id;
	const char* name;
	const char* summary;
	const char* iconSignature;
	color_which tint;
};


static const char* module_icon(const char* moduleId);
static int32 modules_in_category(SettingsManager* manager,
	const char* categoryId, SettingsModule** out, int32 maxOut);


static const SettingsCategory kCategories[] = {
	{ "personalization", "Personalization",
		"Fonts, look and feel, wallpaper, and the Deskbar.",
		"application/x-vnd.Haiku-Appearance", B_CONTROL_HIGHLIGHT_COLOR },
	{ "system", "System",
		"Display, sound, notifications, virtual memory, and this computer.",
		"application/x-vnd.Haiku-Screen", B_PANEL_TEXT_COLOR },
	{ "devices", "Devices",
		"Keyboard, mouse, touchpad, printers, and Bluetooth.",
		"application/x-vnd.Haiku-Devices", B_SUCCESS_COLOR },
	{ "network", "Network",
		"Wired and Wi-Fi connections, and network shares.",
		"application/x-vnd.Haiku-Network", B_LINK_TEXT_COLOR },
	{ "accounts", "Accounts",
		"The people who use this computer, and how they sign in.",
		"application/x-vnd.Be-PEPL", B_WINDOW_TAB_COLOR }
};


class SettingsList : public BAppearanceKit::BSidebar {
public:
								SettingsList();
	virtual	void				FrameResized(float width, float height);
	virtual	void				MouseDown(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
			void				UpdateScrollBar();

private:
			bool				fBarShown;
			bool				fUpdatingBar;
};


// The baseline that puts the capital letters in the middle of a box of
// this height. Manrope has much room above its capitals, so centring on
// ascent plus descent puts the letters low.
static float
search_baseline(const BFont& font, float height)
{
	BRect box;
	font.GetBoundingBoxesAsGlyphs("H", 1, B_SCREEN_METRIC, &box);
	float cap = -box.top;
	if (cap <= 0) {
		font_height metrics;
		font.GetHeight(&metrics);
		cap = metrics.ascent * 0.7f;
	}
	return floorf(height / 2.0f + cap / 2.0f);
}


// The word Search in the empty field, on the same baseline as typed text.
// A click on it, or on the magnifier, puts the cursor in the field.
class SearchHint : public BView {
public:
								SearchHint(BView* target);
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);

private:
			BView*				fTarget;
};


SearchHint::SearchHint(BView* target)
	:
	BView("hint", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fTarget(target)
{
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetFont(r5_settings_plain_font());
}


void
SearchHint::Draw(BRect update)
{
	(void)update;
	BFont font;
	GetFont(&font);
	SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));
	DrawString("Search", BPoint(0, search_baseline(font, Bounds().Height())));
}


void
SearchHint::MouseDown(BPoint where)
{
	(void)where;
	if (fTarget != NULL)
		fTarget->MakeFocus(true);
}


// The magnifier at the left of Search. A sibling on top of the text view,
// so the text view cannot paint over it.
class SearchGlass : public BView {
public:
								SearchGlass();
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
};


SearchGlass::SearchGlass()
	:
	BView("glass", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
}


void
SearchGlass::Draw(BRect update)
{
	(void)update;
	float u = be_plain_font->Size() / 13.0f;
	float size = u * 14.0f;
	float radius = size * 0.32f;
	float cx = u * 10.0f + radius;
	float cy = Bounds().Height() / 2.0f - radius * 0.25f;
	SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));
	SetPenSize(u * 1.6f);
	StrokeEllipse(BPoint(cx, cy), radius, radius);
	StrokeLine(BPoint(cx + radius * 0.72f, cy + radius * 0.72f),
		BPoint(cx + radius * 1.75f, cy + radius * 1.75f));
	SetPenSize(1.0f);
}


void
SearchGlass::MouseDown(BPoint where)
{
	(void)where;
	BTextControl* field = dynamic_cast<BTextControl*>(Parent());
	if (field != NULL && field->TextView() != NULL)
		field->TextView()->MakeFocus(true);
}


class SearchField : public BTextControl {
public:
								SearchField();
	virtual	void				AttachedToWindow();
	virtual	void				DoLayout();
	virtual	void				FrameResized(float width, float height);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);

private:
			void				_SyncHint();
			SearchHint*			fHint;
			SearchGlass*		fGlass;
			BMessageFilter*		fWatch;
};


static BView*
first_navigable(BView* view)
{
	if (view == NULL)
		return NULL;
	if ((view->Flags() & B_NAVIGABLE) != 0 && !view->IsHidden())
		return view;
	for (int32 i = 0; i < view->CountChildren(); i++) {
		BView* found = first_navigable(view->ChildAt(i));
		if (found != NULL)
			return found;
	}
	return NULL;
}


SettingsList::SettingsList()
	:
	BAppearanceKit::BSidebar("nav"),
	fBarShown(true),
	fUpdatingBar(false)
{
}


void
SettingsList::FrameResized(float width, float height)
{
	BAppearanceKit::BSidebar::FrameResized(width, height);
	UpdateScrollBar();
}


void
SettingsList::MouseDown(BPoint where)
{
	int32 index = IndexOf(where);
	BAppearanceKit::BSidebarItem* item = index >= 0
		? dynamic_cast<BAppearanceKit::BSidebarItem*>(ItemAt(index)) : NULL;
	if (item == NULL || !item->IsEnabled() || Window() == NULL)
		return;
	if (item->OutlineLevel() == 0 && CountItemsUnder(item, true) > 0) {
		Select(index);
		ExpandOnly(item);
		BMessage message(kMsgOpenCategory);
		message.AddString("category", item->Id());
		Window()->PostMessage(&message);
		return;
	}
	if (strcmp(item->Id(), "home") == 0) {
		Select(index);
		ExpandOnly(NULL);
		Window()->PostMessage(kMsgOpenHome);
		return;
	}
	BAppearanceKit::BSidebar::MouseDown(where);
	BMessage message(kMsgOpenPage);
	message.AddString("module", item->Id());
	Window()->PostMessage(&message);
}


void
SettingsList::KeyDown(const char* bytes, int32 numBytes)
{
	if (bytes != NULL && numBytes > 0 && bytes[0] == B_TAB) {
		BTextControl* search = Window() != NULL
			? dynamic_cast<BTextControl*>(Window()->FindView("search")) : NULL;
		if (search != NULL && search->TextView() != NULL)
			search->TextView()->MakeFocus(true);
		return;
	}
	if (bytes != NULL && numBytes > 0 && bytes[0] == B_ENTER) {
		int32 index = CurrentSelection();
		BAppearanceKit::BSidebarItem* item = index >= 0
			? dynamic_cast<BAppearanceKit::BSidebarItem*>(ItemAt(index)) : NULL;
		if (item == NULL || Window() == NULL)
			return;
		if (strcmp(item->Id(), "home") == 0)
			Window()->PostMessage(kMsgOpenHome);
		else if (item->OutlineLevel() == 0 && CountItemsUnder(item, true) > 0) {
			BMessage message(kMsgOpenCategory);
			message.AddString("category", item->Id());
			Window()->PostMessage(&message);
		} else {
			BMessage message(kMsgOpenPage);
			message.AddString("module", item->Id());
			Window()->PostMessage(&message);
		}
		return;
	}
	BAppearanceKit::BSidebar::KeyDown(bytes, numBytes);
}


void
SettingsList::UpdateScrollBar()
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


class SearchWatch : public BMessageFilter {
public:
								SearchWatch(BHandler* target)
									:
									BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
									fTarget(target)
								{
								}
	virtual	filter_result		Filter(BMessage* message, BHandler** target)
								{
									(void)target;
									if (message->what == B_KEY_DOWN
										|| message->what == B_PASTE
										|| message->what == B_MOUSE_DOWN) {
										if (fTarget != NULL && fTarget->Looper()
											!= NULL) {
											fTarget->Looper()->PostMessage(
												'shnt', fTarget);
										}
									}
									return B_DISPATCH_MESSAGE;
								}

private:
			BHandler*			fTarget;
};


SearchField::SearchField()
	:
	BTextControl("search", "", "", NULL),
	fHint(NULL),
	fGlass(NULL),
	fWatch(NULL)
{
	SetDivider(0);
	fHint = new SearchHint(TextView());
	AddChild(fHint);
	fGlass = new SearchGlass();
	AddChild(fGlass);
}


void
SearchField::AttachedToWindow()
{
	BTextControl::AttachedToWindow();
	if (TextView() != NULL && fWatch == NULL) {
		fWatch = new SearchWatch(this);
		TextView()->AddFilter(fWatch);
	}
	_SyncHint();
}


void
SearchField::DoLayout()
{
	BTextControl::DoLayout();
	// The text view fills the field inside its frame, so the frame and
	// the text are centred on the u * 34 height.
	if (TextView() != NULL) {
		BRect inner = Bounds().InsetByCopy(2, 2);
		TextView()->MoveTo(inner.LeftTop());
		TextView()->ResizeTo(inner.Width(), inner.Height());
	}
	_SyncHint();
}


void
SearchField::FrameResized(float width, float height)
{
	BTextControl::FrameResized(width, height);
	_SyncHint();
}


void
SearchField::MessageReceived(BMessage* message)
{
	if (message->what == 'shnt') {
		_SyncHint();
		return;
	}
	BTextControl::MessageReceived(message);
}


void
SearchField::_SyncHint()
{
	if (fHint == NULL || fGlass == NULL || TextView() == NULL)
		return;
	float u = be_plain_font->Size() / 13.0f;
	float left = u * 30.0f;
	BRect text = TextView()->Frame();
	// Room for the magnifier. The typed text sits on the same baseline as
	// the hint: the capitals centred in the field.
	BFont font;
	font_height metrics;
	TextView()->GetFontAndColor(0, &font);
	font.GetHeight(&metrics);
	float top = search_baseline(font, text.Height()) - ceilf(metrics.ascent);
	if (top < 0)
		top = 0;
	float oldLeft, oldTop, oldRight, oldBottom;
	TextView()->GetInsets(&oldLeft, &oldTop, &oldRight, &oldBottom);
	if (oldLeft != left || oldTop != top)
		TextView()->SetInsets(left, top, u * 6.0f, 0);
	fGlass->MoveTo(text.left, text.top);
	fGlass->ResizeTo(left - u * 2.0f, text.Height());
	bool empty = Text() == NULL || Text()[0] == '\0';
	fHint->MoveTo(text.left + left, text.top);
	fHint->ResizeTo(text.Width() - left, text.Height());
	if (empty && fHint->IsHidden())
		fHint->Show();
	else if (!empty && !fHint->IsHidden())
		fHint->Hide();
}


void
SearchField::KeyDown(const char* bytes, int32 numBytes)
{
	if (bytes != NULL && numBytes > 0 && bytes[0] == B_TAB && Window() != NULL) {
		BView* deck = Window()->FindView("deck");
		BView* page = deck != NULL ? deck->ChildAt(0) : NULL;
		for (int32 i = 0; deck != NULL && i < deck->CountChildren(); i++) {
			if (!deck->ChildAt(i)->IsHidden())
				page = deck->ChildAt(i);
		}
		BView* next = first_navigable(page);
		if (next != NULL) {
			next->MakeFocus(true);
			return;
		}
	}
	BTextControl::KeyDown(bytes, numBytes);
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
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
			void				ScrollToValue(float value);

private:
			float				_ContentHeight(float width);
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
PageScroller::GetPreferredSize(float* width, float* height)
{
	if (width != NULL)
		*width = 360;
	if (height != NULL)
		*height = 240;
}


BSize
PageScroller::MinSize()
{
	// The cards must not set the window's minimum size.
	return BSize(280, 80);
}


BSize
PageScroller::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


void
PageScroller::ScrollToValue(float value)
{
	if (fContent == NULL || fScrolling)
		return;
	fContent->MoveTo(0, -value);
}


float
PageScroller::_ContentHeight(float width)
{
	if (fContent != NULL && fContent->HasHeightForWidth()) {
		float minHeight = 0;
		float maxHeight = 0;
		float preferred = 0;
		fContent->GetHeightForWidth(width, &minHeight, &maxHeight, &preferred);
	// An unlimited maximum must not become the page height.
	if (preferred > 1 && preferred < 100000)
		return preferred;
	if (minHeight > 1 && minHeight < 100000)
		return minHeight;
}
	float preferred = fContent != NULL ? fContent->PreferredSize().height : 0;
	if (preferred < 1)
		preferred = Bounds().Height();
	return preferred;
}


void
PageScroller::DoLayout()
{
	if (fContent == NULL || fBar == NULL || fScrolling)
		return;

	fScrolling = true;
	// Size the page before laying it out. A layout at the old height
	// leaves a tab body at zero. Height follows the real width, so a
	// card is not measured as if it were only 280 wide.
	float width = Bounds().Width();
	float height = Bounds().Height();
	float barWidth = fBar->PreferredSize().width;
	float contentWidth = width;
	float contentHeight = _ContentHeight(contentWidth);
	bool overflow = contentHeight > height + 1.0f;
	if (overflow) {
		contentWidth = width - barWidth - 1.0f;
		if (contentWidth < 0)
			contentWidth = 0;
		contentHeight = _ContentHeight(contentWidth);
		overflow = contentHeight > height + 1.0f;
	}
	if (contentHeight < 1 || !overflow)
		contentHeight = height;

	float value = fBar->Value();
	float maxValue = 0;
	if (overflow)
		maxValue = contentHeight - height;
	if (value > maxValue)
		value = maxValue;
	if (value < 0)
		value = 0;

	fContent->MoveTo(0, overflow ? -value : 0);
	fContent->ResizeTo(contentWidth, contentHeight);
	fContent->InvalidateLayout(true);
	fContent->Layout(true);
	if (overflow) {
		if (fBar->IsHidden())
			fBar->Show();
		fBar->MoveTo(width - barWidth, 0);
		fBar->ResizeTo(barWidth, height);
		fBar->SetRange(0, maxValue);
		fBar->SetSteps(20, height);
		if (contentHeight > 0)
			fBar->SetProportion(height / contentHeight);
		if (fBar->Value() != value)
			fBar->SetValue(value);
	} else {
		fBar->SetRange(0, 0);
		if (!fBar->IsHidden())
			fBar->Hide();
	}
	fScrolling = false;
}


SettingsWindow::SettingsWindow(SettingsManager* manager,
	SettingsStorage* storage)
	:
	BWindow(BRect(80, 60, 1109, 669), "Settings", B_TITLED_WINDOW,
		B_QUIT_ON_WINDOW_CLOSE),
	fManager(manager),
	fStorage(storage),
	fContext(NULL),
	fSearch(NULL),
	fList(NULL),
	fHeader(NULL),
	fHomeView(NULL),
	fTip(NULL),
	fCards(NULL),
	fDeck(NULL),
	fRevert(NULL),
	fDefaults(NULL),
	fCurrent(NULL),
	fAdjusting(false),
	fLastApplyError(B_OK),
	fLock(NULL),
	fLockRunner(NULL),
	fLockAdmin(false),
	fLockOpen(false),
	fLockButtonHeight(0)
{
	fContext = new SettingsHost(fStorage, BMessenger(this));

	fSearch = new SearchField();
	fSearch->SetModificationMessage(new BMessage(kMsgSearch));

	fList = new SettingsList();
	BScrollView* scroll = new BScrollView("nav-scroll", fList, 0, false, true,
		B_PLAIN_BORDER);

	fLock = new BButton("lock", NULL, new BMessage(kMsgLockButton));
	fHeader = new BAppearanceKit::BPageHeader("heading");

	fDeck = new BView("deck", 0);
	fCards = new BCardLayout();
	fDeck->SetLayout(fCards);
	fDeck->SetExplicitMinSize(BSize(560, 160));

	fRevert = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fDefaults = new BButton("defaults", "Defaults", new BMessage(kMsgDefaults));

	// The list is 200 wide. Search sits under it. The lock, Revert,
	// and Defaults sit on the bottom row of the right column. Both
	// columns leave u*12 above their bottom row, so the list and the
	// page end on the same line, as in the mockup.
	float u = be_plain_font->Size() / 13.0f;
	const float sidebarWidth = 200;
	scroll->SetExplicitMinSize(BSize(sidebarWidth, 160));
	scroll->SetExplicitMaxSize(BSize(sidebarWidth, B_SIZE_UNLIMITED));
	fSearch->SetExplicitMinSize(BSize(sidebarWidth, u * 34.0f));
	fSearch->SetExplicitMaxSize(BSize(sidebarWidth, u * 34.0f));
	fSearch->SetExplicitPreferredSize(BSize(sidebarWidth, u * 34.0f));

	fDeck->SetExplicitMinSize(BSize(560, 160));
	BLayoutBuilder::Group<>(this, B_VERTICAL, u * 12.0f)
		.SetInsets(u * 12.0f)
		.AddGroup(B_HORIZONTAL, u * 14.0f, 1)
			.AddGroup(B_VERTICAL, u * 12.0f, 1)
				.Add(scroll, 1)
				.Add(fSearch)
				.End()
			.AddGroup(B_VERTICAL, u * 12.0f, 1)
				.Add(fHeader)
				.Add(fDeck, 1)
				.AddGroup(B_HORIZONTAL, u * 6.0f, 0)
					.Add(fLock)
					.AddGlue()
					.Add(fRevert)
					.Add(fDefaults)
					.End()
				.End()
			.End();
	for (int32 i = 0; i < CountChildren(); i++)
		r5_settings_apply_plain(ChildAt(i));
	if (fSearch->TextView() != NULL)
		fSearch->TextView()->SetFontAndColor(r5_settings_plain_font());

	float buttonHeight = u * 34.0f;
	fLockButtonHeight = buttonHeight;
	r5_button_set_lock(fLock, true, buttonHeight);
	fLock->SetExplicitMinSize(BSize(u * 38.0f, buttonHeight));
	fLock->SetExplicitMaxSize(BSize(u * 38.0f, buttonHeight));
	fLock->SetExplicitPreferredSize(BSize(u * 38.0f, buttonHeight));
	fRevert->SetExplicitMinSize(BSize(B_SIZE_UNSET, buttonHeight));
	fRevert->SetExplicitMaxSize(BSize(B_SIZE_UNSET, buttonHeight));
	fDefaults->SetExplicitMinSize(BSize(B_SIZE_UNSET, buttonHeight));
	fDefaults->SetExplicitMaxSize(BSize(B_SIZE_UNSET, buttonHeight));
	fLock->SetToolTip("Unlock");
	_ReloadLock(false);
	fLockRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgLockTick), 2000000);

	fRevert->SetEnabled(false);
	fDefaults->SetEnabled(false);
	// Opens at 1030 by 610. The list is 200. A home card is at
	// least 280. 910 is the shortest width. 560 is the shortest
	// height.
	SetSizeLimits(910, 4096, 560, 4096);
	ResizeTo(1030, 610);
	CenterOnScreen();
}


SettingsWindow::~SettingsWindow()
{
	delete fLockRunner;
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
	fHomeView = new HomeView(true);
	float side = be_plain_font->Size() / 13.0f * 44.0f;
	for (uint32 i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); i++) {
		SettingsModule* modules[16];
		int32 count = modules_in_category(fManager, kCategories[i].id,
			modules, 16);
		if (count == 0)
			continue;
		BMessage* message = new BMessage(kMsgOpenCategory);
		message->AddString("category", kCategories[i].id);
		BAppearanceKit::BActionCard* card
			= new BAppearanceKit::BActionCard(kCategories[i].id, message);
		card->SetTitle(kCategories[i].name);
		card->SetSummary(kCategories[i].summary);
		card->SetTagColor(ui_color(kCategories[i].tint));
		BBitmap* icon = r5_mime_icon_bitmap(kCategories[i].iconSignature, side);
		if (icon != NULL)
			r5_settings_apply_prefs_overlay(icon);
		card->SetIcon(icon);
		delete icon;
		BMessage* tags = new BMessage(kMsgOpenPage);
		tags->AddString("category", kCategories[i].id);
		card->SetTagMessage(tags);
		for (int32 page = 0; page < count; page++)
			card->AddTag(modules[page]->Name());
		card->SetTarget(this);
		fHomeView->AddCard(card);
	}
	fCards->AddView(new PageScroller(fHomeView));
	_ShowRecent();
	_BuildSidebar();
	_ShowHome();
	SetSizeLimits(910, 4096, 560, 4096);
	ResizeTo(1030, 610);
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSearch:
		{
			fQuery = fSearch->Text();
			fList->SetFilter(fQuery.String());
			_UpdateTip();
			SettingsList* list = dynamic_cast<SettingsList*>(fList);
			if (list != NULL)
				list->UpdateScrollBar();
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
			if (message->FindString("module", &moduleId) == B_OK) {
				_ShowModule(moduleId);
				break;
			}
			int32 tag = -1;
			const char* category = NULL;
			if (message->FindInt32("be:tag", &tag) == B_OK
				&& message->FindString("category", &category) == B_OK) {
				SettingsModule* modules[16];
				int32 count = modules_in_category(fManager, category,
					modules, 16);
				if (tag >= 0 && tag < count)
					_ShowModule(modules[tag]->Identifier());
			}
			break;
		}
		case kMsgRevert:
			if (fCurrent != NULL && fCurrent->RequiresAdmin() && !fLockOpen)
				break;
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
			if (fCurrent != NULL && fCurrent->RequiresAdmin() && !fLockOpen)
				break;
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
		case kMsgLockButton:
			if (fLockAdmin)
				break;
			if (fLockOpen)
				r5_admin_relock();
			else
				r5_admin_prompt_unlock(BMessenger(this));
			_ReloadLock(false);
			break;
		case kMsgLockTick:
		case kMsgAdminLockChanged:
			_ReloadLock(false);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


static bool
module_before(const SettingsModule* left, const SettingsModule* right)
{
	// Information stays first under System. Every other page is
	// alphabetical by its name, in every category. Do not change this
	// order unless the user explicitly asks. See AGENTS.md.
	bool leftInfo = left->Identifier() != NULL
		&& strcmp(left->Identifier(), "information") == 0;
	bool rightInfo = right->Identifier() != NULL
		&& strcmp(right->Identifier(), "information") == 0;
	if (leftInfo != rightInfo)
		return leftInfo;
	BString leftName(left->Name() != NULL ? left->Name() : "");
	BString rightName(right->Name() != NULL ? right->Name() : "");
	int compare = leftName.ICompare(rightName);
	if (compare != 0)
		return compare < 0;
	BString leftId(left->Identifier() != NULL ? left->Identifier() : "");
	BString rightId(right->Identifier() != NULL ? right->Identifier() : "");
	return leftId.ICompare(rightId) < 0;
}


static int32
modules_in_category(SettingsManager* manager, const char* categoryId,
	SettingsModule** out, int32 maxOut)
{
	int32 count = 0;
	if (manager == NULL || categoryId == NULL || out == NULL || maxOut <= 0)
		return 0;
	for (int32 i = 0; i < manager->Count(); i++) {
		SettingsModule* module = manager->ModuleAt(i);
		if (module == NULL || module->Category() == NULL
			|| strcmp(module->Category(), categoryId) != 0)
			continue;
		if (count >= maxOut)
			continue;
		int32 at = count;
		while (at > 0 && module_before(module, out[at - 1])) {
			out[at] = out[at - 1];
			at--;
		}
		out[at] = module;
		count++;
	}
	return count;
}


void
SettingsWindow::_BuildSidebar()
{
	fAdjusting = true;
	float side = be_plain_font->Size() / 13.0f * 24.0f;
	BBitmap* pebble = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble),
		side);
	BAppearanceKit::BSidebarItem* home
		= new BAppearanceKit::BSidebarItem("Home", "home", pebble);
	home->SetEmphasized(true);
	delete pebble;
	fList->AddItem(home);
	fList->AddItem(new BAppearanceKit::BSidebarItem("", ""));
	for (uint32 c = 0; c < sizeof(kCategories) / sizeof(kCategories[0]); c++) {
		SettingsModule* modules[64];
		int32 count = modules_in_category(fManager, kCategories[c].id,
			modules, 64);
		if (count == 0)
			continue;
		BBitmap* icon = r5_mime_icon_bitmap(kCategories[c].iconSignature, side);
		BAppearanceKit::BSidebarItem* section
			= new BAppearanceKit::BSidebarItem(kCategories[c].name,
				kCategories[c].id, icon);
		delete icon;
		section->SetCount(count);
		fList->AddItem(section);
		for (int32 i = count - 1; i >= 0; i--) {
			BString keywords(modules[i]->Description() != NULL
				? modules[i]->Description() : "");
			keywords << " "
				<< (modules[i]->Keywords() != NULL
					? modules[i]->Keywords() : "")
				<< " " << kCategories[c].name;
			BAppearanceKit::BSidebarItem* page
				= new BAppearanceKit::BSidebarItem(modules[i]->Name(),
					modules[i]->Identifier());
			page->SetKeywords(keywords.String());
			if (modules[i]->RequiresAdmin())
				page->SetBadge(BAppearanceKit::B_SIDEBAR_BADGE_LOCKED);
			// AddUnder inserts each page directly under the category,
			// so adding from the last page leaves Information first.
			fList->AddUnder(page, section);
		}
		fList->Collapse(section);
	}
	fAdjusting = false;
	SettingsList* list = dynamic_cast<SettingsList*>(fList);
	if (list != NULL)
		list->UpdateScrollBar();
}


void
SettingsWindow::_ShowHome()
{
	fCurrent = NULL;
	fCards->SetVisibleItem((int32)0);
	float side = be_plain_font->Size() / 13.0f * 48.0f;
	BBitmap* icon = NULL;
	size_t bytes = 0;
	const void* data = be_app->AppResources() != NULL
		? be_app->AppResources()->LoadResource(B_VECTOR_ICON_TYPE,
			"BEOS:ICON", &bytes) : NULL;
	if (data != NULL)
		icon = r5_vector_icon_bitmap(static_cast<const uint8*>(data), bytes,
			side);
	if (icon == NULL) {
		icon = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble), side);
		if (icon != NULL)
			r5_settings_apply_prefs_overlay(icon);
	}
	_SetHeader(NULL, "Settings",
		"Choose a category, or search for a setting.", icon);
	delete icon;
	fList->ExpandOnly(NULL);
	fList->SelectId("home");
	_ShowRecent();
	_UpdateActions();
	_ApplyPageLock();
	_UpdateTip();
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
	const char* signature = NULL;
	for (uint32 i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); i++) {
		if (strcmp(kCategories[i].id, categoryId) == 0)
			signature = kCategories[i].iconSignature;
	}
	float side = be_plain_font->Size() / 13.0f * 48.0f;
	BBitmap* icon = r5_mime_icon_bitmap(signature, side);
	if (icon != NULL)
		r5_settings_apply_prefs_overlay(icon);
	_SetHeader(NULL, _CategoryName(categoryId),
		_CategorySummary(categoryId), icon);
	delete icon;
	fList->SelectId(categoryId);
	_UpdateActions();
	_ApplyPageLock();
	_UpdateTip();
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
	float side = be_plain_font->Size() / 13.0f * 48.0f;
	BBitmap* icon = r5_mime_icon_bitmap(module_icon(module->Identifier()),
		side);
	if (icon != NULL)
		r5_settings_apply_prefs_overlay(icon);
	_SetHeader(_CategoryName(module->Category()), module->Name(),
		module->Description(), icon);
	delete icon;
	fList->SelectId(module->Identifier());
	_UpdateActions();
	_ApplyPageLock();
	_UpdateTip();
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
		_NoteRecent(fCurrent->Identifier());
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
SettingsWindow::_SetHeader(const char* eyebrow, const char* title,
	const char* description, const BBitmap* icon)
{
	if (fHeader == NULL)
		return;
	fHeader->SetEyebrow(eyebrow);
	fHeader->SetTitle(title);
	fHeader->SetSubtitle(description);
	fHeader->SetIcon(icon);
}


void
SettingsWindow::_UpdateTip()
{
	if (fList == NULL)
		return;
	bool folded = fQuery.Length() == 0;
	for (int32 i = 0; folded && i < fList->CountItems(); i++) {
		BAppearanceKit::BSidebarItem* item
			= dynamic_cast<BAppearanceKit::BSidebarItem*>(fList->ItemAt(i));
		if (item != NULL && item->OutlineLevel() == 0 && item->IsExpanded()
			&& fList->CountItemsUnder(item, true) > 0)
			folded = false;
	}
	fList->SetFooter(folded
		? "type in Search to jump straight to any page." : "");
}


void
SettingsWindow::_NoteRecent(const char* moduleId)
{
	if (fStorage == NULL || moduleId == NULL || moduleId[0] == '\0')
		return;
	BMessage store;
	fStorage->Read("settings-window", store);
	BMessage next;
	next.AddString("recent", moduleId);
	const char* older = NULL;
	int32 kept = 1;
	for (int32 i = 0; store.FindString("recent", i, &older) == B_OK; i++) {
		if (older == NULL || strcmp(older, moduleId) == 0)
			continue;
		if (kept >= 3)
			break;
		next.AddString("recent", older);
		kept++;
	}
	fStorage->Write("settings-window", next);
	_ShowRecent();
}


void
SettingsWindow::_ShowRecent()
{
	if (fHomeView == NULL || fStorage == NULL)
		return;
	BMessage store;
	fStorage->Read("settings-window", store);
	BString names[3];
	BString ids[3];
	int32 count = 0;
	const char* id = NULL;
	for (int32 i = 0; store.FindString("recent", i, &id) == B_OK; i++) {
		SettingsModule* module = fManager->Find(id);
		if (module == NULL)
			continue;
		if (count >= 3)
			break;
		ids[count] = id;
		names[count] = module->Name();
		count++;
	}
	fHomeView->SetRecent(names, ids, count);
}


void
SettingsWindow::_UpdateActions()
{
	bool locked = fCurrent != NULL && fCurrent->RequiresAdmin() && !fLockOpen;
	bool can = fCurrent != NULL && fCurrent->CanSave() && !locked;
	fRevert->SetEnabled(can && fCurrent->IsModified());
	fDefaults->SetEnabled(!locked && fCurrent != NULL
		&& fCurrent->CanApplyDefaults());
}


void
SettingsWindow::_ReloadLock(bool pageChanged)
{
	bool administrator = false;
	bool unlocked = false;
	r5_admin_query(administrator, unlocked);
	bool changed = administrator != fLockAdmin || unlocked != fLockOpen;
	fLockAdmin = administrator;
	fLockOpen = unlocked;
	if (fLock != NULL) {
		r5_button_set_lock(fLock, !fLockOpen, fLockButtonHeight);
		if (fLockAdmin)
			fLock->SetToolTip("Administrator");
		else if (fLockOpen)
			fLock->SetToolTip("Lock");
		else
			fLock->SetToolTip("Unlock");
	}
	if (changed || pageChanged) {
		_ApplyPageLock();
		_UpdateActions();
	}
}


void
SettingsWindow::_ApplyPageLock()
{
	_RestoreLockedControls();
	BView* content = _PageContent();
	if (content != NULL) {
		BMessage refresh(kMsgRefreshPage);
		content->MessageReceived(&refresh);
	}
	if (fCurrent == NULL || fLockOpen || !fCurrent->RequiresAdmin())
		return;
	_DisableLocked(content);
}


void
SettingsWindow::_RestoreLockedControls()
{
	for (int32 i = 0; i < fLockedControls.CountItems(); i++) {
		BControl* control = static_cast<BControl*>(
			fLockedControls.ItemAt(i));
		if (control != NULL)
			control->SetEnabled(true);
	}
	fLockedControls.MakeEmpty();
}


void
SettingsWindow::_DisableLocked(BView* view)
{
	if (view == NULL)
		return;
	BControl* control = dynamic_cast<BControl*>(view);
	if (control != NULL && dynamic_cast<BScrollBar*>(control) == NULL
		&& control->IsEnabled() && !fLockedControls.HasItem(control)) {
		control->SetEnabled(false);
		fLockedControls.AddItem(control);
	}
	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++)
		_DisableLocked(view->ChildAt(i));
}


BView*
SettingsWindow::_PageContent() const
{
	if (fCurrent == NULL || fDeck == NULL)
		return NULL;
	BString name("module:");
	name << fCurrent->Identifier();
	BView* scroller = fDeck->FindView(name.String());
	if (scroller == NULL)
		return NULL;
	for (int32 i = 0; i < scroller->CountChildren(); i++) {
		BView* child = scroller->ChildAt(i);
		if (dynamic_cast<BScrollBar*>(child) == NULL)
			return child;
	}
	return NULL;
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
		{ "fonts", "application/x-vnd.Haiku-Appearance" },
		{ "look", "application/x-vnd.Haiku-Appearance" },
		{ "deskbar", "application/x-vnd.Haiku-DeskbarPreferences" },
		{ "wallpaper", "application/x-vnd.Haiku-Backgrounds" },
		{ "information", "application/x-vnd.Haiku-About" },
		{ "notifications", "application/x-vnd.Haiku-Notifications" },
		{ "virtualmemory", "application/x-vnd.Haiku-VirtualMemory" },
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
	HomeView* home = new HomeView(false);
	SettingsModule* modules[16];
	int32 count = modules_in_category(fManager, categoryId, modules, 16);
	float side = be_plain_font->Size() / 13.0f * 44.0f;
	for (int32 i = 0; i < count; i++) {
		BMessage* message = new BMessage(kMsgOpenPage);
		message->AddString("module", modules[i]->Identifier());
		BAppearanceKit::BActionCard* card
			= new BAppearanceKit::BActionCard(modules[i]->Identifier(),
				message);
		card->SetTitle(modules[i]->Name());
		card->SetSummary(modules[i]->Description());
		BBitmap* icon = r5_mime_icon_bitmap(
			module_icon(modules[i]->Identifier()), side);
		if (icon != NULL)
			r5_settings_apply_prefs_overlay(icon);
		card->SetIcon(icon);
		delete icon;
		card->SetTarget(this);
		home->AddCard(card);
	}
	return new PageScroller(home);
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
	if (moduleId == NULL)
		fList->SelectId("home");
	else
		fList->SelectId(moduleId);
	fAdjusting = false;
}
