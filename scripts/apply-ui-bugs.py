#!/usr/bin/env python3
"""Apply easy UI bugfixes: #11138 #18057 #16838 #16471 #16116 #15690 #1006
#11625 #18518 #18813."""
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


def patch_alert() -> None:
    path = HAIKU / "src/kits/interface/Alert.cpp"
    old = """BAlert::BAlert(BMessage* data)
	:
	BWindow(data)
{
	fInvoker = NULL;
	fAlertSem = -1;
	fAlertValue = -1;

	fTextView = (BTextView*)FindView("_tv_");

	// TODO: window loses default button on dearchive!
	// TODO: ButtonAt() doesn't work afterwards (also affects shortcuts)

	TAlertView* view = (TAlertView*)FindView("_master_");
	if (view)
		view->SetBitmap(_CreateTypeIcon());

	// Get keys
	char key;
	for (int32 i = 0; i < 3; ++i) {
		if (data->FindInt8("_but_key", i, (int8*)&key) == B_OK)
			fKeys[i] = key;
	}

	AddCommonFilter(new(std::nothrow) _BAlertFilter_(this));
}"""
    new = """BAlert::BAlert(BMessage* data)
	:
	BWindow(data)
{
	fInvoker = NULL;
	fAlertSem = -1;
	fAlertValue = -1;
	fButtonLayout = NULL;
	fType = B_INFO_ALERT;
	fButtonSpacing = B_EVEN_SPACING;
	fButtonWidth = B_WIDTH_FROM_WIDEST;
	fKeys.assign(3, 0);

	int32 type;
	if (data->FindInt32("_atype", &type) == B_OK)
		fType = (uint8)type;

	int32 buttonWidth;
	if (data->FindInt32("_but_width", &buttonWidth) == B_OK)
		fButtonWidth = (uint8)buttonWidth;

	fTextView = dynamic_cast<BTextView*>(FindView("_tv_"));
	const char* text = NULL;
	if (data->FindString("_text", &text) == B_OK && text != NULL) {
		if (fTextView == NULL) {
			fTextView = new BTextView("_tv_");
			fTextView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
			fTextView->MakeEditable(false);
			fTextView->MakeSelectable(false);
			fTextView->SetWordWrap(true);
			AddChild(fTextView);
		}
		fTextView->SetText(text);
	}

	// Pre-layout archives used "_master_"; current code names the view
	// "TAlertView".
	fIconView = dynamic_cast<TAlertView*>(FindView("TAlertView"));
	if (fIconView == NULL)
		fIconView = dynamic_cast<TAlertView*>(FindView("_master_"));
	if (fIconView != NULL)
		fIconView->SetBitmap(_CreateTypeIcon());

	char key;
	for (int32 i = 0; i < 3; ++i) {
		if (data->FindInt8("_but_key", i, (int8*)&key) == B_OK)
			fKeys[i] = key;
	}

	AddCommonFilter(new(std::nothrow) _BAlertFilter_(this));
}"""
    replace_once(path, old, new, "Haiku #11138")


def patch_scrollview() -> None:
    path = HAIKU / "src/kits/interface/ScrollView.cpp"
    old = """	if (fHorizontalScrollBar != NULL)
		fHorizontalScrollBar->ResizeBy(-B_V_SCROLL_BAR_WIDTH, 0);
	else if (fVerticalScrollBar != NULL)
		fVerticalScrollBar->ResizeBy(0, -B_H_SCROLL_BAR_HEIGHT);"""
    new = """	// Use the composed scrollbar thickness so the bar lines up with the
	// document-window resize knob (Haiku #18057).
	if (fHorizontalScrollBar != NULL) {
		fHorizontalScrollBar->ResizeBy(
			-be_control_look->GetScrollBarWidth(B_VERTICAL), 0);
	} else if (fVerticalScrollBar != NULL) {
		fVerticalScrollBar->ResizeBy(0,
			-be_control_look->GetScrollBarWidth(B_HORIZONTAL));
	}"""
    replace_once(path, old, new, "Haiku #18057")


def patch_listview() -> None:
    path = HAIKU / "src/kits/interface/ListView.cpp"
    text = path.read_text(encoding="utf-8")
    include = "#include <Region.h>\n"
    while text.count(include) > 1:
        text = text.replace(include, "", 1)
        print(f"collapsed duplicate in {path.name}")
    if include not in text:
        old = "#include <Window.h>\n"
        if old not in text:
            raise SystemExit(f"pattern not found in {path} (Region include)")
        text = text.replace(old, include + old, 1)
        print(f"patched {path} (Region include)")
    else:
        print(f"already {path.name} (Region include)")
    path.write_text(text, encoding="utf-8")
    old = """	item->DrawItem(this, itemRect, complete);
}"""
    new = """	BRegion clip(itemRect);
	ConstrainClippingRegion(&clip);
	item->DrawItem(this, itemRect, complete);
	ConstrainClippingRegion(NULL);
}"""
    replace_once(path, old, new, "Haiku #16838")


def patch_desktop_selection() -> None:
    path = HAIKU / "src/kits/tracker/Pose.cpp"
    old = """	// Set low color to high color on selected Desktop poses, don't worry,
	// high color will be inverted again.
	if (selected && poseView->IsDesktopView())
		drawView->SetLowColor(poseView->HighColor());
	else if (poseView->IsDesktopView())
		drawView->SetLowColor(poseView->LowColor());
	else
		drawView->SetLowUIColor(poseView->LowUIColor());"""
    new = """	// Desktop labels sit on the wallpaper. InvertRect after drawing makes
	// a selected name look like an unselected one on a black background
	// (Haiku #16471). Fill with the list-selection color instead.
	if (selected && poseView->IsDesktopView())
		drawView->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
	else if (poseView->IsDesktopView())
		drawView->SetLowColor(poseView->LowColor());
	else
		drawView->SetLowUIColor(poseView->LowUIColor());"""
    replace_once(path, old, new, "Haiku #16471 low")

    old = """		if (windowActive || isDrawingSelectionRect) {
			// invert colors to select label using "reverse video"
			drawView->InvertRect(invertRect);
		} else if (!windowActive && showSelectionWhenInactive) {"""
    new = """		if (poseView->IsDesktopView()) {
			// Selection fill already drawn; do not invert (Haiku #16471).
		} else if (windowActive || isDrawingSelectionRect) {
			// invert colors to select label using "reverse video"
			drawView->InvertRect(invertRect);
		} else if (!windowActive && showSelectionWhenInactive) {"""
    replace_once(path, old, new, "Haiku #16471 invert")


def patch_desktop_icon_size() -> None:
    path = HAIKU / "src/kits/tracker/PoseView.cpp"
    old = """	if (IsDesktopView() && ViewMode() == kListMode) {
		// recover if desktop window view state set wrong
		fViewState->SetViewMode(kIconMode);
	}
}


void
BPoseView::RestoreState(const BMessage &message)
{
	RestoreColumnState(message);

	BViewState* state = BViewState::InstantiateFromMessage(message);
	if (state != NULL) {
		delete fViewState;
		fViewState = state;
	}

	if (IsDesktopView() && ViewMode() == kListMode) {
		// recover if desktop window view state set wrong
		fViewState->SetViewMode(kIconMode);
	}
}"""
    new = """	if (IsDesktopView() && ViewMode() == kListMode) {
		// recover if desktop window view state set wrong
		fViewState->SetViewMode(kIconMode);
	}

	if (IsDesktopView() && !restoredState)
		fViewState->SetIconSize(48);
}


void
BPoseView::RestoreState(const BMessage &message)
{
	RestoreColumnState(message);

	BViewState* state = BViewState::InstantiateFromMessage(message);
	bool restoredState = state != NULL;
	if (state != NULL) {
		delete fViewState;
		fViewState = state;
	}

	if (IsDesktopView() && ViewMode() == kListMode) {
		// recover if desktop window view state set wrong
		fViewState->SetViewMode(kIconMode);
	}

	if (IsDesktopView() && !restoredState)
		fViewState->SetIconSize(48);
}"""
    replace_once(path, old, new, "Haiku #16116 message")

    # First RestoreState(AttributeStreamNode*) needs restoredState variable
    old = """		if (size > 0 && size < 10000) {
			// check for invalid sizes here to protect against
			// munged attributes
			char* buffer = new char[size];
			off_t result = node->Read(name, 0, B_RAW_TYPE, size, buffer);
			if (result) {
				BMallocIO stream;
				stream.WriteAt(0, buffer, size);
				stream.Seek(0, SEEK_SET);
				BViewState* state = BViewState::InstantiateFromStream(&stream, wrongEndian);
				if (state != NULL) {
					delete fViewState;
					fViewState = state;
				}
			}
			delete[] buffer;
		}
	}"""
    new = """		if (size > 0 && size < 10000) {
			// check for invalid sizes here to protect against
			// munged attributes
			char* buffer = new char[size];
			off_t result = node->Read(name, 0, B_RAW_TYPE, size, buffer);
			if (result) {
				BMallocIO stream;
				stream.WriteAt(0, buffer, size);
				stream.Seek(0, SEEK_SET);
				BViewState* state = BViewState::InstantiateFromStream(&stream, wrongEndian);
				if (state != NULL) {
					delete fViewState;
					fViewState = state;
					restoredState = true;
				}
			}
			delete[] buffer;
		}
	}"""
    replace_once(path, old, new, "Haiku #16116 stream")

    old = """void
BPoseView::RestoreState(AttributeStreamNode* node)
{
	RestoreColumnState(node);
"""
    new = """void
BPoseView::RestoreState(AttributeStreamNode* node)
{
	bool restoredState = false;
	RestoreColumnState(node);
"""
    replace_once(path, old, new, "Haiku #16116 flag")


def patch_softwareupdater() -> None:
    path = HAIKU / "src/apps/softwareupdater/SoftwareUpdaterWindow.cpp"
    replace_once(
        path,
        "	SetExplicitMinSize(BSize(B_SIZE_UNSET, 40));\n"
        "	SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 400));",
        "	SetExplicitMinSize(BSize(B_SIZE_UNSET, 160));\n"
        "	SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 400));",
        "Haiku #15690 list min",
    )
    replace_once(
        path,
        "	SetFlags(Flags() ^ B_AUTO_UPDATE_SIZE_LIMITS);",
        "	SetFlags(Flags() & ~B_AUTO_UPDATE_SIZE_LIMITS);",
        "Haiku #15690 flags",
    )


def patch_backgrounds() -> None:
    header = HAIKU / "src/preferences/backgrounds/BackgroundsView.h"
    replace_once(
        header,
        "			bool				fFoundPositionSetting;\n"
        "};",
        "			bool				fFoundPositionSetting;\n"
        "			bool				fRevertSnapshotValid;\n"
        "			rgb_color			fRevertColor;\n"
        "			int32				fRevertImageIndex;\n"
        "			int32				fRevertPlacement;\n"
        "			BPoint				fRevertOffset;\n"
        "			bool				fRevertOutline;\n"
        "			bool				fRevertCurrentWorkspace;\n"
        "\n"
        "			void				_CaptureRevertSnapshot();\n"
        "			void				_RestoreRevertSnapshot();\n"
        "};",
        "Haiku #1006 header",
    )

    cpp = HAIKU / "src/preferences/backgrounds/BackgroundsView.cpp"
    replace_once(
        cpp,
        "	fFoundPositionSetting(false)\n"
        "{",
        "	fFoundPositionSetting(false),\n"
        "	fRevertSnapshotValid(false)\n"
        "{",
        "Haiku #1006 ctor",
    )
    replace_once(
        cpp,
        "	fApply->SetEnabled(false);\n"
        "	fRevert->SetEnabled(false);\n"
        "}",
        "	fApply->SetEnabled(false);\n"
        "	fRevert->SetEnabled(false);\n"
        "	_CaptureRevertSnapshot();\n"
        "}",
        "Haiku #1006 capture",
    )
    replace_once(
        cpp,
        "		case kMsgRevertSettings:\n"
        "			_UpdateWithCurrent();\n"
        "			break;",
        "		case kMsgRevertSettings:\n"
        "			_RestoreRevertSnapshot();\n"
        "			break;",
        "Haiku #1006 revert",
    )
    replace_once(
        cpp,
            "			_UpdateButtons();\n"
            "			break;\n"
            "		}\n"
            "		case kMsgRevertSettings:",
            "			_UpdateButtons();\n"
            "			_CaptureRevertSnapshot();\n"
            "			break;\n"
            "		}\n"
            "		case kMsgRevertSettings:",
        "Haiku #1006 recapture",
    )

    impl = r'''
void
BackgroundsView::_CaptureRevertSnapshot()
{
	fRevertSnapshotValid = true;
	fRevertColor = fPicker->ValueAsColor();
	BGImageMenuItem* imageItem = (BGImageMenuItem*)fImageMenu->FindMarked();
	fRevertImageIndex = imageItem != NULL ? imageItem->ImageIndex() : -1;
	fRevertPlacement = 0;
	if (fPlacementMenu->FindItem(kMsgScalePlacement)->IsMarked())
		fRevertPlacement = kMsgScalePlacement;
	else if (fPlacementMenu->FindItem(kMsgCenterPlacement)->IsMarked())
		fRevertPlacement = kMsgCenterPlacement;
	else if (fPlacementMenu->FindItem(kMsgTilePlacement)->IsMarked())
		fRevertPlacement = kMsgTilePlacement;
	else
		fRevertPlacement = kMsgManualPlacement;
	fRevertOffset.x = atoi(fXPlacementText->Text());
	fRevertOffset.y = atoi(fYPlacementText->Text());
	fRevertOutline = fIconLabelOutline->Value() == B_CONTROL_ON;
	BMenuItem* workspaceItem = fWorkspaceMenu->FindItem(kMsgCurrentWorkspace);
	fRevertCurrentWorkspace = workspaceItem != NULL && workspaceItem->IsMarked();
}


void
BackgroundsView::_RestoreRevertSnapshot()
{
	if (!fRevertSnapshotValid)
		return;

	fPicker->SetValue(fRevertColor);
	fIconLabelOutline->SetValue(fRevertOutline ? B_CONTROL_ON : B_CONTROL_OFF);

	BMenuItem* placement = fPlacementMenu->FindItem(fRevertPlacement);
	if (placement != NULL)
		placement->SetMarked(true);

	BString xtext, ytext;
	if (fRevertPlacement == kMsgManualPlacement) {
		xtext << (int)fRevertOffset.x;
		ytext << (int)fRevertOffset.y;
	}
	fXPlacementText->SetText(xtext.String());
	fYPlacementText->SetText(ytext.String());

	BGImageMenuItem* imageItem = _FindImageItem(fRevertImageIndex);
	if (imageItem != NULL)
		imageItem->SetMarked(true);

	if (fWorkspaceMenu->CountItems() > 0) {
		uint32 workspaceWhat = fRevertCurrentWorkspace
			? kMsgCurrentWorkspace : kMsgAllWorkspaces;
		BMenuItem* workspaceItem = fWorkspaceMenu->FindItem(workspaceWhat);
		if (workspaceItem != NULL)
			workspaceItem->SetMarked(true);
	}

	fLastImageIndex = fRevertImageIndex;
	_UpdatePreview();
	_UpdateButtons();
}


'''
    # Insert before _UpdateButtons
    text = cpp.read_text(encoding="utf-8")
    if "_RestoreRevertSnapshot" in text and "fRevertOffset.x = atoi" in text:
        print("already BackgroundsView.cpp (impl)")
        return
    needle = "void\nBackgroundsView::_UpdateButtons()\n"
    if needle not in text:
        raise SystemExit("could not insert Backgrounds snapshot impl")
    cpp.write_text(text.replace(needle, impl + needle, 1), encoding="utf-8")
    print(f"patched {cpp} (Haiku #1006 impl)")


def patch_label_clip() -> None:
    box = HAIKU / "src/kits/interface/Box.cpp"
    replace_once(
        box,
        """		fLayoutData->label_box.Set(6.0f, 0, 14.0f + StringWidth(fLabel),
			ceilf(fontHeight.ascent));
		labelHeight = ceilf(fontHeight.ascent + fontHeight.descent) + 1;
""",
        """		// Full line, including leading, so a large font is not clipped
		// against the top of the view (Haiku #11625 #18518).
		labelHeight = ceilf(fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading);
		fLayoutData->label_box.Set(6.0f, 0, 14.0f + StringWidth(fLabel),
			labelHeight);
""",
        "Haiku #11625 #18518",
    )
    replace_once(
        box,
        """		// offset label up by 1/6 the font height
		float lineHeight = fontHeight.ascent + fontHeight.descent;
		float yOffset = roundf(lineHeight / 6.0f);

		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(fLabel, BPoint(10.0f, fontHeight.ascent - yOffset));
""",
        """		// Baseline at the ascent. A negative offset draws above y=0 and
		// the view clips the top of the glyphs (Haiku #11625 #18518).
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(fLabel, BPoint(10.0f, ceilf(fontHeight.ascent)));
""",
        "Haiku #11625 draw",
    )

    sep = HAIKU / "src/kits/interface/SeparatorView.cpp"
    replace_once(
        sep,
        """		height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
""",
        """		height = ceilf(fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading);
""",
        "Haiku #18813 height",
    )
    replace_once(
        sep,
        """		if (fOrientation == B_HORIZONTAL) {
			DrawString(fLabel.String(), BPoint(labelBounds.left,
				labelBounds.top + ceilf(fontHeight.ascent)));
		} else {
""",
        """		float textHeight = ceilf(fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading);
		float spare = labelBounds.Height() + 1.0f - textHeight;
		float textTop = labelBounds.top;
		if (spare > 0.0f)
			textTop += floorf(spare / 2.0f);
		if (fOrientation == B_HORIZONTAL) {
			DrawString(fLabel.String(), BPoint(labelBounds.left,
				textTop + ceilf(fontHeight.ascent)));
		} else {
""",
        "Haiku #18813 baseline",
    )


def main() -> int:
    patch_alert()
    patch_label_clip()
    patch_scrollview()
    patch_listview()
    patch_desktop_selection()
    patch_desktop_icon_size()
    patch_softwareupdater()
    patch_backgrounds()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
