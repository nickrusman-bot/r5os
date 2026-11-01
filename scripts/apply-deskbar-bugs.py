#!/usr/bin/env python3
"""Deskbar/PowerStatus/volume overlay: #20314 #18411 #11397 #18643 #19704
#14846 #16458 #15792 #15843 #12884 #3076 #20099."""
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


def patch_one_open_menu() -> None:
    """Haiku #20099: two BMenuBars in one window (Deskbar leaf and the
    running-apps bar) each installed a window semaphore and left the other
    menu tracking."""
    header = HAIKU / "headers/os/interface/Window.h"
    replace_once(
        header,
        "\tfriend void _set_menu_sem_(BWindow* w, sem_id sem);\n",
        "\tfriend void _set_menu_sem_(BWindow* w, sem_id sem);\n"
        "\tfriend bool _clear_menu_sem_(BWindow* w, sem_id sem);\n",
        "Haiku #20099 clear sem",
    )
    window = HAIKU / "src/kits/interface/Window.cpp"
    replace_once(
        window,
        """void
_set_menu_sem_(BWindow* window, sem_id sem)
{
	if (window != NULL)
		window->fMenuSem = sem;
}
""",
        """void
_set_menu_sem_(BWindow* window, sem_id sem)
{
	if (window != NULL)
		window->fMenuSem = sem;
}


bool
_clear_menu_sem_(BWindow* window, sem_id sem)
{
	// Only the menu that installed the semaphore may clear it. A second
	// menu bar in the same window (Deskbar) would otherwise wipe the new
	// owner's id when the first tracking thread exits (Haiku #20099).
	if (window == NULL || window->fMenuSem != sem)
		return false;
	window->fMenuSem = B_BAD_SEM_ID;
	return true;
}
""",
        "Haiku #20099 clear sem impl",
    )

    menu_bar_h = HAIKU / "headers/os/interface/MenuBar.h"
    replace_once(
        menu_bar_h,
        "class BMenuField;\n",
        "class BMenuField;\n"
        "class BList;\n"
        "class BView;\n",
        "Haiku #20099 forward",
    )
    replace_once(
        menu_bar_h,
        """			void				_InitData(menu_layout layout);
""",
        """			void				_InitData(menu_layout layout);
			void				_StopOtherMenuBars(BView* view, BList& stopped);
""",
        "Haiku #20099 method",
    )

    menu_bar = HAIKU / "src/kits/interface/MenuBar.cpp"
    replace_once(
        menu_bar,
        "#include <LayoutUtils.h>\n",
        "#include <LayoutUtils.h>\n#include <List.h>\n",
        "Haiku #20099 list include",
    )
    replace_once(
        menu_bar,
        """	BWindow* window = Window();
	if (window == NULL)
		debugger("MenuBar must be added to a window before it can be used.");

	BAutolock lock(window);
""",
        """	BWindow* window = Window();
	if (window == NULL)
		debugger("MenuBar must be added to a window before it can be used.");

	// Deskbar has two menu bars. Close the other one before this one tracks
	// (Haiku #20099). The wait is outside the window lock so the tracking
	// thread can take it and exit.
	BList stopped;
	if (window->Lock()) {
		int32 count = window->CountChildren();
		for (int32 i = 0; i < count; i++)
			_StopOtherMenuBars(window->ChildAt(i), stopped);
		window->Unlock();
	}
	for (int32 i = 0; i < stopped.CountItems(); i++) {
		BMenuBar* bar = static_cast<BMenuBar*>(stopped.ItemAt(i));
		for (int32 n = 0; n < 50 && bar->fTracking; n++)
			snooze(2000);
	}

	BAutolock lock(window);
""",
        "Haiku #20099 stop siblings",
    )
    replace_once(
        menu_bar,
        """		fTracking = false;
		_set_menu_sem_(window, B_NO_MORE_SEMS);
		delete_sem(fMenuSem);
""",
        """		fTracking = false;
		_clear_menu_sem_(window, fMenuSem);
		delete_sem(fMenuSem);
""",
        "Haiku #20099 spawn fail",
    )
    replace_once(
        menu_bar,
        """	_set_menu_sem_(window, B_BAD_SEM_ID);
	delete_sem(menuBar->fMenuSem);
	menuBar->fMenuSem = B_BAD_SEM_ID;
""",
        """	_clear_menu_sem_(window, menuBar->fMenuSem);
	delete_sem(menuBar->fMenuSem);
	menuBar->fMenuSem = B_BAD_SEM_ID;
""",
        "Haiku #20099 track end",
    )
    replace_once(
        menu_bar,
        "void BMenuBar::_ReservedMenuBar1() {}\n",
        """void
BMenuBar::_StopOtherMenuBars(BView* view, BList& stopped)
{
	if (view == NULL)
		return;

	if (BMenuBar* bar = dynamic_cast<BMenuBar*>(view)) {
		if (bar != this && bar->fTracking) {
			BPrivate::MenuPrivate(bar).QuitTracking(false);
			stopped.AddItem(bar);
		}
	}

	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++)
		_StopOtherMenuBars(view->ChildAt(i), stopped);
}


void BMenuBar::_ReservedMenuBar1() {}
""",
        "Haiku #20099 stop method",
    )

    popup = HAIKU / "src/kits/interface/PopUpMenu.cpp"
    replace_once(
        popup,
        """		delete_sem(sem);
		if (async && window != NULL)
			_set_menu_sem_(window, B_BAD_SEM_ID);
""",
        """		if (async && window != NULL)
			_clear_menu_sem_(window, sem);
		delete_sem(sem);
""",
        "Haiku #20099 popup fail",
    )
    replace_once(
        popup,
        """	if (data->async && data->window)
		_set_menu_sem_(data->window, B_BAD_SEM_ID);
""",
        """	if (data->async && data->window)
		_clear_menu_sem_(data->window, data->lock);
""",
        "Haiku #20099 popup end",
    )


def main() -> int:
    mixer = HAIKU / "src/bin/desklink/MixerControl.cpp"
    replace_once(
        mixer,
        """				}

				if (param == NULL) {
					errorString = fVolumeWhich
						? "Could not find the soundcard"
						: "Could not find the mixer";
""",
        """				}

				// Mute can follow Master Gain in the parameter web; keep
				// scanning so the Deskbar speaker can mute (Haiku #20314).
				if (fMuteParameter == NULL) {
					for (int i = 0; i < numParams; i++) {
						BParameter* muteParam = fParameterWeb->ParameterAt(i);
						if (strcmp(muteParam->Kind(), B_MUTE) == 0) {
							fMuteParameter = muteParam;
							fMuteMediaNode = muteParam->Web()->Node();
							break;
						}
					}
				}

				if (param == NULL) {
					errorString = fVolumeWhich
						? "Could not find the soundcard"
						: "Could not find the mixer";
""",
        "Haiku #20314 mute param",
    )

    power = HAIKU / "src/apps/powerstatus/PowerStatusView.cpp"
    replace_once(
        power,
        """	fShowLabel = true;
	fShowTime = false;
	fShowStatusIcon = true;
""",
        """	fShowLabel = !fInDeskbar;
	fShowTime = false;
	fShowStatusIcon = true;
""",
        "Haiku #18411 hide deskbar label",
    )
    replace_once(
        power,
        """	return new PowerStatusReplicant(BRect(0, 0, maxWidth - 1, maxHeight - 1),
		B_FOLLOW_TOP, true);
""",
        """	float size = maxHeight > 16.0f ? maxHeight : 16.0f;
	(void)maxWidth;
	return new PowerStatusReplicant(BRect(0, 0, size - 1, size - 1),
		B_FOLLOW_TOP, true);
""",
        "Haiku #18411 deskbar size",
    )

    barView = HAIKU / "src/apps/deskbar/BarView.cpp"
    replace_once(
        barView,
        """void
TBarView::Draw(BRect)
{
	BRect bounds(Bounds());

	rgb_color hilite = tint_color(ViewColor(), B_DARKEN_1_TINT);
""",
        """void
TBarView::Draw(BRect)
{
	if (IsHidden())
		return;

	BRect bounds(Bounds());

	rgb_color hilite = tint_color(ViewColor(), B_DARKEN_1_TINT);
""",
        "Haiku #19704 hidden draw",
    )
    replace_once(
        barView,
        """		case B_LOCALE_CHANGED:
		case kRealignReplicants:
""",
        """		case B_COLORS_UPDATED:
			SetViewUIColor(B_MENU_BACKGROUND_COLOR);
			Invalidate();
			break;

		case B_LOCALE_CHANGED:
		case kRealignReplicants:
""",
        "Haiku #11397 colors",
    )

    team = HAIKU / "src/apps/deskbar/TeamMenuItem.cpp"
    replace_once(
        team,
        """	rgb_color menuColor = ui_color(B_MENU_BACKGROUND_COLOR);
	bool canHandle = !fBarView->Dragging()
		|| fBarView->AppCanHandleTypes(Signature());
	uint32 flags = 0;
	if (_IsSelected() && canHandle)
		flags |= BControlLook::B_ACTIVATED;
""",
        """	rgb_color menuColor = ui_color(B_MENU_BACKGROUND_COLOR);
	bool canHandle = !fBarView->Dragging()
		|| fBarView->AppCanHandleTypes(Signature());
	uint32 flags = 0;
	if (_IsSelected() && canHandle) {
		flags |= BControlLook::B_ACTIVATED;
		menuColor = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);
	}
""",
        "Haiku #18643 selected bg",
    )
    replace_once(
        team,
        """	if (_IsSelected() && IsEnabled() && canHandle)
		menu->SetLowColor(tint_color(ui_color(B_MENU_BACKGROUND_COLOR),
			B_HIGHLIGHT_BACKGROUND_TINT));
	else
		menu->SetLowColor(ui_color(B_MENU_BACKGROUND_COLOR));
""",
        """	if (_IsSelected() && IsEnabled() && canHandle)
		menu->SetLowColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
	else
		menu->SetLowColor(ui_color(B_MENU_BACKGROUND_COLOR));
""",
        "Haiku #18643 selected low",
    )

    timeView = HAIKU / "src/apps/deskbar/TimeView.cpp"
    replace_once(
        timeView,
        """	SetHighColor(ViewColor());
	SetLowColor(ViewColor());
	FillRect(Bounds());
	SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);
""",
        """	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetHighUIColor(B_MENU_BACKGROUND_COLOR);
	FillRect(Bounds());
	SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);
""",
        "Haiku #11397 clock fill",
    )
    replace_once(
        timeView,
        """	if (Vertical()) {
		float appWidth = static_cast<TBarApp*>(be_app)->Settings()->width;
		*width = fMaxWidth
			= std::min(appWidth - (gDragRegionWidth + kHMargin) * 2, timeWidth);
	} else
		*width = fMaxWidth = timeWidth;
""",
        """	timeWidth += kHMargin * 2;
	if (Vertical()) {
		float appWidth = static_cast<TBarApp*>(be_app)->Settings()->width;
		*width = fMaxWidth
			= std::min(appWidth - (gDragRegionWidth + kHMargin) * 2, timeWidth);
	} else
		*width = fMaxWidth = timeWidth;
""",
        "Haiku #14846 weekday width",
    )

    barWindow = HAIKU / "src/apps/deskbar/BarWindow.cpp"
    replace_once(
        barWindow,
        """	sDeskbarMenu->ResetTargets();

	fMenusShown++;
	BWindow::MenusBeginning();
""",
        """	sDeskbarMenu->ResetTargets();

	Activate();

	fMenusShown++;
	BWindow::MenusBeginning();
""",
        "Haiku #16458 activate",
    )

    status = HAIKU / "src/apps/deskbar/StatusView.cpp"
    replace_once(
        status,
        """	if (fDragLocation != kDontDrawDragRegion || fDragLocation != kNoDragRegion)
		DrawDragger();
""",
        """	if (fDragLocation != kDontDrawDragRegion && fDragLocation != kNoDragRegion)
		DrawDragger();
""",
        "Haiku #15792 #15843 drag",
    )

    barApp = HAIKU / "src/apps/deskbar/BarApp.cpp"
    replace_once(
        barApp,
        """		prefs.AddBool("autoHide", fSettings.autoHide);

		prefs.Flatten(fSettingsFile);
""",
        """		prefs.AddBool("autoHide", fSettings.autoHide);

		fSettingsFile->SetSize(0);
		fSettingsFile->Seek(0, SEEK_SET);
		prefs.Flatten(fSettingsFile);
""",
        "Haiku #12884 settings size",
    )
    replace_once(
        barApp,
        """		prefs.AddBool("showTimeZone", fClockSettings.showTimeZone);

		prefs.Flatten(fClockSettingsFile);
""",
        """		prefs.AddBool("showTimeZone", fClockSettings.showTimeZone);

		fClockSettingsFile->SetSize(0);
		fClockSettingsFile->Seek(0, SEEK_SET);
		prefs.Flatten(fClockSettingsFile);
""",
        "Haiku #12884 clock size",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/WindowMenuItem.cpp",
        """		rgb_color backColor = tint_color(menuColor,
			B_HIGHLIGHT_BACKGROUND_TINT);""",
        """		rgb_color backColor = ui_color(B_MENU_SELECTED_BACKGROUND_COLOR);""",
        "Haiku #18643 window item",
    )
    replace_once(
        barView,
        """		if (State() == kExpandoState && !Vertical()) {
			// top or bottom, full
			windowWidth = screenFrame.Width();
		} else
			windowWidth = kHiddenDimension;""",
        """		windowWidth = kHiddenDimension;""",
        "Haiku #19704 hidden width",
    )
    replace_once(
        timeView,
        """		if (timeWidth > fMaxWidth) {
			// time does not fit, push it over to truncate the left side
			// to see the entire time string you must make the window wider
			float difference = timeWidth - fMaxWidth;
			fDateLocation.x -= difference;
			fTimeLocation.x -= difference;
		}""",
        """		if (timeWidth > fMaxWidth && !fShowDayOfWeek) {
			float difference = timeWidth - fMaxWidth;
			fDateLocation.x -= difference;
			fTimeLocation.x -= difference;
		} else {
			fDateLocation.x = 0.0;
			fTimeLocation.x = 0.0;
		}""",
        "Haiku #14846 no left clip",
    )
    replace_once(
        barApp,
        """			fBarView->HideDeskbar(fSettings.autoHide);
			fBarWindow->Unlock();
			break;""",
        """			fBarView->HideDeskbar(fSettings.autoHide);
			fBarWindow->Unlock();
			SaveSettings();
			break;""",
        "Haiku #12884 save autohide",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/DeskbarMenu.cpp",
        """			fBarView->DragStop();""",
        """			fBarView->DragStop(true);""",
        "Haiku #3076 deskbar drag",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/TeamMenu.cpp",
        """			fBarView->DragStop();""",
        """			fBarView->DragStop(true);""",
        "Haiku #3076 team drag",
    )
    replace_once(
        HAIKU / "src/apps/deskbar/WindowMenu.cpp",
        """		barview->DragStop();""",
        """		barview->DragStop(true);""",
        "Haiku #3076 window drag",
    )
    replace_once(
        barApp,
        """		default:
			BApplication::MessageReceived(message);
			break;""",
        """		case B_COLORS_UPDATED:
			if (fBarWindow != NULL && fBarWindow->Lock()) {
				if (fBarView != NULL) {
					fBarView->SetViewUIColor(B_MENU_BACKGROUND_COLOR);
					fBarView->Invalidate();
				}
				fBarWindow->Unlock();
			}
			break;

		default:
			BApplication::MessageReceived(message);
			break;""",
        "Haiku #11397 app colors",
    )
    patch_one_open_menu()
    replace_once(
        barApp,
        "\tif (fSettingsFile->InitCheck() == B_OK) {\n",
        "\tif (fSettingsFile != NULL && fSettingsFile->InitCheck() == B_OK) {\n",
        "deskbar settings file may be unset",
    )
    replace_once(
        barApp,
        "\tif (fClockSettingsFile->InitCheck() == B_OK) {\n",
        "\tif (fClockSettingsFile != NULL\n"
        "\t\t&& fClockSettingsFile->InitCheck() == B_OK) {\n",
        "deskbar clock settings file may be unset",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
