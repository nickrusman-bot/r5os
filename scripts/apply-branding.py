#!/usr/bin/env python3
"""Patch Haiku unofficial branding (Deskbar leaf, Installer logo). Idempotent."""
from __future__ import annotations

import pathlib
import re
import shutil
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


def install_wallpaper(haiku: pathlib.Path) -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "distro" / "artwork" / "r5-wallpaper.jpg"
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    dest = haiku / "data" / "artwork" / "r5-wallpaper.jpg"
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)
    print(f"copied {dest}")

    package = haiku / "build" / "jam" / "packages" / "Haiku"
    text = package.read_text(encoding="utf-8")
    old = (
        "local logoArtwork =\n"
        '\t$(HAIKU_INCLUDE_TRADEMARKS)"HAIKU logo - white on blue - big.png"\n'
        '\t$(HAIKU_INCLUDE_TRADEMARKS)"HAIKU logo - white on blue - normal.png" ;\n'
    )
    new = "local logoArtwork = r5-wallpaper.jpg ;\n"
    if "local logoArtwork = r5-wallpaper.jpg ;" in text:
        print(f"already {package.name} (wallpaper)")
    elif old not in text:
        raise SystemExit(f"Haiku artwork list not found in {package}")
    else:
        package.write_text(text.replace(old, new, 1), encoding="utf-8")
        print(f"patched {package} (wallpaper)")

    desktop = haiku / "src" / "data" / "directory_attrs" / "home-desktop.rdef"
    rdef = desktop.read_text(encoding="utf-8")
    if "r5-wallpaper.jpg" in rdef:
        print(f"already {desktop.name} (wallpaper)")
        return
    rdef = rdef.rstrip() + (
        "\n\n"
        'resource(1, "be:bgndimginfo") message {\n'
        '\t"be:bgndimginfopath" = "/boot/system/data/artwork/r5-wallpaper.jpg",\n'
        '\t"be:bgndimginfomode" = 2,\n'
        '\t"be:bgndimginfoworkspaces" = -1,\n'
        '\t"be:bgndimginfooffset" = point { 0, 0 },\n'
        '\t"be:bgndimginfoerasetext" = false,\n'
        '\t"be:bgndimginfoset" = 0\n'
        "};\n"
    )
    desktop.write_text(rdef, encoding="utf-8")
    print(f"patched {desktop} (wallpaper)")


def patch_boot_prompt(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    # The copied source already says "R5". A blanket replace would also
    # rewrite the R5OS license notice.
    if "R5OS modifications in this file are GPL-3.0-or-later." in text:
        print(f"already {path.name} (boot prompt)")
        return
    text = text.replace(
        'B_TRANSLATE("Welcome to R5OS!")',
        'B_TRANSLATE("Welcome to R5 Operating System!")',
    )
    updated = text.replace("R5OS", "R5")
    if updated == path.read_text(encoding="utf-8"):
        print(f"already {path.name} (boot prompt)")
        return
    path.write_text(updated, encoding="utf-8")
    print(f"patched {path.name} (boot prompt)")


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
    patch_boot_prompt(HAIKU / "src/apps/firstbootprompt/BootPromptWindow.cpp")
    install_wallpaper(HAIKU)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
