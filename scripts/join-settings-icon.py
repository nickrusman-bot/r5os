#!/usr/bin/env python3
"""Join the R5 wave pebble and the preferences wrench into one HVIF.

The pebble is the vector_icon in distro/rest/src/apps/aboutsystem/AboutSystem.rdef.
The wrench is the last two shapes of R_PrefsDirIcon in Haiku's
src/kits/tracker/TrackerIcons.rdef. That is the same picture Settings already
draws from distro/settings/overlay-prefs.hvif. The script reads both HVIF
blobs, writes one HVIF, and refuses when a section count would pass 255.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ABOUT = ROOT / "distro" / "rest" / "src" / "apps" / "aboutsystem" / "AboutSystem.rdef"
TRACKER = pathlib.Path(
    sys.argv[1]
    if len(sys.argv) > 1
    else "/home/nickr/r5os/haiku/src/kits/tracker/TrackerIcons.rdef"
)
RDEF = ROOT / "distro" / "settings" / "Settings.rdef"
INC = ROOT / "distro" / "settings" / "r5_pebble.inc"

MAGIC = bytes((0x6E, 0x63, 0x69, 0x66))
STYLE_SOLID = 1
STYLE_GRADIENT = 2
STYLE_SOLID_NO_ALPHA = 3
STYLE_GRAY = 4
STYLE_GRAY_NO_ALPHA = 5
SHAPE_PATH = 10
PATH_COMMANDS = 1 << 2
PATH_NO_CURVES = 1 << 3
GRAD_TRANSFORM = 1 << 1
GRAD_NO_ALPHA = 1 << 2
GRAD_GRAYS = 1 << 4
SHAPE_TRANSFORM = 1 << 1
SHAPE_LOD = 1 << 3
SHAPE_TRANSFORMERS = 1 << 4
SHAPE_TRANSLATION = 1 << 5
AFFINE = 20
CONTOUR = 21
PERSPECTIVE = 22
STROKE = 23


class Cursor:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.at = 0

    def need(self, count: int) -> None:
        if self.at + count > len(self.data):
            raise SystemExit("HVIF ended early")

    def u8(self) -> int:
        self.need(1)
        value = self.data[self.at]
        self.at += 1
        return value

    def u16(self) -> int:
        self.need(2)
        value = self.data[self.at] | (self.data[self.at + 1] << 8)
        self.at += 2
        return value

    def skip(self, count: int) -> None:
        self.need(count)
        self.at += count

    def coord(self) -> None:
        value = self.u8()
        if value & 128:
            self.u8()

    def float24(self) -> None:
        self.skip(3)

    def color(self, alpha: bool, gray: bool) -> None:
        if alpha and gray:
            self.skip(2)
        elif alpha:
            self.skip(4)
        elif gray:
            self.skip(1)
        else:
            self.skip(3)


def take_section(cur: Cursor, kind: str) -> tuple[int, bytes]:
    start = cur.at
    count = cur.u8()
    if kind == "style":
        for _ in range(count):
            skip_style(cur)
    elif kind == "path":
        for _ in range(count):
            skip_path(cur)
    else:
        for _ in range(count):
            skip_shape(cur)
    return count, cur.data[start + 1 : cur.at]


def skip_style(cur: Cursor) -> None:
    kind = cur.u8()
    if kind == STYLE_SOLID:
        cur.color(True, False)
    elif kind == STYLE_SOLID_NO_ALPHA:
        cur.color(False, False)
    elif kind == STYLE_GRAY:
        cur.color(True, True)
    elif kind == STYLE_GRAY_NO_ALPHA:
        cur.color(False, True)
    elif kind == STYLE_GRADIENT:
        cur.u8()
        flags = cur.u8()
        stops = cur.u8()
        if flags & GRAD_TRANSFORM:
            for _ in range(6):
                cur.float24()
        alpha = not (flags & GRAD_NO_ALPHA)
        gray = bool(flags & GRAD_GRAYS)
        for _ in range(stops):
            cur.u8()
            cur.color(alpha, gray)
    else:
        cur.skip(cur.u16())


def skip_path(cur: Cursor) -> None:
    flags = cur.u8()
    points = cur.u8()
    if flags & PATH_NO_CURVES:
        for _ in range(points * 2):
            cur.coord()
    elif flags & PATH_COMMANDS:
        size = (points + 3) // 4
        commands = cur.data[cur.at : cur.at + size]
        cur.skip(size)
        bit = 0
        byte = 0
        for index in range(points):
            if bit == 0:
                byte = commands[index // 4]
            command = (byte >> bit) & 0x03
            bit = (bit + 2) % 8
            coords = {0: 1, 1: 1, 2: 2, 3: 6}[command]
            for _ in range(coords):
                cur.coord()
    else:
        for _ in range(points * 6):
            cur.coord()


def skip_shape(cur: Cursor) -> None:
    kind = cur.u8()
    if kind != SHAPE_PATH:
        cur.skip(cur.u16())
        return
    cur.u8()
    paths = cur.u8()
    cur.skip(paths)
    flags = cur.u8()
    if flags & SHAPE_TRANSFORM:
        for _ in range(6):
            cur.float24()
    elif flags & SHAPE_TRANSLATION:
        cur.coord()
        cur.coord()
    if flags & SHAPE_LOD:
        cur.skip(2)
    if flags & SHAPE_TRANSFORMERS:
        count = cur.u8()
        for _ in range(count):
            skip_transformer(cur)


def skip_transformer(cur: Cursor) -> None:
    kind = cur.u8()
    if kind == AFFINE:
        cur.skip(24)
    elif kind == CONTOUR:
        cur.skip(3)
    elif kind == PERSPECTIVE:
        for _ in range(9):
            cur.float24()
    elif kind == STROKE:
        cur.skip(3)
    else:
        cur.skip(cur.u16())


def parse(data: bytes) -> tuple[int, bytes, int, bytes, list[bytes]]:
    if not data.startswith(MAGIC):
        raise SystemExit("not an HVIF blob")
    cur = Cursor(data)
    cur.skip(4)
    styles, style_bytes = take_section(cur, "style")
    paths, path_bytes = take_section(cur, "path")
    shape_start = cur.at
    shape_count = cur.u8()
    shapes: list[bytes] = []
    for _ in range(shape_count):
        begin = cur.at
        skip_shape(cur)
        shapes.append(cur.data[begin:cur.at])
    if cur.at != len(data):
        raise SystemExit(f"HVIF has {len(data) - cur.at} unused bytes")
    (shape_start)
    return styles, style_bytes, paths, path_bytes, shapes


def shift_shape(shape: bytes, style_offset: int, path_offset: int) -> bytes:
    if shape[0] != SHAPE_PATH:
        raise SystemExit("the wrench shape is not a path shape")
    out = bytearray(shape)
    out[1] = shape[1] + style_offset
    if out[1] < shape[1]:
        raise SystemExit("style index overflow")
    path_count = shape[2]
    for index in range(path_count):
        was = shape[3 + index]
        out[3 + index] = was + path_offset
        if out[3 + index] < was:
            raise SystemExit("path index overflow")
    return bytes(out)


def quoted(data: bytes) -> str:
    text = data.hex().upper()
    lines = []
    while text:
        chunk = text[:64]
        text = text[64:]
        lines.append('\t$"' + chunk + '"')
    return "\n".join(lines)


def rdef_bytes(path: pathlib.Path, marker: str) -> bytes:
    text = path.read_text(encoding="utf-8")
    at = text.find(marker)
    if at < 0:
        raise SystemExit(f"{marker} not found in {path}")
    block = text[at:]
    end = block.find("};")
    if end < 0:
        end = block.find(";")
    block = block[:end]
    return bytes.fromhex("".join(re.findall(r'\$"([0-9A-Fa-f]+)"', block)))


def main() -> None:
    if not TRACKER.is_file():
        raise SystemExit(f"TrackerIcons.rdef not found: {TRACKER}")
    pebble = rdef_bytes(ABOUT, "resource vector_icon")
    prefs = rdef_bytes(TRACKER, "resource(R_PrefsDirIcon)")
    p_styles, p_style_bytes, p_paths, p_path_bytes, p_shapes = parse(pebble)
    w_styles, w_style_bytes, w_paths, w_path_bytes, w_shapes = parse(prefs)
    if len(w_shapes) < 2:
        raise SystemExit("the preferences icon has fewer than two shapes")
    wrench = w_shapes[-2:]
    if p_styles + w_styles > 255 or p_paths + w_paths > 255:
        raise SystemExit("a combined count would pass 255")
    if len(p_shapes) + len(wrench) > 255:
        raise SystemExit("a combined shape count would pass 255")
    shifted = [shift_shape(shape, p_styles, p_paths) for shape in wrench]
    combined = bytearray(MAGIC)
    combined.append(p_styles + w_styles)
    combined += p_style_bytes
    combined += w_style_bytes
    combined.append(p_paths + w_paths)
    combined += p_path_bytes
    combined += w_path_bytes
    combined.append(len(p_shapes) + len(shifted))
    for shape in p_shapes + shifted:
        combined += shape
    parse(bytes(combined))

    rdef = RDEF.read_text(encoding="utf-8")
    block = (
        "\n// The application icon is the R5 wave pebble from\n"
        "// distro/rest/src/apps/aboutsystem/AboutSystem.rdef with the last\n"
        "// two shapes of R_PrefsDirIcon (Haiku src/kits/tracker/TrackerIcons.rdef,\n"
        "// the same wrench as overlay-prefs.hvif). Built by\n"
        "// scripts/join-settings-icon.py. Do not edit the bytes by hand.\n"
        'resource(0, "BEOS:ICON") #\'VICN\' array {\n'
        + quoted(bytes(combined))
        + "\n};\n"
    )
    rdef = re.sub(
        r"\n// The application icon is the R5 wave pebble.*?^\};\n",
        "\n",
        rdef,
        count=1,
        flags=re.S | re.M,
    )
    if 'resource(0, "BEOS:ICON")' in rdef:
        rdef = re.sub(
            r'resource\(0, "BEOS:ICON"\).*?^\};\n',
            "",
            rdef,
            count=1,
            flags=re.S | re.M,
        )
    RDEF.write_text(rdef.rstrip() + "\n" + block, encoding="utf-8")

    parts = [f"0x{byte:02x}" for byte in pebble]
    lines = []
    row: list[str] = []
    for part in parts:
        row.append(part)
        if len(", ".join(row)) > 76:
            lines.append("\t" + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("\t" + ", ".join(row))
    if lines:
        lines[-1] = lines[-1].rstrip(",")
    text = (
        "/*\n"
        " * R5 wave pebble, copied from AboutSystem.rdef by\n"
        " * scripts/join-settings-icon.py. Do not edit by hand.\n"
        " */\n"
        "static const uint8 kR5Pebble[] = {\n"
        + "\n".join(lines)
        + "\n};\n"
    )
    INC.write_text(text, encoding="utf-8")
    print(f"pebble {len(pebble)} wrench shapes 2 combined {len(combined)}")


if __name__ == "__main__":
    main()
