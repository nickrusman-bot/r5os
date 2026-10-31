#!/usr/bin/env python3
"""Point the Haiku cross sysroot at the devel package for the additions build.

Requires R5OS_HAIKU_TREE. Idempotent. Does not patch VirtualBox sources;
that is distro/vbox-guest/virtualbox-7.2.22-r5os.patch.
"""
import os
import stat
import sys

tree = os.environ.get("R5OS_HAIKU_TREE")
if not tree:
    sys.exit("Set R5OS_HAIKU_TREE to the Haiku checkout.")

cross_bin = os.path.join(tree, "generated.x86_64", "cross-tools-x86_64", "bin")
prefix = os.path.join(cross_bin, "x86_64-unknown-haiku-")
sysroot = os.path.join(
    tree, "generated.x86_64", "cross-tools-x86_64", "sysroot", "boot", "system"
)
devel = os.path.join(
    tree,
    "generated.x86_64",
    "objects",
    "haiku",
    "x86_64",
    "packaging",
    "packages_build",
    "regular",
    "hpkg_-haiku_devel.hpkg",
    "contents",
    "develop",
)
runtime_lib = os.path.join(
    tree,
    "generated.x86_64",
    "objects",
    "haiku",
    "x86_64",
    "packaging",
    "packages_build",
    "regular",
    "hpkg_-haiku.hpkg",
    "contents",
    "lib",
)
if not os.path.isdir(devel) or not os.path.isdir(runtime_lib):
    sys.exit("Jam the Haiku tree first. Missing the devel or runtime package.")

vbox = os.environ.get("R5OS_VBOX_TREE", "")
wrap = os.path.join(vbox, "r5os-cross-bin") if vbox else os.path.join(tree, "r5os-cross-bin")
os.makedirs(wrap, exist_ok=True)
os.makedirs(sysroot, exist_ok=True)

develop_root = os.path.join(sysroot, "develop")
if os.path.islink(develop_root) or os.path.isfile(develop_root):
    os.remove(develop_root)
os.makedirs(develop_root, exist_ok=True)
headers = os.path.join(develop_root, "headers")
if not os.path.exists(headers):
    os.symlink(os.path.join(devel, "headers"), headers)
lib_dir = os.path.join(develop_root, "lib")
os.makedirs(lib_dir, exist_ok=True)
src_lib = os.path.join(devel, "lib")
for name in os.listdir(src_lib):
    src = os.path.join(src_lib, name)
    dest = os.path.join(lib_dir, name)
    if os.path.lexists(dest):
        continue
    if os.path.islink(src):
        target_name = os.path.basename(os.readlink(src))
        absolute = os.path.join(runtime_lib, target_name)
        if os.path.exists(absolute):
            os.symlink(absolute, dest)
        else:
            os.symlink(os.path.abspath(src), dest)
    else:
        os.symlink(src, dest)

system_lib = os.path.join(sysroot, "lib")
if not os.path.exists(system_lib):
    os.symlink(runtime_lib, system_lib)

build_packages = os.path.join(tree, "generated.x86_64", "build_packages")
zlib_real = None
zlib_headers = None
if os.path.isdir(build_packages):
    for name in os.listdir(build_packages):
        if name.startswith("zlib-") and name.endswith("-x86_64"):
            candidate = os.path.join(build_packages, name, "lib")
            header_dir = os.path.join(build_packages, name, "develop", "headers")
            if os.path.isdir(candidate) and os.path.isdir(header_dir):
                for libname in os.listdir(candidate):
                    if libname.startswith("libz.so."):
                        zlib_real = os.path.join(candidate, libname)
                zlib_headers = header_dir
if zlib_real and zlib_headers:
    for name in os.listdir(zlib_headers):
        dest = os.path.join(headers, name)
        if not os.path.lexists(dest):
            os.symlink(os.path.join(zlib_headers, name), dest)
    for dest_dir in (lib_dir, system_lib):
        so = os.path.join(dest_dir, os.path.basename(zlib_real))
        if not os.path.lexists(so):
            os.symlink(zlib_real, so)
        for name in ("libz.so.1", "libz.so"):
            dest = os.path.join(dest_dir, name)
            if not os.path.lexists(dest):
                os.symlink(so, dest)

gcc_rt = None
gcc_dev = None
if os.path.isdir(build_packages):
    for name in os.listdir(build_packages):
        if name.startswith("gcc_syslibs-") and "devel" not in name:
            gcc_rt = os.path.join(build_packages, name, "lib")
        if name.startswith("gcc_syslibs_devel-"):
            gcc_dev = os.path.join(build_packages, name, "develop", "lib")
if gcc_rt and gcc_dev and os.path.isdir(gcc_rt) and os.path.isdir(gcc_dev):
    links = {}
    for libname in os.listdir(gcc_rt):
        if libname.startswith("libstdc++.so."):
            real = os.path.join(gcc_rt, libname)
            links["libstdc++.so"] = real
            links["libstdc++.so.6"] = real
            links[libname] = real
        if libname.startswith("libgcc_s.so"):
            links["libgcc_s.so"] = os.path.join(gcc_rt, libname)
            links["libgcc_s.so.1"] = os.path.join(gcc_rt, libname)
    sup = os.path.join(gcc_dev, "libsupc++.a")
    if os.path.exists(sup):
        links["libsupc++.a"] = sup
    for dest_dir in (lib_dir, system_lib):
        for name, src in links.items():
            dest = os.path.join(dest_dir, name)
            if os.path.lexists(dest) or not os.path.exists(src):
                continue
            os.symlink(src, dest)

uname = os.path.join(wrap, "uname")
with open(uname, "w", encoding="utf-8", newline="\n") as handle:
    handle.write(
        "#!/bin/sh\n"
        'if [ "$1" = "-s" ]; then echo Haiku; exit 0; fi\n'
        'if [ "$1" = "-m" ]; then echo x86_64; exit 0; fi\n'
        'exec /usr/bin/uname "$@"\n'
    )
os.chmod(uname, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP)

for name in (
    "gcc", "g++", "c++", "ar", "as", "ld", "nm", "objcopy", "objdump",
    "ranlib", "strip", "readelf", "size", "strings",
):
    dest = os.path.join(wrap, name)
    src = prefix + name
    if not os.path.exists(src):
        continue
    if os.path.lexists(dest):
        os.remove(dest)
    os.symlink(src, dest)

print("sysroot", sysroot)
print("wrap", wrap)
