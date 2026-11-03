#!/usr/bin/env python3
"""Copy the network-services timeout fix into the Haiku tree."""
from __future__ import annotations

import pathlib
import shutil
import sys

FILES = (
    "headers/private/netservices/NetworkRequest.h",
    "src/kits/network/libnetservices/NetworkRequest.cpp",
    "src/kits/network/libnetservices/HttpRequest.cpp",
)


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    haiku = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "haiku")
    source_root = root / "distro" / "network"
    if not haiku.is_dir():
        raise SystemExit(f"haiku tree not found: {haiku}")

    for relative in FILES:
        source = source_root / relative
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        destination = haiku / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")


if __name__ == "__main__":
    main()
