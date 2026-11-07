#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail on a Renoir PCI id outside the device tables, and on a bare
numeric assignment in gpu_get_info that has no Linux v6.12 comment."""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TREE = os.path.join(ROOT, "distro", "graphics")
ALLOW = {
    os.path.join("headers", "private", "r5gpu", "r5_amd_device.h"),
    os.path.join("src", "add-ons", "kernel", "drivers", "graphics",
                 "radeon_hd", "driver.cpp"),
}
PCI = re.compile(r"0x(?:15e7|1636|1638|164c)\b", re.IGNORECASE)
ASSIGN = re.compile(r"=\s*(?:0x[0-9a-fA-F]{2,}|[0-9]{2,})\b")
OLD = re.compile(r"\bkCuPerSh\b|num_shader_engines\s*=\s*1\b")


def pci_hits(text, path):
    hits = []
    for number, line in enumerate(text.splitlines(), 1):
        if PCI.search(line):
            hits.append("%s:%d: %s" % (path, number, line.strip()))
    return hits


def info_hits(text, path):
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if "gpu_get_info(" in line and not line.strip().startswith("//"):
            start = i
            break
    if start is None:
        return []
    depth = 0
    seen = False
    hits = []
    for i in range(start, len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        if "{" in lines[i]:
            seen = True
        if seen and depth <= 0 and i > start:
            end = i
            break
    else:
        end = len(lines) - 1
    window = lines[start:end + 1]
    for offset, line in enumerate(window):
        if not ASSIGN.search(line):
            continue
        if "Linux v6.12" in line:
            continue
        earlier = "\n".join(window[max(0, offset - 6):offset + 1])
        if "Linux v6.12" in earlier:
            continue
        hits.append("%s:%d: %s" % (path, start + offset + 1, line.strip()))
    return hits


def old_hits(text, path):
    if path.replace("\\", "/").endswith("r5_amd_device.h"):
        return []
    hits = []
    for number, line in enumerate(text.splitlines(), 1):
        if OLD.search(line):
            hits.append("%s:%d: %s" % (path, number, line.strip()))
    return hits


def scan_tree():
    hits = []
    for dirpath, _dirs, names in os.walk(TREE):
        for name in names:
            if not name.endswith((".h", ".hpp", ".c", ".cpp")):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, TREE)
            with open(full, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            if rel not in ALLOW and rel.replace("\\", "/") not in {
                    a.replace("\\", "/") for a in ALLOW}:
                hits.extend(pci_hits(text, rel))
            if name == "r5_gpu.cpp":
                hits.extend(info_hits(text, rel))
            hits.extend(old_hits(text, rel))
    return hits


def main():
    probe = "int x;\nif (pci == 0x15e7)\n\tx = 1;\n"
    found = pci_hits(probe, "probe")
    if not found:
        print("probe missed the deliberate id")
        return 1
    print("probe rejected")
    hits = scan_tree()
    if hits:
        print("\n".join(hits))
        return 1
    print("ok gpu hardcode")
    return 0


if __name__ == "__main__":
    sys.exit(main())
