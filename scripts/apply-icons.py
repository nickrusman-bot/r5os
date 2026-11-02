#!/usr/bin/env python3
"""Public icon helpers. Idempotent copy into the Haiku tree."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "icons"
FILES = (
    "Jamfile",
    "R5Icons.cpp",
    "R5Icons.h",
    "standard_icons.inc",
    "lock_icons.inc",
    "TRACKER_ICON_NOTICE.txt",
    "HAIKU_ICON_NOTICE.txt",
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


def apply_icons(haiku: pathlib.Path | None = None) -> None:
    tree = haiku if haiku is not None else HAIKU
    if not tree.is_dir():
        raise SystemExit(f"haiku tree not found: {tree}")

    dest = tree / "src" / "preferences" / "r5icons"
    dest.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name}")

    header = tree / "headers" / "os" / "interface" / "R5Icons.h"
    shutil.copy2(SRC / "R5Icons.h", header)
    print("copied R5Icons.h into the public headers")

    replace_once(
        tree / "src" / "preferences" / "Jamfile",
        "SubInclude HAIKU_TOP src preferences settings ;\n",
        "SubInclude HAIKU_TOP src preferences r5icons ;\n"
        "SubInclude HAIKU_TOP src preferences settings ;\n",
        "r5icons jam",
    )
    lib_line = "\t\t\tlibpackage.so\n"
    lib_insert = "\t\t\tlibpackage.so\n\t\t\tlibr5icons.so\n"
    for profile in ("regular", "minimum"):
        replace_once(
            tree / "build" / "jam" / "images" / "definitions" / profile,
            lib_line,
            lib_insert,
            "libr5icons.so on " + profile,
        )


def main() -> None:
    apply_icons()


if __name__ == "__main__":
    main()
