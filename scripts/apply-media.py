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
    "src/preferences/media/MediaWindow.cpp",
    "src/kits/interface/MenuField.cpp",
    "headers/os/midi2/Midi2Defs.h",
    "headers/os/midi2/Midi2Property.h",
    "headers/os/midi2/MidiProducer.h",
    "headers/os/drivers/midi_driver.h",
    "src/kits/midi2/MidiLocalProducer.cpp",
    "src/kits/midi2/MidiLocalConsumer.cpp",
    "src/servers/midi/PortDrivers.cpp",
    "src/add-ons/kernel/drivers/midi/usb_midi/usb_midi.h",
    "src/add-ons/kernel/drivers/midi/usb_midi/usb_midi.cpp",
    "src/bin/midi2test/Jamfile",
    "src/bin/midi2test/midi2test.cpp",
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

    hook_midi2test(haiku)


def hook_midi2test(haiku: pathlib.Path) -> None:
    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin midi2test ;\n"
    anchor = "SubInclude HAIKU_TOP src bin writembr ;\n"
    if line not in text:
        if anchor not in text:
            raise SystemExit("midi2test anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile")
    else:
        print("already src/bin/Jamfile")

    regular = haiku / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += midi2test ;\n"
    anchor = "SYSTEM_BIN += userguide quicktour welcome ;\n"
    if line not in text:
        if anchor not in text:
            raise SystemExit("midi2test anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image")
    else:
        print("already regular image")


if __name__ == "__main__":
    main()
