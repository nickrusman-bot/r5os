#!/usr/bin/env bash
# After jam, replace Mesa 22 on r5-os.image with libglvnd + Mesa 25.
# Jam still needs the Mesa 22 devel package to compile OpenGL apps.
set -eu
HAIKU=/home/nickr/r5os/haiku
WIN=/mnt/c/Users/nickr/Projects/R5OS
PKGDIR="$WIN/distro/packages/x86_64"
IMG="$HAIKU/generated.x86_64/r5-os.image"
BFS="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"

if [[ ! -f "$IMG" ]]; then
	echo "no image: $IMG" >&2
	exit 1
fi

"$BFS" "$IMG" <<EOF
rm myfs/system/packages/mesa-22.0.5-3-x86_64.hpkg
rm myfs/system/packages/mesa_devel-22.0.5-3-x86_64.hpkg
rm myfs/system/packages/mesa_swpipe-22.0.5-3-x86_64.hpkg
rm myfs/system/packages/libglvnd-1.7.0-1-x86_64.hpkg
rm myfs/system/packages/mesa-25.3.6-1-x86_64.hpkg
rm myfs/system/packages/mesa_lavapipe-25.3.6-1-x86_64.hpkg
rm myfs/system/packages/llvm21_runtime-21.1.8-1-x86_64.hpkg
rm myfs/system/packages/vulkan_loader-1.4.367-1-x86_64.hpkg
cp :$PKGDIR/libglvnd-1.7.0-1-x86_64.hpkg myfs/system/packages/libglvnd-1.7.0-1-x86_64.hpkg
cp :$PKGDIR/mesa-25.3.6-1-x86_64.hpkg myfs/system/packages/mesa-25.3.6-1-x86_64.hpkg
cp :$PKGDIR/mesa_lavapipe-25.3.6-1-x86_64.hpkg myfs/system/packages/mesa_lavapipe-25.3.6-1-x86_64.hpkg
cp :$PKGDIR/llvm21_runtime-21.1.8-1-x86_64.hpkg myfs/system/packages/llvm21_runtime-21.1.8-1-x86_64.hpkg
cp :$PKGDIR/vulkan_loader-1.4.367-1-x86_64.hpkg myfs/system/packages/vulkan_loader-1.4.367-1-x86_64.hpkg
sync
ls myfs/system/packages/libglvnd-1.7.0-1-x86_64.hpkg
ls myfs/system/packages/mesa-25.3.6-1-x86_64.hpkg
ls myfs/system/packages/mesa_lavapipe-25.3.6-1-x86_64.hpkg
ls myfs/system/packages/llvm21_runtime-21.1.8-1-x86_64.hpkg
ls myfs/system/packages/vulkan_loader-1.4.367-1-x86_64.hpkg
quit
EOF
echo mesa_packages_installed
