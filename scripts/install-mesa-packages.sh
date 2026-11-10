#!/usr/bin/env bash
# The image solver installs libglvnd and Mesa 25. This checks Mesa 22 stayed off.
set -eu
HAIKU=/home/nickr/r5os/haiku
IMG="$HAIKU/generated.x86_64/r5-os.image"
BFS="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"

if [[ ! -f "$IMG" ]]; then
	echo "no image: $IMG" >&2
	exit 1
fi

out="$("$BFS" "$IMG" <<'EOF'
ls myfs/system/packages
quit
EOF
)"
printf '%s\n' "$out" | grep -E 'mesa|libglvnd|llvm21_runtime|vulkan_loader' || true

if printf '%s\n' "$out" | grep -E -q 'mesa-22|mesa_devel-22|mesa_swpipe-22|mesa-25.3.6-1|mesa_zink-25'; then
	echo "the image still has the old mesa package" >&2
	exit 1
fi
for name in \
	libglvnd-1.7.0-1-x86_64.hpkg \
	libdrm-2.4.124-1-x86_64.hpkg \
	mesa-25.3.6-2-x86_64.hpkg \
	mesa_devel-25.3.6-1-x86_64.hpkg \
	mesa_lavapipe-25.3.6-2-x86_64.hpkg \
	mesa_radv-25.3.6-1-x86_64.hpkg \
	llvm21_runtime-21.1.8-1-x86_64.hpkg \
	vulkan_loader-1.4.367-2-x86_64.hpkg \
	vulkan_tools-1.4.367-1-x86_64.hpkg \
	vulkanteapot-1.0.0-1-x86_64.hpkg
do
	if ! printf '%s\n' "$out" | grep -q "$name"; then
		echo "missing $name" >&2
		exit 1
	fi
done
echo mesa_packages_ok
PKGDIR="$(cd "$(dirname "$0")/.." && pwd)/distro/packages/x86_64"
if [[ ! -d "$PKGDIR" ]]; then
	PKGDIR=/mnt/c/Users/nickr/Projects/R5OS/distro/packages/x86_64
fi
PKG="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package"
LEAK="$(mktemp -d)"
for name in \
	libdrm-2.4.124-1-x86_64.hpkg \
	mesa-25.3.6-2-x86_64.hpkg \
	mesa_radv-25.3.6-1-x86_64.hpkg \
	mesa_lavapipe-25.3.6-2-x86_64.hpkg \
	vulkanteapot-1.0.0-1-x86_64.hpkg
do
	rm -rf "$LEAK/$name"
	mkdir -p "$LEAK/$name"
	"$PKG" extract -C "$LEAK/$name" "$PKGDIR/$name"
done
if grep -R -a -l '/home/nickr' "$LEAK"; then
	echo "a shipped package still contains /home/nickr" >&2
	exit 1
fi
rm -rf "$LEAK"
echo ok package paths
CHECK="$(cd "$(dirname "$0")" && pwd)/check-egl-vendor.py"
if [[ ! -f "$CHECK" ]]; then
	CHECK="/mnt/c/Users/nickr/Projects/R5OS/scripts/check-egl-vendor.py"
fi
python3 "$CHECK"
