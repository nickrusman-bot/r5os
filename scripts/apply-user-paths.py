#!/usr/bin/env python3
"""Copy native find_directory sources into the Haiku tree. Idempotent."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "user-paths"

COPIES = (
    ("apps/midiplayer/MidiPlayerWindow.cpp", "src/apps/midiplayer/MidiPlayerWindow.cpp"),
    ("apps/midiplayer/MidiPlayerWindow.h", "src/apps/midiplayer/MidiPlayerWindow.h"),
    ("apps/poorman/PoorManApplication.cpp", "src/apps/poorman/PoorManApplication.cpp"),
    ("apps/poorman/PoorManPreferencesWindow.cpp", "src/apps/poorman/PoorManPreferencesWindow.cpp"),
    ("apps/poorman/PoorManWindow.cpp", "src/apps/poorman/PoorManWindow.cpp"),
    ("apps/poorman/constants.cpp", "src/apps/poorman/constants.cpp"),
    ("apps/poorman/constants.h", "src/apps/poorman/constants.h"),
    ("preferences/joysticks/JoyWin.cpp", "src/preferences/joysticks/JoyWin.cpp"),
    ("storage/mime/DatabaseLocation.h",
     "headers/private/storage/mime/DatabaseLocation.h"),
    ("storage/mime/database_support.cpp",
     "src/kits/storage/mime/database_support.cpp"),
    ("data/mime_db/application/x-vnd.haiku-virtual-directory",
     "src/data/mime_db/application/x-vnd.haiku-virtual-directory"),
)


def main() -> int:
    if not (HAIKU / "src").is_dir():
        raise SystemExit(f"haiku tree missing: {HAIKU}")
    for rel, dest_rel in COPIES:
        src = SRC / rel
        dest = HAIKU / dest_rel
        if not src.is_file():
            raise SystemExit(f"missing {src}")
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        dest.touch()
        print(f"copied {rel} -> {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
