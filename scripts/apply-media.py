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
    "src/kits/media/MediaAddOn.cpp",
    "src/kits/media/MediaNode.cpp",
    "src/kits/media/DefaultMediaTheme.cpp",
    "src/kits/media/MediaFormats.cpp",
    "src/servers/media/NotificationManager.cpp",
    "src/servers/media/media_server.cpp",
    "src/servers/media/DefaultManager.cpp",
    "src/servers/media/DefaultManager.h",
    "src/servers/media_addon/MediaAddonServer.cpp",
    "src/servers/media_addon/MediaFilePlayer.cpp",
    "src/servers/media_addon/MediaFilePlayer.h",
    "src/add-ons/media/media-add-ons/mixer/AudioMixer.cpp",
    "src/add-ons/media/media-add-ons/mixer/Interpolate.cpp",
    "src/add-ons/media/media-add-ons/mixer/Interpolate.h",
    "src/add-ons/media/media-add-ons/mixer/MixerCore.cpp",
    "src/add-ons/media/media-add-ons/mixer/MixerCore.h",
    "src/add-ons/media/media-add-ons/mixer/VirtualSurround.cpp",
    "src/add-ons/media/media-add-ons/mixer/VirtualSurround.h",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioAddOn.cpp",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioAddOn.h",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioDevice.h",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioDevice.cpp",
    "src/add-ons/media/media-add-ons/multi_audio/MultiAudioNode.cpp",
    "src/add-ons/media/media-add-ons/multi_audio/Resampler.cpp",
    "src/add-ons/kernel/drivers/audio/usb/Stream.cpp",
    "src/add-ons/kernel/drivers/audio/usb/Stream.h",
    "src/add-ons/kernel/drivers/audio/usb/UsbAudioFormat.h",
    "src/add-ons/kernel/drivers/audio/usb/usb_audio_packets.h",
    "src/add-ons/kernel/drivers/audio/usb/AudioControlInterface.cpp",
    "src/add-ons/kernel/drivers/audio/usb/AudioControlInterface.h",
    "src/add-ons/kernel/drivers/audio/usb/AudioStreamingInterface.cpp",
    "src/add-ons/kernel/drivers/audio/usb/AudioStreamingInterface.h",
    "src/add-ons/kernel/drivers/audio/usb/Device.cpp",
    "src/add-ons/kernel/drivers/audio/usb/Device.h",
    "src/add-ons/kernel/drivers/audio/hda/hda_codec.cpp",
    "src/add-ons/kernel/drivers/audio/hda/hda_codec_defs.h",
    "src/add-ons/kernel/drivers/audio/hda/hda_controller.cpp",
    "src/add-ons/kernel/drivers/audio/hda/hda_multi_audio.cpp",
    "headers/os/drivers/usb/USB_audio.h",
    "src/preferences/media/MediaViews.cpp",
    "src/preferences/media/MediaViews.h",
    "src/preferences/media/MediaWindow.cpp",
    "src/preferences/media/MediaWindow.h",
    "src/preferences/media/Jamfile",
    "src/kits/interface/MenuField.cpp",
    "src/kits/device/USBEndpoint.cpp",
    "headers/os/device/USBKit.h",
    "headers/os/midi2/Midi2Defs.h",
    "headers/os/midi2/Midi2Property.h",
    "headers/os/midi2/Midi2Profile.h",
    "headers/os/midi2/Midi2Stream.h",
    "headers/os/midi2/Midi2Translate.h",
    "headers/os/midi2/Midi2Usb.h",
    "headers/os/midi2/MidiProducer.h",
    "headers/os/drivers/midi_driver.h",
    "src/kits/midi2/Jamfile",
    "src/kits/midi2/Midi2Library.cpp",
    "src/kits/midi2/MidiLocalProducer.cpp",
    "src/kits/midi2/MidiLocalConsumer.cpp",
    "src/servers/midi/PortDrivers.cpp",
    "src/add-ons/kernel/drivers/midi/usb_midi/usb_midi.h",
    "src/add-ons/kernel/drivers/midi/usb_midi/usb_midi.cpp",
    "src/add-ons/kernel/drivers/midi/usb_midi/usb_midi_parse.h",
    "src/add-ons/kernel/drivers/midi/usb_midi/usb_midi_velocity.h",
    "src/bin/midi2test/Jamfile",
    "src/bin/midi2test/midi2test.cpp",
    "src/bin/usb_audiotest/Jamfile",
    "src/bin/usb_audiotest/usb_audiotest.cpp",
    "src/bin/camtest/Jamfile",
    "src/bin/camtest/camtest.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/AddOn.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/AddOn.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamBufferedFilterInterface.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamBufferedFilterInterface.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamBufferingDeframer.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamBufferingDeframer.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamColorSpaceTransform.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamColorSpaceTransform.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamDebug.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamDeframer.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamDeframer.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamDevice.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamDevice.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamFilterInterface.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamFilterInterface.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamRoster.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamRoster.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamSensor.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamSensor.h",
    "src/add-ons/media/media-add-ons/usb_webcam/CamStreamingDeframer.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/CamStreamingDeframer.h",
    "src/add-ons/media/media-add-ons/usb_webcam/Jamfile",
    "src/add-ons/media/media-add-ons/usb_webcam/Producer.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/Producer.h",
    "src/add-ons/media/media-add-ons/usb_webcam/README.txt",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/NW80xCamDevice.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/NW80xCamDevice.h",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/quickcam/QuickCamDevice.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/quickcam/QuickCamDevice.h",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/sonix/SonixCamDevice.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/sonix/SonixCamDevice.h",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/uvc/UVCCamDevice.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/addons/uvc/UVCCamDevice.h",
    "src/add-ons/media/media-add-ons/usb_webcam/cstransforms/Bayer.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/sensors/hdcs1000.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/sensors/hv7131e1.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/sensors/pas106b.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/sensors/pb0100.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/sensors/tas5110c1b.cpp",
    "src/add-ons/media/media-add-ons/usb_webcam/sensors/tas5130d1b.cpp",
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
    hook_webcam(haiku)
    hook_mixer(haiku)


def hook_mixer(haiku: pathlib.Path) -> None:
    jam = haiku / "src" / "add-ons" / "media" / "media-add-ons" / "mixer" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    if "VirtualSurround.cpp" in text:
        print("already mixer Jamfile")
        return
    anchor = "MixerCore.cpp\n"
    if anchor not in text:
        raise SystemExit("mixer Jamfile has no MixerCore.cpp line")
    jam.write_text(
        text.replace(anchor, "MixerCore.cpp\n\t\t\tVirtualSurround.cpp\n", 1),
        encoding="utf-8",
    )
    print("patched mixer Jamfile")


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

    line = "SubInclude HAIKU_TOP src bin usb_audiotest ;\n"
    if line not in jam.read_text(encoding="utf-8"):
        text = jam.read_text(encoding="utf-8")
        anchor = "SubInclude HAIKU_TOP src bin midi2test ;\n"
        if anchor not in text:
            anchor = "SubInclude HAIKU_TOP src bin writembr ;\n"
        if anchor not in text:
            raise SystemExit("usb_audiotest anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched usb_audiotest Jamfile")
    else:
        print("already usb_audiotest Jamfile")

    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += usb_audiotest ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += midi2test ;\n"
        if anchor not in text:
            raise SystemExit("usb_audiotest anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched usb_audiotest image")
    else:
        print("already usb_audiotest image")


def hook_webcam(haiku: pathlib.Path) -> None:
    for relative in (
        "src/add-ons/media/media-add-ons/usb_webcam/addons/uvc/USB_video.h",
        "src/add-ons/media/media-add-ons/usb_webcam/addons/uvc/UVCDeframer.cpp",
        "src/add-ons/media/media-add-ons/usb_webcam/addons/uvc/UVCDeframer.h",
    ):
        leftover = haiku / relative
        if leftover.is_file():
            leftover.unlink()
            print(f"removed {relative}")

    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin camtest ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin usb_audiotest ;\n"
        if anchor not in text:
            raise SystemExit("camtest anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched camtest Jamfile")

    regular = haiku / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += camtest ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += usb_audiotest ;\n"
        if anchor not in text:
            raise SystemExit("camtest anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched camtest image")

    minimum = haiku / "build" / "jam" / "images" / "definitions" / "minimum"
    text = minimum.read_text(encoding="utf-8")
    if "\n\tcamtest\n" not in text:
        anchor = "\n\tbfsinfo\n"
        if anchor not in text:
            raise SystemExit("camtest anchor missing in minimum image")
        minimum.write_text(text.replace(anchor, anchor + "\tcamtest\n", 1),
            encoding="utf-8")
        print("patched minimum camtest")
    text = minimum.read_text(encoding="utf-8")
    addon = "SYSTEM_ADD_ONS_MEDIA = \n\tusb_webcam.media_addon\n;\n"
    empty = "SYSTEM_ADD_ONS_MEDIA = ;\n"
    if "usb_webcam.media_addon" not in text:
        if empty not in text:
            raise SystemExit("webcam anchor missing in minimum image")
        minimum.write_text(text.replace(empty, addon, 1), encoding="utf-8")
        print("patched minimum webcam")


if __name__ == "__main__":
    main()
