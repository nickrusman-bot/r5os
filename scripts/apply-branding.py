#!/usr/bin/env python3
"""Patch Haiku unofficial branding (Deskbar leaf, Installer logo). Idempotent."""
from __future__ import annotations

import pathlib
import re
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")

LEAF_ELSE = (
    "resource(R_LeafLogoBitmap, \"r5_deskbar_logo.png\") #'PNG ' "
    'import "../../../data/artwork/R5OS-deskbar.png";'
)

BAR_OFFICIAL = """\tBBitmap* icon = NULL;
	size_t dataSize;
	const void* data = AppResSet()->FindResource(B_VECTOR_ICON_TYPE,
		R_LeafLogoBitmap, &dataSize);
	if (data != NULL) {
		// seems valid, scale bitmap according to be_bold_font size
		float width = std::max(63.f, ceilf(63 * be_bold_font->Size() / 12.f));
		float height = std::max(22.f, ceilf(22 * be_bold_font->Size() / 12.f));
		icon = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGBA32);
		if (icon->InitCheck() != B_OK
			|| BIconUtils::GetVectorIcon((const uint8*)data, dataSize, icon)
					!= B_OK) {
			delete icon;
			icon = NULL;
		}
	}"""

BAR_PATCHED = """\tBBitmap* icon = NULL;
#ifdef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
	size_t dataSize;
	const void* data = AppResSet()->FindResource(B_VECTOR_ICON_TYPE,
		R_LeafLogoBitmap, &dataSize);
	if (data != NULL) {
		// seems valid, scale bitmap according to be_bold_font size
		float width = std::max(63.f, ceilf(63 * be_bold_font->Size() / 12.f));
		float height = std::max(22.f, ceilf(22 * be_bold_font->Size() / 12.f));
		icon = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGBA32);
		if (icon->InitCheck() != B_OK
			|| BIconUtils::GetVectorIcon((const uint8*)data, dataSize, icon)
					!= B_OK) {
			delete icon;
			icon = NULL;
		}
	}
#else
	icon = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "r5_deskbar_logo.png");
#endif"""


def patch_icons_rdef(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    if "r5_deskbar_logo.png" in text:
        print(f"already patched {path}")
        return
    m = re.search(
        r"resource\(R_LeafLogoBitmap\) vector_icon \{.*?\n\};",
        text,
        re.S,
    )
    if not m:
        raise SystemExit(f"R_LeafLogoBitmap not found in {path}")
    wrapped = (
        "#ifdef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL\n"
        + m.group(0)
        + "\n#else\n"
        + LEAF_ELSE
        + "\n#endif"
    )
    path.write_text(text[: m.start()] + wrapped + text[m.end() :], encoding="utf-8")
    print(f"patched {path}")


def patch_barmenubar(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    if "r5_deskbar_logo.png" in text:
        print(f"already patched {path}")
        return
    if "#include <TranslationUtils.h>" not in text:
        text = text.replace(
            "#include <IconUtils.h>\n",
            "#include <IconUtils.h>\n#include <TranslatorFormats.h>\n"
            "#include <TranslationUtils.h>\n",
            1,
        )
    if BAR_OFFICIAL not in text:
        raise SystemExit(f"leaf loader not found in {path}")
    path.write_text(text.replace(BAR_OFFICIAL, BAR_PATCHED, 1), encoding="utf-8")
    print(f"patched {path}")


def patch_deskbar_jam(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    old = ": be localestub tracker $(targetLib)"
    new = ": be translation localestub tracker $(targetLib)"
    if "translation localestub" in text:
        print(f"already patched {path}")
        return
    if old not in text:
        raise SystemExit(f"Deskbar link line not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path}")


def patch_installer_rdef(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    old = 'resource(1, "walter_logo.png") #\'PNG \' import "../../../data/artwork/Walter logo.png";'
    new = 'resource(1, "walter_logo.png") #\'PNG \' import "../../../data/artwork/R5OS-logo.png";'
    if "artwork/R5OS-logo.png" in text:
        print(f"already patched {path}")
        return
    if old not in text:
        if "R5 OS logo.png" in text or "R5OS-logo.png" in text:
            print(f"already patched {path}")
            return
        raise SystemExit(f"walter import not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path}")


def rewrite_product_name(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    new = (
        text.replace("R5 OS logo.png", "R5OS-logo.png")
        .replace("R5 OS deskbar.png", "R5OS-deskbar.png")
        .replace("R5 OS", "R5OS")
    )
    if new == text:
        print(f"already {path.name} (R5OS name)")
        return
    path.write_text(new, encoding="utf-8")
    print(f"patched {path} (R5OS name)")


def main() -> int:
    patch_icons_rdef(HAIKU / "src/apps/deskbar/icons.rdef")
    patch_barmenubar(HAIKU / "src/apps/deskbar/BarMenuBar.cpp")
    patch_deskbar_jam(HAIKU / "src/apps/deskbar/Jamfile")
    patch_installer_rdef(HAIKU / "src/apps/installer/Installer.rdef")
    for rel in (
        "src/apps/deskbar/icons.rdef",
        "src/apps/deskbar/DeskbarMenu.cpp",
        "src/apps/installer/Installer.rdef",
        "src/apps/installer/InstallerWindow.cpp",
        "src/apps/aboutsystem/AboutSystem.cpp",
        "src/apps/aboutsystem/AboutSystem.rdef",
        "src/apps/firstbootprompt/BootPromptWindow.cpp",
    ):
        rewrite_product_name(HAIKU / rel)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
