#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install VirtualBox 7.2.22 guest additions into the R5OS image.

The binaries are not stored in git. This script copies a local build and the
matching source into the Haiku tree, then teaches Jam to put them on the
image. It fails if either is missing.
"""
from __future__ import annotations

import hashlib
import os
import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]

STAGE = HAIKU / "src" / "add-ons" / "kernel" / "r5_vbox_guest" / "stage"
BIN = STAGE / "binaries"
SRC = STAGE / "source"

# The device stub stays small. The generic module is the large one.
# Swapping them panics or leaves the guest with no /dev/misc/vboxguest.
ELF_LIMITS = {
    "vboxguest": (200_000, 2_000_000),
    "vboxdev": (8_000, 80_000),
    "vboxsf": (20_000, 500_000),
    "vboxvideo": (10_000, 500_000),
    "vboxvideo.accelerant": (10_000, 500_000),
    "VBoxTray": (100_000, 20_000_000),
    "VBoxService": (100_000, 20_000_000),
    "VBoxControl": (100_000, 20_000_000),
}

JAM_MARK = "# R5OS VirtualBox guest additions\n"
LAUNCH_MARK = "job x-vnd.R5OS-vbox-service"


def fail(message: str) -> None:
    raise SystemExit(message)


def vbox_tree() -> pathlib.Path:
    env = os.environ.get("R5OS_VBOX_TREE")
    if env:
        return pathlib.Path(env)
    return pathlib.Path("/home/nickr/r5os/third_party/VirtualBox-7.2.22")


def tarball_path() -> pathlib.Path:
    env = os.environ.get("R5OS_VBOX_TARBALL")
    if env:
        return pathlib.Path(env)
    return pathlib.Path("/home/nickr/r5os/third_party/VirtualBox-7.2.22.tar.bz2")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stage_binaries() -> None:
    built = vbox_tree() / "out" / "haiku.amd64" / "release" / "bin" / "additions"
    if not built.is_dir():
        fail(f"VirtualBox additions build is missing: {built}")
    BIN.mkdir(parents=True, exist_ok=True)
    for name, (low, high) in ELF_LIMITS.items():
        src = built / name
        if not src.is_file():
            fail(f"missing {src}")
        size = src.stat().st_size
        if size < low or size > high:
            fail(f"{name} is {size} bytes, expected {low}..{high}")
        dest = BIN / name
        shutil.copy2(src, dest)
        dest.chmod(0o755)
        print(f"staged {name} ({size} bytes)")


def stage_source() -> None:
    archive = tarball_path()
    if not archive.is_file():
        fail(f"VirtualBox source tarball is missing: {archive}")
    if archive.stat().st_size < 200_000_000:
        fail(f"{archive} is too small to be the 7.2.22 source tarball")
    patch = ROOT / "distro" / "vbox-guest" / "virtualbox-7.2.22-r5os.patch"
    if not patch.is_file():
        fail(f"missing {patch}")
    gpl = HAIKU / "data" / "system" / "data" / "licenses" / "GNU GPL v3"
    if not gpl.is_file():
        fail(f"missing {gpl}")
    gpl_text = gpl.read_bytes()
    if b"GNU GENERAL PUBLIC LICENSE" not in gpl_text[:400]:
        fail(f"{gpl} is not the GNU GPL text")
    if b"Version 3, 29 June 2007" not in gpl_text[:400]:
        fail(f"{gpl} is not GPL version 3")

    SRC.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archive, SRC / "VirtualBox-7.2.22.tar.bz2")
    shutil.copy2(patch, SRC / "virtualbox-7.2.22-r5os.patch")
    (SRC / "COPYING").write_bytes(gpl_text)
    for name in (
        "build-vbox-additions.sh",
        "vbox-additions-setup.py",
        "vbox-pkg-config.py",
    ):
        src = ROOT / "scripts" / name
        if not src.is_file():
            fail(f"missing {src}")
        shutil.copy2(src, SRC / name)
    readme = ROOT / "distro" / "vbox-guest" / "README.md"
    shutil.copy2(readme, SRC / "README.md")

    digest = sha256(archive)
    notice = f"""VirtualBox Guest Additions 7.2.22 as installed on this R5OS disk.

License: GNU General Public License, version 3 only (GPL-3.0-only).
COPYING in this directory is that unmodified license text.
Some IPRT files are offered as GPL-3.0-only or CDDL-1.0. This build uses the GPL-3.0-only terms.
The "or later" grant on new R5OS files is not added to the VirtualBox sources.

Corresponding source for the guest binaries on this disk:
- VirtualBox-7.2.22.tar.bz2
  SHA-256: {digest}
  Official download: https://download.virtualbox.org/virtualbox/7.2.22/VirtualBox-7.2.22.tar.bz2
- virtualbox-7.2.22-r5os.patch
  Apply inside the unpacked tree with: patch -p1
- build-vbox-additions.sh, vbox-additions-setup.py, and vbox-pkg-config.py
- README.md (how the cross build is set up)

The binaries are the Haiku AMD64 guest additions built from that tarball plus that patch.
They are not stored in the R5OS git repository. Publishing the git repository without this tarball is not this source.

Installed on this disk:
- /boot/system/non-packaged/add-ons/kernel/generic/vboxguest
- /boot/system/non-packaged/add-ons/kernel/drivers/dev/misc/vboxguest
  (the small device stub; the file is built as vboxdev and must not be replaced with the generic module)
- /boot/system/non-packaged/add-ons/kernel/file_systems/vboxsf
- /boot/system/non-packaged/add-ons/kernel/drivers/dev/graphics/vboxvideo
- /boot/system/non-packaged/add-ons/accelerants/vboxvideo.accelerant
- /boot/system/non-packaged/bin/VBoxService
- /boot/system/non-packaged/bin/VBoxTray
- /boot/system/non-packaged/bin/VBoxControl

To install a modified version, build with those scripts and the R5OS AMD64 cross compiler, copy the files to the same paths, and restart. No extra key is required. Removing vboxvideo leaves the VESA display. Safe mode skips the VirtualBox launch jobs.

A host shared folder is not mounted by itself. When VirtualBox offers one, mount it with:
mount -t vboxsf -p <share-name> <directory>

The clipboard R5OS starts on its own is the serial clipboard. VirtualBox drag-and-drop is not part of this build.

This notice is R5OS text, GPL-3.0-or-later. The VirtualBox files keep their own license.
"""
    (SRC / "SOURCE.txt").write_text(notice, encoding="utf-8", newline="\n")
    print(f"staged source ({archive.stat().st_size} bytes, sha256 {digest})")


JAM_BLOCK = """
# R5OS VirtualBox guest additions
local r5VBoxStage = [ FDirName $(HAIKU_TOP) src add-ons kernel r5_vbox_guest stage ] ;
local r5VBoxSrc = [ FDirName $(r5VBoxStage) source ] ;
local r5VBoxBin = [ FDirName $(r5VBoxStage) binaries ] ;

local r5VBoxGuest = <r5vbox>vboxguest ;
local r5VBoxDev = <r5vbox>vboxdev ;
local r5VBoxSf = <r5vbox>vboxsf ;
local r5VBoxVideo = <r5vbox>vboxvideo ;
local r5VBoxAccel = <r5vbox>vboxvideo.accelerant ;
local r5VBoxTray = <r5vbox>VBoxTray ;
local r5VBoxService = <r5vbox>VBoxService ;
local r5VBoxControl = <r5vbox>VBoxControl ;
SEARCH on $(r5VBoxGuest) $(r5VBoxDev) $(r5VBoxSf) $(r5VBoxVideo) $(r5VBoxAccel)
	$(r5VBoxTray) $(r5VBoxService) $(r5VBoxControl) = $(r5VBoxBin) ;

local r5VBoxTarball = <r5vbox>VirtualBox-7.2.22.tar.bz2 ;
local r5VBoxPatch = <r5vbox>virtualbox-7.2.22-r5os.patch ;
local r5VBoxCopying = <r5vbox>COPYING ;
local r5VBoxNotice = <r5vbox>SOURCE.txt ;
local r5VBoxReadme = <r5vbox>README.md ;
local r5VBoxBuild = <r5vbox>build-vbox-additions.sh ;
local r5VBoxSetup = <r5vbox>vbox-additions-setup.py ;
local r5VBoxPkg = <r5vbox>vbox-pkg-config.py ;
SEARCH on $(r5VBoxTarball) $(r5VBoxPatch) $(r5VBoxCopying) $(r5VBoxNotice)
	$(r5VBoxReadme) $(r5VBoxBuild) $(r5VBoxSetup) $(r5VBoxPkg) = $(r5VBoxSrc) ;

AddDirectoryToHaikuImage system non-packaged add-ons kernel generic ;
AddDirectoryToHaikuImage system non-packaged add-ons kernel file_systems ;
AddDirectoryToHaikuImage system non-packaged add-ons kernel drivers dev graphics ;
AddDirectoryToHaikuImage system non-packaged add-ons kernel drivers dev misc ;
AddDirectoryToHaikuImage system non-packaged add-ons accelerants ;
AddDirectoryToHaikuImage system non-packaged data licenses ;
AddDirectoryToHaikuImage system non-packaged data licenses virtualbox ;

AddFilesToHaikuImage system non-packaged add-ons kernel generic
	: $(r5VBoxGuest) : vboxguest ;
AddFilesToHaikuImage system non-packaged add-ons kernel drivers dev misc
	: $(r5VBoxDev) : vboxguest ;
AddFilesToHaikuImage system non-packaged add-ons kernel file_systems
	: $(r5VBoxSf) : vboxsf ;
AddFilesToHaikuImage system non-packaged add-ons kernel drivers dev graphics
	: $(r5VBoxVideo) : vboxvideo ;
AddFilesToHaikuImage system non-packaged add-ons accelerants
	: $(r5VBoxAccel) : vboxvideo.accelerant ;
AddFilesToHaikuImage system non-packaged bin : $(r5VBoxTray) : VBoxTray ;
AddFilesToHaikuImage system non-packaged bin : $(r5VBoxService) : VBoxService ;
AddFilesToHaikuImage system non-packaged bin : $(r5VBoxControl) : VBoxControl ;

AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxTarball) : VirtualBox-7.2.22.tar.bz2 ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxPatch) : virtualbox-7.2.22-r5os.patch ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxCopying) : COPYING ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxNotice) : SOURCE.txt ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxReadme) : README.md ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxBuild) : build-vbox-additions.sh ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxSetup) : vbox-additions-setup.py ;
AddFilesToHaikuImage system non-packaged data licenses virtualbox
	: $(r5VBoxPkg) : vbox-pkg-config.py ;
"""


def patch_jam() -> None:
    path = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    text = path.read_text(encoding="utf-8")
    if JAM_MARK in text:
        print(f"already {path.name} (vbox guest image)")
        return
    anchor = "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n"
    if anchor not in text:
        fail(f"pattern not found in {path} (vbox guest image)")
    path.write_text(text.replace(anchor, anchor + JAM_BLOCK, 1), encoding="utf-8")
    print(f"patched {path} (vbox guest image)")


def patch_launch() -> None:
    path = HAIKU / "data" / "launch" / "user"
    text = path.read_text(encoding="utf-8")
    if LAUNCH_MARK in text:
        print(f"already {path.name} (vbox guest jobs)")
        return
    anchor = (
        "\tjob x-vnd.R5OS-vbox-clip {\n"
        "\t\tlaunch /system/servers/r5_vbox_clip\n"
        "\t\trequires x-vnd.Be-TSKB\n"
        "\t\tlegacy\n"
        "\t\tno_safemode\n"
        "\t}\n"
    )
    insert = (
        anchor
        + "\n"
        + "\tjob x-vnd.R5OS-vbox-service {\n"
        + "\t\tlaunch /system/non-packaged/bin/VBoxService -f --disable-memballoon --timesync-no-set-start\n"
        + "\t\tlegacy\n"
        + "\t\tno_safemode\n"
        + "\t}\n"
        + "\n"
        + "\tjob x-vnd.R5OS-vbox-tray {\n"
        + "\t\tlaunch /system/non-packaged/bin/VBoxTray\n"
        + "\t\trequires x-vnd.Be-TSKB\n"
        + "\t\tlegacy\n"
        + "\t\tno_safemode\n"
        + "\t}\n"
    )
    if anchor not in text:
        fail(f"pattern not found in {path} (vbox guest jobs)")
    if text.count(anchor) != 1:
        fail(f"vbox clip job anchor is not unique in {path}")
    path.write_text(text.replace(anchor, insert, 1), encoding="utf-8")
    print(f"patched {path} (vbox guest jobs)")


def main() -> int:
    stage_binaries()
    stage_source()
    patch_jam()
    patch_launch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
