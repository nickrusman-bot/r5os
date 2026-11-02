#!/usr/bin/env python3
"""Copy R5OS media-stack fixes into the Haiku tree.

The files under distro/media/ keep their Haiku MIT headers. They replace the
same path in the Haiku tree. A missing source file fails the apply.
"""
from __future__ import annotations

import pathlib
import shutil
import sys

FILES = (
    "src/kits/media/TimeSource.cpp",
    "src/servers/media/DefaultManager.cpp",
    "src/servers/media_addon/MediaAddonServer.cpp",
    "src/servers/media_addon/MediaFilePlayer.cpp",
    "src/servers/media_addon/MediaFilePlayer.h",
    "src/add-ons/media/media-add-ons/mixer/AudioMixer.cpp",
    "src/add-ons/media/media-add-ons/mixer/Interpolate.cpp",
    "src/add-ons/media/media-add-ons/mixer/Interpolate.h",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioAddOn.cpp",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioAddOn.h",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioDevice.h",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioNode.cpp",
    "src/add-ons/media/media-add-ons/multi_audio/Resampler.cpp",
    "src/add-ons/kernel/drivers/audio/usb/Stream.cpp",
    "src/add-ons/kernel/drivers/audio/usb/Stream.h",
    "src/preferences/media/MediaViews.cpp",
    "src/preferences/media/MediaViews.h",
)


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    haiku = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "haiku")
    source_root = root / "distro" / "media"
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
