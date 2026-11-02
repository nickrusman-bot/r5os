#!/usr/bin/env python3
"""Copy the Notifications preference and server fixes. Full-file copy."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "notifications"

COPIES = (
    ("PrefletWin.cpp", HAIKU / "src" / "preferences" / "notifications"),
    ("NotificationsView.cpp", HAIKU / "src" / "preferences" / "notifications"),
    ("NotificationWindow.cpp", HAIKU / "src" / "servers" / "notification"),
    ("Notifications.h", HAIKU / "headers" / "private" / "notification"),
)


def main() -> None:
    for name, dest in COPIES:
        dest.mkdir(parents=True, exist_ok=True)
        target = dest / name
        shutil.copy2(SRC / name, target)
        target.touch()
        print(f"copied {name}")


if __name__ == "__main__":
    main()
