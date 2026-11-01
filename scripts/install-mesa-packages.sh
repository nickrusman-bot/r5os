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

if printf '%s\n' "$out" | grep -E -q 'mesa-22|mesa_devel-22|mesa_swpipe-22'; then
	echo "mesa 22 is still on the image" >&2
	exit 1
fi
for name in \
	libglvnd-1.7.0-1-x86_64.hpkg \
	mesa-25.3.6-1-x86_64.hpkg \
	mesa_devel-25.3.6-1-x86_64.hpkg \
	mesa_lavapipe-25.3.6-1-x86_64.hpkg \
	llvm21_runtime-21.1.8-1-x86_64.hpkg \
	vulkan_loader-1.4.367-1-x86_64.hpkg \
	vulkan_tools-1.4.367-1-x86_64.hpkg
do
	if ! printf '%s\n' "$out" | grep -q "$name"; then
		echo "missing $name" >&2
		exit 1
	fi
done
echo mesa_packages_ok
