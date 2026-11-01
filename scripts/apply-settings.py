#!/usr/bin/env python3
"""Settings host and the Appearance module. Idempotent build-tool copy."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "settings"
USERS = ROOT / "distro" / "users"

SOURCES = (
    "AppearanceModule.cpp",
    "HomeView.cpp",
    "HomeView.h",
    "Jamfile",
    "LegacyPages.cpp",
    "LegacyPages.h",
    "Settings.rdef",
    "SettingsApp.cpp",
    "SettingsFont.cpp",
    "SettingsFont.h",
    "SettingsHost.cpp",
    "SettingsHost.h",
    "SettingsManager.cpp",
    "SettingsManager.h",
    "SettingsMessages.h",
    "SettingsModule.h",
    "SettingsStorage.cpp",
    "SettingsStorage.h",
    "SettingsWindow.cpp",
    "SettingsWindow.h",
    "WallpaperModule.cpp",
    "WallpaperModule.h",
)


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_fonts() -> None:
    src = ROOT / "distro" / "fonts" / "manrope"
    dest = HAIKU / "data" / "system" / "data" / "settings" / "fonts"
    dest.mkdir(parents=True, exist_ok=True)
    for name in (
        "manrope-regular.ttf",
        "manrope-bold.ttf",
        "OFL.txt",
        "FONTLOG.txt",
    ):
        shutil.copy2(src / name, dest / name)
        print(f"copied font {name}")
    ui = HAIKU / "data" / "system" / "data" / "fonts" / "ttfonts"
    ui.mkdir(parents=True, exist_ok=True)
    for name in (
        "manrope-regular.ttf",
        "manrope-bold.ttf",
        "OFL.txt",
        "FONTLOG.txt",
    ):
        shutil.copy2(src / name, ui / name)
        print(f"copied ui font {name}")


def copy_sources() -> None:
    dest = HAIKU / "src" / "preferences" / "settings"
    dest.mkdir(parents=True, exist_ok=True)
    for name in SOURCES:
        shutil.copy2(SRC / name, dest / name)
        (dest / name).touch()
        print(f"copied {name}")
    for name in ("AdminLock.cpp", "AdminLock.h", "AccountAdmin.h"):
        shutil.copy2(USERS / name, dest / name)
        (dest / name).touch()
        print(f"copied {name} from users")


def patch_jams() -> None:
    replace_once(
        HAIKU / "src" / "preferences" / "Jamfile",
        "SubInclude HAIKU_TOP src preferences users ;\n",
        "SubInclude HAIKU_TOP src preferences users ;\n"
        "SubInclude HAIKU_TOP src preferences settings ;\n",
        "settings jam",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_PREFERENCES += Users ;\n",
        "SYSTEM_PREFERENCES += Users ;\n"
        "SYSTEM_PREFERENCES += Settings ;\n",
        "settings preference",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "minimum",
        "SYSTEM_ADD_ONS_SCREENSAVERS = ;\n",
        "SYSTEM_ADD_ONS_SCREENSAVERS = ;\n"
        "\n"
        "SYSTEM_ADD_ONS_SETTINGS = ;\n",
        "settings addon variable",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "\tSpider\n] ;\n",
        "\tSpider\n] ;\n"
        "\n"
        "SYSTEM_ADD_ONS_SETTINGS += settings_appearance ;\n",
        "settings addon list",
    )
    replace_once(
        HAIKU / "build" / "jam" / "packages" / "Haiku",
        "AddFilesToPackage add-ons Screen\\ Savers : "
        "$(SYSTEM_ADD_ONS_SCREENSAVERS) ;\n",
        "AddFilesToPackage add-ons Screen\\ Savers : "
        "$(SYSTEM_ADD_ONS_SCREENSAVERS) ;\n"
        "\n"
        "AddFilesToPackage add-ons Settings : $(SYSTEM_ADD_ONS_SETTINGS) ;\n",
        "settings addon package",
    )
    replace_once(
        HAIKU / "build" / "jam" / "packages" / "Haiku",
        "AddFilesToPackage data fonts psfonts : $(psFonts) ;\n",
        "AddFilesToPackage data fonts psfonts : $(psFonts) ;\n"
        "\n"
        "local settingsFontDir = [ FDirName $(HAIKU_TOP) data system data "
        "settings fonts ] ;\n"
        "local settingsFonts = [ Glob $(settingsFontDir) : *.ttf *.txt ] ;\n"
        "AddFilesToPackage data settings fonts : $(settingsFonts) ;\n",
        "settings fonts",
    )
    replace_once(
        HAIKU / "build" / "jam" / "packages" / "Haiku",
        "AddFilesToPackage data settings fonts : $(settingsFonts) ;\n",
        "AddFilesToPackage data settings fonts : $(settingsFonts) ;\n"
        "\n"
        "local uiFontDir = [ FDirName $(HAIKU_TOP) data system data fonts "
        "ttfonts ] ;\n"
        "local uiFonts = [ Glob $(uiFontDir) : manrope-*.ttf *.txt ] ;\n"
        "AddFilesToPackage data fonts ttfonts : $(uiFonts) ;\n",
        "ui fonts",
    )


def main() -> None:
    if not HAIKU.is_dir():
        raise SystemExit(f"haiku tree not found: {HAIKU}")
    copy_sources()
    copy_fonts()
    patch_jams()


if __name__ == "__main__":
    main()
