#!/usr/bin/env python3
"""Copy About R5 text into the Haiku tree.

The window itself is the native source in distro/rest/src/apps/aboutsystem.
This script only installs the text files and the Jam search paths.
"""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
ABOUT = HAIKU / "src" / "apps" / "aboutsystem"


def copy_texts() -> None:
    ABOUT.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "distro" / "WHATSNEW", ABOUT / "r5_whatsnew.txt")
    shutil.copy2(ROOT / "distro" / "about" / "licenses.txt",
                 ABOUT / "r5_licenses.txt")
    symbol = ROOT / "distro" / "artwork" / "r5-symbol.png"
    artwork = HAIKU / "data" / "artwork"
    if symbol.is_file() and artwork.is_dir():
        shutil.copy2(symbol, artwork / "r5-symbol.png")
    rdef = ABOUT / "AboutSystem.rdef"
    if rdef.is_file():
        rdef.touch()
    print(f"copied WHATSNEW -> {ABOUT / 'r5_whatsnew.txt'}")
    print(f"copied licenses -> {ABOUT / 'r5_licenses.txt'}")


def patch_jam() -> None:
    jam = ABOUT / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    search = (
        "SEARCH on r5_whatsnew.txt = [ FDirName $(HAIKU_TOP) "
        "src apps aboutsystem ] ;\n"
    )
    depend = "Depends AboutSystem : r5_whatsnew.txt ;\n"
    license_search = (
        "SEARCH on r5_licenses.txt = [ FDirName $(HAIKU_TOP) "
        "src apps aboutsystem ] ;\n"
    )
    license_depend = "Depends AboutSystem : r5_licenses.txt ;\n"
    changed = False
    if license_search not in text and depend in text:
        text = text.replace(depend, depend + license_search + license_depend, 1)
        changed = True
        print("patched Jamfile (licenses jam depend)")
    if search not in text and depend in text:
        text = text.replace(depend, search + depend, 1)
        changed = True
        print("patched Jamfile (whatsnew jam search)")
    if "r5_whatsnew.txt" not in text:
        text += (
            "\nSEARCH on r5_whatsnew.txt = [ FDirName $(HAIKU_TOP) "
            "src apps aboutsystem ] ;\n"
            "Depends AboutSystem : r5_whatsnew.txt ;\n"
            "SEARCH on r5_licenses.txt = [ FDirName $(HAIKU_TOP) "
            "src apps aboutsystem ] ;\n"
            "Depends AboutSystem : r5_licenses.txt ;\n"
        )
        changed = True
        print("patched Jamfile (whatsnew jam depend)")
    if changed:
        jam.write_text(text, encoding="utf-8")
    else:
        print("already Jamfile (about text)")
    link = ": [ TargetLibstdc++ ] be translation localestub\n"
    linked = ": [ TargetLibstdc++ ] be translation localestub libr5ui.so\n"
    if "libr5ui.so" in text:
        print("already Jamfile (libr5ui)")
        return
    if link not in text:
        raise SystemExit("AboutSystem link line not found")
    text = text.replace(link, linked, 1)
    jam.write_text(text, encoding="utf-8", newline="\n")
    print("patched Jamfile (libr5ui)")


def main() -> int:
    copy_texts()
    patch_jam()
    return 0


if __name__ == "__main__":
    sys.exit(main())
