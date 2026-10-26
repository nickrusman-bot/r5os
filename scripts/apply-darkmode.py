#!/usr/bin/env python3
"""Apply easy dark-mode UI fixes (#20009 scrollbar, #19836 BeControlLook)."""
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
    replace_once(hcl, border_ctx_old, border_ctx_new, "scrollbar border")

    be = HAIKU / "src/add-ons/control_look/BeControlLook/BeControlLook.cpp"
    replace_once(be, BE_BG_OLD, BE_BG_NEW, "Haiku #19836 fill")
    replace_once(be, BE_FRAME_OLD, BE_FRAME_NEW, "Haiku #19836 frame")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
