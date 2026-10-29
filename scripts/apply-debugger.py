#!/usr/bin/env python3
"""Show the Debugger team window on the current screen. Copy-only."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "debugger"


def copy(name: str, dest: pathlib.Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SRC / name, dest)
    dest.touch()
    print(f"copied {name} -> {dest}")


def main() -> int:
    copy(
        "GraphicalUserInterface.cpp",
        HAIKU / "src" / "apps" / "debugger" / "user_interface" / "gui"
        / "GraphicalUserInterface.cpp",
    )
    copy(
        "TeamDebugger.cpp",
        HAIKU / "src" / "kits" / "debugger" / "controllers" / "TeamDebugger.cpp",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
