#!/usr/bin/env python3
"""Share one app_server desktop until StartSession. Copy-only."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "app_server"


def main() -> int:
    dest = HAIKU / "src" / "servers" / "app" / "AppServer.cpp"
    shutil.copy2(SRC / "AppServer.cpp", dest)
    dest.touch()
    print(f"copied AppServer.cpp -> {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
