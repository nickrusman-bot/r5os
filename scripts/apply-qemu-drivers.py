#!/usr/bin/env python3
"""Ship virtio-blk on AMD64 so QEMU virtio disks work (Haiku only packed it
for ARM/RISC-V)."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")

RESTRICTED = "virtio_block@arm,arm64,riscv64"
ENABLED = "virtio_block"


def enable_virtio_block(path: pathlib.Path) -> None:
    text = path.read_text(encoding="utf-8")
    if RESTRICTED not in text:
        if ENABLED in text:
            print(f"already {path.name} (virtio_block on all archs)")
            return
        raise SystemExit(f"pattern not found in {path} (virtio_block)")
    path.write_text(text.replace(RESTRICTED, ENABLED), encoding="utf-8")
    print(f"patched {path} (virtio_block for x86_64 QEMU)")


def main() -> int:
    for rel in (
        "build/jam/packages/Haiku",
        "build/jam/packages/HaikuBootstrap",
    ):
        enable_virtio_block(HAIKU / rel)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
