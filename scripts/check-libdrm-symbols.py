#!/usr/bin/env python3
"""Compare undefined libdrm symbols in a Mesa binary with the R5 libdrm exports."""
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/nickr/r5os")
LIBS = [
    ROOT / "r5-sysroot/lib/libdrm.so.2",
    ROOT / "r5-sysroot/lib/libdrm_amdgpu.so.1",
]
BINARIES = [
    ROOT / "mesa-build/mesa-25.3.6/build-radv/src/amd/vulkan/libvulkan_radeon.so",
    ROOT / "mesa-build/mesa-25.3.6/build-radv/src/gallium/targets/lavapipe/libvulkan_lvp.so",
]


def dynamic(path):
    text = subprocess.check_output(["nm", "-D", "--defined-only", str(path)], text=True)
    names = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            names.add(parts[-1])
    return names


def undefined(path):
    text = subprocess.check_output(["nm", "-D", "-u", str(path)], text=True)
    names = []
    for line in text.splitlines():
        parts = line.split()
        if parts:
            names.append(parts[-1])
    return names


def main():
    exports = set()
    for lib in LIBS:
        exports |= dynamic(lib)
    missing = []
    for binary in BINARIES:
        if not binary.exists():
            print(f"absent {binary}")
            continue
        for name in undefined(binary):
            if name.startswith("drm") or name.startswith("amdgpu_"):
                if name not in exports:
                    missing.append(f"{binary.name} {name}")
    if missing:
        print("missing")
        print("\n".join(missing))
        return 1
    print("ok libdrm symbols")
    return 0


if __name__ == "__main__":
    sys.exit(main())
