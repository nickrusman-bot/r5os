#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wi-Fi and Bluetooth on the MediaTek MT7921/MT7922, from distro/.

Copies, with idempotent Jam and image hooks only:
- libr5wifi.so (distro/wifi/lib) and its public headers (headers/os/net),
- the WiFi program (distro/wifi/app),
- the mt7922wifi driver (distro/network/wlan/mt7922wifi),
- BNetworkRoster with the additive AddPersistentNetwork(network, password)
  (distro/network/netapi),
- the MediaTek Wi-Fi and Bluetooth firmware (distro/firmware/mediatek) into
  system/non-packaged/data/firmware/{mt7922wifi,h2generic}.
"""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]

LIB_SOURCES = ("Jamfile", "WiFiController.cpp", "NetworkListView.cpp",
    "JoinOtherWindow.cpp", "WiFiView.cpp")
LIB_HEADERS = ("R5WiFi.h", "R5WiFiNetworkList.h", "R5WiFiGlyphs.h",
    "R5WiFiIconPaths.h", "R5WiFiListView.h", "R5WiFiJoinOther.h",
    "R5WiFiView.h")
APP_SOURCES = ("Jamfile", "WiFi.cpp", "WiFiWindow.cpp", "WiFiWindow.h",
    "WiFi.rdef")
WIFI_FIRMWARE = ("WIFI_MT7961_patch_mcu_1_2_hdr.bin",
    "WIFI_RAM_CODE_MT7961_1.bin")
BT_FIRMWARE = ("BT_RAM_CODE_MT7961_1_2_hdr.bin",)
MARK = "r5MtkDir"

JAM_BLOCK = """
# R5OS MediaTek MT7921 (MT7961) firmware, from linux-firmware, unmodified.
# mt7922wifi and h2generic read it from non-packaged data/firmware/<driver>.
local r5MtkDir = [ FDirName $(HAIKU_TOP) data firmware mediatek ] ;
local r5MtkWifi = <r5mtk>WIFI_MT7961_patch_mcu_1_2_hdr.bin
	<r5mtk>WIFI_RAM_CODE_MT7961_1.bin ;
local r5MtkBt = <r5mtk>BT_RAM_CODE_MT7961_1_2_hdr.bin ;
local r5MtkLicense = <r5mtk>LICENCE.mediatek ;
SEARCH on $(r5MtkWifi) $(r5MtkBt) $(r5MtkLicense) = $(r5MtkDir) ;

AddDirectoryToHaikuImage system non-packaged data firmware mt7922wifi ;
AddFilesToHaikuImage system non-packaged data firmware mt7922wifi
	: $(r5MtkWifi) $(r5MtkLicense) ;
AddDirectoryToHaikuImage system non-packaged data firmware h2generic ;
AddFilesToHaikuImage system non-packaged data firmware h2generic
	: $(r5MtkBt) $(r5MtkLicense) ;
"""


def insert_once(path: pathlib.Path, anchor: str, insert: str, marker: str,
        tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if marker in text:
        print(f"already {path.name} ({tag})")
        return
    if text.count(anchor) != 1:
        raise SystemExit(f"pattern not found once in {path} ({tag})")
    path.write_text(text.replace(anchor, anchor + insert, 1), encoding="utf-8",
        newline="\n")
    print(f"patched {path.name} ({tag})")


def copy(source: pathlib.Path, destination: pathlib.Path, binary: bool = False
        ) -> None:
    if not source.is_file():
        raise SystemExit(f"missing {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes()
    if not binary:
        data = data.replace(b"\r\n", b"\n")
    destination.write_bytes(data)


def copy_sources() -> None:
    wifi = ROOT / "distro" / "wifi"
    for name in LIB_SOURCES:
        copy(wifi / "lib" / name, HAIKU / "src/preferences/r5wifi" / name)
    for name in LIB_HEADERS:
        copy(wifi / "lib" / name, HAIKU / "headers/os/net" / name)
    for name in APP_SOURCES:
        copy(wifi / "app" / name, HAIKU / "src/preferences/wifi" / name)
    print("copied libr5wifi.so and WiFi")

    driver = ROOT / "distro/network/wlan/mt7922wifi"
    for source in driver.rglob("*"):
        if source.is_file():
            copy(source, HAIKU / "src/add-ons/kernel/drivers/network/wlan"
                / "mt7922wifi" / source.relative_to(driver))
    print("copied mt7922wifi")

    netapi = ROOT / "distro/network/netapi"
    copy(netapi / "NetworkRoster.h", HAIKU / "headers/os/net/NetworkRoster.h")
    copy(netapi / "NetworkRoster.cpp",
        HAIKU / "src/kits/network/libnetapi/NetworkRoster.cpp")
    print("copied NetworkRoster")

    firmware = ROOT / "distro/firmware/mediatek"
    for name in WIFI_FIRMWARE + BT_FIRMWARE:
        copy(firmware / name, HAIKU / "data/firmware/mediatek" / name, True)
    copy(firmware / "LICENCE.mediatek",
        HAIKU / "data/firmware/mediatek/LICENCE.mediatek")
    print("copied MediaTek firmware")


def hook_jam() -> None:
    preferences = HAIKU / "src/preferences/Jamfile"
    insert_once(preferences, "SubInclude HAIKU_TOP src preferences r5ui ;\n",
        "SubInclude HAIKU_TOP src preferences r5wifi ;\n"
        "SubInclude HAIKU_TOP src preferences wifi ;\n",
        "src preferences r5wifi ;", "r5wifi and WiFi jam")
    wlan = HAIKU / "src/add-ons/kernel/drivers/network/wlan/Jamfile"
    insert_once(wlan,
        "SubInclude HAIKU_TOP src add-ons kernel drivers network wlan "
        "idualwifi7260 ;\n",
        "SubInclude HAIKU_TOP src add-ons kernel drivers network wlan "
        "mt7922wifi ;\n",
        "wlan mt7922wifi ;", "mt7922wifi jam")


def hook_image() -> None:
    regular = HAIKU / "build/jam/images/definitions/regular"
    insert_once(regular, "\t\t\tlibr5ui.so\n", "\t\t\tlibr5wifi.so\n",
        "libr5wifi.so", "libr5wifi.so")
    insert_once(regular, "SYSTEM_PREFERENCES += Settings ;\n",
        "SYSTEM_PREFERENCES += WiFi ;\n", "SYSTEM_PREFERENCES += WiFi ;",
        "WiFi preference")
    insert_once(regular,
        "\t\tzydwifi1211\n\t}@ # x86,x86_64,riscv64,arm64\n] ;\n",
        "SYSTEM_ADD_ONS_DRIVERS_NET += [ FFilterByBuildFeatures\n"
        "\t# MediaTek MT7921/MT7922 (air/OS), tested on x86_64 only\n"
        "\tx86_64 @{\n\t\tmt7922wifi\n\t}@ # x86_64\n] ;\n",
        "\t\tmt7922wifi\n", "mt7922wifi image")
    insert_once(regular,
        "AddFilesToHaikuImage home config settings : $(shortcutsSettingsFile) ;\n",
        JAM_BLOCK, MARK, "MediaTek firmware")


def main() -> int:
    copy_sources()
    hook_jam()
    hook_image()
    return 0


if __name__ == "__main__":
    sys.exit(main())
