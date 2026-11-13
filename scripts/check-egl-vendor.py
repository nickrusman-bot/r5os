#!/usr/bin/env python3
"""Fail the image if libEGL_mesa cannot be loaded by libglvnd.

The vendor path must name a file next to the json (it contains '/').
Every non-weak undefined symbol must be exported by a NEEDED library
from the packages that this image installs.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

PKG = Path("/home/nickr/r5os/haiku/generated.x86_64/objects/linux/x86_64/release/tools/package/package")
DL = Path("/home/nickr/r5os/haiku/generated.x86_64/download")
BUILT = Path("/home/nickr/r5os/haiku/generated.x86_64/build_packages")
SYS = Path("/home/nickr/r5os/haiku/generated.x86_64/cross-tools-x86_64/sysroot/boot/system/lib")
STAGE = Path("/tmp/r5-egl-check")


def extract(hpkg, dest):
    dest.mkdir(parents=True, exist_ok=True)
    subprocess.check_call([str(PKG), "extract", "-C", str(dest), str(hpkg)])


def exports(path):
    try:
        text = subprocess.check_output(
            ["nm", "-D", "--defined-only", str(path)], text=True, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        return set()
    names = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            names.add(parts[-1])
            names.add(parts[-1].split("@")[0])
    return names


def main():
    if STAGE.exists():
        shutil.rmtree(STAGE)
    mesa = DL / "mesa-25.3.6-5-x86_64.hpkg"
    extract(mesa, STAGE / "mesa")
    json_path = STAGE / "mesa/add-ons/opengl/egl_vendor.d/50_mesa.json"
    text = json_path.read_text()
    if '"./libEGL_mesa.so.0"' not in text:
        print("libEGL_mesa vendor path is not a file next to the json")
        return 1
    egl = STAGE / "mesa/add-ons/opengl/egl_vendor.d/libEGL_mesa.so.0.0.0"
    dyn = subprocess.check_output(["readelf", "-d", str(egl)], text=True)
    needed = []
    for line in dyn.splitlines():
        if "(NEEDED)" in line:
            needed.append(line.split("[", 1)[1].rstrip("]"))
    libs = {}
    for root in (
        DL / "llvm21_runtime-21.1.8-1-x86_64.hpkg",
        DL / "zlib-1.3.2-1-x86_64.hpkg",
        DL / "libdrm-2.4.124-2-x86_64.hpkg",
        DL / "expat-2.8.2-1-x86_64.hpkg",
        DL / "gcc_syslibs-13.3.0_2023_08_10-6-x86_64.hpkg",
    ):
        dest = STAGE / root.name
        extract(root, dest)
        for dirpath, _, files in os.walk(dest):
            for name in files:
                if ".so" in name and name not in libs:
                    libs[name] = Path(dirpath) / name
    for name in ("libbe.so", "libroot.so", "libstdc++.so.6"):
        hit = SYS / name
        if hit.exists():
            libs.setdefault(name, hit)
        built = BUILT / "gcc_syslibs-13.3.0_2023_08_10-6-x86_64/lib" / name
        if built.exists():
            libs.setdefault(name, built)
    missing_libs = [name for name in needed if name not in libs]
    if missing_libs:
        print("NEEDED library not in the image packages:", " ".join(missing_libs))
        return 1
    provided = set()
    for name in needed:
        provided |= exports(libs[name])
    symbols = subprocess.check_output(["readelf", "-W", "-s", str(egl)], text=True)
    missing = []
    for line in symbols.splitlines():
        parts = line.split()
        if "UND" not in parts:
            continue
        und = parts.index("UND")
        if und == 0 or und + 1 >= len(parts):
            continue
        if parts[und - 2] == "WEAK":
            continue
        sym = parts[und + 1]
        if sym.startswith("("):
            continue
        if sym not in provided and sym.split("@")[0] not in provided:
            missing.append(sym)
    if missing:
        print("unresolved libEGL_mesa symbols:")
        print("\n".join(missing))
        return 1
    print("ok egl vendor")
    return 0


if __name__ == "__main__":
    sys.exit(main())
