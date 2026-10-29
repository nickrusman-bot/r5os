#!/usr/bin/env python3
"""R5OS Login on the boot path. Idempotent copy + launch hook."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "login"


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
    dest = HAIKU / "src" / "apps" / "login"
    dest.mkdir(parents=True, exist_ok=True)
    for name in (
        "DesktopWindow.cpp",
        "DesktopWindow.h",
        "Jamfile",
        "Login.rdef",
        "LoginApp.cpp",
        "LoginApp.h",
        "LoginView.cpp",
        "LoginView.h",
        "LoginWindow.cpp",
        "LoginWindow.h",
        "main.cpp",
    ):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")


def patch_launch() -> None:
    replace_once(
        HAIKU / "data" / "launch" / "user",
        "target first_boot {\n"
        "	job x-vnd.Haiku-FirstBootPrompt\n"
        "}\n",
        "target login {\n"
        "	job x-vnd.Haiku-Login {\n"
        "		launch /system/apps/Login\n"
        "		on initial_volumes_mounted\n"
        "		legacy\n"
        "	}\n"
        "}\n"
        "\n"
        "target first_boot {\n"
        "	job x-vnd.Haiku-FirstBootPrompt\n"
        "}\n",
        "login target",
    )
    replace_once(
        HAIKU / "data" / "launch" / "user",
        "	else {\n"
        "		desktop\n"
        "	}\n"
        "}\n",
        "	else {\n"
        "		login\n"
        "	}\n"
        "}\n",
        "run login not desktop",
    )


def patch_image() -> None:
    regular = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    replace_once(
        regular,
        "SYSTEM_APPS += [ FFilterByBuildFeatures\n"
        "	ActivityMonitor\n",
        "SYSTEM_APPS += Login ;\n"
        "SYSTEM_APPS += [ FFilterByBuildFeatures\n"
        "	ActivityMonitor\n",
        "Login SYSTEM_APPS",
    )


def main() -> int:
    copy_sources()
    patch_launch()
    patch_image()
    return 0


if __name__ == "__main__":
    sys.exit(main())
