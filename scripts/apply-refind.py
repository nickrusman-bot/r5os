#!/usr/bin/env python3
"""Stage rEFInd and teach the image how to put it on the EFI partition.

This script builds refind_x64.efi with
GNU-EFI when that binary is older than the sources, then copies the loader,
the black theme, the icons, and the license texts into the Haiku tree.
Filesystem drivers are not built and are not installed.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

HAIKU = Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = Path(__file__).resolve().parents[1]
REFIND = ROOT / "distro/system/boot/loader/refind"
OURS = ROOT / "distro/system/boot/loader/r5"
MARK = "R5OS-REFIND"

IMAGE_RULES_OLD = """\
\t# public efi keys for efi bios trust
\tEFIKEYS on $(image) = [ FDirName $(HAIKU_TOP) data boot efi keys ] ;

\tBuildEfiSystemPartition1 $(image) : $(fatshell) $(macVolumeIcon) $(efiLoader) ;
"""

IMAGE_RULES_NEW = """\
\t# public efi keys for efi bios trust
\tEFIKEYS on $(image) = [ FDirName $(HAIKU_TOP) data boot efi keys ] ;

\t# R5OS-REFIND. rEFInd is the removable-media loader. The Haiku loader
\t# stays in EFI/haiku and is the program that reads the kernel.
\tlocal r5EfiDir = [ FDirName $(HAIKU_TOP) data boot r5-efi ] ;
\tR5EFIDIR on $(image) = $(r5EfiDir) ;
\tDepends $(image) : [ FDirName $(r5EfiDir) refind_x64.efi ] ;
\tDepends $(image) : [ FDirName $(r5EfiDir) refind.conf ] ;
\tDepends $(image) : [ FDirName $(r5EfiDir) themes r5-black background.png ] ;

\tBuildEfiSystemPartition1 $(image) : $(fatshell) $(macVolumeIcon) $(efiLoader) ;
"""

ACTION_OLD_START = "actions BuildEfiSystemPartition1\n{\n"
ACTION_NEW = """\
actions BuildEfiSystemPartition1
{
\t$(RM) $(<)

\texport $(HOST_ADD_BUILD_COMPATIBILITY_LIB_DIR)

\t# R5OS-REFIND. 16 MiB, so the icons and both loaders fit.
\tbash $(R5EFIDIR)/populate-esp.sh $(<) "$(2[1])" "$(2[2])" "$(2[3])" \\
\t\t$(R5EFIDIR) $(EFINAME) $(EFIKEYS)
}
"""

JAM_BLOCK = """
# R5OS-REFIND. The installer reads this directory on the live system.
local r5RefindDir = [ FDirName $(HAIKU_TOP) data boot r5-efi ] ;
AddDirectoryToHaikuImage system non-packaged data boot ;
CopyDirectoryToHaikuImage system non-packaged data boot
\t: $(r5RefindDir) : refind : -x populate-esp.sh ;
"""


def _lf(path: Path) -> bytes:
    data = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return data


def _newer_sources(binary: Path) -> bool:
    if not binary.is_file():
        return True
    built = binary.stat().st_mtime
    for path in REFIND.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix not in {".c", ".h", ".S", ".s"}:
            continue
        if "filesystems" in path.parts:
            continue
        if path.stat().st_mtime > built:
            return True
    return False


def _gnuefi_make_args() -> list[str]:
    """Point make at GNU-EFI. Ubuntu's objcopy no longer knows efi-app-*."""
    system_crt = Path("/usr/lib/crt0-efi-x86_64.o")
    system_header = Path("/usr/include/efi/efi.h")
    extra = [
        "OMIT_SBAT=1",
        "FORMAT=-I elf64-x86-64 -O pei-x86-64 --subsystem efi-app",
    ]
    if system_crt.is_file() and system_header.is_file():
        return extra
    src = ROOT / "generated/gnu-efi-build/gnu-efi-3.0.18"
    prefix = ROOT / "generated/gnu-efi"
    header = prefix / "include/efi/efi.h"
    crt = prefix / "lib/crt0-efi-x86_64.o"
    if not header.is_file() or not crt.is_file():
        libefi = src / "x86_64/lib/libefi.a"
        if not libefi.is_file():
            raise SystemExit(
                "gnu-efi is missing. Install the gnu-efi package, or place "
                "the 3.0.18 libraries under generated/gnu-efi-build."
            )
        efi_inc = prefix / "include/efi"
        if efi_inc.exists():
            shutil.rmtree(efi_inc)
        efi_inc.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(src / "inc", efi_inc)
        lib = prefix / "lib"
        lib.mkdir(parents=True, exist_ok=True)
        shutil.copy2(libefi, lib / "libefi.a")
        shutil.copy2(src / "x86_64/gnuefi/libgnuefi.a", lib / "libgnuefi.a")
        shutil.copy2(src / "x86_64/gnuefi/crt0-efi-x86_64.o", crt)
        shutil.copy2(src / "gnuefi/elf_x86_64_efi.lds", lib / "elf_x86_64_efi.lds")
        print(f"staged gnu-efi into {prefix}")
    lib = prefix / "lib"
    inc = prefix / "include/efi"
    return [
        f"EFIINC={inc}",
        f"GNUEFILIB={lib}",
        f"EFILIB={lib}",
        f"EFICRT0={lib}",
        *extra,
    ]


def build_refind() -> Path:
    binary = REFIND / "refind" / "refind_x64.efi"
    if _newer_sources(binary):
        print("building rEFInd (make gnuefi)")
        subprocess.run(["make", "gnuefi", *_gnuefi_make_args()], cwd=REFIND, check=True)
    if not binary.is_file():
        raise SystemExit(f"missing {binary}")
    return binary


def stage(binary: Path) -> Path:
    destination = HAIKU / "data/boot/r5-efi"
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    shutil.copy2(binary, destination / "refind_x64.efi")
    for name in ("refind.conf", "populate-esp.sh"):
        data = _lf(OURS / name)
        (destination / name).write_bytes(data)
    os.chmod(destination / "populate-esp.sh", 0o755)
    for name in ("COPYING.txt", "LICENSE.txt", "CREDITS.txt"):
        shutil.copy2(REFIND / name, destination / name)
    shutil.copytree(REFIND / "icons", destination / "icons")
    theme_src = REFIND / "themes" / "r5-black"
    theme_dst = destination / "themes" / "r5-black"
    theme_dst.mkdir(parents=True)
    (theme_dst / "theme.conf").write_bytes(_lf(theme_src / "theme.conf"))
    shutil.copy2(theme_src / "background.png", theme_dst / "background.png")
    print(f"staged {destination}")
    return destination


def patch_image_rules() -> None:
    path = HAIKU / "build/jam/ImageRules"
    text = path.read_text(encoding="utf-8")
    if MARK in text and "populate-esp.sh" in text:
        print(f"already {path.name} ({MARK})")
        return
    if IMAGE_RULES_OLD not in text or ACTION_OLD_START not in text:
        raise SystemExit(f"pattern not found in {path}")
    if text.count(ACTION_OLD_START) != 1:
        raise SystemExit(f"action marker is not unique in {path}")
    start = text.find(ACTION_OLD_START)
    end = text.find("\n}", start)
    if end < 0:
        raise SystemExit(f"action end not found in {path}")
    text = text[:start] + ACTION_NEW + text[end + 2 :]
    text = text.replace(IMAGE_RULES_OLD, IMAGE_RULES_NEW, 1)
    if MARK not in text or "populate-esp.sh" not in text:
        raise SystemExit(f"patch did not land in {path}")
    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"patched {path}")


def patch_regular() -> None:
    path = HAIKU / "build/jam/images/definitions/regular"
    text = path.read_text(encoding="utf-8")
    if MARK in text:
        print(f"already {path.name} ({MARK})")
        return
    anchor = "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n"
    if anchor not in text:
        raise SystemExit(f"pattern not found in {path}")
    path.write_text(
        text.replace(anchor, anchor + JAM_BLOCK, 1),
        encoding="utf-8",
        newline="\n",
    )
    print(f"patched {path}")


def main() -> int:
    if not REFIND.is_dir() or not (REFIND / "COPYING.txt").is_file():
        raise SystemExit(f"missing rEFInd sources in {REFIND}")
    background = REFIND / "themes/r5-black/background.png"
    if not background.is_file():
        raise SystemExit(f"missing {background}")
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")
    stage(build_refind())
    patch_image_rules()
    patch_regular()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
