#!/usr/bin/env python3
"""Replace libvulkan_radeon.so and bump the package revision.

The previous file kept the name mesa_radv-25.3.6-1. An older copy of that
same name, from 9 Nov, could be the one the image installed. The new name
is mesa_radv-25.3.6-4, so a cached 25.3.6-3 cannot be installed in its place.
"""
import shutil
import subprocess
import sys
from pathlib import Path

HAIKU = Path("/home/nickr/r5os/haiku")
PKG = HAIKU / "generated.x86_64/objects/linux/x86_64/release/tools/package/package"
STRIP = HAIKU / "generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-strip"
SRC = Path("/home/nickr/r5os/mesa-build/mesa-25.3.6/build-radv/src/amd/vulkan/libvulkan_radeon.so")


def drop_build_path(path: Path) -> None:
    data = bytearray(path.read_bytes())
    needle = b"/home/nickr"
    start = 0
    while True:
        index = data.find(needle, start)
        if index < 0:
            break
        end = data.find(b"\x00", index)
        if end < 0:
            raise SystemExit(f"unterminated build path in {path}")
        replacement = b"/boot/system/lib"
        if end - index < len(replacement):
            raise SystemExit(f"build path is shorter than the system path in {path}")
        data[index:index + len(replacement)] = replacement
        data[index + len(replacement):end] = b"\x00" * (end - index - len(replacement))
        start = end
    path.write_bytes(data)


def main() -> None:
    root = Path(sys.argv[1])
    out_dir = root / "distro" / "packages" / "x86_64"
    previous = out_dir / "mesa_radv-25.3.6-1-x86_64.hpkg"
    dest = out_dir / "mesa_radv-25.3.6-4-x86_64.hpkg"
    stage = Path("/tmp/r5-radv-pkg")
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    subprocess.check_call([str(PKG), "extract", "-C", str(stage), str(previous)])
    so = stage / "lib" / "libvulkan_radeon.so"
    shutil.copy2(SRC, so)
    subprocess.check_call([str(STRIP), "--strip-unneeded", str(so)])
    drop_build_path(so)
    if b"/home/nickr" in so.read_bytes():
        raise SystemExit("libvulkan_radeon.so still contains /home/nickr")
    boot = root / "distro" / "graphics" / "boot"
    bindir = stage / "bin"
    bindir.mkdir(exist_ok=True)
    for name in ("boot2.sh", "check.sh"):
        text = (boot / name).read_text(encoding="utf-8").replace("\r\n", "\n")
        (bindir / name).write_text(text, encoding="utf-8", newline="\n")
        (bindir / name).chmod(0o755)
    info = stage / ".PackageInfo"
    text = info.read_text(encoding="utf-8")
    text = text.replace("version\t\t\t25.3.6-1", "version\t\t\t25.3.6-4")
    text = text.replace("mesa_radv = 25.3.6-1", "mesa_radv = 25.3.6-4")
    if "25.3.6-4" not in text:
        raise SystemExit("package version was not bumped")
    info.write_text(text, encoding="utf-8")
    if dest.exists():
        dest.unlink()
    subprocess.check_call([str(PKG), "create", "-C", str(stage), str(dest)])
    print("created", dest, dest.stat().st_size)


if __name__ == "__main__":
    main()
