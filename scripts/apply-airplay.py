#!/usr/bin/env python3
"""Ship the R5OS AirPlay receiver and AboutSystem credits. Idempotent."""
from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "airplay"
THIRD = SRC / "third_party"


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
    dest = HAIKU / "src" / "servers" / "airplay"
    dest.mkdir(parents=True, exist_ok=True)
    for name in (
        "AirPlayServer.cpp",
        "AirPlayPlayer.cpp",
        "AirPlayPlayer.h",
        "AirPlayCrypto.cpp",
        "AirPlayCrypto.h",
        "AirPlaySender.cpp",
        "AirPlaySender.h",
        "AirPlayDeskbar.cpp",
        "AirPlayMessages.h",
        "AirPlayServer.rdef",
        "AirPlayDeskbar.rdef",
        "Jamfile",
        "THIRD_PARTY.md",
    ):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")
    for name in ("alac.c", "alac.h", "aes.c", "aes.h"):
        shutil.copy2(THIRD / name, dest / name)
        print(f"copied {name} -> {dest}")


def extract_openssl() -> None:
    gen = HAIKU / "generated.x86_64"
    download = gen / "download"
    dest = gen / "r5-openssl"
    package = (
        gen
        / "objects"
        / "linux"
        / "x86_64"
        / "release"
        / "tools"
        / "package"
        / "package"
    )
    if not package.is_file():
        print("warning: Haiku package tool missing; skip openssl extract")
        return
    devel = sorted(download.glob("openssl3_devel-*-x86_64.hpkg"))
    runtime = sorted(download.glob("openssl3-*-x86_64.hpkg"))
    if (dest / "include" / "openssl" / "bio.h").is_file() and (
        dest / "lib" / "libcrypto.so.3"
    ).is_file():
        print(f"already extracted openssl -> {dest}")
        return
    if not devel or not runtime:
        print("warning: openssl3 hpkgs not in download/; jam may fail")
        return
    inc = dest / "include"
    lib = dest / "lib"
    inc.mkdir(parents=True, exist_ok=True)
    lib.mkdir(parents=True, exist_ok=True)
    tmp_dev = dest / "_dev"
    tmp_rt = dest / "_rt"
    shutil.rmtree(tmp_dev, ignore_errors=True)
    shutil.rmtree(tmp_rt, ignore_errors=True)
    tmp_dev.mkdir()
    tmp_rt.mkdir()
    subprocess.check_call([str(package), "extract", "-C", str(tmp_dev), str(devel[-1])])
    subprocess.check_call([str(package), "extract", "-C", str(tmp_rt), str(runtime[-1])])
    headers = tmp_dev / "develop" / "headers"
    if headers.is_dir():
        if inc.exists():
            shutil.rmtree(inc)
        shutil.copytree(headers, inc)
    crypto = tmp_rt / "lib" / "libcrypto.so.3"
    if crypto.is_file():
        shutil.copy2(crypto, lib / "libcrypto.so.3")
    shutil.rmtree(tmp_dev, ignore_errors=True)
    shutil.rmtree(tmp_rt, ignore_errors=True)
    print(f"extracted openssl headers/libs -> {dest}")


def patch_jams() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "Jamfile",
        "SubInclude HAIKU_TOP src servers app ;\n",
        "SubInclude HAIKU_TOP src servers app ;\n"
        "SubInclude HAIKU_TOP src servers airplay ;\n",
        "airplay jam",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_SERVERS += [ FFilterByBuildFeatures\n",
        "SYSTEM_SERVERS += airplay_server ;\n"
        "SYSTEM_SERVERS += [ FFilterByBuildFeatures\n",
        "airplay SYSTEM_SERVERS",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "SYSTEM_SERVERS += airplay_server ;\n",
        "SYSTEM_SERVERS += airplay_server ;\n"
        "SYSTEM_SERVERS += airplay_deskbar ;\n",
        "airplay_deskbar SYSTEM_SERVERS",
    )


def patch_launch() -> None:
    replace_once(
        HAIKU / "data" / "launch" / "system",
        "service x-vnd.Haiku-net_server {\n"
        "	launch /system/servers/net_server\n"
        "	no_safemode\n"
        "	legacy\n"
        "}\n",
        "service x-vnd.Haiku-net_server {\n"
        "	launch /system/servers/net_server\n"
        "	no_safemode\n"
        "	legacy\n"
        "}\n"
        "\n"
        "service x-vnd.R5OS-airplay {\n"
        "	launch /system/servers/airplay_server\n"
        "	no_safemode\n"
        "	legacy\n"
        "}\n",
        "airplay launch job",
    )
    replace_once(
        HAIKU / "data" / "launch" / "system",
        "service x-vnd.R5OS-airplay {\n"
        "	launch /system/servers/airplay_server\n"
        "	no_safemode\n"
        "	legacy\n"
        "}\n",
        "service x-vnd.R5OS-airplay {\n"
        "	launch /system/servers/airplay_server\n"
        "	no_safemode\n"
        "	legacy\n"
        "}\n"
        "\n"
        "service x-vnd.R5OS-airplay-tray {\n"
        "	launch /system/servers/airplay_deskbar\n"
        "	no_safemode\n"
        "	legacy\n"
        "}\n",
        "airplay tray launch job",
    )


def patch_about() -> None:
    replace_once(
        HAIKU / "src" / "apps" / "aboutsystem" / "AboutSystem.cpp",
        "		\"https://www.netbsd.org\");\n"
        "\n"
        "	// FFmpeg copyrights\n",
        "		\"https://www.netbsd.org\");\n"
        "\n"
        "#ifndef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL\n"
        "	AddCopyrightEntry(\"R5OS AirPlay\",\n"
        "		B_TRANSLATE(\"AirPlay 1 RAOP audio receiver. Protocol flow and "
        "AirPort Express RSA key from Shairport (James Laird) and Shairport "
        "Sync (Mike Brady). ALAC decoder by David Hammerton. AES-128-CBC from "
        "tiny-AES-c (kokke). RSA via OpenSSL 3. AirPlay 2 is advertised "
        "(_airplay._tcp, /info); HomeKit pairing and PTP are not included.\\n\"\n"
        "		COPYRIGHT_STRING \"2011-2013 James Laird, \"\n"
        "		COPYRIGHT_STRING \"2014-2025 Mike Brady, \"\n"
        "		COPYRIGHT_STRING \"2005 David Hammerton, \"\n"
        "		COPYRIGHT_STRING \"tiny-AES-c authors. All rights reserved.\"),\n"
        "		StringVector(\"MIT\", \"Unlicense\", NULL),\n"
        "		StringVector(),\n"
        "		\"https://github.com/mikebrady/shairport-sync\");\n"
        "#endif\n"
        "\n"
        "	// FFmpeg copyrights\n",
        "AboutSystem AirPlay credits",
    )


def main() -> int:
    copy_sources()
    extract_openssl()
    patch_jams()
    patch_launch()
    patch_about()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
