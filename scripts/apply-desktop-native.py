#!/usr/bin/env python3
"""Copy distro/desktop over the Haiku tree.

Interface, Tracker, Deskbar, locale, control look, and the related preference
and app_server files. The Haiku MIT (or other original) header stays. R5OS
edits are the SPDX-Snippet blocks and are MIT.
"""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
SOURCE = pathlib.Path(__file__).resolve().parents[1] / "distro" / "desktop"


def main() -> int:
    if not SOURCE.is_dir():
        raise SystemExit(f"missing {SOURCE}")
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")
    count = 0
    for path in sorted(SOURCE.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(SOURCE)
        destination = HAIKU / relative
        if not destination.parent.is_dir():
            raise SystemExit(f"missing {destination.parent}")
        data = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        if destination.is_file() and destination.read_bytes().replace(
            b"\r\n", b"\n"
        ).replace(b"\r", b"\n") == data:
            print(f"already {relative.as_posix()}")
        else:
            destination.write_bytes(data)
            print(f"copied {relative.as_posix()}")
        count += 1
    if count == 0:
        raise SystemExit(f"no files under {SOURCE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
