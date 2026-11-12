#!/usr/bin/env python3
"""Wi-Fi: one-screen join, remember networks, Deskbar tray (R5OS)."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "network"


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if "R5OS modifications in this file are GPL-3.0-or-later." in text:
        print(f"already {path.name} ({tag})")
        return
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_sources() -> None:
    dest = HAIKU / "src" / "apps" / "networkstatus"
    # NetworkStatus is native R5 source now (distro/network/networkstatus):
    # the Wi-Fi arcs, the Wi-Fi settings item and the R5 Wi-Fi join window.
    # It replaces the string patches this script used to apply to it.
    for name in ("NetworkStatusView.cpp", "NetworkStatusView.h", "Jamfile"):
        source = SRC / "networkstatus" / name
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        shutil.copy2(source, dest / name)
        print(f"copied networkstatus/{name}")
    for name in ("WiFiWindow.cpp", "WiFiWindow.h"):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")



def patch_autoconfig() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.cpp",
        """			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
			}
""",
        """			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
				fJoiningNetwork = false;
			}
""",
        "rejoin after link loss",
    )



def patch_launch() -> None:
    user_launch = (HAIKU / "data" / "launch" / "user").read_text(encoding="utf-8")
    if "job x-vnd.R5OS-NetworkStatus" in user_launch:
        print("already user (NetworkStatus deskbar job)")
        return
    replace_once(
        HAIKU / "data" / "launch" / "user",
        """	service x-vnd.Be-TSKB {
		launch /system/Deskbar
		on initial_volumes_mounted
	}
""",
        """	service x-vnd.Be-TSKB {
		launch /system/Deskbar
		on initial_volumes_mounted
	}

	job x-vnd.R5OS-NetworkStatus {
		launch /system/apps/NetworkStatus --deskbar
		requires x-vnd.Be-TSKB
		on initial_volumes_mounted
		legacy
		no_safemode
	}
""",
        "NetworkStatus deskbar job",
    )


def main() -> int:
    copy_sources()
    patch_autoconfig()
    patch_launch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
