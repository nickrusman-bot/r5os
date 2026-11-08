#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the complete R5 Operating System Visual Brand Kit & Artwork Assets.

Outputs all 21 brand kit files into `distro/artwork/brand-kit/` and writes the
master vector `distro/artwork/r5-symbol.svg`. Pass `--install` to also overwrite
the active `r5-os-deskbar.png`, `r5-os-logo.png`, `r5-os-splash.png`,
`r5-os-splash.bgr`, and `splash_icons.png` in `distro/artwork/`.
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import shutil
import sys
import tempfile

import PIL
from PIL import Image, ImageDraw, ImageFilter, ImageFont

REQUIRED_PILLOW = "13.0.0"
if PIL.__version__ != REQUIRED_PILLOW:
    raise SystemExit(
        f"Pillow {REQUIRED_PILLOW} is required so the artwork hashes stay stable. "
        f"This interpreter has {PIL.__version__}."
    )

ROOT = pathlib.Path(__file__).resolve().parent.parent
ARTWORK_DIR = ROOT / "distro" / "artwork"
OUT_DIR = ARTWORK_DIR / "brand-kit"
FONT_DIR = ROOT / "distro" / "fonts" / "manrope"
WALLPAPER_SRC = ARTWORK_DIR / "r5-wallpaper.jpg"

BOLD_TTF = FONT_DIR / "Manrope-Bold.ttf"
REGULAR_TTF = FONT_DIR / "Manrope-Regular.ttf"


def save_png(image: Image.Image, path: pathlib.Path) -> None:
    """PNG with no time, no comment, and a fixed zlib level."""
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG", optimize=False, compress_level=9)


# --- 1. Cubic Bezier & SVG Path Helpers ---

def cubic_bezier(
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    p3: tuple[float, float],
    steps: int = 80,
) -> list[tuple[float, float]]:
    pts: list[tuple[float, float]] = []
    for i in range(steps):
        t = i / float(steps)
        mt = 1.0 - t
        x = (
            (mt ** 3) * p0[0]
            + 3.0 * (mt ** 2) * t * p1[0]
            + 3.0 * mt * (t ** 2) * p2[0]
            + (t ** 3) * p3[0]
        )
        y = (
            (mt ** 3) * p0[1]
            + 3.0 * (mt ** 2) * t * p1[1]
            + 3.0 * mt * (t ** 2) * p2[1]
            + (t ** 3) * p3[1]
        )
        pts.append((x, y))
    return pts


def sample_curve_ring(
    segments: list[tuple[tuple[float, float], ...]], scale: float
) -> list[tuple[float, float]]:
    poly: list[tuple[float, float]] = []
    for p0, p1, p2, p3 in segments:
        s0 = (p0[0] * scale, p0[1] * scale)
        s1 = (p1[0] * scale, p1[1] * scale)
        s2 = (p2[0] * scale, p2[1] * scale)
        s3 = (p3[0] * scale, p3[1] * scale)
        poly.extend(cubic_bezier(s0, s1, s2, s3, steps=96))
    return poly


def parse_evenodd(d: str) -> list[list[tuple[tuple[float, float], ...]]]:
    """Two or more closed subpaths of cubic segments, from the SVG."""
    tokens = re.findall(r"[MCZ]|-?\d+(?:\.\d+)?", d)
    rings: list[list[tuple[tuple[float, float], ...]]] = []
    current: list[tuple[tuple[float, float], ...]] = []
    cursor: tuple[float, float] | None = None
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token == "M":
            if current:
                rings.append(current)
                current = []
            cursor = (float(tokens[index + 1]), float(tokens[index + 2]))
            index += 3
        elif token == "C":
            if cursor is None:
                raise SystemExit("SVG cubic before a move")
            numbers = [float(tokens[index + offset]) for offset in range(1, 7)]
            end = (numbers[4], numbers[5])
            current.append((cursor, (numbers[0], numbers[1]), (numbers[2], numbers[3]), end))
            cursor = end
            index += 7
        elif token == "Z":
            index += 1
        else:
            raise SystemExit(f"unexpected SVG token {token}")
    if current:
        rings.append(current)
    if len(rings) < 2:
        raise SystemExit("the Wave Pebble path needs an outer ring and an inner wave")
    return rings


def load_pebble_paths(svg_path: pathlib.Path) -> dict[str, list[list[tuple[tuple[float, float], ...]]]]:
    text = svg_path.read_text(encoding="utf-8")
    paths: dict[str, list[list[tuple[tuple[float, float], ...]]]] = {}
    for name, body in re.findall(r'id="(r5-[a-z]+)" d="([^"]+)"', text):
        paths[name] = parse_evenodd(body)
    if "r5-full" not in paths or "r5-small" not in paths:
        raise SystemExit("r5-symbol.svg is missing r5-full or r5-small")
    return paths


def lerp_color(
    c0: tuple[int, int, int], c1: tuple[int, int, int], t: float
) -> tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    return (
        int(round(c0[0] + (c1[0] - c0[0]) * t)),
        int(round(c0[1] + (c1[1] - c0[1]) * t)),
        int(round(c0[2] + (c1[2] - c0[2]) * t)),
    )


def eval_stops(
    stops: list[tuple[float, tuple[int, int, int]]], t: float
) -> tuple[int, int, int]:
    if t <= stops[0][0]:
        return stops[0][1]
    if t >= stops[-1][0]:
        return stops[-1][1]
    for i in range(len(stops) - 1):
        t0, c0 = stops[i]
        t1, c1 = stops[i + 1]
        if t0 <= t <= t1:
            local = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
            return lerp_color(c0, c1, local)
    return stops[-1][1]


BRAND_STOPS = [
    (0.00, (0, 132, 255)),     # #0084FF - BeOS / R5 Blue
    (0.45, (56, 152, 255)),    # #3898FF - Sky Wave Blue
    (0.72, (101, 184, 145)),   # #65B891 - Seafoam Transition
    (1.00, (255, 184, 0)),     # #FFB800 - Haiku Tab Gold
]


def make_diagonal_gradient(
    size: int, stops: list[tuple[float, tuple[int, int, int]]]
) -> Image.Image:
    """Create a diagonal gradient from (12%, 12%) to (88%, 88%)."""
    grid = 256
    img = Image.new("RGB", (grid, grid))
    px = img.load()
    x0, y0 = 0.12 * grid, 0.12 * grid
    x1, y1 = 0.88 * grid, 0.88 * grid
    dx, dy = x1 - x0, y1 - y0
    denom = dx * dx + dy * dy
    for y in range(grid):
        for x in range(grid):
            t = ((x - x0) * dx + (y - y0) * dy) / denom
            px[x, y] = eval_stops(stops, t)
    if size != grid:
        img = img.resize((size, size), Image.Resampling.BICUBIC)
    return img


PEBBLE_PATHS: dict[str, list[list[tuple[tuple[float, float], ...]]]] = {}


def render_wave_pebble(
    target_size: int,
    mode: str = "color",
    with_shadow: bool = False,
    simplified: bool = False,
) -> Image.Image:
    """Render the Wave Pebble. simplified uses the thick wave from the same SVG."""
    ss = 4 if target_size <= 512 else 2
    canvas = target_size * ss
    scale = canvas / 100.0
    rings = PEBBLE_PATHS["r5-small" if simplified else "r5-full"]

    mask = Image.new("L", (canvas, canvas), 0)
    mdraw = ImageDraw.Draw(mask)
    mdraw.polygon(sample_curve_ring(rings[0], scale), fill=255)
    mdraw.polygon(sample_curve_ring(rings[1], scale), fill=0)

    if mode == "color":
        fill_rgb = make_diagonal_gradient(canvas, BRAND_STOPS).convert("RGBA")
        if not simplified:
            hl_layer = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
            hdraw = ImageDraw.Draw(hl_layer)
            hl_pts = cubic_bezier(
                (20.0 * scale, 31.0 * scale),
                (29.0 * scale, 16.0 * scale),
                (45.0 * scale, 11.0 * scale),
                (60.0 * scale, 14.0 * scale),
                steps=64,
            )
            stroke_w = max(2, int(round(4.5 * scale)))
            hdraw.line(hl_pts, fill=(255, 255, 255, 58), width=stroke_w, joint="curve")
            hl_layer = hl_layer.filter(ImageFilter.GaussianBlur(radius=4.5 * scale))
            fill_rgb = Image.alpha_composite(fill_rgb, hl_layer)
    elif mode == "white":
        fill_rgb = Image.new("RGBA", (canvas, canvas), (255, 255, 255, 255))
    elif mode == "dark":
        fill_rgb = Image.new("RGBA", (canvas, canvas), (15, 23, 42, 255))
    else:
        raise ValueError(f"unknown mode {mode}")

    shape = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    shape.paste(fill_rgb, (0, 0), mask)

    if with_shadow:
        shadow_alpha = mask.filter(ImageFilter.GaussianBlur(radius=2.2 * scale))
        shadow = Image.new("RGBA", (canvas, canvas), (0, 24, 64, 0))
        sa = shadow_alpha.point(lambda v: int(v * 0.24))
        shadow.putalpha(sa)
        bg = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        bg.paste(shadow, (0, int(round(2.0 * scale))), shadow)
        shape = Image.alpha_composite(bg, shape)

    return shape.resize((target_size, target_size), Image.Resampling.LANCZOS)


# --- 2. Horizontal Wordmark / Lockup Generator ---

def render_horizontal_lockup(
    height: int = 160,
    theme: str = "light",
    transparent: bool = True,
    subtitle: str | None = None,
) -> Image.Image:
    """Render [Wave Pebble] + Manrope Bold 'R5' + Manrope Regular 'Operating System'."""
    ss = 2
    H = height * ss
    icon_px = int(round(H * 0.72))

    font_r5 = ImageFont.truetype(str(BOLD_TTF), int(round(H * 0.36)))
    font_os = ImageFont.truetype(str(REGULAR_TTF), int(round(H * 0.30)))
    font_sub = ImageFont.truetype(str(REGULAR_TTF), int(round(H * 0.125)))

    dummy = Image.new("RGBA", (10, 10))
    ddraw = ImageDraw.Draw(dummy)

    r5_bbox = ddraw.textbbox((0, 0), "R5", font=font_r5, anchor="ls")
    os_bbox = ddraw.textbbox((0, 0), "Operating System", font=font_os, anchor="ls")
    r5_w = r5_bbox[2] - r5_bbox[0]
    os_w = os_bbox[2] - os_bbox[0]
    gap_icon_text = int(round(H * 0.16))
    gap_words = int(round(H * 0.09))
    pad_x = int(round(H * 0.16))

    total_text_w = r5_w + gap_words + os_w
    if subtitle:
        sub_bbox = ddraw.textbbox((0, 0), subtitle, font=font_sub, anchor="ls")
        total_text_w = max(total_text_w, sub_bbox[2] - sub_bbox[0])

    W = pad_x * 2 + icon_px + gap_icon_text + total_text_w

    if transparent:
        bg_col = (0, 0, 0, 0)
    elif theme == "light":
        bg_col = (255, 255, 255, 255)
    else:
        bg_col = (10, 14, 23, 255)

    img = Image.new("RGBA", (W, H), bg_col)
    draw = ImageDraw.Draw(img)

    pebble = render_wave_pebble(icon_px, mode="color", with_shadow=(theme == "light"))
    icon_y = (H - icon_px) // 2
    img.alpha_composite(pebble, (pad_x, icon_y))

    text_x = pad_x + icon_px + gap_icon_text
    if theme == "light":
        col_r5 = (15, 23, 42, 255)       # #0F172A Slate 900
        col_os = (51, 65, 85, 255)       # #334155 Slate 700
        col_sub = (100, 116, 139, 255)   # #64748B Slate 500
    else:
        col_r5 = (255, 255, 255, 255)    # #FFFFFF Pure White
        col_os = (203, 213, 225, 255)    # #CBD5E1 Slate 300
        col_sub = (148, 163, 184, 255)   # #94A3B8 Slate 400

    if subtitle:
        baseline_y = int(round(H * 0.54))
        sub_y = int(round(H * 0.77))
        draw.text((text_x, baseline_y), "R5", font=font_r5, fill=col_r5, anchor="ls")
        draw.text(
            (text_x + r5_w + gap_words, baseline_y),
            "Operating System",
            font=font_os,
            fill=col_os,
            anchor="ls",
        )
        draw.text((text_x, sub_y), subtitle, font=font_sub, fill=col_sub, anchor="ls")
    else:
        baseline_y = int(round(H * 0.61))
        draw.text((text_x, baseline_y), "R5", font=font_r5, fill=col_r5, anchor="ls")
        draw.text(
            (text_x + r5_w + gap_words, baseline_y),
            "Operating System",
            font=font_os,
            fill=col_os,
            anchor="ls",
        )

    return img.resize((W // ss, H // ss), Image.Resampling.LANCZOS)


# --- 3. Boot Splash Generator (exact 372x96 PNG + 24-bit BGR raw) ---

def render_boot_splash() -> tuple[Image.Image, bytes]:
    """372×96 lockup on white. Dark text, no border."""
    W, H = 372, 96
    ss = 4
    sw, sh = W * ss, H * ss

    img = Image.new("RGBA", (sw, sh), (255, 255, 255, 255))
    draw = ImageDraw.Draw(img)

    icon_px = 58 * ss
    pebble = render_wave_pebble(icon_px, mode="color", with_shadow=True)
    icon_x = 22 * ss
    icon_y = (sh - icon_px) // 2
    img.alpha_composite(pebble, (icon_x, icon_y))

    font_r5 = ImageFont.truetype(str(BOLD_TTF), 31 * ss)
    font_os = ImageFont.truetype(str(REGULAR_TTF), 25 * ss)

    r5_bbox = draw.textbbox((0, 0), "R5", font=font_r5, anchor="ls")
    r5_w = r5_bbox[2] - r5_bbox[0]

    text_x = icon_x + icon_px + 14 * ss
    baseline_y = int(round(sh * 0.60))

    draw.text((text_x, baseline_y), "R5", font=font_r5, fill=(15, 23, 42, 255), anchor="ls")
    draw.text(
        (text_x + r5_w + 8 * ss, baseline_y),
        "Operating System",
        font=font_os,
        fill=(51, 65, 85, 255),
        anchor="ls",
    )

    final_png = img.resize((W, H), Image.Resampling.LANCZOS).convert("RGB")
    r, g, b = final_png.split()
    bgr_img = Image.merge("RGB", (b, g, r))
    bgr_bytes = bgr_img.tobytes()
    assert len(bgr_bytes) == 372 * 96 * 3
    return final_png, bgr_bytes


HAIKU_SPLASH_ICONS = ROOT / "distro" / "artwork" / "haiku-splash-icons.png"
# Icon 4 of the 308×80 strip. The leaf sits in this box of each 40×40 tile.
SPLASH_DISK_INDEX = 3
SPLASH_LEAF_BOX = (5, 21, 18, 34)


def _near_black(pixel: tuple[int, int, int]) -> bool:
    return max(pixel) < 16


def lift_splash_icons_off_black(source: Image.Image) -> Image.Image:
    """The Haiku strip is painted on black. The rim is a few pixels of that
    color mixed with black. Recover the coverage and put the tiles on white."""
    rgb = source.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    output = Image.new("RGB", (width, height), (255, 255, 255))
    out = output.load()
    # The rounded plate fades across three pixels. Farther than that is the icon.
    rim = 3
    distance = [[rim + 1] * width for _ in range(height)]
    # Only the black around the tiles. Black inside an icon stays part of the icon.
    seen = [[False] * width for _ in range(height)]
    pending = []
    for y in range(height):
        for x in range(width):
            if x not in (0, width - 1) and y not in (0, height - 1):
                continue
            if not _near_black(pixels[x, y]) or seen[y][x]:
                continue
            seen[y][x] = True
            pending.append((x, y))
    head = 0
    while head < len(pending):
        x, y = pending[head]
        head += 1
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= width or ny >= height or seen[ny][nx]:
                    continue
                if not _near_black(pixels[nx, ny]):
                    continue
                seen[ny][nx] = True
                pending.append((nx, ny))
    for y in range(height):
        for x in range(width):
            if seen[y][x]:
                distance[y][x] = 0
    head = 0
    while head < len(pending):
        x, y = pending[head]
        head += 1
        step = distance[y][x] + 1
        if step > rim:
            continue
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= width or ny >= height:
                    continue
                if distance[ny][nx] <= step:
                    continue
                distance[ny][nx] = step
                pending.append((nx, ny))

    for y in range(height):
        for x in range(width):
            color = pixels[x, y]
            if distance[y][x] == 0:
                continue
            if distance[y][x] > rim:
                out[x, y] = color
                continue
            neighbors = []
            for dy in range(-5, 6):
                for dx in range(-5, 6):
                    nx, ny = x + dx, y + dy
                    if nx < 0 or ny < 0 or nx >= width or ny >= height:
                        continue
                    if distance[ny][nx] <= rim:
                        continue
                    neighbors.append(pixels[nx, ny])
            if neighbors:
                solid = tuple(
                    sum(channel[index] for channel in neighbors) // len(neighbors)
                    for index in range(3)
                )
            else:
                solid = color
            strongest = max(solid)
            if strongest < 1:
                continue
            channel = max(range(3), key=lambda index: solid[index])
            coverage = min(1.0, color[channel] / float(strongest))
            out[x, y] = tuple(
                int(round(solid[index] * coverage + 255 * (1.0 - coverage)))
                for index in range(3)
            )
    return output


def _splash_tile_origin(index: int, row: int) -> tuple[int, int]:
    return 2 + index * 44, row * 40


def render_splash_icons(source: Image.Image) -> Image.Image:
    """Keep Haiku's seven icons. White ground, and the R5 mark on the disk."""
    if source.size != (308, 80):
        raise SystemExit(f"splash icons must be 308x80, got {source.size}")
    image = lift_splash_icons_off_black(source).convert("RGBA")
    left, top, right, bottom = SPLASH_LEAF_BOX
    badge_w = right - left
    badge_h = bottom - top
    color_badge = render_wave_pebble(max(badge_w, badge_h), mode="color", with_shadow=False)
    color_badge = color_badge.resize((badge_w, badge_h), Image.Resampling.LANCZOS)
    for row in (0, 1):
        origin_x, origin_y = _splash_tile_origin(SPLASH_DISK_INDEX, row)
        box = (
            origin_x + left,
            origin_y + top,
            origin_x + right,
            origin_y + bottom,
        )
        sample = image.getpixel((origin_x + 24, origin_y + 14))
        cleared = Image.new("RGBA", (badge_w, badge_h), sample)
        image.paste(cleared, (box[0], box[1]))
        badge = color_badge
        if row == 1:
            # The lower row is the dimmed set. Match its disk.
            color_disk = image.getpixel((_splash_tile_origin(SPLASH_DISK_INDEX, 0)[0] + 24, 14))
            scale = max(sample[0], sample[1], sample[2]) / float(
                max(color_disk[0], color_disk[1], color_disk[2], 1)
            )
            gray = Image.new("RGBA", color_badge.size)
            src = color_badge.load()
            dst = gray.load()
            for y in range(color_badge.size[1]):
                for x in range(color_badge.size[0]):
                    red, green, blue, alpha = src[x, y]
                    level = (red + green + blue) / 3.0
                    value = int(round(level * scale))
                    dst[x, y] = (value, value, value, alpha)
            badge = gray
        image.alpha_composite(badge, (box[0], box[1]))
    return image.convert("RGB")


# --- 4. Isometric System Disk Icon with R5 Wave Pebble Overlay (air/OS style) ---

def render_system_disk_icon(target_size: int = 128) -> Image.Image:
    ss = 4
    S = target_size * ss
    u = S / 100.0

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    shadow_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(shadow_layer)
    sdraw.polygon(
        [(10 * u, 60 * u), (50 * u, 82 * u), (92 * u, 58 * u), (50 * u, 38 * u)],
        fill=(0, 15, 40, 85),
    )
    shadow_layer = shadow_layer.filter(ImageFilter.GaussianBlur(radius=3.0 * u))
    img = Image.alpha_composite(img, shadow_layer)
    draw = ImageDraw.Draw(img)

    left_wall = [(8 * u, 50 * u), (50 * u, 72 * u), (50 * u, 81 * u), (8 * u, 59 * u)]
    right_wall = [(50 * u, 72 * u), (92 * u, 50 * u), (92 * u, 59 * u), (50 * u, 81 * u)]
    draw.polygon(left_wall, fill=(122, 132, 146, 255))
    draw.polygon(right_wall, fill=(86, 95, 108, 255))

    top_plate = [(8 * u, 50 * u), (50 * u, 28 * u), (92 * u, 50 * u), (50 * u, 72 * u)]
    top_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(top_mask).polygon(top_plate, fill=255)
    top_grad = make_diagonal_gradient(
        S,
        [
            (0.0, (248, 250, 253)),
            (0.55, (220, 227, 237)),
            (1.0, (184, 194, 209)),
        ],
    ).convert("RGBA")
    img.paste(top_grad, (0, 0), top_mask)
    draw = ImageDraw.Draw(img)

    outline_col = (58, 66, 76, 255)
    lw = max(2, int(round(1.8 * u)))
    draw.line(
        [(8 * u, 50 * u), (50 * u, 28 * u), (92 * u, 50 * u), (50 * u, 72 * u), (8 * u, 50 * u)],
        fill=outline_col,
        width=lw,
        joint="curve",
    )
    draw.line(
        [(8 * u, 50 * u), (8 * u, 59 * u), (50 * u, 81 * u), (92 * u, 59 * u), (92 * u, 50 * u)],
        fill=outline_col,
        width=lw,
        joint="curve",
    )
    draw.line([(50 * u, 72 * u), (50 * u, 81 * u)], fill=outline_col, width=lw)

    led_poly = [(15 * u, 56.5 * u), (23 * u, 60.7 * u), (23 * u, 63.7 * u), (15 * u, 59.5 * u)]
    draw.polygon(led_poly, fill=(64, 224, 104, 255))

    badge_size = int(round(46 * u))
    badge_x = int(round(36 * u))
    badge_y = int(round(14 * u))

    disc_pad = int(round(3.0 * u))
    disc_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ddraw = ImageDraw.Draw(disc_layer)
    ddraw.ellipse(
        [
            badge_x + disc_pad,
            badge_y + disc_pad,
            badge_x + badge_size - disc_pad,
            badge_y + badge_size - disc_pad,
        ],
        fill=(255, 255, 255, 240),
        outline=(70, 82, 98, 120),
        width=max(1, int(round(1.0 * u))),
    )
    img = Image.alpha_composite(img, disc_layer)

    pebble = render_wave_pebble(badge_size, mode="color", with_shadow=True)
    img.alpha_composite(pebble, (badge_x, badge_y))

    return img.resize((target_size, target_size), Image.Resampling.LANCZOS)


# --- 5. About R5 Window Single-Column Mockup ---

def read_release() -> tuple[str, str]:
    """Version and build for example cards. Product artwork does not call this."""

    def first_line(path: pathlib.Path) -> str:
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                return line
        raise SystemExit(f"empty {path}")

    return first_line(ROOT / "distro" / "VERSION"), first_line(ROOT / "distro" / "BUILD")


def write_disk_badge(png: bytes, destination: pathlib.Path) -> None:
    rows = []
    for start in range(0, len(png), 12):
        chunk = ", ".join(f"0x{byte:02x}" for byte in png[start : start + 12])
        rows.append(f"\t{chunk},")
    text = (
        "// Generated by scripts/generate-r5-artwork.py. Do not edit.\n"
        "// SPDX-FileCopyrightText: 2026 R5OS Contributors\n"
        "// SPDX-License-Identifier: GPL-3.0-or-later\n"
        "\n"
        "#ifndef R5_DISK_BADGE_H\n"
        "#define R5_DISK_BADGE_H\n"
        "\n"
        "static const uint8 kR5DiskBadgePng[] = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "\n"
        "static const size_t kR5DiskBadgePngSize = sizeof(kR5DiskBadgePng);\n"
        "\n"
        "#endif\n"
    )
    destination.write_text(text, encoding="utf-8", newline="\n")


def example_version_line(version: str, build: str) -> str:
    return f"example  {version} Build {build}"


def render_about_r5_mockup(version: str, build: str) -> Image.Image:
    """Render a pixel-crisp preview of the single-column About R5 window on the R5 wallpaper."""
    W, H = 960, 680
    if WALLPAPER_SRC.is_file():
        bg = Image.open(WALLPAPER_SRC).convert("RGBA").resize((W, H), Image.Resampling.LANCZOS)
    else:
        bg = Image.new("RGBA", (W, H), (150, 190, 250, 255))

    draw = ImageDraw.Draw(bg)
    db_x, db_y, db_w, db_h = W - 185, 14, 171, 30
    draw.rounded_rectangle(
        [db_x, db_y, db_x + db_w, db_y + db_h],
        radius=3,
        fill=(224, 224, 224, 255),
        outline=(136, 136, 136, 255),
    )
    draw.rounded_rectangle(
        [db_x + 4, db_y + 3, db_x + 34, db_y + db_h - 3],
        radius=2,
        fill=(242, 242, 242, 255),
        outline=(168, 168, 168, 255),
    )
    desk_pebble = render_wave_pebble(20, mode="color", with_shadow=False)
    bg.alpha_composite(desk_pebble, (db_x + 9, db_y + 5))
    draw.line([(db_x + 40, db_y + 5), (db_x + 40, db_y + db_h - 5)], fill=(160, 160, 160, 255), width=1)
    font_small_b = ImageFont.truetype(str(BOLD_TTF), 12)
    draw.text((db_x + 122, db_y + 19), "13:40", font=font_small_b, fill=(30, 41, 59, 255), anchor="ls")

    disk_ico = render_system_disk_icon(64)
    bg.alpha_composite(disk_ico, (28, 22))
    draw.text((60, 98), "R5", font=font_small_b, fill=(255, 255, 255, 255), anchor="ms")

    win_w, win_h = 540, 520
    win_x = (W - win_w) // 2
    win_y = 88

    sh_layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sdraw = ImageDraw.Draw(sh_layer)
    sdraw.rectangle([win_x + 4, win_y + 28, win_x + win_w + 4, win_y + win_h + 6], fill=(0, 18, 45, 95))
    sh_layer = sh_layer.filter(ImageFilter.GaussianBlur(radius=14))
    bg = Image.alpha_composite(bg, sh_layer)
    draw = ImageDraw.Draw(bg)

    tab_h = 25
    draw.rectangle([win_x, win_y, win_x + 128, win_y + tab_h], fill=(248, 204, 38, 255), outline=(168, 130, 0, 255))
    draw.rectangle([win_x + 7, win_y + 7, win_x + 17, win_y + 17], fill=(255, 232, 138, 255), outline=(168, 130, 0, 255))
    font_tab = ImageFont.truetype(str(BOLD_TTF), 13)
    draw.text((win_x + 25, win_y + 17), "About R5", font=font_tab, fill=(20, 20, 20, 255), anchor="ls")

    body_top = win_y + tab_h
    draw.rectangle(
        [win_x, body_top, win_x + win_w, win_y + win_h],
        fill=(237, 237, 237, 255),
        outline=(128, 128, 128, 255),
    )

    pad = 14
    cx0 = win_x + pad
    cx1 = win_x + win_w - pad

    card_top = body_top + pad
    card_bot = card_top + 88
    draw.rectangle([cx0, card_top, cx1, card_bot], fill=(255, 255, 255, 255), outline=(196, 196, 196, 255))
    about_pebble = render_wave_pebble(60, mode="color", with_shadow=True)
    bg.alpha_composite(about_pebble, (cx0 + 14, card_top + 14))

    font_h_r5 = ImageFont.truetype(str(BOLD_TTF), 28)
    font_h_os = ImageFont.truetype(str(REGULAR_TTF), 23)
    font_ver = ImageFont.truetype(str(REGULAR_TTF), 12)

    tx = cx0 + 88
    ty = card_top + 46
    r5_box = draw.textbbox((0, 0), "R5", font=font_h_r5, anchor="ls")
    r5_width = r5_box[2] - r5_box[0]
    draw.text((tx, ty), "R5", font=font_h_r5, fill=(15, 23, 42, 255), anchor="ls")
    draw.text((tx + r5_width + 8, ty), "Operating System", font=font_h_os, fill=(51, 65, 85, 255), anchor="ls")
    draw.text(
        (tx, card_top + 68),
        f"Version {version} • AMD64",
        font=font_ver,
        fill=(15, 23, 42, 255),
        anchor="ls",
    )

    info_top = card_bot + 12
    info_bot = info_top + 132
    draw.rectangle([cx0, info_top, cx1, info_bot], fill=(228, 228, 228, 255), outline=(196, 196, 196, 255))

    font_lbl = ImageFont.truetype(str(BOLD_TTF), 12)
    font_val = ImageFont.truetype(str(REGULAR_TTF), 12)

    col1_x = cx0 + 14
    col2_x = cx0 + 270
    row1_y = info_top + 24
    row2_y = info_top + 78

    draw.text((col1_x, row1_y), "12 Processors:", font=font_lbl, fill=(25, 25, 25, 255), anchor="ls")
    draw.text((col1_x, row1_y + 18), "AMD Ryzen 5 5625U @ 2.30 GHz", font=font_val, fill=(65, 65, 65, 255), anchor="ls")

    draw.text((col2_x, row1_y), "15842 MiB Memory:", font=font_lbl, fill=(25, 25, 25, 255), anchor="ls")
    draw.text((col2_x, row1_y + 18), "612 MiB used (4%)", font=font_val, fill=(65, 65, 65, 255), anchor="ls")

    draw.text((col1_x, row2_y), "Kernel:", font=font_lbl, fill=(25, 25, 25, 255), anchor="ls")
    draw.text((col1_x, row2_y + 18), "Nov 8, 2026, 1:40:00 PM", font=font_val, fill=(65, 65, 65, 255), anchor="ls")

    draw.text((col2_x, row2_y), "Time running:", font=font_lbl, fill=(25, 25, 25, 255), anchor="ls")
    draw.text((col2_x, row2_y + 18), "24 minutes, 15 seconds", font=font_val, fill=(65, 65, 65, 255), anchor="ls")

    font_build = ImageFont.truetype(str(REGULAR_TTF), 10)
    draw.text(
        (col1_x, info_bot - 8),
        f"Build {build}",
        font=font_build,
        fill=(160, 160, 160, 255),
        anchor="ls",
    )

    tabs_top = info_bot + 14
    tab_bar_h = 24
    draw.rectangle([cx0, tabs_top, cx0 + 92, tabs_top + tab_bar_h], fill=(255, 255, 255, 255), outline=(170, 170, 170, 255))
    draw.text((cx0 + 46, tabs_top + 16), "What's new", font=font_lbl, fill=(20, 20, 20, 255), anchor="ms")
    draw.rectangle([cx0 + 95, tabs_top + 2, cx0 + 168, tabs_top + tab_bar_h], fill=(220, 220, 220, 255), outline=(170, 170, 170, 255))
    draw.text((cx0 + 131, tabs_top + 16), "Credits", font=font_val, fill=(80, 80, 80, 255), anchor="ms")
    draw.rectangle([cx0 + 171, tabs_top + 2, cx0 + 250, tabs_top + tab_bar_h], fill=(220, 220, 220, 255), outline=(170, 170, 170, 255))
    draw.text((cx0 + 210, tabs_top + 16), "Licenses", font=font_val, fill=(80, 80, 80, 255), anchor="ms")

    pane_top = tabs_top + tab_bar_h
    pane_bot = win_y + win_h - pad
    draw.rectangle([cx0, pane_top, cx1, pane_bot], fill=(255, 255, 255, 255), outline=(170, 170, 170, 255))
    draw.line([(cx0 + 1, pane_top), (cx0 + 91, pane_top)], fill=(255, 255, 255, 255), width=2)

    font_wn_h = ImageFont.truetype(str(BOLD_TTF), 13)
    draw.text(
        (cx0 + 12, pane_top + 24),
        example_version_line(version, build),
        font=font_wn_h,
        fill=(42, 131, 36, 255),
        anchor="ls",
    )
    draw.text(
        (cx0 + 12, pane_top + 44),
        "R5 has a new wave-pebble icon, Deskbar button, boot screen, and About R5 window.",
        font=font_val,
        fill=(40, 40, 40, 255),
        anchor="ls",
    )
    draw.text(
        (cx0 + 12, pane_top + 74),
        "example",
        font=font_wn_h,
        fill=(42, 131, 36, 255),
        anchor="ls",
    )
    draw.text(
        (cx0 + 12, pane_top + 94),
        "Demos includes VulkanTeapot. It draws a teapot with Vulkan.",
        font=font_val,
        fill=(40, 40, 40, 255),
        anchor="ls",
    )

    return bg.convert("RGB")


def write_svg_files(artwork_dir: pathlib.Path, kit_dir: pathlib.Path) -> None:
    svg_color = """<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: GPL-3.0-or-later
-->
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="512" height="512">
  <defs>
    <linearGradient id="r5-wave" x1="12%" y1="12%" x2="88%" y2="88%">
      <stop offset="0%" stop-color="#0084ff"/>
      <stop offset="45%" stop-color="#3898ff"/>
      <stop offset="72%" stop-color="#65b891"/>
      <stop offset="100%" stop-color="#ffb800"/>
    </linearGradient>
  </defs>
  <path id="r5-full" d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z M54,24 C42,24 37,32 43,42 C49,51 46,62 35,66 C26,69 27,77 41,77 C64,77 77,64 77,48 C77,33 66,24 54,24 Z" fill="url(#r5-wave)" fill-rule="evenodd"/>
  <path id="r5-small" d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z M60,30 C46,28 36,40 44,50 C50,58 42,68 32,72 C26,75 32,82 48,80 C66,78 78,64 76,48 C74,34 70,30 60,30 Z" fill="url(#r5-wave)" fill-rule="evenodd"/>
  <path d="M22,29 C30,17 44,12 58,14"
        fill="none" stroke="#ffffff" stroke-width="2.5" stroke-linecap="round" opacity="0.38"/>
</svg>
"""
    kit_dir.mkdir(parents=True, exist_ok=True)
    artwork_dir.mkdir(parents=True, exist_ok=True)
    (kit_dir / "r5-symbol.svg").write_text(svg_color, encoding="utf-8", newline="\n")
    (artwork_dir / "r5-symbol.svg").write_text(svg_color, encoding="utf-8", newline="\n")


PRODUCT_NAMES = (
    "r5-symbol.svg",
    "r5-symbol.png",
    "r5-symbol-1024.png",
    "r5-symbol-512.png",
    "r5-symbol-256.png",
    "r5-symbol-128.png",
    "r5-symbol-64.png",
    "r5-symbol-32.png",
    "r5-symbol-22.png",
    "r5-symbol-16.png",
    "r5-symbol-mono-white.png",
    "r5-symbol-mono-dark.png",
    "r5-os-deskbar.png",
    "r5-os-logo.png",
    "r5-os-splash.png",
    "r5-os-splash.bgr",
    "splash_icons.png",
)

INSTALLED_NAMES = (
    "r5-symbol.svg",
    "r5-symbol.png",
    "r5-os-deskbar.png",
    "r5-os-logo.png",
    "r5-os-splash.png",
    "r5-os-splash.bgr",
    "splash_icons.png",
)


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def produce(artwork_dir: pathlib.Path, kit_dir: pathlib.Path, badge_path: pathlib.Path) -> None:
    version, build = read_release()
    write_svg_files(artwork_dir, kit_dir)
    global PEBBLE_PATHS
    PEBBLE_PATHS = load_pebble_paths(artwork_dir / "r5-symbol.svg")

    for sz in (1024, 512, 256, 128, 64, 32, 22, 16):
        img = render_wave_pebble(
            sz, mode="color", with_shadow=(sz >= 64), simplified=(sz <= 22)
        )
        save_png(img, kit_dir / f"r5-symbol-{sz}.png")

    mark = render_wave_pebble(16, mode="color", with_shadow=False, simplified=True)
    deskbar = Image.new("RGBA", (28, 22), (0, 0, 0, 0))
    deskbar.alpha_composite(mark, ((28 - 16) // 2, (22 - 16) // 2))
    save_png(deskbar, kit_dir / "r5-os-deskbar.png")

    save_png(
        render_wave_pebble(512, mode="white", with_shadow=False),
        kit_dir / "r5-symbol-mono-white.png",
    )
    save_png(
        render_wave_pebble(512, mode="dark", with_shadow=False),
        kit_dir / "r5-symbol-mono-dark.png",
    )

    save_png(
        render_horizontal_lockup(height=140, theme="light", transparent=True),
        kit_dir / "r5-os-logo.png",
    )
    save_png(
        render_horizontal_lockup(
            height=160,
            theme="light",
            transparent=False,
            subtitle=example_version_line(version, build),
        ),
        kit_dir / "r5-lockup-light-card.png",
    )
    save_png(
        render_horizontal_lockup(
            height=160,
            theme="dark",
            transparent=False,
            subtitle=example_version_line(version, build),
        ),
        kit_dir / "r5-lockup-dark-card.png",
    )

    splash_png, splash_bgr = render_boot_splash()
    save_png(splash_png, kit_dir / "r5-os-splash.png")
    (kit_dir / "r5-os-splash.bgr").write_bytes(splash_bgr)

    if not HAIKU_SPLASH_ICONS.is_file():
        raise SystemExit(f"missing {HAIKU_SPLASH_ICONS}")
    icons = render_splash_icons(Image.open(HAIKU_SPLASH_ICONS))
    save_png(icons, kit_dir / "splash_icons.png")

    for sz in (128, 64, 32):
        save_png(render_system_disk_icon(sz), kit_dir / f"r5-system-disk-{sz}.png")

    save_png(
        render_about_r5_mockup(version, build),
        kit_dir / "r5-about-window-preview.png",
    )

    symbol = render_wave_pebble(256, mode="color", with_shadow=False)
    save_png(symbol, kit_dir / "r5-symbol.png")
    write_disk_badge((kit_dir / "r5-symbol.png").read_bytes(), badge_path)

    for name in INSTALLED_NAMES:
        if name == "r5-symbol.svg":
            continue
        shutil.copy2(kit_dir / name, artwork_dir / name)


def check_artwork() -> int:
    badge = ROOT / "distro" / "desktop" / "src" / "kits" / "tracker" / "r5_disk_badge.h"
    with tempfile.TemporaryDirectory(prefix="r5-artwork-") as temporary:
        root = pathlib.Path(temporary)
        artwork = root / "artwork"
        kit = root / "brand-kit"
        fresh_badge = root / "r5_disk_badge.h"
        produce(artwork, kit, fresh_badge)
        mismatches = []
        for name in INSTALLED_NAMES:
            live = ARTWORK_DIR / name
            fresh = artwork / name
            if sha256(live) != sha256(fresh):
                mismatches.append(str(live.relative_to(ROOT)))
        for name in PRODUCT_NAMES:
            live = OUT_DIR / name
            fresh = kit / name
            if sha256(live) != sha256(fresh):
                mismatches.append(str(live.relative_to(ROOT)))
        if sha256(badge) != sha256(fresh_badge):
            mismatches.append(str(badge.relative_to(ROOT)))
    if mismatches:
        print("artwork does not match the generator:", file=sys.stderr)
        for item in mismatches:
            print(f"  {item}", file=sys.stderr)
        return 1
    print("artwork matches the generator")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install",
        action="store_true",
        help="Write the product artwork into distro/artwork/ and the disk-badge header.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Regenerate the product artwork in a temporary directory and compare hashes.",
    )
    args = parser.parse_args()
    if args.check and args.install:
        raise SystemExit("--check does not write the tree")
    if args.check:
        return check_artwork()

    badge = ROOT / "distro" / "desktop" / "src" / "kits" / "tracker" / "r5_disk_badge.h"
    if args.install:
        produce(ARTWORK_DIR, OUT_DIR, badge)
        print("Installed active artwork assets into:", ARTWORK_DIR)
    else:
        kit = OUT_DIR
        with tempfile.TemporaryDirectory(prefix="r5-artwork-") as temporary:
            produce(pathlib.Path(temporary), kit, badge)
    print("Generated all R5 Brand Kit assets in:", OUT_DIR)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
