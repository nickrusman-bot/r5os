#!/usr/bin/env python3
"""Deskbar/PowerStatus/volume overlay: #20314 #18411 #11397 #18643 #19704
#14846 #16458 #15792 #15843 #12884."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text and old not in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
