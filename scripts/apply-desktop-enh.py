#!/usr/bin/env python3
"""Desktop enhancements overlay:
#2136 #5000 #4276 #4626 #2542 #16245 Deskbar
#3732 #3171 #4216 #4793 #5930 Tracker
#3813 Screenshot
#4089 #3505 app_server
#3677 Appearance tabs
#2701 pointer size
"""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def patch_deskbar() -> None:
    replace_once(
        HAIKU / "src/apps/deskbar/BarSettings.h",
        """	bool autoHide;
};
""",
        """	bool autoHide;
};

const uint32 kShowDesktop = 'ShDt';
const uint32 kCheckHungApp = 'HgAp';
const uint32 kForceQuitTeam = 'FrQt';
""",
        "Haiku #2136 #16245 message ids",
    )

    replace_once(
        HAIKU / "src/apps/deskbar/DeskbarMenu.cpp",
        """	item = new BMenuItem(B_TRANSLATE("Show replicants"),
		new BMessage(kToggleDraggers));
	item->SetEnabled(!dragging);
	item->SetMarked(BDragger::AreDraggersDrawn());
	AddItem(item);
""",
        """	item = new BMenuItem(B_TRANSLATE("Show replicants"),
		new BMessage(kToggleDraggers));
	item->SetEnabled(!dragging);
	item->SetMarked(BDragger::AreDraggersDrawn());
	AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Show desktop"),
		new BMessage(kShowDesktop));
	item->SetEnabled(!dragging);
	AddItem(item);
""",
        "Haiku #2136 show desktop item",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/DeskbarMenu.cpp",
        """				case kShowSplash:
				case kToggleDraggers:
				case kConfigShow:
""",
        """				case kShowSplash:
				case kShowDesktop:
				case kToggleDraggers:
				case kConfigShow:
""",
        "Haiku #2136 reset target",
    )

    replace_once(
        HAIKU / "src/apps/deskbar/BarApp.cpp",
        """#include <Roster.h>
""",
        """#include <Alert.h>
#include <OS.h>
#include <Roster.h>
#include <String.h>
#include <WindowInfo.h>
""",
        "Haiku #2136 BarApp includes",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/BarApp.cpp",
        """#include "StatusView.h"
#include "Switcher.h"
""",
        """#include "BarSettings.h"
#include "ShowHideMenuItem.h"
#include "StatusView.h"
#include "Switcher.h"
""",
        "Haiku #2136 BarApp local includes",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/BarApp.cpp",
        """#include "icons.h"


BLocker TBarApp::sSubscriberLock;
""",
        """#include "icons.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BarApp"


BLocker TBarApp::sSubscriberLock;
""",
        "Haiku #16245 BarApp catalog",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/BarApp.cpp",
        """		case kShowSplash:
			run_be_about();
			break;
""",
        """		case kShowSplash:
			run_be_about();
			break;

		case kShowDesktop:
		{
			BList teams;
			be_roster->GetAppList(&teams);
			BRect zoom(0, 0, 0, 0);
			for (int32 i = 0; i < teams.CountItems(); i++) {
				team_id team = (addr_t)teams.ItemAt(i);
				app_info info;
				if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
					continue;
				if (strcasecmp(info.signature, kTrackerSignature) == 0
					|| strcasecmp(info.signature, kDeskbarSignature) == 0)
					continue;
				do_minimize_team(zoom, team, false);
			}
			break;
		}

		case kCheckHungApp:
		{
			team_id team;
			if (message->FindInt32("team", &team) != B_OK)
				break;
			team_info info;
			if (get_team_info(team, &info) != B_OK)
				break;
			const char* name;
			if (message->FindString("name", &name) != B_OK)
				name = info.args;
			BString text(B_TRANSLATE("%name is not responding."));
			text.ReplaceFirst("%name", name);
			BAlert* alert = new BAlert("unresponsive", text.String(),
				B_TRANSLATE("Wait"), B_TRANSLATE("Force quit"), NULL,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 1)
				kill_team(team);
			break;
		}
""",
        "Haiku #2136 #16245 BarApp handlers",
    )

    replace_once(
        HAIKU / "src/apps/deskbar/ShowHideMenuItem.cpp",
        """#include <Debug.h>
#include <Roster.h>
#include <WindowInfo.h>
""",
        """#include <Application.h>
#include <Debug.h>
#include <MessageRunner.h>
#include <Roster.h>
#include <WindowInfo.h>

#include "BarSettings.h"
""",
        "Haiku #16245 ShowHide includes",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/ShowHideMenuItem.cpp",
        """				messenger.SendMessage(command);
				break;
			}
""",
        """				messenger.SendMessage(command);
				BMessage check(kCheckHungApp);
				check.AddInt32("team", team);
				check.AddString("name", aInfo.ref.name);
				BMessageRunner::StartSending(be_app, &check, 4000000, 1);
				break;
			}
""",
        "Haiku #16245 hung-app ping",
    )

    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenuItem.h",
        """			bool				HandleMouseDown(BPoint where);
			status_t			Invoke(BMessage* message = NULL);
""",
        """			bool				HandleMouseDown(BPoint where);
			bool				HasListedWindows() const;
			status_t			Invoke(BMessage* message = NULL);
""",
        "Haiku #4276 HasListedWindows",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenuItem.cpp",
        """#include <Bitmap.h>
#include <ControlLook.h>
#include <Debug.h>
#include <Font.h>
#include <Mime.h>
#include <Region.h>
#include <Roster.h>
""",
        """#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Debug.h>
#include <Font.h>
#include <MenuItem.h>
#include <Mime.h>
#include <OS.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Roster.h>
""",
        "Haiku #4626 TeamMenuItem includes",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenuItem.cpp",
        """#include "WindowMenuItem.h"


static float sHPad, sVPad, sLabelOffset = 0.0f;
""",
        """#include "BarSettings.h"
#include "WindowMenuItem.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TeamMenuItem"


static float sHPad, sVPad, sLabelOffset = 0.0f;
""",
        "Haiku #4626 translation context",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenuItem.cpp",
        """	} else if ((modifiers & B_CONTROL_KEY) != 0) {
		// control click - show all/hide all shortcut
		BMessage showMessage((modifiers & B_SHIFT_KEY) != 0
			? kMinimizeTeam : kBringTeamToFront);
		showMessage.AddInt32("itemIndex", menu->IndexOf(this));
		window->PostMessage(&showMessage, menu);
		return true;
	}

	return false;
}
""",
        """	} else if ((modifiers & B_CONTROL_KEY) != 0) {
		// control click - show all/hide all shortcut
		BMessage showMessage((modifiers & B_SHIFT_KEY) != 0
			? kMinimizeTeam : kBringTeamToFront);
		showMessage.AddInt32("itemIndex", menu->IndexOf(this));
		window->PostMessage(&showMessage, menu);
		return true;
	} else if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		BPopUpMenu popup("team");
		popup.AddItem(new BMenuItem(B_TRANSLATE("Hide all"),
			new BMessage(B_MINIMIZE_WINDOW)));
		popup.AddItem(new BMenuItem(B_TRANSLATE("Show all"),
			new BMessage(B_BRING_TO_FRONT)));
		popup.AddItem(new BMenuItem(B_TRANSLATE("Close all"),
			new BMessage(B_QUIT_REQUESTED)));
		popup.AddSeparatorItem();
		popup.AddItem(new BMenuItem(B_TRANSLATE("Force quit"),
			new BMessage(kForceQuitTeam)));
		BMenuItem* chosen = popup.Go(menu->ConvertToScreen(where), true, true);
		if (chosen != NULL && chosen->Message() != NULL) {
			uint32 what = chosen->Message()->what;
			if (what == kForceQuitTeam) {
				int32 teamCount = fTeam->CountItems();
				for (int32 index = 0; index < teamCount; index++)
					kill_team((addr_t)fTeam->ItemAt(index));
			} else {
				TShowHideMenuItem::TeamShowHideCommon((int32)what, fTeam);
			}
		}
		return true;
	}

	return false;
}


bool
TTeamMenuItem::HasListedWindows() const
{
	if (fTeam == NULL)
		return false;

	int32 teamCount = fTeam->CountItems();
	for (int32 i = 0; i < teamCount; i++) {
		team_id team = (addr_t)fTeam->ItemAt(i);
		int32 count = 0;
		int32* tokens = get_token_list(team, &count);
		if (tokens == NULL)
			continue;
		for (int32 j = 0; j < count; j++) {
			client_window_info* info = get_window_info(tokens[j]);
			if (info == NULL)
				continue;
			bool listed = TWindowMenu::WindowShouldBeListed(info);
			free(info);
			if (listed) {
				free(tokens);
				return true;
			}
		}
		free(tokens);
	}
	return false;
}
""",
        "Haiku #4626 #2542 #4276 context and windows",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenuItem.cpp",
        """	// draw expander arrow
	if (fBarView->Vertical()
		&& static_cast<TBarApp*>(be_app)->Settings()->superExpando
		&& fBarView->ExpandoState()) {
		DrawExpanderArrow();
	}
""",
        """	// draw expander arrow
	if (fBarView->Vertical()
		&& static_cast<TBarApp*>(be_app)->Settings()->superExpando
		&& fBarView->ExpandoState()
		&& HasListedWindows()) {
		DrawExpanderArrow();
	}
""",
        "Haiku #4276 hide expander",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenuItem.cpp",
        """BRect
TTeamMenuItem::ExpanderBounds() const
{
	BRect bounds(Frame());
	bounds.left = bounds.right - kSwitchWidth;
	return bounds;
}
""",
        """BRect
TTeamMenuItem::ExpanderBounds() const
{
	if (!HasListedWindows())
		return BRect(0, 0, -1, -1);

	BRect bounds(Frame());
	bounds.left = bounds.right - kSwitchWidth;
	return bounds;
}
""",
        "Haiku #4276 empty expander bounds",
    )

    replace_once(
        HAIKU / "src/apps/deskbar/ExpandoMenuBar.cpp",
        """	// check for three finger salute, a.k.a. Vulcan Death Grip
	if (teamItem->HandleMouseDown(where))
		return;
			// absorb the message

	// check if within expander bounds to expand window items
""",
        """	// check for three finger salute, a.k.a. Vulcan Death Grip
	if (teamItem->HandleMouseDown(where))
		return;
			// absorb the message

	if (Vertical() && static_cast<TBarApp*>(be_app)->Settings()->superExpando
		&& (buttons & B_PRIMARY_MOUSE_BUTTON) != 0) {
		app_info info;
		if (be_roster->GetActiveAppInfo(&info) == B_OK
			&& teamItem->Teams()->HasItem((void*)(addr_t)info.team)) {
			TShowHideMenuItem::TeamShowHideCommon(B_MINIMIZE_WINDOW,
				teamItem->Teams());
			return;
		}
		be_roster->ActivateApp((addr_t)teamItem->Teams()->ItemAt(0));
		return;
	}

	// check if within expander bounds to expand window items
""",
        "Haiku #5000 click hide/activate",
    )

    replace_once(
        HAIKU / "src/apps/deskbar/BarMenuBar.h",
        """	virtual	void			MouseMoved(BPoint where, uint32 code,
								const BMessage* message);
	virtual	void			Draw(BRect);
""",
        """	virtual	void			MouseMoved(BPoint where, uint32 code,
								const BMessage* message);
	virtual	void			MouseDown(BPoint where);
	virtual	void			Draw(BRect);
""",
        "Haiku #5000 BarMenuBar MouseDown",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/BarMenuBar.cpp",
        """#include "BarMenuBar.h"

#include <algorithm>
""",
        """#include "BarMenuBar.h"

#include <algorithm>

#include <Application.h>
""",
        "Haiku #5000 BarMenuBar Application",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/BarMenuBar.cpp",
        """#include "BarMenuTitle.h"
#include "BarView.h"
#include "BarWindow.h"
""",
        """#include "BarApp.h"
#include "BarMenuTitle.h"
#include "BarSettings.h"
#include "BarView.h"
#include "BarWindow.h"
""",
        "Haiku #5000 BarMenuBar settings",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/BarMenuBar.cpp",
        """void
TBarMenuBar::MouseMoved(BPoint where, uint32 code, const BMessage* message)
{
""",
        """void
TBarMenuBar::MouseDown(BPoint where)
{
	BMessage* currentMessage = Window() != NULL
		? Window()->CurrentMessage() : NULL;
	int32 buttons = 0;
	if (currentMessage != NULL)
		currentMessage->FindInt32("buttons", &buttons);
	if (fDeskbarMenuItem != NULL
		&& fDeskbarMenuItem->Frame().Contains(where)
		&& (buttons & B_TERTIARY_MOUSE_BUTTON) != 0) {
		be_app->PostMessage(kShowDesktop);
		return;
	}
	BMenuBar::MouseDown(where);
}


void
TBarMenuBar::MouseMoved(BPoint where, uint32 code, const BMessage* message)
{
""",
        "Haiku #5000 middle-click leaf",
    )


def patch_tracker() -> None:
    replace_once(
        HAIKU / "src/kits/tracker/TextWidget.cpp",
        """	textView->SelectAll();
	textView->ScrollToSelection();
		// scroll to beginning so that text is visible
	textView->MakeFocus();
""",
        """	int32 length = textView->TextLength();
	int32 end = length;
	Model* model = pose->TargetModel();
	if (model != NULL && !model->IsDirectory() && !model->IsQuery()
		&& !model->IsVolume() && view->FirstColumn() != NULL
		&& fAttrHash == view->FirstColumn()->AttrHash()) {
		const char* text = textView->Text();
		if (text != NULL) {
			const char* dot = strrchr(text, '.');
			if (dot != NULL && dot != text)
				end = (int32)(dot - text);
		}
	}
	if (end > 0)
		textView->Select(0, end);
	else
		textView->SelectAll();
	textView->ScrollToSelection();
		// scroll to beginning so that text is visible
	textView->MakeFocus();
""",
        "Haiku #3732 rename without suffix",
    )
    replace_once(
        HAIKU / "src/kits/tracker/TextWidget.cpp",
        """	BTextView* text = dynamic_cast<BTextView*>(view->FindView("WidgetTextView"));
	if (text != NULL)
		text->SelectAll();
""",
        """	BTextView* text = dynamic_cast<BTextView*>(view->FindView("WidgetTextView"));
	if (text == NULL)
		return;
	int32 length = text->TextLength();
	int32 end = length;
	const char* value = text->Text();
	if (view->FirstColumn() != NULL
		&& fAttrHash == view->FirstColumn()->AttrHash() && value != NULL) {
		const char* dot = strrchr(value, '.');
		if (dot != NULL && dot != value)
			end = (int32)(dot - value);
	}
	if (end > 0)
		text->Select(0, end);
	else
		text->SelectAll();
""",
        "Haiku #3732 SelectAll suffix",
    )

    replace_once(
        HAIKU / "src/kits/tracker/FSUtils.cpp",
        """			alert->AddButton(B_TRANSLATE("Skip"));
			alert->AddButton(B_TRANSLATE("Skip all"));
			alert->AddButton(B_TRANSLATE("Replace"));
			alert->AddButton(B_TRANSLATE("Replace all"));
			switch (alert->Go()) {
				case 0:
					conflictResolution = kCanceled;
					return B_ERROR;
				case 1:
					conflictResolution = kSkipAll;
					return B_ERROR;
				case 2:
					conflictResolution = kReplace;
					break;
				case 3:
					conflictResolution = kReplaceAll;
					break;
			}
		} else {
			alert = new BAlert("", replaceMsg.String(),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Replace"));
			alert->SetShortcut(0, B_ESCAPE);
			switch (alert->Go()) {
				case 0:
					conflictResolution = kCanceled;
					return B_ERROR;
				case 1:
					conflictResolution = kReplace;
					break;
			}
		}
""",
        """			alert->AddButton(B_TRANSLATE("Skip"));
			alert->AddButton(B_TRANSLATE("Skip all"));
			alert->AddButton(B_TRANSLATE("Replace"));
			alert->AddButton(B_TRANSLATE("Replace all"));
			alert->AddButton(B_TRANSLATE("Cancel"));
			alert->SetShortcut(4, B_ESCAPE);
			switch (alert->Go()) {
				case 0:
					return B_ERROR;
				case 1:
					conflictResolution = kSkipAll;
					return B_ERROR;
				case 2:
					conflictResolution = kReplace;
					break;
				case 3:
					conflictResolution = kReplaceAll;
					break;
				default:
					conflictResolution = kCanceled;
					return B_ERROR;
			}
		} else {
			alert = new BAlert();
			alert->SetText(replaceMsg.String());
			alert->AddButton(B_TRANSLATE("Skip"));
			alert->AddButton(B_TRANSLATE("Replace"));
			alert->AddButton(B_TRANSLATE("Cancel"));
			alert->SetShortcut(2, B_ESCAPE);
			switch (alert->Go()) {
				case 0:
					return B_ERROR;
				case 1:
					conflictResolution = kReplace;
					break;
				default:
					conflictResolution = kCanceled;
					return B_ERROR;
			}
		}
""",
        "Haiku #3171 skip vs cancel",
    )
    replace_once(
        HAIKU / "src/kits/tracker/FSUtils.cpp",
        """			if (CheckName(moveMode, &sourceEntry, &destDir,
					collisionCount > 1, conflictCheckResult) != B_OK) {
				// we will skip the current item, because we got a conflict
				// and were asked to or because there was some conflict
""",
        """			if (CheckName(moveMode, &sourceEntry, &destDir,
					collisionCount > 1, conflictCheckResult) != B_OK) {
				if (conflictCheckResult == kCanceled)
					break;
				// we will skip the current item, because we got a conflict
				// and were asked to or because there was some conflict
""",
        "Haiku #3171 abort on cancel",
    )

    replace_once(
        HAIKU / "src/kits/tracker/ContainerWindow.cpp",
        """		navMenu->AddNavDir(&model, what, this, true);
			// ask NavMenu to populate submenu for us
	}

	// add the home dir
	if (find_directory(B_USER_DIRECTORY, &path) == B_OK
		&& entry.SetTo(path.Path()) == B_OK && model.SetTo(&entry) == B_OK) {
		navMenu->AddNavDir(&model, what, this, true);
	}
""",
        """		navMenu->AddNavDir(&model, what, this, false);
	}

	// add the home dir
	if (find_directory(B_USER_DIRECTORY, &path) == B_OK
		&& entry.SetTo(path.Path()) == B_OK && model.SetTo(&entry) == B_OK) {
		navMenu->AddNavDir(&model, what, this, false);
	}
""",
        "Haiku #4216 flatten desktop/home",
    )
    replace_once(
        HAIKU / "src/kits/tracker/ContainerWindow.cpp",
        """			if (volume.GetRootDirectory(&directory) == B_OK
				&& directory.GetEntry(&entry) == B_OK
				&& model.SetTo(&entry) == B_OK) {
				navMenu->AddNavDir(&model, what, this, true);
					// ask NavMenu to populate submenu for us
			}
""",
        """			if (volume.GetRootDirectory(&directory) == B_OK
				&& directory.GetEntry(&entry) == B_OK
				&& model.SetTo(&entry) == B_OK) {
				navMenu->AddNavDir(&model, what, this, false);
			}
""",
        "Haiku #4216 flatten volumes",
    )
    replace_once(
        HAIKU / "src/kits/tracker/ContainerWindow.cpp",
        """	item = new BMenuItem(B_TRANSLATE("Preferences" B_UTF8_ELLIPSIS),
		new BMessage(kShowSettingsWindow), ',');
	item->SetTarget(be_app);
	menu->AddItem(item);
}
""",
        """	item = new BMenuItem(B_TRANSLATE("Preferences" B_UTF8_ELLIPSIS),
		new BMessage(kShowSettingsWindow), ',');
	item->SetTarget(be_app);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	FavoritesMenu* favorites = new FavoritesMenu(B_TRANSLATE("Favorites"),
		new BMessage(B_REFS_RECEIVED), new BMessage(B_REFS_RECEIVED),
		BMessenger(be_app), false);
	BMenuItem* editFavorites = new BMenuItem(
		B_TRANSLATE("Edit favorites" B_UTF8_ELLIPSIS),
		new BMessage(kEditFavorites));
	editFavorites->SetTarget(this);
	favorites->AddItem(editFavorites);
	menu->AddItem(favorites);
}
""",
        "Haiku #5930 favorites menu",
    )
    replace_once(
        HAIKU / "src/kits/tracker/ContainerWindow.cpp",
        """		case kFindButton:
			(new FindWindow())->Show();
			break;
""",
        """		case kFindButton:
			(new FindWindow())->Show();
			break;

		case kEditFavorites:
		{
			BPath path;
			if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
				path.Append("Tracker");
				path.Append("Go");
				BEntry entry(path.Path());
				entry_ref ref;
				if (entry.GetRef(&ref) == B_OK) {
					BMessage refs(B_REFS_RECEIVED);
					refs.AddRef("refs", &ref);
					be_app->PostMessage(&refs);
				}
			}
			break;
		}
""",
        "Haiku #5930 edit favorites",
    )

    replace_once(
        HAIKU / "src/kits/tracker/infowindow/GeneralInfoView.cpp",
        """#include <Catalog.h>
""",
        """#include <Catalog.h>
#include <Clipboard.h>
""",
        "Haiku #4793 clipboard include",
    )
    replace_once(
        HAIKU / "src/kits/tracker/infowindow/GeneralInfoView.cpp",
        """void
GeneralInfoView::MouseDown(BPoint where)
{
	// Start tracking the mouse if we are in any of the hotspots
	if (fLinkRect.Contains(where)) {
""",
        """void
GeneralInfoView::MouseDown(BPoint where)
{
	BMessage* currentMessage = Window() != NULL
		? Window()->CurrentMessage() : NULL;
	int32 buttons = 0;
	if (currentMessage != NULL)
		currentMessage->FindInt32("buttons", &buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		const char* copy = NULL;
		if (fPathRect.Contains(where))
			copy = fPathStr.String();
		else if (fLinkRect.Contains(where))
			copy = fLinkToStr.String();
		else if (fSizeRect.Contains(where))
			copy = fSizeString.String();
		else if (fDescRect.Contains(where))
			copy = fDescStr.String();
		if (copy != NULL && be_clipboard->Lock()) {
			be_clipboard->Clear();
			BMessage* clip = be_clipboard->Data();
			if (clip != NULL) {
				clip->AddData("text/plain", B_MIME_TYPE, copy, strlen(copy));
				be_clipboard->Commit();
			}
			be_clipboard->Unlock();
		}
		return;
	}

	// Start tracking the mouse if we are in any of the hotspots
	if (fLinkRect.Contains(where)) {
""",
        "Haiku #4793 copy info text",
    )


def patch_screenshot() -> None:
    replace_once(
        HAIKU / "src/apps/screenshot/ScreenshotWindow.cpp",
        """	fAreaSelect = new BRadioButton(B_TRANSLATE("Selected area"),
		new BMessage(kShowSelectedArea));
	fAreaSelect->SetEnabled(false);
""",
        """	fAreaSelect = new BRadioButton(B_TRANSLATE("Selected area"),
		new BMessage(kShowSelectedArea));
	fAreaSelect->SetEnabled(true);
""",
        "Haiku #3813 enable area radio",
    )
    replace_once(
        HAIKU / "src/apps/screenshot/ScreenshotWindow.cpp",
        """		case kShowSelectedArea:
			fShotType = kShowSelectedArea;
			fWindowBorder->SetEnabled(false);

			delete fScreenshot;
			fScreenshot = fUtility.MakeScreenshot(fIncludeCursor, fIncludeBorder, fShotType,
				fSelectedArea);
			_UpdatePreviewPanel();
			break;
""",
        """		case kShowSelectedArea:
			fShotType = kShowSelectedArea;
			fWindowBorder->SetEnabled(false);
			if (!fSelectedArea.IsValid()) {
				be_app->PostMessage(new BMessage(SS_LAUNCH_AREA_SELECTOR));
				break;
			}

			delete fScreenshot;
			fScreenshot = fUtility.MakeScreenshot(fIncludeCursor, fIncludeBorder, fShotType,
				fSelectedArea);
			_UpdatePreviewPanel();
			break;
""",
        "Haiku #3813 select area first",
    )


def patch_app_server() -> None:
    replace_once(
        HAIKU / "src/servers/app/decorator/DefaultDecorator.cpp",
        """			if (tab && tab->buttonFocus) {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fFocusFrameColor, B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fFocusFrameColor, B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			} else {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fNonFocusFrameColor, B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fNonFocusFrameColor, B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fNonFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fNonFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fNonFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fNonFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fNonFocusTextColor;
			}
			break;
""",
        """			if (tab && tab->buttonFocus) {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fFocusFrameColor, B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fFocusFrameColor, B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			} else {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fNonFocusFrameColor, B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fNonFocusFrameColor, B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fNonFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fNonFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fNonFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fNonFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fNonFocusTextColor;
			}
			if (tab != NULL && tab->title.Length() > 0) {
				uint32 hash = 2166136261u;
				for (int32 i = 0; i < tab->title.Length(); i++)
					hash = (hash ^ (uint8)tab->title[i]) * 16777619u;
				rgb_color tint = _colors[COLOR_TAB];
				int red = (int)tint.red + (int)(hash & 31) - 16;
				int green = (int)tint.green + (int)((hash >> 5) & 31) - 16;
				int blue = (int)tint.blue + (int)((hash >> 10) & 31) - 16;
				tint.red = (uint8)std::max(0, std::min(255, red));
				tint.green = (uint8)std::max(0, std::min(255, green));
				tint.blue = (uint8)std::max(0, std::min(255, blue));
				_colors[COLOR_TAB] = tint;
				_colors[COLOR_TAB_LIGHT] = tint_color(tint, B_LIGHTEN_2_TINT);
				_colors[COLOR_TAB_BEVEL] = tint_color(tint, B_LIGHTEN_1_TINT);
				_colors[COLOR_TAB_SHADOW] = tint_color(tint, B_DARKEN_1_TINT);
			}
			break;
""",
        "Haiku #4089 per-window tab tint",
    )

    replace_once(
        HAIKU / "src/servers/app/Desktop.cpp",
        """	fVirtualScreen.UpdateFrame();

	for (Window* window = fAllWindows.FirstWindow(); window != NULL;
			window = window->NextWindow(kAllWindowList)) {
		if (window->Screen() == screen)
			window->ServerWindow()->ScreenChanged(&update);
	}
}
""",
        """	fVirtualScreen.UpdateFrame();

	BRect screenFrame = screen->Frame();
	for (Window* window = fAllWindows.FirstWindow(); window != NULL;
			window = window->NextWindow(kAllWindowList)) {
		if (window->Screen() != screen || window->IsHidden()
			|| !window->IsNormal())
			continue;
		BRect frame = window->Frame();
		if (screenFrame.Intersects(frame))
			continue;
		float x = frame.left;
		float y = frame.top;
		if (frame.right < screenFrame.left)
			x = screenFrame.left;
		else if (frame.left > screenFrame.right)
			x = screenFrame.right - frame.Width();
		if (frame.bottom < screenFrame.top)
			y = screenFrame.top;
		else if (frame.top > screenFrame.bottom)
			y = screenFrame.bottom - frame.Height();
		window->MoveBy((int32)(x - frame.left), (int32)(y - frame.top));
	}

	for (Window* window = fAllWindows.FirstWindow(); window != NULL;
			window = window->NextWindow(kAllWindowList)) {
		if (window->Screen() == screen)
			window->ServerWindow()->ScreenChanged(&update);
	}
}
""",
        "Haiku #3505 reposition offscreen",
    )
    replace_once(
        HAIKU / "src/servers/app/Desktop.cpp",
        """	fCursorManager.InitializeCursors(fSettings->DefaultBoldFont().Size() / 12.0f);
""",
        """	fCursorManager.InitializeCursors(r5_cursor_scale(
		fSettings->DefaultBoldFont().Size() / 12.0f));
""",
        "Haiku #2701 cursor scale init",
    )
    replace_once(
        HAIKU / "src/servers/app/Desktop.cpp",
        """	RebuildAndRedrawAfterWindowChange(window, dirty);

	fCursorManager.InitializeCursors(fSettings->DefaultBoldFont().Size() / 12.0f);
}
""",
        """	RebuildAndRedrawAfterWindowChange(window, dirty);

	fCursorManager.InitializeCursors(r5_cursor_scale(
		fSettings->DefaultBoldFont().Size() / 12.0f));
}
""",
        "Haiku #2701 cursor scale fonts",
    )
    replace_once(
        HAIKU / "src/servers/app/Desktop.cpp",
        """static inline float
square_vector_length(float x, float y)
""",
        """static float
r5_cursor_scale(float fontScale)
{
	float extra = 1.0f;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("r5_cursor_scale");
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			file.Read(&extra, sizeof(extra));
		if (extra < 1.0f)
			extra = 1.0f;
		if (extra > 3.0f)
			extra = 3.0f;
	}
	return fontScale * extra;
}


static inline float
square_vector_length(float x, float y)
""",
        "Haiku #2701 cursor scale helper",
    )
    replace_once(
        HAIKU / "src/servers/app/Desktop.cpp",
        """#include <Entry.h>
#include <FindDirectory.h>
#include <Message.h>
""",
        """#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
""",
        "Haiku #2701 File include",
    )


def patch_prefs_boot() -> None:
    replace_once(
        HAIKU / "src/preferences/appearance/AppearanceWindow.h",
        """		LookAndFeelSettingsView*	fLookAndFeelSettings;
};
""",
        """		LookAndFeelSettingsView*	fLookAndFeelSettings;
		BTabView*					fTabView;
};
""",
        "Haiku #3677 tab member",
    )
    replace_once(
        HAIKU / "src/preferences/appearance/AppearanceWindow.h",
        """									AppearanceWindow(BRect frame);
			void					MessageReceived(BMessage *message);
""",
        """									AppearanceWindow(BRect frame);
			void					MessageReceived(BMessage *message);
	virtual	bool					QuitRequested();
""",
        "Haiku #3677 QuitRequested",
    )
    replace_once(
        HAIKU / "src/preferences/appearance/AppearanceWindow.cpp",
        """#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <SeparatorView.h>
#include <TabView.h>
""",
        """#include <Button.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <Path.h>
#include <SeparatorView.h>
#include <TabView.h>
""",
        "Haiku #3677 appearance includes",
    )
    replace_once(
        HAIKU / "src/preferences/appearance/AppearanceWindow.cpp",
        """	tabView->AddTab(fAntialiasingSettings);
	tabView->SetBorder(B_NO_BORDER);
""",
        """	tabView->AddTab(fAntialiasingSettings);
	tabView->SetBorder(B_NO_BORDER);
	fTabView = tabView;
	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("Appearance_settings");
		BFile file(settingsPath.Path(), B_READ_ONLY);
		BMessage settings;
		int32 tab = 0;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK
			&& settings.FindInt32("tab", &tab) == B_OK)
			tabView->Select(tab);
	}
""",
        "Haiku #3677 restore tab",
    )
    replace_once(
        HAIKU / "src/preferences/appearance/AppearanceWindow.cpp",
        """void
AppearanceWindow::MessageReceived(BMessage *message)
{
""",
        """bool
AppearanceWindow::QuitRequested()
{
	BPath settingsPath;
	if (fTabView != NULL
		&& find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("Appearance_settings");
		BFile file(settingsPath.Path(), B_READ_WRITE | B_CREATE_FILE);
		BMessage settings;
		if (file.InitCheck() == B_OK) {
			settings.Unflatten(&file);
			settings.RemoveName("tab");
			settings.AddInt32("tab", fTabView->Selection());
			file.SetSize(0);
			file.Seek(0, SEEK_SET);
			settings.Flatten(&file);
		}
	}
	return BWindow::QuitRequested();
}


void
AppearanceWindow::MessageReceived(BMessage *message)
{
""",
        "Haiku #3677 save tab",
    )

    replace_once(
        HAIKU / "src/preferences/input/SettingsView.h",
        """				BSlider*		fAccelerationSlider;
};
""",
        """				BSlider*		fAccelerationSlider;
				BSlider*		fPointerSizeSlider;
};
""",
        "Haiku #2701 pointer slider member",
    )
    replace_once(
        HAIKU / "src/preferences/input/SettingsView.cpp",
        """#include <Box.h>
#include <Catalog.h>
#include <Debug.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <TextControl.h>
""",
        """#include <Box.h>
#include <Catalog.h>
#include <Debug.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Path.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <TextControl.h>
""",
        "Haiku #2701 settings includes",
    )
    replace_once(
        HAIKU / "src/preferences/input/SettingsView.cpp",
        """	fAccelerationSlider
		= new BSlider("mouse_acceleration", B_TRANSLATE("Mouse acceleration"),
			new BMessage(kMsgAccelerationFactor), 0, 1000, B_HORIZONTAL);
	fAccelerationSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAccelerationSlider->SetHashMarkCount(7);
""",
        """	fAccelerationSlider
		= new BSlider("mouse_acceleration", B_TRANSLATE("Mouse acceleration"),
			new BMessage(kMsgAccelerationFactor), 0, 1000, B_HORIZONTAL);
	fAccelerationSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAccelerationSlider->SetHashMarkCount(7);

	fPointerSizeSlider = new BSlider("pointer_size",
		B_TRANSLATE("Pointer size"), new BMessage(kMsgCursorSpeed),
		0, 1000, B_HORIZONTAL);
	fPointerSizeSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fPointerSizeSlider->SetHashMarkCount(5);
	fPointerSizeSlider->SetLimitLabels(B_TRANSLATE("Small"),
		B_TRANSLATE("Large"));
""",
        "Haiku #2701 pointer slider",
    )
    replace_once(
        HAIKU / "src/preferences/input/SettingsView.cpp",
        """				.AddGroup(B_HORIZONTAL, 0)
					.Add(fAccelerationSlider)
					.End()
				.End()
""",
        """				.AddGroup(B_HORIZONTAL, 0)
					.Add(fAccelerationSlider)
					.End()
				.AddGroup(B_HORIZONTAL, 0)
					.Add(fPointerSizeSlider)
					.End()
				.End()
""",
        "Haiku #2701 pointer layout",
    )
    replace_once(
        HAIKU / "src/preferences/input/SettingsView.cpp",
        """	fAcceptFirstClickBox->SetEnabled(
		fSettings.MouseMode() != B_FOCUS_FOLLOWS_MOUSE);
}
""",
        """	fAcceptFirstClickBox->SetEnabled(
		fSettings.MouseMode() != B_FOCUS_FOLLOWS_MOUSE);

	float extra = 1.0f;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("r5_cursor_scale");
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			file.Read(&extra, sizeof(extra));
	}
	if (extra < 1.0f)
		extra = 1.0f;
	if (extra > 2.0f)
		extra = 2.0f;
	fPointerSizeSlider->SetValue((int32)((extra - 1.0f) * 1000.0f));
}
""",
        "Haiku #2701 pointer from settings",
    )
    replace_once(
        HAIKU / "src/preferences/input/InputMouse.cpp",
        """#include "InputMouse.h"
""",
        """#include "InputMouse.h"

#include <AppDefs.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
""",
        "Haiku #2701 InputMouse includes",
    )
    replace_once(
        HAIKU / "src/preferences/input/InputMouse.cpp",
        """		case kMsgAccelerationFactor:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				// slow = 0, fast = 262144
				fSettings->SetAccelerationFactor(
					(int32)pow(value * 4.0 / 1000, 2) * 16384);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}
""",
        """		case kMsgAccelerationFactor:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				// slow = 0, fast = 262144
				fSettings->SetAccelerationFactor(
					(int32)pow(value * 4.0 / 1000, 2) * 16384);
				fDefaultsButton->SetEnabled(fSettings->IsDefaultable());
				fRevertButton->SetEnabled(fSettings->IsRevertable());
			}
			break;
		}

		case kMsgCursorSpeed:
		{
			int32 value;
			if (message->FindInt32("be:value", &value) == B_OK) {
				float extra = 1.0f + (float)value / 1000.0f;
				BPath path;
				if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
					path.Append("r5_cursor_scale");
					BFile file(path.Path(),
						B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
					if (file.InitCheck() == B_OK)
						file.Write(&extra, sizeof(extra));
				}
			}
			break;
		}
""",
        "Haiku #2701 save pointer size",
    )

    replace_once(
        HAIKU / "src/system/boot/platform/generic/video_splash.cpp",
        """	uint8 fillByte = 0;
	uint32 fillWord = 0;
	if (gKernelArgs.frame_buffer.depth >= 24) {
		fillByte = 0x14;
		fillWord = 0x00141a22;
	}
	if (size >= 64) {
		// Align writes
		for (addr_t align = (8 - (frameBuffer & 7)) & 7; pos < align; pos++)
			*(char*)(frameBuffer + pos) = fillByte;
		// Write eight bytes, many many times, but not too many
		for (addr_t alignSize = size - 8; pos < alignSize; pos +=8) {
			*(uint32*)(frameBuffer + pos) = fillWord;
			*(uint32*)(frameBuffer + pos + 4) = fillWord;
		}
	}
	// Write a few bytes more
	for (; pos < size; pos++)
		*(char*)(frameBuffer + pos) = fillByte;
""",
        """	if (size >= 64) {
		// Align writes
		for (addr_t align = (8 - (frameBuffer & 7)) & 7; pos < align; pos++)
			*(char*)(frameBuffer + pos) = 0;
		// Write eight bytes, many many times, but not too many
		for (addr_t alignSize = size - 8; pos < alignSize; pos +=8) {
			*(uint32*)(frameBuffer + pos) = 0;
			*(uint32*)(frameBuffer + pos + 4) = 0;
		}
	}
	// Write a few bytes more
	for (; pos < size; pos++)
		*(char*)(frameBuffer + pos) = 0;
""",
        "Haiku #3115 revert splash fill",
    )


def main() -> int:
    patch_deskbar()
    patch_tracker()
    patch_screenshot()
    patch_app_server()
    patch_prefs_boot()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
