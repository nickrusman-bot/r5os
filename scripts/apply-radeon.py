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
    "src/add-ons/accelerants/radeon_hd/clearstate_defs.h",
    "src/add-ons/accelerants/radeon_hd/clearstate_gfx9.h",
    "src/add-ons/accelerants/radeon_hd/pll.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd_private.h",
    "headers/private/graphics/radeon_hd/radeon_hd.h",
    "src/bin/gfxinfo/Jamfile",
    "src/bin/gfxinfo/gfxinfo.cpp",
)


def hook_gfxinfo(haiku: pathlib.Path) -> None:
    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin gfxinfo ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin usb_audiotest ;\n"
        if anchor not in text:
            anchor = "SubInclude HAIKU_TOP src bin midi2test ;\n"
        if anchor not in text:
            anchor = "SubInclude HAIKU_TOP src bin writembr ;\n"
        if anchor not in text:
            raise SystemExit("gfxinfo anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile")
    else:
        print("already src/bin/Jamfile")

    regular = haiku / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += gfxinfo ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += usb_audiotest ;\n"
        if anchor not in text:
            anchor = "SYSTEM_BIN += midi2test ;\n"
        if anchor not in text:
            raise SystemExit("gfxinfo anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image")
    else:
        print("already regular image")


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
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")
    hook_gfxinfo(haiku)


if __name__ == "__main__":
    main()
