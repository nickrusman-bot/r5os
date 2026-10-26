#!/usr/bin/env python3
"""Tracker desktop/list overlay: #2372 #2864 #3291 #8246 #20363 #20419
#19070 #16678 #17901 #18060 #18514 #19084."""
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
    pose = HAIKU / "src/kits/tracker/PoseView.cpp"
    replace_once(
        pose,
        """		case kIconMode:
		{
			const float gridOffset = ceilf(IconSizeInt() * 0.875f),
				offsetValue = ceilf(IconSizeInt() * 0.625f);
			grid->Set(IconSizeInt() + gridOffset, IconSizeInt() + gridOffset);
			offset->Set(offsetValue, offsetValue);
			break;
		}
""",
        """		case kIconMode:
		{
			const float gridOffset = ceilf(IconSizeInt() * 0.5f),
				offsetValue = ceilf(IconSizeInt() * 0.25f);
			grid->Set(IconSizeInt() + gridOffset, IconSizeInt() + gridOffset);
			offset->Set(offsetValue, offsetValue);
			break;
		}
""",
        "Haiku #2372 #18514 icon grid",
    )
    replace_once(
        pose,
        """		if (checkValidLocation && !rect.Intersects(viewBounds)) {
			fHintLocation = PinToGrid(BPoint(0.0, 0.0), fGrid, fOffset);
			pose->SetLocation(fHintLocation, this);
			rect = pose->CalcRect(this);
			break;
		}
""",
        """		if (checkValidLocation && !rect.Intersects(viewBounds)) {
			// Do not pile leftovers at 0,0 (Haiku #2864 #8246 #20363).
			fHintLocation = PinToGrid(
				BPoint(fOffset.x + 8, fOffset.y + 8), fGrid, fOffset);
			pose->SetLocation(fHintLocation, this);
			rect = pose->CalcRect(this);
			break;
		}
""",
        "Haiku #2864 #8246 #20363 place",
    )
    replace_once(
        pose,
        """		deskbarFrame.InsetBy(-10, -10);
		if (deskbarFrame.Intersects(rect))
			return false;
""",
        """		deskbarFrame.InsetBy(-24, -24);
		if (deskbarFrame.Intersects(rect))
			return false;
""",
        "Haiku #3291 deskbar inset",
    )
    replace_once(
        pose,
        """	if (maxWidth > kMinColumnWidth || maxWidth < column->Width()) {
		ResizeColumn(column, maxWidth);
		return true;
	}
""",
        """	if (maxWidth > column->Width()) {
		ResizeColumn(column, maxWidth);
		return true;
	}
""",
        "Haiku #18060 size column",
    )

    container = HAIKU / "src/kits/tracker/ContainerWindow.cpp"
    replace_once(
        container,
        """	// update "Clean up" on Desktop and bail out
	if (PoseView()->IsDesktopView()) {
		Shortcuts()->UpdateCleanupItem(Shortcuts()->FindItem(parent, kCleanup, kCleanupAll));
		return;
	}
""",
        """	if (PoseView()->IsDesktopView()) {
		Shortcuts()->UpdateCleanupItem(Shortcuts()->FindItem(parent, kCleanup, kCleanupAll));
		// still offer Arrange by on the Desktop (Haiku #20419)
	}
""",
        "Haiku #20419 arrange by",
    )

    status = HAIKU / "src/kits/tracker/StatusWindow.cpp"
    replace_once(
        status,
        """		SetHighColor(0, 0, 0);
		if (Value() == B_CONTROL_ON)
			updateRect.OffsetBy(1, 1);
		FillRect(updateRect);
	} else {
		updateRect = Bounds();
		updateRect.InsetBy(9, 7);
		BRect rect(updateRect);
		rect.right -= 3;

		updateRect.left += 3;
		updateRect.OffsetBy(1, 0);
		SetHighColor(0, 0, 0);
""",
        """		SetHighUIColor(B_CONTROL_TEXT_COLOR);
		if (Value() == B_CONTROL_ON)
			updateRect.OffsetBy(1, 1);
		FillRect(updateRect);
	} else {
		updateRect = Bounds();
		updateRect.InsetBy(9, 7);
		BRect rect(updateRect);
		rect.right -= 3;

		updateRect.left += 3;
		updateRect.OffsetBy(1, 0);
		SetHighUIColor(B_CONTROL_TEXT_COLOR);
""",
        "Haiku #19070 stop/pause",
    )

    title = HAIKU / "src/kits/tracker/TitleView.cpp"
    replace_once(
        title,
        """	view->SetHighUIColor(B_PANEL_TEXT_COLOR, pressed ? B_DARKEN_1_TINT : 1.0f);
""",
        """	view->SetHighUIColor(B_DOCUMENT_TEXT_COLOR, pressed ? B_DARKEN_1_TINT : 1.0f);
""",
        "Haiku #16678 header text",
    )
    replace_once(
        title,
        """	float tint = B_NO_TINT;
	color_which highColor = view->HighUIColor(&tint);
	view->SetHighUIColor(view->LowUIColor(), B_DARKEN_1_TINT);
	view->StrokeLine(from, to);
	view->SetHighUIColor(highColor, tint);
""",
        """	float tint = B_NO_TINT;
	color_which highColor = view->HighUIColor(&tint);
	float lineTint = view->LowColor().IsLight() ? B_DARKEN_1_TINT : B_LIGHTEN_1_TINT;
	view->SetHighUIColor(view->LowUIColor(), lineTint);
	view->StrokeLine(from, to);
	view->SetHighUIColor(highColor, tint);
""",
        "Haiku #19084 resize line",
    )

    count = HAIKU / "src/kits/tracker/CountView.cpp"
    replace_once(
        count,
        """	rgb_color color = ViewColor();
	if (IsTypingAhead())
		color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	SetLowColor(color);
	be_control_look->DrawBorder(this, bounds, updateRect,
""",
        """	rgb_color color = ViewColor();
	if (IsTypingAhead())
		color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	SetLowColor(color);
	SetHighColor(color);
	FillRect(bounds);
	be_control_look->DrawBorder(this, bounds, updateRect,
""",
        "Haiku #17901 count fill",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
