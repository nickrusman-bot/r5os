#!/usr/bin/env python3
"""Install Renoir GFX firmware where the accelerant can read it.

The blobs and AMD's license stay unmodified. Linux names these files for
GC 9.3 Renoir. The image path is system/non-packaged/data/firmware/amdgpu.
"""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
MARK = "r5AmdGpuFw"
NAMES = (
    "renoir_pfp.bin",
    "renoir_me.bin",
    "renoir_ce.bin",
    "renoir_rlc.bin",
    "LICENSE.amdgpu",
)

JAM_BLOCK = """
# r5AmdGpuFw Renoir GFX firmware. The accelerant reads this path.
local r5AmdGpuDir = [ FDirName $(HAIKU_TOP) data firmware amdgpu ] ;
local r5AmdGpuPfp = <r5amdgpu>renoir_pfp.bin ;
local r5AmdGpuMe = <r5amdgpu>renoir_me.bin ;
local r5AmdGpuCe = <r5amdgpu>renoir_ce.bin ;
local r5AmdGpuRlc = <r5amdgpu>renoir_rlc.bin ;
local r5AmdGpuLicense = <r5amdgpu>LICENSE.amdgpu ;
SEARCH on $(r5AmdGpuPfp) $(r5AmdGpuMe) $(r5AmdGpuCe) $(r5AmdGpuRlc) $(r5AmdGpuLicense)
	= $(r5AmdGpuDir) ;

AddDirectoryToHaikuImage system non-packaged data firmware amdgpu ;
AddFilesToHaikuImage system non-packaged data firmware amdgpu
	: $(r5AmdGpuPfp) ;
AddFilesToHaikuImage system non-packaged data firmware amdgpu
	: $(r5AmdGpuMe) ;
AddFilesToHaikuImage system non-packaged data firmware amdgpu
	: $(r5AmdGpuCe) ;
AddFilesToHaikuImage system non-packaged data firmware amdgpu
	: $(r5AmdGpuRlc) ;
AddFilesToHaikuImage system non-packaged data firmware amdgpu
	: $(r5AmdGpuLicense) ;
"""


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "distro/firmware/amdgpu"
    for name in NAMES:
        if not (source / name).is_file():
            raise SystemExit(f"missing {source / name}")
    pfp = (source / "renoir_pfp.bin").read_bytes()
    if len(pfp) < 16 or pfp[12:14] != b"\x09\x00":
        raise SystemExit("renoir_pfp.bin is not a GC 9.x header")

    destination = HAIKU / "data/firmware/amdgpu"
    destination.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        (destination / name).write_bytes((source / name).read_bytes())
        print(f"copied data/firmware/amdgpu/{name}")

    jam = HAIKU / "build/jam/images/definitions/regular"
    text = jam.read_text(encoding="utf-8")
    if MARK in text and "renoir_rlc.bin" in text:
        print(f"already {jam.name} (amdgpu firmware)")
        return 0
    if MARK in text:
        ce = "local r5AmdGpuCe = <r5amdgpu>renoir_ce.bin ;\n"
        search = (
            "SEARCH on $(r5AmdGpuPfp) $(r5AmdGpuMe) $(r5AmdGpuCe)"
            " $(r5AmdGpuLicense)\n"
        )
        add = (
            "AddFilesToHaikuImage system non-packaged data firmware amdgpu\n"
            "\t: $(r5AmdGpuCe) ;\n"
        )
        if ce not in text or search not in text or add not in text:
            raise SystemExit(f"amdgpu jam block in {jam} has no RLC slot")
        text = text.replace(ce, ce + "local r5AmdGpuRlc = <r5amdgpu>renoir_rlc.bin ;\n", 1)
        text = text.replace(
            search,
            "SEARCH on $(r5AmdGpuPfp) $(r5AmdGpuMe) $(r5AmdGpuCe)"
            " $(r5AmdGpuRlc) $(r5AmdGpuLicense)\n",
            1,
        )
        text = text.replace(
            add,
            add
            + "AddFilesToHaikuImage system non-packaged data firmware amdgpu\n"
            + "\t: $(r5AmdGpuRlc) ;\n",
            1,
        )
        jam.write_text(text, encoding="utf-8", newline="\n")
        print(f"patched {jam} (amdgpu RLC firmware)")
        return 0
    anchor = "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n"
    if anchor not in text:
        raise SystemExit(f"pattern not found in {jam} (amdgpu firmware)")
    block = JAM_BLOCK.replace("\r\n", "\n").replace("\r", "\n")
    jam.write_text(text.replace(anchor, anchor + block, 1), encoding="utf-8", newline="\n")
    print(f"patched {jam} (amdgpu firmware)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
