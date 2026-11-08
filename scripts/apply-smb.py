#!/usr/bin/env python3
"""Copy the SMB share sources into the Haiku tree and hook the Jamfiles.

libsmb2, the smbfs userlandfs add-on, the mount server, and the package.
Tracker sources live under distro/desktop and are copied by
apply-desktop-native.py. This script only adds them to the Tracker Jamfile.
"""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "smb"


def copy_tree(source: pathlib.Path, destination: pathlib.Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"missing {source}")
    count = 0
    for path in sorted(source.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        target = destination / relative
        if not target.parent.is_dir():
            target.parent.mkdir(parents=True, exist_ok=True)
        data = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
        if target.is_file() and target.read_bytes().replace(b"\r\n", b"\n").replace(
            b"\r", b"\n"
        ) == data:
            print(f"already {relative.as_posix()}")
        else:
            target.write_bytes(data)
            print(f"copied {relative.as_posix()}")
        count += 1
    if count == 0:
        raise SystemExit(f"no files under {source}")


def insert(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {tag}")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {tag}")


def main() -> int:
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")
    copy_tree(SRC / "src", HAIKU / "src")
    copy_tree(SRC / "headers", HAIKU / "headers")
    copy_tree(SRC / "build", HAIKU / "build")

    insert(
        HAIKU / "src" / "libs" / "Jamfile",
        "SubInclude HAIKU_TOP src libs libsolv ;\n",
        "SubInclude HAIKU_TOP src libs libsolv ;\n"
        "SubInclude HAIKU_TOP src libs libsmb2 ;\n",
        "libsmb2",
    )
    insert(
        HAIKU / "src" / "add-ons" / "kernel" / "file_systems" / "Jamfile",
        "SubInclude HAIKU_TOP src add-ons kernel file_systems userlandfs ;\n",
        "SubInclude HAIKU_TOP src add-ons kernel file_systems smbfs ;\n"
        "SubInclude HAIKU_TOP src add-ons kernel file_systems userlandfs ;\n",
        "smbfs",
    )
    tracker = HAIKU / "src" / "kits" / "tracker" / "Jamfile"
    insert(
        tracker,
        "\t\t\tNavMenu.cpp\n\t\t\tNodePreloader.cpp\n",
        "\t\t\tNavMenu.cpp\n\t\t\tNetworkSharesSettings.cpp\n"
        "\t\t\tNodePreloader.cpp\n",
        "tracker sources",
    )
    insert(
        tracker,
        "\tNavMenu.cpp\n\tOpenWithWindow.cpp\n",
        "\tNavMenu.cpp\n\tNetworkSharesSettings.cpp\n\tOpenWithWindow.cpp\n",
        "tracker catalog",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
