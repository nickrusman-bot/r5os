#!/usr/bin/env python3
"""Apply easy dark-mode UI fixes (#20009 scrollbar, #19836 BeControlLook,
#19629 flat buttons, #19630 scrollbar buttons, #20418 custom button color,
#20372 plain BTextView)."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")

SCROLL_OLD = """	if (isEnabled) {
		gradient1Tint = 1.10;
		gradient2Tint = 1.05;
		darkEdge1Tint = B_DARKEN_3_TINT;
		darkEdge2Tint = B_DARKEN_2_TINT;
		shadowTint = gradient1Tint;
	} else {
		gradient1Tint = 0.9;
		gradient2Tint = 0.8;
		darkEdge1Tint = B_DARKEN_2_TINT;
		darkEdge2Tint = B_DARKEN_2_TINT;
		shadowTint = gradient1Tint;
	}"""

SCROLL_NEW = """	if (base.IsLight()) {
		if (isEnabled) {
			gradient1Tint = 1.10;
			gradient2Tint = 1.05;
			darkEdge1Tint = B_DARKEN_3_TINT;
			darkEdge2Tint = B_DARKEN_2_TINT;
			shadowTint = gradient1Tint;
		} else {
			gradient1Tint = 0.9;
			gradient2Tint = 0.8;
			darkEdge1Tint = B_DARKEN_2_TINT;
			darkEdge2Tint = B_DARKEN_2_TINT;
			shadowTint = gradient1Tint;
		}
	} else {
		// Dark panel: lighten the trough and edges so the bar reads against
		// the background (Haiku #20009).
		if (isEnabled) {
			gradient1Tint = 0.88;
			gradient2Tint = 0.78;
			darkEdge1Tint = 0.70;
			darkEdge2Tint = 0.82;
			shadowTint = gradient1Tint;
		} else {
			gradient1Tint = 0.92;
			gradient2Tint = 0.86;
			darkEdge1Tint = 0.80;
			darkEdge2Tint = 0.88;
			shadowTint = gradient1Tint;
		}
	}"""

BORDER_OLD = """	view->SetHighColor(tint_color(base, B_DARKEN_2_TINT));"""

BORDER_NEW = """	view->SetHighColor(tint_color(base,
		base.IsLight() ? B_DARKEN_2_TINT : 0.75f));"""

BE_BG_OLD = """	// fill the button area
	view->SetHighColor(tint_color(base, B_LIGHTEN_1_TINT));
	view->FillRect(rect);"""

BE_BG_NEW = """	// fill the button area
	view->SetHighColor(tint_color(base,
		base.IsLight() ? B_LIGHTEN_1_TINT : B_LIGHTEN_2_TINT));
	view->FillRect(rect);"""

BE_FRAME_OLD = """	rgb_color lighten1 = tint_color(base, B_LIGHTEN_1_TINT); // 231
	lighten1.red++; lighten1.green++; lighten1.blue++; // 232 = 231 + 1"""

BE_FRAME_NEW = """	rgb_color lighten1 = tint_color(base,
		base.IsLight() ? B_LIGHTEN_1_TINT : B_LIGHTEN_2_TINT);
	if (base.IsLight()) {
		lighten1.red++; lighten1.green++; lighten1.blue++;
	}"""


def replace_all(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        if new in text:
            print(f"already {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new), encoding="utf-8")
    print(f"patched {path} ({tag})")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if tag in text and old not in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def main() -> int:
    hcl = HAIKU / "src/kits/interface/HaikuControlLook.cpp"
    replace_once(hcl, SCROLL_OLD, SCROLL_NEW, "Haiku #20009")
    # only the border in DrawScrollBarBorder — first exact SetHighColor DARKEN_2
    # after DrawScrollBarBorder is unique enough if we include the comment.
    border_ctx_old = """	bool isEnabled = (flags & B_DISABLED) == 0;
	bool isFocused = (flags & B_FOCUSED) != 0;

	view->SetHighColor(tint_color(base, B_DARKEN_2_TINT));"""
    border_ctx_new = """	bool isEnabled = (flags & B_DISABLED) == 0;
	bool isFocused = (flags & B_FOCUSED) != 0;

	view->SetHighColor(tint_color(base,
		base.IsLight() ? B_DARKEN_2_TINT : 0.75f));"""
    replace_once(hcl, border_ctx_old, border_ctx_new, "0.75f")

    be = HAIKU / "src/add-ons/control_look/BeControlLook/BeControlLook.cpp"
    replace_once(be, BE_BG_OLD, BE_BG_NEW,
        "base.IsLight() ? B_LIGHTEN_1_TINT : B_LIGHTEN_2_TINT));\n\tview->FillRect(rect);")
    replace_once(be, BE_FRAME_OLD, BE_FRAME_NEW,
        "if (base.IsLight()) {\n\t\tlighten1.red++; lighten1.green++; lighten1.blue++;\n\t}")

    scroll = HAIKU / "src/kits/interface/ScrollBar.cpp"
    button_colors = """	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	// Arrow buttons follow the control color. The trough stays the panel
	// color. Haiku #19630 button colors
	rgb_color button = ui_color(B_CONTROL_BACKGROUND_COLOR);
	rgb_color buttonText = ui_color(B_CONTROL_TEXT_COLOR);

"""
    extra_buttons = """	rgb_color buttonText = ui_color(B_CONTROL_TEXT_COLOR);
	// Arrow buttons follow the control color. The trough stays the panel
	// color. Haiku #19630 button colors
	rgb_color button = ui_color(B_CONTROL_BACKGROUND_COLOR);
	rgb_color buttonText = ui_color(B_CONTROL_TEXT_COLOR);
"""
    scroll_text = scroll.read_text(encoding="utf-8")
    if extra_buttons in scroll_text:
        scroll_text = scroll_text.replace(
            extra_buttons,
            "\trgb_color buttonText = ui_color(B_CONTROL_TEXT_COLOR);\n",
            1,
        )
        print(f"collapsed duplicate in {scroll.name}")
    if button_colors not in scroll_text:
        old = """	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);

"""
        if old not in scroll_text:
            raise SystemExit(f"pattern not found in {scroll} (Haiku #19630 button colors)")
        scroll_text = scroll_text.replace(old, button_colors, 1)
        print(f"patched {scroll} (Haiku #19630 button colors)")
    else:
        print(f"already {scroll.name} (Haiku #19630 button colors)")
    scroll.write_text(scroll_text, encoding="utf-8")
    replace_all(
        scroll,
        "base, text, flags",
        "button, buttonText, flags",
        "Haiku #19630 button args",
    )

    button = HAIKU / "src/kits/interface/Button.cpp"
    replace_once(
        button,
        """	// Behave like BeOS: the colors of the button do not depend on the current view colors.
	// This is relied upon by some code that overrides Draw, changes the colors before calling
	// the superclass draw, and don't expect that to have any effect. For example, this is seen
	// in LibLayout used in Wonderbrush.
	rgb_color text = ui_color(B_CONTROL_TEXT_COLOR);
	rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
""",
        """	// High and low color changes inside an overridden Draw stay ignored
	// (LibLayout / Wonderbrush). SetViewColor() clears the UI color and is
	// the custom face. A flat button follows its parent so hover does not
	// jump to the control gray. Haiku #20418 #19629 button face
	rgb_color text = ui_color(B_CONTROL_TEXT_COLOR);
	rgb_color base = ui_color(B_CONTROL_BACKGROUND_COLOR);
	if (ViewUIColor() == B_NO_COLOR) {
		base = ViewColor();
		if (base.IsDark() == text.IsDark()) {
			text = base.IsDark()
				? make_color(255, 255, 255, 255)
				: make_color(0, 0, 0, 255);
		}
	} else if (_Flag(FLAG_FLAT) && Parent() != NULL) {
		base = Parent()->LowColor();
	}
""",
        "Haiku #20418 #19629 button face",
    )

    textview = HAIKU / "src/kits/interface/TextView.cpp"
    color_case = """		case B_COLORS_UPDATED:
			// Plain text keeps one run. Styled runs stay put: a run does not
			// record which UI color it came from. Haiku #20372 plain text color
			if (!fStylable
				&& message->HasColor(ui_color_name(B_DOCUMENT_TEXT_COLOR))) {
				rgb_color textColor = HighColor();
				SetFontAndColor(0, fText->Length(), NULL, 0, &textColor);
			}
			break;

"""
    text = textview.read_text(encoding="utf-8")
    while text.count(color_case) > 1:
        text = text.replace(color_case, "", 1)
        textview.write_text(text, encoding="utf-8")
        text = textview.read_text(encoding="utf-8")
        print(f"collapsed duplicate in {textview.name}")
    if color_case in text:
        print(f"already {textview.name} (Haiku #20372 plain text color)")
    else:
        old = """		case B_SELECT_ALL:
			SelectAll();
			break;

		case B_INPUT_METHOD_EVENT:
"""
        new = """		case B_SELECT_ALL:
			SelectAll();
			break;

""" + color_case + """		case B_INPUT_METHOD_EVENT:
"""
        if old not in text:
            raise SystemExit(f"pattern not found in {textview} (Haiku #20372)")
        textview.write_text(text.replace(old, new, 1), encoding="utf-8")
        print(f"patched {textview} (Haiku #20372 plain text color)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
