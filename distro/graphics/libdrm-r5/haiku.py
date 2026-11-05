#!/usr/bin/env python3
"""Apply the Haiku edits to a clean libdrm 2.4.124 tree.

The edits live in patches/haiku-xf86drm.patch. Pass the libdrm source root.
"""
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
here = Path(__file__).resolve().parent
header = here.parents[0] / "headers" / "private" / "r5gpu" / "r5_gpu.h"
for name in ("r5_translate.c", "r5_translate.h"):
    target = root / name
    target.write_bytes((here / name).read_bytes())
    print(f"copied {target}")
(root / "r5_gpu.h").write_bytes(header.read_bytes())
print(f"copied {root / 'r5_gpu.h'}")

patches = [
    "haiku-xf86drm.patch",
    "haiku-drmioctl.patch",
    "haiku-bo-map.patch",
    "haiku-meson.patch",
]
for name in patches:
    patch = here / "patches" / name
    result = subprocess.run(
        ["patch", "-p1", "--forward", "--batch", "-i", str(patch)],
        cwd=root,
        text=True,
        capture_output=True,
    )
    output = (result.stdout or "") + (result.stderr or "")
    if result.returncode != 0 and "previously applied" not in output.lower() \
            and "Reversed" not in output:
        sys.stderr.write(output)
        raise SystemExit(f"{name} failed: {result.returncode}")
    print(f"patched {name}")
print(f"patched {root}")
