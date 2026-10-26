#!/usr/bin/env python3
"""Replace kSplashLogo24BitCompressedImage in images-sans-tm.h from BGR raw."""
import pathlib
import re
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
BGR = ROOT / "distro" / "artwork" / "r5-os-splash.bgr"
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


def main() -> int:
    if HEADER is None or not HEADER.is_file():
        print("usage: generate_images_sans_tm.py <images-sans-tm.h>", file=sys.stderr)
        return 2
    raw = BGR.read_bytes()
    if len(raw) != 372 * 96 * 3:
        print(f"unexpected BGR size {len(raw)}", file=sys.stderr)
        return 1
    compressed = zlib.compress(raw, 9)
    body = c_array(compressed)
    text = HEADER.read_text(encoding="utf-8")
    pattern = re.compile(
        r"static uint8 kSplashLogo24BitCompressedImage\[\] = \{\n.*?\n\};",
        re.S,
    )
    repl = (
        "static uint8 kSplashLogo24BitCompressedImage[] = {\n"
        + body
        + "\n};"
    )
    new, n = pattern.subn(repl, text, count=1)
    if n != 1:
        print("did not find kSplashLogo24BitCompressedImage", file=sys.stderr)
        return 1
    HEADER.write_text(new, encoding="utf-8")
    print(f"Updated {HEADER} logo 24-bit ({len(compressed)} compressed bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
