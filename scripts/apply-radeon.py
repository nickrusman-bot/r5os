#!/usr/bin/env python3
"""Copy the R5OS radeon_hd sources into the Haiku tree.

The files under distro/graphics/ keep the Haiku MIT header. R5OS changes are
marked with SPDX-Snippet and are GPL-3.0-or-later. They replace the same path
in the Haiku tree. A missing source file fails the apply.
"""
from __future__ import annotations

import hashlib
import pathlib
import shutil
import sys

CLEARSTATE_SHA256 = {
    "src/add-ons/accelerants/radeon_hd/clearstate_defs.h":
        "70eb9f9a57d2cacb2ee2d7b9f17485384019ac03c845ce4bea941acdae137285",
    "src/add-ons/accelerants/radeon_hd/clearstate_gfx9.h":
        "e885c7f6475efd74e7729688a70992ff9543979363eb029c628bd95627fb34b5",
}

FILES = (
    "src/add-ons/accelerants/radeon_hd/accelerant.cpp",
    "src/add-ons/accelerants/radeon_hd/connector.cpp",
    "src/add-ons/accelerants/radeon_hd/display.cpp",
    "src/add-ons/accelerants/radeon_hd/mode.cpp",
    "src/add-ons/accelerants/radeon_hd/gpu.cpp",
    "src/add-ons/accelerants/radeon_hd/gfx9_compute.s",
    "src/add-ons/accelerants/radeon_hd/gfx9_compute_shader.h",
    "src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h",
    "src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp",
    "src/add-ons/accelerants/radeon_hd/common/gfx9_start.h",
    "src/add-ons/accelerants/radeon_hd/common/gfx9_check.h",
    "src/add-ons/accelerants/radeon_hd/Jamfile",
    "src/add-ons/accelerants/radeon_hd/clearstate_defs.h",
    "src/add-ons/accelerants/radeon_hd/clearstate_gfx9.h",
    "src/add-ons/accelerants/radeon_hd/pll.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
    "headers/private/r5gpu/r5_gpu.h",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/Jamfile",
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
        expected = CLEARSTATE_SHA256.get(relative)
        if expected is not None:
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            print(f"sha256 {relative} {digest}")
            if digest != expected:
                raise SystemExit(
                    f"{relative} sha256 {digest} expected {expected}")
        destination = haiku / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")
    hook_gfxinfo(haiku)


if __name__ == "__main__":
    main()
