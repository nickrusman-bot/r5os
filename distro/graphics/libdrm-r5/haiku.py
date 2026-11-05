#!/usr/bin/env python3
"""Apply the Haiku edits to a clean libdrm 2.4.124 tree.

The edits live in patches/haiku-xf86drm.patch. Pass the libdrm source root.
"""
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
patch = Path(__file__).resolve().parent / "patches" / "haiku-xf86drm.patch"
result = subprocess.run(
    ["patch", "-p1", "--forward", "--batch", "-i", str(patch)],
    cwd=root,
    text=True,
)
if result.returncode != 0:
    raise SystemExit(f"patch failed: {result.returncode}")
print(f"patched {root}")
