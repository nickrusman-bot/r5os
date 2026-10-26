#!/usr/bin/env python3
"""Crop, make white transparent, write PNGs for AboutSystem."""
from __future__ import annotations

import sys
from pathlib import Path

try:
	from PIL import Image
except ImportError:
	sys.stderr.write("Pillow is required (python3-pil)\n")
	sys.exit(1)


def distill(src: Path, dest: Path, max_width: int) -> None:
	im = Image.open(src).convert("RGBA")
	pixels = im.load()
	w, h = im.size
	# Near-white -> alpha
	for y in range(h):
		for x in range(w):
			r, g, b, a = pixels[x, y]
			if r > 245 and g > 245 and b > 245:
				pixels[x, y] = (r, g, b, 0)
	bbox = im.getbbox()
	if bbox:
		im = im.crop(bbox)
	if im.size[0] > max_width:
		ratio = max_width / float(im.size[0])
		im = im.resize((max_width, int(im.size[1] * ratio)), Image.Resampling.LANCZOS)
	dest.parent.mkdir(parents=True, exist_ok=True)
	im.save(dest, "PNG")
	print(f"Wrote {dest} {im.size[0]}x{im.size[1]}")


def main() -> int:
	root = Path(__file__).resolve().parents[1]
	src = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "distro" / "artwork" / "r5-os-logo.jpg"
	out = root / "distro" / "artwork" / "r5-os-logo.png"
	distill(src, out, 420)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
