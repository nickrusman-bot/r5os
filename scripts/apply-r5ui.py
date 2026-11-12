#!/usr/bin/env python3
"""Public UI helpers. Idempotent copy into the Haiku tree."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "ui"
FILES = (
    "Jamfile",
    "R5UI.cpp",
    "R5UI.h",
    "Card.cpp",
    "Card.h",
    "SidebarItem.cpp",
    "SidebarItem.h",
    "Sidebar.cpp",
    "Sidebar.h",
    "ActionCard.cpp",
    "ActionCard.h",
    "PageHeader.cpp",
    "PageHeader.h",
    "ScreenArrangement.cpp",
    "ScreenArrangement.h",
    "UserCard.cpp",
    "UserCard.h",
    "UserListItem.cpp",
    "UserListItem.h",
    "PasswordControl.cpp",
    "PasswordControl.h",
    "IconView.cpp",
    "R5IconView.h",
    "AccentButton.cpp",
    "AccentButton.h",
    "AdminLock.cpp",
    "AdminLock.h",
    "AdminLockProtocol.h",
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


def apply_r5ui(haiku: pathlib.Path | None = None) -> None:
    tree = haiku if haiku is not None else HAIKU
    if not tree.is_dir():
        raise SystemExit(f"haiku tree not found: {tree}")

    dest = tree / "src" / "preferences" / "r5ui"
    dest.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name}")

    headers = tree / "headers" / "os" / "interface"
    for name in ("R5UI.h", "Card.h", "SidebarItem.h", "Sidebar.h",
            "ActionCard.h", "PageHeader.h", "ScreenArrangement.h",
            "UserCard.h", "UserListItem.h", "PasswordControl.h",
            "R5IconView.h", "AccentButton.h", "AdminLock.h"):
        shutil.copy2(SRC / name, headers / name)
        print(f"copied {name} into the public headers")

    jamfile = tree / "src" / "preferences" / "Jamfile"
    r5ui_line = "SubInclude HAIKU_TOP src preferences r5ui ;\n"
    jam_text = jamfile.read_text(encoding="utf-8")
    if r5ui_line in jam_text:
        print(f"already {jamfile.name} (r5ui jam)")
    else:
        replace_once(
            jamfile,
            "SubInclude HAIKU_TOP src preferences settings ;\n",
            r5ui_line + "SubInclude HAIKU_TOP src preferences settings ;\n",
            "r5ui jam",
        )
    lib_line = "\t\t\tlibr5icons.so\n"
    lib_insert = "\t\t\tlibr5icons.so\n\t\t\tlibr5ui.so\n"
    for profile in ("regular", "minimum"):
        replace_once(
            tree / "build" / "jam" / "images" / "definitions" / profile,
            lib_line,
            lib_insert,
            "libr5ui.so on " + profile,
        )


def main() -> None:
    apply_r5ui()


if __name__ == "__main__":
    main()
