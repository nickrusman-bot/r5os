#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""linear_size against RADV's 16-byte buffer requirement.

RADV rounds the requirement up to 16 (radv_get_buffer_memory_requirements).
The R5 swapchain uses the prime-path alignment: row stride 256, size 4096.
"""


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def row(width, height):
    stride = align(width * 4, 256)
    linear = align(stride * height, 4096)
    radv = align(linear, 16)
    mark = "ok" if linear >= radv else "FAIL"
    print(f"{width}x{height} stride {stride} linear {linear} radv {radv} {mark}")
    return linear >= radv


def main():
    sizes = [
        (1, 1),
        (63, 17),
        (64, 64),
        (333, 211),
        # r5-wsi-window-test.sh frames, inclusive BRect.
        (241, 181),
        (461, 371),
        (321, 281),
        # VulkanTeapot --window and vkcube opening frames.
        (384, 384),
        (500, 500),
    ]
    ok = True
    for width, height in sizes:
        ok = row(width, height) and ok
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
