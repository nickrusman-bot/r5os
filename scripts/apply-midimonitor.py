#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""MIDI Monitor. Idempotent copy into the Haiku tree, Jam SubInclude, and the
regular image app and Deskbar lists."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "haiku")
SRC = ROOT / "distro" / "apps" / "midimonitor"


def copy_sources() -> None:
    dest = HAIKU / "src" / "apps" / "midimonitor"
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    for path in sorted(SRC.iterdir()):
        if not path.is_file():
            continue
        data = path.read_bytes().replace(b"\r\n", b"\n")
        target = dest / path.name
        if target.is_file() and target.read_bytes() == data:
            continue
        target.write_bytes(data)
        count += 1
    print(f"copied {count} MIDI Monitor files")


def patch_jams() -> None:
    jam = HAIKU / "src" / "apps" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "HaikuSubInclude midimonitor ;\n"
    anchor = "HaikuSubInclude midiplayer ;\n"
    if line in text:
        print("already src/apps/Jamfile (midimonitor)")
    elif anchor not in text:
        raise SystemExit("midimonitor anchor missing in src/apps/Jamfile")
    else:
        jam.write_text(text.replace(anchor, line + anchor, 1), encoding="utf-8")
        print("patched src/apps/Jamfile (midimonitor)")

    # The same two lines are in SYSTEM_APPS and in DESKBAR_APPLICATIONS.
    regular = HAIKU / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    if "\tMidiMonitor\n" in text:
        if text.count("\tMidiMonitor\n") != 2:
            raise SystemExit("MidiMonitor is in the regular image a wrong "
                "number of times")
        print("already regular image (MidiMonitor)")
        return
    anchor = "\tMediaPlayer\n\tMidiPlayer\n"
    if text.count(anchor) != 2:
        raise SystemExit("MidiMonitor anchor not found twice in regular image")
    regular.write_text(
        text.replace(anchor, "\tMediaPlayer\n\tMidiMonitor\n\tMidiPlayer\n"),
        encoding="utf-8")
    print("patched regular image (MidiMonitor)")


def main() -> int:
    if not HAIKU.is_dir():
        raise SystemExit(f"missing {HAIKU}")
    copy_sources()
    patch_jams()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
