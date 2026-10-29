#!/usr/bin/env python3
"""Users & Accounts preference. Idempotent build-tool copy."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "users"


def replace_once(
    path: pathlib.Path, old: str, new: str, tag: str, optional: bool = False
) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        if optional:
            print(f"skip {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_sources() -> None:
    dest = HAIKU / "src" / "preferences" / "users"
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("HomeLock.cpp", "HomeLock.h", "Users.cpp", "Users.rdef", "Jamfile"):
        shutil.copy2(SRC / name, dest / name)
        (dest / name).touch()
        print(f"copied {name} -> {dest}")


def patch_jams() -> None:
    replace_once(
        HAIKU / "src" / "preferences" / "Jamfile",
        "SubInclude HAIKU_TOP src preferences virtualmemory ;\n",
        "SubInclude HAIKU_TOP src preferences virtualmemory ;\n"
        "SubInclude HAIKU_TOP src preferences users ;\n",
        "users jam",
    )
    regular = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    if "SYSTEM_PREFERENCES += Users ;" in text:
        print("already regular (Users SYSTEM_PREFERENCES)")
        return
    replace_once(
        regular,
        "SYSTEM_PREFERENCES += [ FFilterByBuildFeatures\n"
        "	Bluetooth\n",
        "SYSTEM_PREFERENCES += Users ;\n"
        "SYSTEM_PREFERENCES += [ FFilterByBuildFeatures\n"
        "	Bluetooth\n",
        "users SYSTEM_PREFERENCES",
    )


def main() -> int:
    copy_sources()
    patch_jams()
    return 0


if __name__ == "__main__":
    sys.exit(main())
