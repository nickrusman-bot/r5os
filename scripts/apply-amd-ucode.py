#!/usr/bin/env python3
"""Install AMD family 19h microcode on the boot volume.

The bootloader reads system/non-packaged/data/firmware/amd-ucode before
packagefs exists. The blob and AMD's license stay unmodified.
"""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
MARK = "r5AmdUcode"

JAM_BLOCK = """
# R5OS AMD family 19h microcode. The bootloader reads this path.
local r5AmdUcodeDir = [ FDirName $(HAIKU_TOP) data firmware amd-ucode ] ;
local r5AmdUcode = <r5ucode>microcode_amd_fam19h.bin ;
local r5AmdUcodeLicense = <r5ucode>LICENSE.amd-ucode ;
SEARCH on $(r5AmdUcode) $(r5AmdUcodeLicense) = $(r5AmdUcodeDir) ;

AddDirectoryToHaikuImage system non-packaged data firmware ;
AddDirectoryToHaikuImage system non-packaged data firmware amd-ucode ;
AddFilesToHaikuImage system non-packaged data firmware amd-ucode
	: $(r5AmdUcode) ;
AddFilesToHaikuImage system non-packaged data firmware amd-ucode
	: $(r5AmdUcodeLicense) ;
"""


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "distro/firmware/amd-ucode"
    blob = source / "microcode_amd_fam19h.bin"
    license_file = source / "LICENSE.amd-ucode"
    if not blob.is_file() or not license_file.is_file():
        raise SystemExit(f"missing AMD microcode in {source}")
    if blob.read_bytes()[:4] != b"\x44\x4d\x41\x00":
        raise SystemExit("microcode_amd_fam19h.bin magic is not AMD")
    destination = HAIKU / "data/firmware/amd-ucode"
    destination.mkdir(parents=True, exist_ok=True)
    for name in (blob.name, license_file.name):
        data = (source / name).read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        if name.endswith(".bin"):
            data = (source / name).read_bytes()
        (destination / name).write_bytes(data)
        print(f"copied data/firmware/amd-ucode/{name}")

    jam = HAIKU / "build/jam/images/definitions/regular"
    text = jam.read_text(encoding="utf-8")
    if MARK in text:
        print(f"already {jam.name} (amd ucode)")
        return 0
    anchor = "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n"
    if anchor not in text:
        raise SystemExit(f"pattern not found in {jam} (amd ucode)")
    block = JAM_BLOCK.replace("\r\n", "\n").replace("\r", "\n")
    jam.write_text(text.replace(anchor, anchor + block, 1), encoding="utf-8", newline="\n")
    print(f"patched {jam} (amd ucode)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
