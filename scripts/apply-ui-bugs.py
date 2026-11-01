#!/usr/bin/env python3
"""Apply easy UI bugfixes: #11138 #18057 #16838 #16471 #16116 #15690 #1006
#11625 #18518 #18813 #11735 #13680 #6873 #18648. The clock
also follows the 24-hour choice."""
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


def replace_if_missing(path: pathlib.Path, old: str, new: str, tag: str,
        marker: str) -> None:
    """Skip when marker is already present.

    replace_once applies again when `new` still contains `old`.
    """
    text = path.read_text(encoding="utf-8")
    if marker in text:
        print(f"already {path.name} ({tag})")
        return
    replace_once(path, old, new, tag)


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


def patch_small_controls() -> None:
    text_input_h = HAIKU / "src/kits/interface/TextInput.h"
    replace_once(
        text_input_h,
        """		void			SetInitialText();

virtual	void			Paste(BClipboard *clipboard);
""",
        """		void			SetInitialText();
		void			SetText(const char* text,
							const text_run_array* runs = NULL);

virtual	void			Paste(BClipboard *clipboard);
""",
        "Haiku #11735 set text decl",
    )
    replace_once(
        text_input_h,
        """		bool			fInMouseDown;
};
""",
        """		bool			fInMouseDown;
		bool			fSuppressModification;
};
""",
        "Haiku #11735 suppress field",
    )

    text_input = HAIKU / "src/kits/interface/TextInput.cpp"
    replace_once(
        text_input,
        """	BTextView(frame, "_input_", textRect, resizeMask, flags),
	fPreviousText(NULL),
	fInMouseDown(false)
""",
        """	BTextView(frame, "_input_", textRect, resizeMask, flags),
	fPreviousText(NULL),
	fInMouseDown(false),
	fSuppressModification(false)
""",
        "Haiku #11735 init frame",
    )
    replace_once(
        text_input,
        """	BTextView(archive),
	fPreviousText(NULL),
	fInMouseDown(false)
""",
        """	BTextView(archive),
	fPreviousText(NULL),
	fInMouseDown(false),
	fSuppressModification(false)
""",
        "Haiku #11735 init archive",
    )
    replace_once(
        text_input,
        """void
_BTextInput_::SetInitialText()
{
	free(fPreviousText);
	fPreviousText = NULL;

	if (Text() != NULL)
		fPreviousText = strdup(Text());
}


void
_BTextInput_::Paste(BClipboard* clipboard)
""",
        """void
_BTextInput_::SetInitialText()
{
	free(fPreviousText);
	fPreviousText = NULL;

	if (Text() != NULL)
		fPreviousText = strdup(Text());
}


void
_BTextInput_::SetText(const char* text, const text_run_array* runs)
{
	// Programmatic text must not look like the user typed it
	// (Haiku #11735).
	fSuppressModification = true;
	BTextView::SetText(text, runs);
	fSuppressModification = false;
}


void
_BTextInput_::Paste(BClipboard* clipboard)
""",
        "Haiku #11735 set text",
    )
    replace_once(
        text_input,
        """	TextControl()->InvokeNotify(TextControl()->ModificationMessage(),
		B_CONTROL_MODIFIED);
}


void
_BTextInput_::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);

	TextControl()->InvokeNotify(TextControl()->ModificationMessage(),
		B_CONTROL_MODIFIED);
}
""",
        """	if (!fSuppressModification) {
		TextControl()->InvokeNotify(TextControl()->ModificationMessage(),
			B_CONTROL_MODIFIED);
	}
}


void
_BTextInput_::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);

	if (!fSuppressModification) {
		TextControl()->InvokeNotify(TextControl()->ModificationMessage(),
			B_CONTROL_MODIFIED);
	}
}
""",
        "Haiku #11735 skip notify",
    )

    slider = HAIKU / "src/kits/interface/Slider.cpp"
    replace_once(
        slider,
        """	if (!IsEnabled())
		return;

	if (BarFrame().Contains(point) || ThumbFrame().Contains(point))
		fInitialLocation = _Location();
""",
        """	if (!IsEnabled())
		return;

	// A click in the padding is not a slider click.
	if (!BarFrame().Contains(point) && !ThumbFrame().Contains(point))
		return;

	fInitialLocation = _Location();
""",
        "Haiku #13680 click on bar",
    )
    replace_once(
        slider,
        """			}
		}
		if (_Location() != fInitialLocation)
			Invoke();
	}
}
""",
        """			}
		}
		// A second click on the same value still sends the message
		// (Haiku #13680).
		Invoke();
	}
}
""",
        "Haiku #13680 sync invoke",
    )
    replace_once(
        slider,
        """	if (IsTracking()) {
		if (_Location() != fInitialLocation)
			Invoke();

		SetTracking(false);
""",
        """	if (IsTracking()) {
		// A second click on the same value still sends the message
		// (Haiku #13680).
		Invoke();

		SetTracking(false);
""",
        "Haiku #13680 async invoke",
    )

    scroll_bar = HAIKU / "src/kits/interface/ScrollBar.cpp"
    replace_once(
        scroll_bar,
        """	bool upEnabled = fValue > fMin;
""",
        """	// A bar that already shows everything keeps its arrows off
	// (Haiku #6873). Proportion 0 is the non-proportional mode.
	bool canScroll = fMin < fMax && fProportion >= 0.0f
		&& fProportion < 1.0f;
	bool upEnabled = canScroll && fValue > fMin;
""",
        "Haiku #6873 up arrow",
    )
    replace_once(
        scroll_bar,
        """	bool downEnabled = fValue < fMax;
""",
        """	bool downEnabled = canScroll && fValue < fMax;
""",
        "Haiku #6873 down arrow",
    )


    option_pop_up = HAIKU / "src/kits/interface/OptionPopUp.cpp"
    replace_once(
        option_pop_up,
        """void
BOptionPopUp::SetValue(int32 value)
{
	BControl::SetValue(value);
	BMenu* menu = fMenuField->Menu();
	if (menu == NULL)
		return;

	int32 numItems = menu->CountItems();
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item && item->Message()) {
			int32 itemValue;
			item->Message()->FindInt32("be:value", &itemValue);
			if (itemValue == value) {
				item->SetMarked(true);
				break;
			}
		}
	}
}
""",
        """void
BOptionPopUp::SetValue(int32 value)
{
	BMenu* menu = fMenuField->Menu();
	if (menu == NULL) {
		BControl::SetValue(value);
		return;
	}

	int32 numItems = menu->CountItems();
	for (int32 i = 0; i < numItems; i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item == NULL || item->Message() == NULL)
			continue;

		int32 itemValue;
		if (item->Message()->FindInt32("be:value", &itemValue) != B_OK)
			continue;
		if (itemValue != value)
			continue;

		item->SetMarked(true);
		BControl::SetValue(value);
		return;
	}
}
""",
        "Haiku #18648 unknown value",
    )


def patch_locale_clock() -> None:
    header = HAIKU / "headers/os/locale/FormattingConventions.h"
    replace_once(
        header,
        """			void				UnsetExplicitUse24HourClock();

	virtual	status_t			Archive(BMessage* archive,
""",
        """			void				UnsetExplicitUse24HourClock();
			void				CoerceFormatForClock(BString& outFormat) const;

	virtual	status_t			Archive(BMessage* archive,
""",
        "24-hour coerce public",
    )
    replace_once(
        header,
        """			U_ICU_NAMESPACE::Locale*		fICULocale;
			void				CoerceFormatForClock(BString& outFormat) const;
""",
        """			U_ICU_NAMESPACE::Locale*		fICULocale;
""",
        "24-hour coerce private",
    )

    conventions = HAIKU / "src/kits/locale/FormattingConventions.cpp"
    replace_once(
        conventions,
        """		return fCachedUse24HourClock == CLOCK_HOURS_24;
	}

	return fExplicitUse24HourClock == CLOCK_HOURS_24;
""",
        """		return fCachedUse24HourClock == CLOCK_HOURS_24;
	}

	// A cached 12/24 choice must stick when no explicit choice is stored.
	return use24HourClock == CLOCK_HOURS_24;
""",
        "24-hour clock cache",
    )

    date_time = HAIKU / "src/kits/locale/DateTimeFormat.cpp"
    replace_once(
        date_time,
        """	UErrorCode error = U_ZERO_ERROR;
	DateTimePatternGenerator* generator
		= DateTimePatternGenerator::createInstance(
			*BLanguage::Private(&fLanguage).ICULocale(), error);
""",
        """	UErrorCode error = U_ZERO_ERROR;
	// Month names follow the preferred language only when that checkbox is
	// on. The hour cycle follows the formatting country otherwise, so a
	// Dutch country does not keep an English AM/PM pattern.
	Locale* icuLocale = fConventions.UseStringsFromPreferredLanguage()
		? BLanguage::Private(&fLanguage).ICULocale()
		: BFormattingConventions::Private(&fConventions).ICULocale();
	DateTimePatternGenerator* generator
		= DateTimePatternGenerator::createInstance(*icuLocale, error);
""",
        "deskbar clock locale",
    )
    coerced = """	BString buffer;
	BStringByteSink stringConverter(&buffer);
	pattern.toUTF8(stringConverter);

	// The pattern generator can keep AM/PM for an English locale after the
	// skeleton asked for 24-hour. The preflet examples already coerce.
	fConventions.CoerceFormatForClock(buffer);

	fConventions.SetExplicitDateTimeFormat(dateStyle, timeStyle, buffer);
"""
    original = """	BString buffer;
	BStringByteSink stringConverter(&buffer);
	pattern.toUTF8(stringConverter);

	fConventions.SetExplicitDateTimeFormat(dateStyle, timeStyle, buffer);
"""
    text = date_time.read_text(encoding="utf-8")
    if coerced in text:
        print(f"already {date_time.name} (deskbar clock pattern)")
    elif original in text:
        date_time.write_text(text.replace(original, coerced, 1), encoding="utf-8")
        print(f"patched {date_time} (deskbar clock pattern)")
    else:
        raise SystemExit(f"pattern not found in {date_time} (deskbar clock pattern)")
    replace_once(
        conventions,
        """			case 'h':
				if (!inQuote)
					*previousPos = 'H';
				break;
			case 'k':
				if (!inQuote)
					*previousPos = 'K';
				break;
			case 'a':
				if (!inQuote) {
""",
        """			case 'h':
			case 'K':
			case 'k':
				// H is 0-23. K is 0-11 and k is 1-24; both must become H or
				// 18:00 still draws as 6.
				if (!inQuote)
					*previousPos = 'H';
				break;
			case 'a':
			case 'b':
			case 'B':
				if (!inQuote) {
""",
        "24-hour pattern letters",
    )
    replace_once(
        conventions,
        """	if (use24HourClock != CLOCK_HOURS_UNSET) {
		// adjust to 12/24-hour clock as requested
		bool localeUses24HourClock = !FormatUsesAmPm(outFormat);
		if (localeUses24HourClock) {
			if (use24HourClock == CLOCK_HOURS_12)
				CoerceFormatTo12HourClock(outFormat);
		} else {
			if (use24HourClock == CLOCK_HOURS_24)
				CoerceFormatTo24HourClock(outFormat);
		}
	}
""",
        """	if (use24HourClock == CLOCK_HOURS_24) {
		// Also rewrite a 12-hour pattern that has no AM/PM marker.
		CoerceFormatTo24HourClock(outFormat);
	} else if (use24HourClock == CLOCK_HOURS_12
		&& !FormatUsesAmPm(outFormat)) {
		CoerceFormatTo12HourClock(outFormat);
	}
""",
        "24-hour coerce always",
    )

    patch_locale_broadcast()
    patch_clock_display()


def patch_locale_broadcast() -> None:
    """Other programs must see a locale change even when the settings file
    cannot be written. The preflet preview uses its own roster.

    Confirmed on the 20:58 image: Dutch (Netherlands) made the Deskbar
    clock 20:01, matching the preview. Do not gate the broadcast on
    _SaveLocaleSettings(). A later language message has no conventions
    field; Refresh() before ApplySettings can put a stale file back and
    the clock returns to AM/PM. That part is still open.
    """
    header = HAIKU / "headers/private/locale/LocaleRosterData.h"
    replace_once(
        header,
        """			status_t			SetFilesystemTranslationPreferred(
									bool preferred);
private:
""",
        """			status_t			SetFilesystemTranslationPreferred(
									bool preferred);
			status_t			ApplyUpdatedSettings(const BMessage* message);

private:
""",
        "apply locale message",
    )
    public = HAIKU / "headers/os/locale/LocaleRoster.h"
    replace_if_missing(
        public,
        """			status_t			Refresh();
									// Refresh the internal data from the
									// settings file(s)
""",
        """			status_t			Refresh();
									// Refresh the internal data from the
									// settings file(s)

			status_t			ApplySettings(const BMessage* message);
									// Apply one B_LOCALE_CHANGED message on
									// top of the settings file. A field that
									// is not in the message is left as it was.
""",
        "public locale apply",
        "Apply one B_LOCALE_CHANGED message",
    )
    roster = HAIKU / "src/kits/locale/LocaleRoster.cpp"
    replace_if_missing(
        roster,
        """status_t
BLocaleRoster::Refresh()
{
	return fData->Refresh();
}
""",
        """status_t
BLocaleRoster::Refresh()
{
	return fData->Refresh();
}


status_t
BLocaleRoster::ApplySettings(const BMessage* message)
{
	return fData->ApplyUpdatedSettings(message);
}
""",
        "roster apply settings",
        "BLocaleRoster::ApplySettings",
    )

    data = HAIKU / "src/kits/locale/LocaleRosterData.cpp"
    live = """	// The preflet examples update from this process. Deskbar and Tracker
	// only hear the broadcast, so a failed save must not hide the change.
"""
    replace_once(
        data,
        """	status = _SetDefaultFormattingConventions(newFormattingConventions);

	if (status == B_OK)
		status = _SaveLocaleSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddDefaultFormattingConventionsToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        """	status = _SetDefaultFormattingConventions(newFormattingConventions);

	if (status == B_OK) {
""" + live + """		_SaveLocaleSettings();

		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddDefaultFormattingConventionsToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        "broadcast formatting",
    )
    replace_once(
        data,
        """	status = _SetDefaultTimeZone(newZone);

	if (status == B_OK)
		status = _SaveTimeSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddDefaultTimeZoneToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        """	status = _SetDefaultTimeZone(newZone);

	if (status == B_OK) {
""" + live + """		_SaveTimeSettings();

		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddDefaultTimeZoneToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        "broadcast time zone",
    )
    replace_once(
        data,
        """	status = _SetPreferredLanguages(languages);

	if (status == B_OK)
		status = _SaveLocaleSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddPreferredLanguagesToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        """	status = _SetPreferredLanguages(languages);

	if (status == B_OK) {
""" + live + """		_SaveLocaleSettings();

		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddPreferredLanguagesToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        "broadcast languages",
    )
    replace_once(
        data,
        """	_SetFilesystemTranslationPreferred(preferred);

	status_t status = _SaveLocaleSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddFilesystemTranslationPreferenceToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}
""",
        """	_SetFilesystemTranslationPreferred(preferred);

""" + live + """	_SaveLocaleSettings();

	BMessage updateMessage(B_LOCALE_CHANGED);
	status_t status = _AddFilesystemTranslationPreferenceToMessage(
		&updateMessage);
	if (status == B_OK)
		status = be_roster->Broadcast(&updateMessage);
""",
        "broadcast folder names",
    )
    replace_once(
        data,
        """	if (status == B_OK)
		status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);

	BFile file;
	if (status == B_OK) {
		path.Append("Locale settings");
""",
        """	if (status == B_OK) {
		// Create ~/config/settings when the first locale change is saved.
		status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	}

	BFile file;
	if (status == B_OK) {
		path.Append("Locale settings");
""",
        "create locale settings directory",
    )
    replace_if_missing(
        data,
        """status_t
LocaleRosterData::_LoadLocaleSettings()
""",
        """status_t
LocaleRosterData::ApplyUpdatedSettings(const BMessage* message)
{
	if (message == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	// Refresh() already loaded the file. Fields in this message win, so a
	// country or language change is visible when that file was not updated.
	BString conventionsID;
	if (message->FindString("conventions", &conventionsID) == B_OK) {
		BFormattingConventions conventions(message);
		_SetDefaultFormattingConventions(conventions);
	}

	if (message->HasString("language"))
		_SetPreferredLanguages(message);

	bool preferred;
	if (message->FindBool("filesys", &preferred) == B_OK)
		_SetFilesystemTranslationPreferred(preferred);

	return B_OK;
}


status_t
LocaleRosterData::_LoadLocaleSettings()
""",
        "apply locale message body",
        "LocaleRosterData::ApplyUpdatedSettings",
    )

    bar = HAIKU / "src/apps/deskbar/BarApp.cpp"
    replace_if_missing(
        bar,
        """#include "BarApp.h"
""",
        """#include "BarApp.h"

#include "DeskbarMenu.h"


namespace BPrivate {
	void ForceUnloadCatalog();
}
""",
        "deskbar menu include",
        '#include "DeskbarMenu.h"',
    )
    replace_once(
        bar,
        """		case B_LOCALE_CHANGED:
		{
			BLocaleRoster::Default()->Refresh();

			bool localize;
			if (message->FindBool("filesys", &localize) == B_OK)
				gLocalizedNamePreferred = localize;
		}
""",
        """		case B_LOCALE_CHANGED:
		{
			// The file may still be the previous locale. Apply the message
			// after Refresh() so that copy does not win.
			BLocaleRoster::Default()->Refresh();
			BLocaleRoster::Default()->ApplySettings(message);
			BPrivate::ForceUnloadCatalog();
			gLocalizedNamePreferred
				= BLocaleRoster::Default()->IsFilesystemTranslationPreferred();

			if (message->HasString("language") && fBarWindow != NULL
				&& fBarWindow->Lock()) {
				TDeskbarMenu* menu = fBarWindow->DeskbarMenu();
				if (menu != NULL && menu->LockLooper()) {
					menu->ForceRebuild();
					menu->UnlockLooper();
				}
				fBarWindow->Unlock();
			}
		}
""",
        "deskbar applies locale message",
    )


def patch_clock_display() -> None:
    """Draw the Deskbar clock with BTimeFormat, the same call as the preview.

    Confirmed: Dutch (Netherlands) showed 20:01 in Deskbar. The pattern
    generator kept AM/PM on the 19:05 and 19:34 images. Do not go back to
    BDateTimeFormat for the drawn string, and do not Refresh() the
    settings file from Pulse().
    """
    time_view = HAIKU / "src/apps/deskbar/TimeView.cpp"
    text = time_view.read_text(encoding="utf-8")
    marker = "The clock uses the same short time pattern as the Locale preflet."
    if marker in text:
        print(f"already {time_view.name} (clock display)")
        return

    old_pulse_v2 = """void
TTimeView::Pulse()
{
	// The locale broadcast refreshes the roster, then posts to the clock.
	// If that post is missed the clock keeps the pattern from boot.
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("Locale settings");

		struct stat st;
		static time_t sMtime = 0;
		bool changed = false;
		if (stat(path.Path(), &st) == 0 && st.st_mtime != sMtime) {
			sMtime = st.st_mtime;
			BLocaleRoster::Default()->Refresh();
			changed = true;
		}

		BFormattingConventions conventions;
		if (BLocale::Default()->GetFormattingConventions(&conventions) == B_OK) {
			static bool sSaw24 = false;
			static BString sSawID;
			bool use24 = conventions.Use24HourClock();
			if (changed || use24 != sSaw24 || sSawID != conventions.ID()) {
				sSaw24 = use24;
				sSawID = conventions.ID();
				UpdateTimeFormat();
			}
		}
	}

	time_t curTime = time(NULL);
"""
    old_pulse_v1 = """void
TTimeView::Pulse()
{
	time_t curTime = time(NULL);
"""
    new_pulse = """void
TTimeView::Pulse()
{
	// The clock uses the same short time pattern as the Locale preflet.
	// Reloading the settings file here would put en_US back when that
	// file was not written yet.
	BFormattingConventions conventions;
	if (BLocale::Default()->GetFormattingConventions(&conventions) == B_OK) {
		static bool sSaw24 = false;
		static BString sSawID;
		static bool sReady = false;
		bool use24 = conventions.Use24HourClock();
		if (!sReady || use24 != sSaw24 || sSawID != conventions.ID()) {
			sReady = true;
			sSaw24 = use24;
			sSawID = conventions.ID();
			UpdateTimeFormat();
		}
	}

	time_t curTime = time(NULL);
"""
    if old_pulse_v2 in text:
        text = text.replace(old_pulse_v2, new_pulse, 1)
    elif old_pulse_v1 in text:
        text = text.replace(old_pulse_v1, new_pulse, 1)
    else:
        raise SystemExit(f"pulse not found in {time_view}")

    old_get = """void
TTimeView::GetCurrentTime()
{
	fTimeFormat->Format(fCurrentTimeStr, sizeof(fCurrentTimeStr), fCurrentTime,
		B_SHORT_DATE_FORMAT, B_SHORT_TIME_FORMAT);
}
"""
    new_get = """void
TTimeView::GetCurrentTime()
{
	// BTimeFormat reads the formatting country's own pattern. The
	// date-time pattern generator can keep an English AM/PM skeleton.
	BTimeFormatStyle style = B_SHORT_TIME_FORMAT;
	if (fShowTimeZone)
		style = B_FULL_TIME_FORMAT;
	else if (fShowSeconds)
		style = B_MEDIUM_TIME_FORMAT;

	BString formatted;
	BTimeFormat timeFormat;
	if (timeFormat.Format(formatted, fCurrentTime, style) == B_OK
		&& formatted.Length() > 0) {
		strlcpy(fCurrentTimeStr, formatted.String(), sizeof(fCurrentTimeStr));
	}
}
"""
    if old_get not in text:
        raise SystemExit(f"GetCurrentTime not found in {time_view}")
    text = text.replace(old_get, new_get, 1)

    old_includes = """#include <string.h>
#include <sys/stat.h>

#include <String.h>

#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <FormattingConventions.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <MenuItem.h>
#include <Path.h>
"""
    fresh_includes = """#include <string.h>

#include <String.h>

#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
#include <Locale.h>
#include <MenuItem.h>
"""
    new_includes = """#include <string.h>

#include <String.h>

#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
#include <FormattingConventions.h>
#include <Locale.h>
#include <MenuItem.h>
#include <TimeFormat.h>
"""
    if old_includes in text:
        text = text.replace(old_includes, new_includes, 1)
    elif fresh_includes in text:
        text = text.replace(fresh_includes, new_includes, 1)
    elif "#include <TimeFormat.h>" not in text:
        raise SystemExit(f"includes not found in {time_view}")

    time_view.write_text(text, encoding="utf-8", newline="\n")
    print(f"patched {time_view} (clock display)")


def main() -> int:
    patch_alert()
    patch_label_clip()
    patch_scrollview()
    patch_listview()
    patch_desktop_selection()
    patch_desktop_icon_size()
    patch_softwareupdater()
    patch_backgrounds()
    patch_small_controls()
    patch_locale_clock()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
