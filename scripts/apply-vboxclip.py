#!/usr/bin/env python3
"""Host/guest text clipboard over COM1 for VirtualBox. Idempotent."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "vboxclip"


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def copy_sources() -> None:
    dest = HAIKU / "src" / "servers" / "r5_vbox_clip"
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("VBoxClip.cpp", "VBoxClip.rdef", "Jamfile"):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")


def patch_jams() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "Jamfile",
        "SubInclude HAIKU_TOP src servers airplay ;\n",
        "SubInclude HAIKU_TOP src servers airplay ;\n"
        "SubInclude HAIKU_TOP src servers r5_vbox_clip ;\n",
        "vbox clip jam",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_SERVERS += airplay_deskbar ;\n",
        "SYSTEM_SERVERS += airplay_deskbar ;\n"
        "SYSTEM_SERVERS += r5_vbox_clip ;\n",
        "vbox clip SYSTEM_SERVERS",
    )


def patch_launch() -> None:
    replace_once(
        HAIKU / "data" / "launch" / "user",
        """	job x-vnd.R5OS-NetworkStatus {
		launch /system/apps/NetworkStatus --deskbar
		requires x-vnd.Be-TSKB
		on initial_volumes_mounted
		legacy
		no_safemode
	}
""",
        """	job x-vnd.R5OS-NetworkStatus {
		launch /system/apps/NetworkStatus --deskbar
		requires x-vnd.Be-TSKB
		on initial_volumes_mounted
		legacy
		no_safemode
	}

	job x-vnd.R5OS-vbox-clip {
		launch /system/servers/r5_vbox_clip
		requires x-vnd.Be-TSKB
		on initial_volumes_mounted
		legacy
		no_safemode
	}
""",
        "vbox clip launch job",
    )


def main() -> int:
    copy_sources()
    patch_jams()
    patch_launch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
