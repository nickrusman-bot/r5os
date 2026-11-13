#!/usr/bin/env python3
"""Share one app_server desktop until StartSession. Copy-only."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "app_server"


def copy(name: str, dest: pathlib.Path) -> None:
    shutil.copy2(SRC / name, dest)
    dest.touch()
    print(f"copied {name} -> {dest}")


def main() -> int:
    dest = HAIKU / "src" / "servers" / "app" / "AppServer.cpp"
    copy("AppServer.cpp", dest)
    config = HAIKU / "src" / "servers" / "app" / "ServerConfig.h"
    copy("ServerConfig.h", config)
    local = HAIKU / "src" / "servers" / "app" / "drawing" / "interface" / "local"
    copy("AccelerantHWInterface.cpp", local / "AccelerantHWInterface.cpp")
    copy("GPUCopy.cpp", local / "GPUCopy.cpp")
    copy("GPUCopy.h", local / "GPUCopy.h")
    copy("GPUCopyBands.h", local / "GPUCopyBands.h")
    copy("local-Jamfile", local / "Jamfile")
    for name in ("Compositor.cpp", "Compositor.h", "WindowBuffer.cpp",
            "WindowBuffer.h", "WindowHWInterface.cpp", "WindowHWInterface.h"):
        copy(name, local / name)
    copy("compositor/compositor_shaders.h", local / "compositor_shaders.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
