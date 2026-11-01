/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "SettingsWindow.h"

#include "HomeView.h"
#include "SettingsHost.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CardLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdio.h>
#include <string.h>


struct SettingsCategory {
	const char* id;
	const char* name;
	const char* summary;
	const char* iconSignature;
};


static const SettingsCategory kCategories[] = {
	{ "personalization", "Personalization",
		"Appearance, the desktop, and wallpaper.",
		"application/x-vnd.Haiku-Appearance" },
	{ "system", "System", "Display and sound.",
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
	owner->SetFont(be_bold_font);
	font_height height;
	be_bold_font->GetHeight(&height);
	owner->DrawString(Text(),
		BPoint(frame.left + 8, frame.bottom - height.descent - 2));
	owner->SetFont(be_plain_font);
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
	fCards(NULL),
	fDeck(NULL),
	fBack(NULL),
	fRevert(NULL),
	fSave(NULL),
	fCurrent(NULL),
	fAdjusting(false)
{
	fContext = new SettingsHost(fStorage, BMessenger(this));

	fSearch = new BTextControl("search", "Search:", "", NULL);
	fSearch->SetModificationMessage(new BMessage(kMsgSearch));

	fList = new BListView("nav");
	fList->SetSelectionMessage(new BMessage(kMsgOpenPage));
	BScrollView* scroll = new BScrollView("nav-scroll", fList, 0, false, true);

	fBack = new BButton("back", "Back", new BMessage(kMsgGoBack));
	fTitle = new BStringView("title", "Settings");
	fDescription = new BStringView("description",
		"One place for the computer's settings.");
	BFont titleFont(be_bold_font);
	titleFont.SetSize(be_bold_font->Size() + 4);
	fTitle->SetFont(&titleFont);

	fDeck = new BView("deck", 0);
	fCards = new BCardLayout();
	fDeck->SetLayout(fCards);
	fDeck->SetExplicitMinSize(BSize(360, 240));

	fRevert = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fSave = new BButton("save", "Save", new BMessage(kMsgSave));

	// Fixed width. A flexible sidebar grows when the page's preferred
	// width shrinks, which is the jump between Home and a page.
	const float sidebarWidth = 248;
	scroll->SetExplicitMinSize(BSize(sidebarWidth - 16, 160));
	scroll->SetExplicitMaxSize(BSize(sidebarWidth - 16, B_SIZE_UNLIMITED));
	BGroupView* sidebar = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	sidebar->SetExplicitMinSize(BSize(sidebarWidth, B_SIZE_UNSET));
	sidebar->SetExplicitMaxSize(BSize(sidebarWidth, B_SIZE_UNLIMITED));
	BLayoutBuilder::Group<>(sidebar)
		.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING,
			B_USE_SMALL_SPACING, B_USE_WINDOW_SPACING)
		.Add(scroll, 1)
		.Add(fSearch)
		.Add(new BButton("home", "Home", new BMessage(kMsgOpenHome)));

	fDeck->SetExplicitMinSize(BSize(760, 280));
	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.Add(sidebar, 0)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(fTitle)
			.Add(fDescription)
			.Add(fDeck)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.AddGlue()
				.Add(fBack)
				.Add(fRevert)
				.Add(fSave)
			.End()
		.End();

	fBack->SetEnabled(false);
	fRevert->SetEnabled(false);
	fSave->SetEnabled(false);
	SetSizeLimits(980, 4096, 560, 4096);
	ResizeTo(1100, 680);
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
	if (!_ConfirmLeave())
		return false;
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
		count++;
	}
	if (count == 0) {
		items[0].id = "";
		items[0].name = "No pages";
		items[0].summary = "No settings modules were found.";
		items[0].iconSignature = NULL;
		count = 1;
	}
	fCards->AddView(new HomeView(items, count));
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
			if (_ConfirmLeave())
				_ShowHome();
			break;
		case kMsgOpenCategory:
		{
			const char* category = NULL;
			if (message->FindString("category", &category) != B_OK)
				break;
			if (_ConfirmLeave())
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
			if (moduleId != NULL && _ConfirmLeave())
				_ShowModule(moduleId);
			break;
		}
		case kMsgGoBack:
		{
			if (!_ConfirmLeave())
				break;
			if (fHistory.CountItems() < 2) {
				_ShowHome();
				break;
			}
			delete static_cast<SettingsPlace*>(
				fHistory.RemoveItem(fHistory.CountItems() - 1));
			SettingsPlace* place = static_cast<SettingsPlace*>(
				fHistory.LastItem());
			delete static_cast<SettingsPlace*>(
				fHistory.RemoveItem(fHistory.CountItems() - 1));
			if (place->category)
				_ShowCategory(place->id.String());
			else if (place->id == "home")
				_ShowHome();
			else
				_ShowModule(place->id.String());
			delete place;
			break;
		}
		case kMsgSave:
		{
			if (fCurrent == NULL || !fCurrent->CanSave())
				break;
			if (fCurrent->RequiresAdmin()
				&& !fContext->AdminChangeAllowed()) {
				BAlert* alert = new BAlert("Settings",
					"An administrator has to unlock this page before it can be saved.",
					"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->Go(NULL);
				break;
			}
			status_t status = fCurrent->SaveSettings(fContext);
			if (status != B_OK) {
				BString text("Could not save ");
				text << fCurrent->Name() << ".\n" << strerror(status);
				BAlert* alert = new BAlert("Settings", text.String(), "OK",
					NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->Go(NULL);
			}
			_UpdateActions();
			break;
		}
		case kMsgRevert:
			if (fCurrent != NULL && fCurrent->CanSave()) {
				fCurrent->RevertSettings(fContext);
				_UpdateActions();
			}
			break;
		case kMsgModuleChanged:
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
}


void
SettingsWindow::_ShowHome()
{
	fCurrent = NULL;
	fCards->SetVisibleItem((int32)0);
	_SetHeader("Settings", "One place for the computer's settings.");
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
	_SetHeader(_CategoryName(categoryId), _CategorySummary(categoryId));
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
	_SetHeader(module->Name(), module->Description());
	_Remember(module->Identifier(), false);
	_SelectSidebar(module->Identifier());
	_UpdateActions();
}


bool
SettingsWindow::_ConfirmLeave()
{
	if (fCurrent == NULL || !fCurrent->IsModified())
		return true;
	BAlert* alert = new BAlert("Settings",
		"This page has changes that are not saved.",
		"Cancel", "Revert", "Save", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	int32 choice = alert->Go();
	if (choice == 0)
		return false;
	if (choice == 2) {
		if (fCurrent->RequiresAdmin() && !fContext->AdminChangeAllowed())
			return false;
		if (fCurrent->SaveSettings(fContext) != B_OK)
			return false;
	} else
		fCurrent->RevertSettings(fContext);
	return true;
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
SettingsWindow::_SetHeader(const char* title, const char* description)
{
	fTitle->SetText(title != NULL ? title : "");
	fDescription->SetText(description != NULL ? description : "");
}


void
SettingsWindow::_UpdateActions()
{
	bool can = fCurrent != NULL && fCurrent->CanSave();
	bool dirty = can && fCurrent->IsModified();
	fSave->SetEnabled(dirty);
	fRevert->SetEnabled(dirty);
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
	view->SetName(name.String());
	fCards->AddView(view);
	return view;
}


BView*
SettingsWindow::_CategoryView(const char* categoryId, const char* name)
{
	(void)name;
	BGroupView* group = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BStringView* summary = new BStringView("summary",
		_CategorySummary(categoryId));
	group->AddChild(summary);
	for (int32 i = 0; i < fManager->Count(); i++) {
		SettingsModule* module = fManager->ModuleAt(i);
		if (module == NULL || strcmp(module->Category(), categoryId) != 0)
			continue;
		BMessage* message = new BMessage(kMsgOpenPage);
		message->AddString("module", module->Identifier());
		group->AddChild(new BButton(module->Identifier(), module->Name(),
			message));
	}
	group->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	return group;
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
