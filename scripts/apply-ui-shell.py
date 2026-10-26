#!/usr/bin/env python3
"""Shell/UI overlay leftovers: #18970 #15541 #14991 #16618 #18300."""
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
    hcl = HAIKU / "src/kits/interface/HaikuControlLook.cpp"
    replace_once(
        hcl,
        """	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the surface edges

	// colors
	float topTint;
	float bottomTint;

	if ((flags & B_ACTIVATED) != 0) {
""",
        """	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the surface edges

	// colors
	float topTint;
	float bottomTint;

	if ((flags & B_DISABLED) != 0) {
		_FillGradient(view, rect, base, 1.05, 1.08);
		return;
	}

	if ((flags & B_ACTIVATED) != 0) {
""",
        "Haiku #16618 disabled menubar",
    )

    menu = HAIKU / "src/kits/interface/Menu.cpp"
    replace_once(
        menu,
        """		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
	}
""",
        """		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
		else if (frame.left < screenFrame.left)
			frame.OffsetBy(screenFrame.left - frame.left, 0);
	}
""",
        "Haiku #18300 screen border",
    )
    replace_once(
        menu,
        """		if (frame.left < 0)
			frame.OffsetBy(-frame.left + 6, 0);""",
        """		if (frame.left < screenFrame.left)
			frame.OffsetBy(screenFrame.left - frame.left + 6, 0);""",
        "Haiku #18300 left screen",
    )

    pose = HAIKU / "src/kits/tracker/PoseView.cpp"
    replace_once(
        pose,
        """		default:
			_inherited::MessageReceived(message);
			break;
	}
}
""",
        """		case B_COLORS_UPDATED:
			if (HasSystemColors())
				AdoptSystemColors();
			Invalidate();
			break;

		default:
			_inherited::MessageReceived(message);
			break;
	}
}
""",
        "Haiku #15541 #14991 colors",
    )
    replace_once(
        HAIKU / "src/kits/tracker/DesktopPoseView.cpp",
        """	int32 desktopBrightness = ui_color(B_DESKTOP_COLOR).Brightness();
	SetHighColor(LowColor().Brightness() <= desktopBrightness ? kWhite : kBlack);
}""",
        """	SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
	rgb_color text = HighColor();
	rgb_color bg = LowColor();
	int32 contrast = (int32)text.Brightness() - (int32)bg.Brightness();
	if (contrast < 0)
		contrast = -contrast;
	if (contrast < 80)
		SetHighColor(bg.IsDark() ? kWhite : kBlack);
}""",
        "Haiku #15541 desktop text",
    )
    replace_once(
        HAIKU / "src/add-ons/control_look/FlatControlLook/FlatControlLook.cpp",
        """	bool isEnabled = (flags & B_DISABLED) != 0;""",
        """	bool isEnabled = (flags & B_DISABLED) == 0;""",
        "Haiku #16618 flat look",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
