#!/usr/bin/env python3
"""Global UI scale in Screen prefs (Haiku #13087)."""
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


def main() -> int:
    replace_once(
        HAIKU / "src/kits/interface/Window.cpp",
        """#include <WindowPrivate.h>
""",
        """#include <WindowPrivate.h>
#include <InterfacePrivate.h>
""",
        "Haiku #13087 Window font include",
    )
    replace_once(
        HAIKU / "src/kits/interface/Window.cpp",
        """		case B_FONTS_UPDATED:
		{
			fTopView->_FontsUpdated(message);
			target->MessageReceived(message);
			break;
		}
""",
        """		case B_FONTS_UPDATED:
		{
			_init_global_fonts_();
			fTopView->_FontsUpdated(message);
			target->MessageReceived(message);
			Layout(false);
			break;
		}
""",
        "Haiku #13087 reload fonts",
    )

    replace_once(
        HAIKU / "src/preferences/screen/Constants.h",
        """static const uint32 POP_RESOLUTION_MSG = 'pres';
""",
        """static const uint32 POP_RESOLUTION_MSG = 'pres';
static const uint32 POP_UI_SCALE_MSG = 'pusc';
""",
        "Haiku #13087 scale message",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.h",
        """			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;
""",
        """			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;
			BPopUpMenu*		fScaleMenu;
			BMenuField*		fScaleField;
""",
        "Haiku #13087 scale members",
    )
    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.h",
            """			float			fOriginalBrightness;
			bool			fModified;
""",
            """			float			fOriginalBrightness;
			float			fOriginalScale;
			float			fSelectedScale;
			bool			fModified;

			void			_ApplyUiScale(float scale);
			void			_MarkUiScale(float scale);
""",
        "Haiku #13087 scale state",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """#include <Catalog.h>
""",
        """#include <AppDefs.h>
#include <Catalog.h>
""",
        "Haiku #13087 AppDefs include",
    )
    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
""",
        """#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Menu.h>
""",
        "Haiku #13087 Font include",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """//	#pragma mark - ScreenWindow
""",
        """extern void _set_system_font_(const char* which, font_family family,
	font_style style, float size);


static float
r5_read_ui_scale()
{
	float scale = 1.0f;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("r5_ui_scale");
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			file.Read(&scale, sizeof(scale));
	}
	if (scale < 1.0f)
		scale = 1.0f;
	if (scale > 2.0f)
		scale = 2.0f;
	return scale;
}


static void
r5_write_ui_scale(float scale)
{
	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK)
		return;
	BPath path(settings);
	path.Append("r5_ui_scale");
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		file.Write(&scale, sizeof(scale));
	path.SetTo(settings.Path());
	path.Append("r5_cursor_scale");
	BFile cursor(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (cursor.InitCheck() == B_OK)
		cursor.Write(&scale, sizeof(scale));
}


//	#pragma mark - ScreenWindow
""",
        "Haiku #13087 scale helpers",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """	fUndoScreenMode(this),
	fModified(false)
{
	BScreen screen(this);
""",
        """	fUndoScreenMode(this),
	fOriginalScale(1.0f),
	fSelectedScale(1.0f),
	fModified(false)
{
	BScreen screen(this);
""",
        "Haiku #13087 ctor scale",
    )
    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """	_UpdateOriginal();
	_BuildSupportedColorSpaces();
	fActive = fSelected = fOriginal;
""",
        """	_UpdateOriginal();
	_BuildSupportedColorSpaces();
	fActive = fSelected = fOriginal;
	fOriginalScale = fSelectedScale = r5_read_ui_scale();
""",
        "Haiku #13087 read scale",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);

	fColorsMenu = new BPopUpMenu("colors", true, false);
""",
        """	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);

	fScaleMenu = new BPopUpMenu("ui scale", true, true);
	static const float kUiScales[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
	for (int32 i = 0; i < 5; i++) {
		BString label;
		label.SetToFormat("%g%%", kUiScales[i] * 100.0f);
		BMessage* scaleMessage = new BMessage(POP_UI_SCALE_MSG);
		scaleMessage->AddFloat("scale", kUiScales[i]);
		BMenuItem* item = new BMenuItem(label.String(), scaleMessage);
		fScaleMenu->AddItem(item);
		if (kUiScales[i] == fSelectedScale)
			item->SetMarked(true);
	}
	if (fScaleMenu->FindMarked() == NULL && fScaleMenu->ItemAt(0) != NULL)
		fScaleMenu->ItemAt(0)->SetMarked(true);
	fScaleField = new BMenuField("ScaleMenu", B_TRANSLATE("Scale:"),
		fScaleMenu);
	fScaleField->SetAlignment(B_ALIGN_RIGHT);

	fColorsMenu = new BPopUpMenu("colors", true, false);
""",
        "Haiku #13087 scale menu",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """			.Add(fResolutionField->CreateLabelLayoutItem(), 0, 0)
			.Add(fResolutionField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fColorsField->CreateLabelLayoutItem(), 0, 1)
			.Add(fColorsField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fRefreshField->CreateLabelLayoutItem(), 0, 2)
			.Add(fRefreshField->CreateMenuBarLayoutItem(), 1, 2)
			.Add(fCombineField->CreateLabelLayoutItem(), 0, 3)
			.Add(fCombineField->CreateMenuBarLayoutItem(), 1, 3)
			.Add(fSwapDisplaysField->CreateLabelLayoutItem(), 0, 4)
			.Add(fSwapDisplaysField->CreateMenuBarLayoutItem(), 1, 4)
			.Add(fUseLaptopPanelField->CreateLabelLayoutItem(), 0, 5)
			.Add(fUseLaptopPanelField->CreateMenuBarLayoutItem(), 1, 5)
			.Add(fTVStandardField->CreateLabelLayoutItem(), 0, 6)
			.Add(fTVStandardField->CreateMenuBarLayoutItem(), 1, 6)
""",
        """			.Add(fResolutionField->CreateLabelLayoutItem(), 0, 0)
			.Add(fResolutionField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fScaleField->CreateLabelLayoutItem(), 0, 1)
			.Add(fScaleField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fColorsField->CreateLabelLayoutItem(), 0, 2)
			.Add(fColorsField->CreateMenuBarLayoutItem(), 1, 2)
			.Add(fRefreshField->CreateLabelLayoutItem(), 0, 3)
			.Add(fRefreshField->CreateMenuBarLayoutItem(), 1, 3)
			.Add(fCombineField->CreateLabelLayoutItem(), 0, 4)
			.Add(fCombineField->CreateMenuBarLayoutItem(), 1, 4)
			.Add(fSwapDisplaysField->CreateLabelLayoutItem(), 0, 5)
			.Add(fSwapDisplaysField->CreateMenuBarLayoutItem(), 1, 5)
			.Add(fUseLaptopPanelField->CreateLabelLayoutItem(), 0, 6)
			.Add(fUseLaptopPanelField->CreateMenuBarLayoutItem(), 1, 6)
			.Add(fTVStandardField->CreateLabelLayoutItem(), 0, 7)
			.Add(fTVStandardField->CreateMenuBarLayoutItem(), 1, 7)
""",
        "Haiku #13087 scale layout",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """		case POP_RESOLUTION_MSG:
		{
			message->FindInt32("width", &fSelected.width);
			message->FindInt32("height", &fSelected.height);
""",
        """		case POP_UI_SCALE_MSG:
		{
			float scale = 1.0f;
			if (message->FindFloat("scale", &scale) == B_OK)
				_ApplyUiScale(scale);
			_CheckApplyEnabled();
			break;
		}

		case POP_RESOLUTION_MSG:
		{
			message->FindInt32("width", &fSelected.width);
			message->FindInt32("height", &fSelected.height);
""",
        "Haiku #13087 scale handler",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """			screen.SetBrightness(fOriginalBrightness);
			fBrightnessSlider->SetValue(fOriginalBrightness * 255);

			_UpdateActiveMode();
			break;
		}
""",
        """			screen.SetBrightness(fOriginalBrightness);
			fBrightnessSlider->SetValue(fOriginalBrightness * 255);
			_ApplyUiScale(fOriginalScale);

			_UpdateActiveMode();
			break;
		}
""",
        "Haiku #13087 revert scale",
    )

    replace_once(
        HAIKU / "src/preferences/screen/ScreenWindow.cpp",
        """	fRevertButton->SetEnabled(columns != fOriginalWorkspacesColumns
		|| rows != fOriginalWorkspacesRows
		|| brightness != fOriginalBrightness
		|| fSelected != fOriginal);
}
""",
        """	fRevertButton->SetEnabled(columns != fOriginalWorkspacesColumns
		|| rows != fOriginalWorkspacesRows
		|| brightness != fOriginalBrightness
		|| fSelected != fOriginal
		|| fSelectedScale != fOriginalScale);
}


void
ScreenWindow::_MarkUiScale(float scale)
{
	for (int32 i = 0; i < fScaleMenu->CountItems(); i++) {
		BMenuItem* item = fScaleMenu->ItemAt(i);
		if (item == NULL)
			continue;
		BMessage* message = item->Message();
		float value = 0.0f;
		item->SetMarked(message != NULL
			&& message->FindFloat("scale", &value) == B_OK && value == scale);
	}
}


void
ScreenWindow::_ApplyUiScale(float scale)
{
	if (scale < 1.0f)
		scale = 1.0f;
	if (scale > 2.0f)
		scale = 2.0f;
	float from = fSelectedScale;
	if (from < 0.01f)
		from = 1.0f;
	if (scale == from) {
		_MarkUiScale(scale);
		return;
	}

	float factor = scale / from;
	const BFont* fonts[] = {be_plain_font, be_bold_font, be_fixed_font};
	const char* names[] = {"plain", "bold", "fixed"};
	for (int32 i = 0; i < 3; i++) {
		font_family family;
		font_style style;
		fonts[i]->GetFamilyAndStyle(&family, &style);
		float size = fonts[i]->Size() * factor;
		if (size < 8.0f)
			size = 8.0f;
		if (size > 36.0f)
			size = 36.0f;
		_set_system_font_(names[i], family, style, size);
	}

	menu_info info;
	if (get_menu_info(&info) == B_OK) {
		info.font_size *= factor;
		if (info.font_size < 8.0f)
			info.font_size = 8.0f;
		if (info.font_size > 36.0f)
			info.font_size = 36.0f;
		set_menu_info(&info);
	}

	r5_write_ui_scale(scale);
	fSelectedScale = scale;
	_MarkUiScale(scale);
	_init_global_fonts_();
	BMessage updated(B_FONTS_UPDATED);
	be_roster->Broadcast(&updated);
}
""",
        "Haiku #13087 apply scale",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
