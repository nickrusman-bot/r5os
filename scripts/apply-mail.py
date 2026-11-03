#!/usr/bin/env python3
"""Copy the mail date fix and the POP3 flush fix into the Haiku tree."""
from __future__ import annotations

import pathlib
import shutil
import sys


FILES = (
    "src/kits/mail/mail_util.cpp",
    "src/add-ons/mail_daemon/inbound_protocols/pop3/POP3.cpp",
)


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    haiku = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "haiku")
    if not haiku.is_dir():
        raise SystemExit(f"haiku tree not found: {haiku}")
    for relative in FILES:
        source = root / "distro" / "mail" / relative
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        destination = haiku / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")


if __name__ == "__main__":
    main()
