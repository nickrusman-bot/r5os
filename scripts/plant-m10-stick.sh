#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
#
# M10 stick: plant the boot 1 settings (distro/graphics/boot/radeon_hd,
# "gpu_selftest true" and nothing else) into r5-os-anyboot.iso, so the
# first boot runs the self-test with every GPU switch at its default.
# check, boot-next and check-user come from the mesa_radv package.
# Run in WSL after the anyboot jam, before write-stick.py.
set -eu
HAIKU=/home/nickr/r5os/haiku
IMG="$HAIKU/generated.x86_64/r5-os-anyboot.iso"
BFS="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE=/tmp/m10-plant
rm -rf "$STAGE"
mkdir -p "$STAGE"
GIT="/mnt/c/Program Files/Git/bin/git.exe"
WINROOT=$(wslpath -w "$ROOT")
"$GIT" -C "$WINROOT" show "HEAD:distro/graphics/boot/radeon_hd" > "$STAGE/radeon_hd"
"$BFS" --start-offset 6291456 "$IMG" <<EOT
mkdir myfs/home/config
mkdir myfs/home/config/settings
mkdir myfs/home/config/settings/kernel
mkdir myfs/home/config/settings/kernel/drivers
put $STAGE/radeon_hd myfs/home/config/settings/kernel/drivers/radeon_hd
quit
EOT
echo planted
