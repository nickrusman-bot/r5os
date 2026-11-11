#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail if an anyboot was built from a different tree than this branch.

The shared Haiku tree can still hold another branch's overlay. This compares
three things, and stops on the first mismatch:

- every radeon source at this commit, the worktree, and the Haiku tree
- the built r5-gpu-selftest and radeon_hd against the same files in the image
- check.sh and boot2.sh in the image against this commit

write-stick.py runs this before it writes. A mix exits 1.
"""

from __future__ import annotations

import ast
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HAIKU = "/home/nickr/r5os/haiku"
GENERATED = f"{HAIKU}/generated.x86_64"
BFS = f"{GENERATED}/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
PKG = f"{GENERATED}/objects/linux/x86_64/release/tools/package/package"
OFFSET = "6291456"

PACKAGES = (
    "mesa_lavapipe-25.3.6-3-x86_64.hpkg",
    "mesa_radv-25.3.6-4-x86_64.hpkg",
    "vulkan_loader-1.4.367-3-x86_64.hpkg",
    "vulkan_tools-1.4.367-2-x86_64.hpkg",
    "vulkanteapot-1.0.0-2-x86_64.hpkg",
)

HAIKU_PKG = "haiku-r1~beta6_r5_2026.1_dev-1-x86_64.hpkg"
BUILT = {
    "r5-gpu-selftest": (
        f"{GENERATED}/objects/haiku/x86_64/release/bin/r5-gpu-selftest/r5-gpu-selftest",
        "bin/r5-gpu-selftest",
    ),
    "radeon_hd": (
        f"{GENERATED}/objects/haiku/x86_64/release/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd",
        "add-ons/kernel/drivers/dev/graphics/radeon_hd",
    ),
}

SCRIPTS = {
    "check.sh": "distro/graphics/boot/check.sh",
    "boot2.sh": "distro/graphics/boot/boot2.sh",
}

STRINGS = (b"gpu_isolation_test", b"window vkcube")


def die(message: str) -> None:
    print(f"MIX: {message}", file=sys.stderr)
    raise SystemExit(1)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def head_commit() -> str:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        die(f"cannot read the branch head: {error}")
    return out.strip()


def blob(commit: str, relative: str) -> bytes:
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "show", f"{commit}:{relative}"])
    except subprocess.CalledProcessError:
        die(f"{relative} is not in {commit}")


def at_head(commit: str, relative: str) -> None:
    """The worktree file is the file at this commit. Git's filters apply."""
    result = subprocess.run(
        ["git", "-C", str(ROOT), "diff", "--quiet", commit, "--", relative],
        capture_output=True, text=True)
    if result.returncode != 0:
        die(f"{relative} in the worktree is not {commit}")


def source_list() -> tuple[str, ...]:
    path = ROOT / "scripts" / "apply-radeon.py"
    module = ast.parse(path.read_text(encoding="utf-8"))
    for node in module.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id == "FILES":
                value = ast.literal_eval(node.value)
                return tuple(value)
    die("apply-radeon.py has no FILES list")


def wsl(script: str) -> str:
    try:
        out = subprocess.check_output(
            ["wsl", "-e", "bash", "-lc", script], text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        die(error.stdout or str(error))
    return out


def tree_hashes(relatives: list[str]) -> dict[str, str]:
    lines = "\n".join(
        f"sha256sum {HAIKU}/{relative}" for relative in relatives)
    text = wsl(lines)
    found = {}
    for line in text.splitlines():
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        digest, name = parts
        relative = name.replace(f"{HAIKU}/", "", 1)
        found[relative] = digest
    return found


def check_sources(commit: str) -> None:
    copied = list(source_list())
    loose = list(SCRIPTS.values()) + [
        "distro/graphics/src/bin/r5-wsi-window-test/r5-wsi-window-test.sh"]
    tree = tree_hashes(copied)
    matched = 0
    for haiku_rel in copied:
        relative = f"distro/graphics/{haiku_rel}"
        at_head(commit, relative)
        work = ROOT / relative
        if not work.is_file():
            die(f"{relative} is missing in the worktree at {commit}")
        digest = sha256(work.read_bytes())
        if tree.get(haiku_rel) != digest:
            die(
                f"{haiku_rel} in the Haiku tree is not the worktree copy at {commit}: "
                f"tree {tree.get(haiku_rel)} worktree {digest}")
        matched += 1
    print(f"sources {matched} match {commit}")
    for relative in loose:
        at_head(commit, relative)


def extract(iso_wsl: str, out: str) -> None:
    gets = [f"get myfs/system/packages/{HAIKU_PKG} {out}/{HAIKU_PKG}"]
    for package in PACKAGES:
        gets.append(f"get myfs/system/packages/{package} {out}/{package}")
    script = ROOT / "generated" / "_pre-stick-extract.sh"
    script.write_text(
        "#!/bin/bash\nset -eu\n"
        f"rm -rf {out}\nmkdir -p {out}/haiku {out}/radv {out}/teapot\n"
        f"'{BFS}' --start-offset {OFFSET} '{iso_wsl}' <<EOF\n"
        + "\n".join(gets) + "\nquit\nEOF\n"
        f"'{PKG}' extract -C {out}/haiku {out}/{HAIKU_PKG}\n"
        f"'{PKG}' extract -C {out}/radv {out}/mesa_radv-25.3.6-4-x86_64.hpkg\n"
        f"'{PKG}' extract -C {out}/teapot {out}/vulkanteapot-1.0.0-2-x86_64.hpkg\n"
        f"test -s {out}/haiku/bin/r5-gpu-selftest\n"
        f"test -s {out}/haiku/add-ons/kernel/drivers/dev/graphics/radeon_hd\n"
        f"test -s {out}/radv/bin/check.sh\ntest -s {out}/radv/bin/boot2.sh\n"
        f"test -s {out}/teapot/bin/r5-wsi-window-test.sh\n",
        encoding="utf-8", newline="\n")
    wsl_script = wsl(f"wslpath -u '{script}'").strip()
    wsl(f"sed -i 's/\\r$//' '{wsl_script}' && bash '{wsl_script}'")


def file_sha_wsl(path: str) -> str:
    line = wsl(f"sha256sum '{path}'").strip().split()
    if not line:
        die(f"no hash for {path}")
    return line[0]


def check_image(commit: str, iso: str) -> None:
    iso_wsl = wsl(f"wslpath -u '{iso}'").strip()
    out = "/tmp/r5-pre-stick"
    extract(iso_wsl, out)
    selftest = Path(wsl(f"wslpath -w {out}/haiku/bin/r5-gpu-selftest").strip())
    data = selftest.read_bytes()
    for needle in STRINGS:
        if needle not in data:
            die(f"r5-gpu-selftest has no {needle.decode()}; the image is the old selftest")
        print(f"string {needle.decode()}")
    for name, (built, image) in BUILT.items():
        image_sha = file_sha_wsl(f"{out}/haiku/{image}")
        built_sha = file_sha_wsl(built)
        if image_sha != built_sha:
            die(
                f"{name} in the image is not the build from {commit}: "
                f"image {image_sha} build {built_sha}")
        print(f"binary {name} {image_sha}")
    for name, relative in SCRIPTS.items():
        image_sha = file_sha_wsl(f"{out}/radv/bin/{name}")
        committed = sha256(blob(commit, relative))
        if image_sha != committed:
            die(
                f"{name} in the image is not {commit}: "
                f"image {image_sha} branch {committed}")
        print(f"script {name} {image_sha}")
    window = sha256(blob(
        commit, "distro/graphics/src/bin/r5-wsi-window-test/r5-wsi-window-test.sh"))
    packed = file_sha_wsl(f"{out}/teapot/bin/r5-wsi-window-test.sh")
    if packed != window:
        die(
            f"r5-wsi-window-test.sh in the image is not {commit}: "
            f"image {packed} branch {window}")
    print(f"script r5-wsi-window-test.sh {packed}")
    for package in PACKAGES:
        image_sha = file_sha_wsl(f"{out}/{package}")
        disk = sha256((ROOT / "distro" / "packages" / "x86_64" / package).read_bytes())
        if image_sha != disk:
            die(f"{package} in the image is {image_sha}, the branch copy is {disk}")
        print(f"package {package} {image_sha}")


def main() -> int:
    if len(sys.argv) != 2:
        die("usage: check-pre-stick.py IMAGE")
    iso = str(Path(sys.argv[1]).resolve())
    if not Path(iso).is_file():
        die(f"no image at {iso}")
    commit = head_commit()
    print(f"branch HEAD {commit}")
    check_sources(commit)
    check_image(commit, iso)
    print(f"pre-stick ok {commit}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
