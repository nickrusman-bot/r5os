#!/usr/bin/env python3
"""Replace the 24-bit splash arrays in images-sans-tm.h from BGR raw."""
import pathlib
import re
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOGO = ROOT / "distro" / "artwork" / "r5-os-splash.bgr"
ICONS = ROOT / "distro" / "artwork" / "splash_icons.bgr"
LOGO_BYTES = 372 * 96 * 3
ICONS_BYTES = 308 * 80 * 3
HEADER = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None


def c_array(data: bytes) -> str:
    parts = []
    line = ["\t"]
    for i, b in enumerate(data):
        line.append(f"{b}, ")
        if (i + 1) % 12 == 0:
            parts.append("".join(line).rstrip())
            line = ["\t"]
    if line != ["\t"]:
        parts.append("".join(line).rstrip())
    return "\n".join(parts)


def replace_array(text: str, name: str, compressed: bytes) -> str:
    pattern = re.compile(
        rf"static uint8 {name}\[\] = \{{\n.*?\n\}};",
        re.S,
    )
    repl = f"static uint8 {name}[] = {{\n" + c_array(compressed) + "\n};"
    new, count = pattern.subn(repl, text, count=1)
    if count != 1:
        print(f"did not find {name}", file=sys.stderr)
        raise SystemExit(1)
    return new


def main() -> int:
    if HEADER is None or not HEADER.is_file():
        print("usage: generate_images_sans_tm.py <images-sans-tm.h>", file=sys.stderr)
        return 2
    logo = LOGO.read_bytes()
    icons = ICONS.read_bytes()
    if len(logo) != LOGO_BYTES:
        print(f"unexpected logo BGR size {len(logo)}", file=sys.stderr)
        return 1
    if len(icons) != ICONS_BYTES:
        print(f"unexpected icon BGR size {len(icons)}", file=sys.stderr)
        return 1
    text = HEADER.read_text(encoding="utf-8")
    if "static const uint16 kSplashIconsWidth = 308;" not in text:
        print("kSplashIconsWidth is not 308", file=sys.stderr)
        return 1
    if "static const uint16 kSplashIconsHeight = 80;" not in text:
        print("kSplashIconsHeight is not 80", file=sys.stderr)
        return 1
    eight = re.search(
        r"static uint8 kSplashIcons8BitCompressedImage\[\] = \{\n.*?\n\};",
        text,
        re.S,
    )
    if eight is None:
        print("did not find kSplashIcons8BitCompressedImage", file=sys.stderr)
        return 1
    text = replace_array(text, "kSplashLogo24BitCompressedImage", zlib.compress(logo, 9))
    text = replace_array(text, "kSplashIcons24BitCompressedImage", zlib.compress(icons, 9))
    if eight.group(0) not in text:
        print("the 8-bit icon image changed", file=sys.stderr)
        return 1
    if "static const uint16 kSplashIconsWidth = 308;" not in text:
        print("kSplashIconsWidth changed", file=sys.stderr)
        return 1
    if "static const uint16 kSplashIconsHeight = 80;" not in text:
        print("kSplashIconsHeight changed", file=sys.stderr)
        return 1
    HEADER.write_text(text, encoding="utf-8", newline="\n")
    print(f"Updated {HEADER} logo and icon 24-bit images")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
