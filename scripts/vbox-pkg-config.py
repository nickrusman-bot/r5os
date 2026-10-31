#!/usr/bin/env python3
"""Minimal pkg-config so the VirtualBox host build can see zlib."""
import sys

args = sys.argv[1:]
if "--version" in args or "--atleast-pkgconfig-version" in args:
    print("0.29.2")
    sys.exit(0)
mods = [a for a in args if not a.startswith("-")]
flags = [a for a in args if a.startswith("-")]
known = {
    "zlib": {"cflags": "", "libs": "-lz"},
}


def fail():
    sys.exit(1)


for mod in mods:
    if mod not in known:
        fail()
if "--exists" in flags:
    sys.exit(0)
out = []
if "--cflags" in flags:
    out.extend(known[m]["cflags"] for m in mods)
if "--libs" in flags:
    out.extend(known[m]["libs"] for m in mods)
print(" ".join(part for part in out if part))
