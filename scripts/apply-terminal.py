#!/usr/bin/env python3
"""Copy the Terminal wheel fix into the Haiku tree."""
from __future__ import annotations

import pathlib
import shutil
import sys


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    haiku = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "haiku")
    relative = "src/apps/terminal/TermView.cpp"
    source = root / "distro" / "apps" / "terminal" / relative
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    if not haiku.is_dir():
        raise SystemExit(f"haiku tree not found: {haiku}")
    destination = haiku / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    print(f"copied {relative}")


if __name__ == "__main__":
    main()
