#!/usr/bin/env python3
"""Copy the R5OS radeon_hd sources into the Haiku tree.

The files under distro/graphics/ keep the Haiku MIT header. R5OS changes are
marked with SPDX-Snippet and are GPL-3.0-or-later. They replace the same path
in the Haiku tree. A missing source file fails the apply.
"""
from __future__ import annotations

import pathlib
import shutil
import sys

FILES = (
    "src/add-ons/accelerants/radeon_hd/accelerant.cpp",
    "src/add-ons/accelerants/radeon_hd/connector.cpp",
    "src/add-ons/accelerants/radeon_hd/display.cpp",
    "src/add-ons/accelerants/radeon_hd/mode.cpp",
    "src/add-ons/accelerants/radeon_hd/gpu.cpp",
    "src/add-ons/accelerants/radeon_hd/pll.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp",
)


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    haiku = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "haiku")
    source_root = root / "distro" / "graphics"
    if not haiku.is_dir():
        raise SystemExit(f"haiku tree not found: {haiku}")

    for relative in FILES:
        source = source_root / relative
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        destination = haiku / relative
        if not destination.parent.is_dir():
            raise SystemExit(f"missing destination directory {destination.parent}")
        shutil.copyfile(source, destination)
        print(f"copied {relative}")


if __name__ == "__main__":
    main()
