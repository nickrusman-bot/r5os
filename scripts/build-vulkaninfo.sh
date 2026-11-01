#!/bin/bash
# Cross-build Khronos vulkaninfo 1.4.367 and pack it as vulkan_tools.
# Apache-2.0. The program loads libvulkan at run time. It does not talk to a GPU.
# This script compiles and packs. It does not run vulkaninfo.
set -eu
HAIKU=/home/nickr/r5os/haiku
SRCROOT=/home/nickr/r5os/mesa-build
CXX="$HAIKU/generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++"
PKG="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package"
TARBALL="$SRCROOT/Vulkan-Tools-1.4.367.tar.gz"
URL="https://github.com/KhronosGroup/Vulkan-Tools/archive/refs/tags/v1.4.367.tar.gz"
SRC="$SRCROOT/Vulkan-Tools-1.4.367/vulkaninfo"
SYS="$SRCROOT/sysroot"
STAGE="$SRCROOT/vulkaninfo-out"
ROOT="$SRCROOT/vulkaninfo-pkg"
OUT="$(cd "$(dirname "$0")/.." && pwd)/distro/packages/x86_64/vulkan_tools-1.4.367-1-x86_64.hpkg"

if [[ ! -f "$TARBALL" ]]; then
	curl -L --fail -o "$TARBALL" "$URL"
fi
if [[ ! -f "$SRC/vulkaninfo.cpp" ]]; then
	tar -xzf "$TARBALL" -C "$SRCROOT"
fi

mkdir -p "$STAGE"
"$CXX" -std=c++17 -O2 \
	-DVK_ENABLE_BETA_EXTENSIONS -DVK_NO_PROTOTYPES -DVK_USE_PLATFORM_DISPLAY_KHR \
	-I"$SRC" -I"$SRC/generated" -I"$SYS/develop/headers" \
	-o "$STAGE/vulkaninfo" "$SRC/vulkaninfo.cpp"

rm -rf "$ROOT"
mkdir -p "$ROOT/bin"
cp -a "$STAGE/vulkaninfo" "$ROOT/bin/vulkaninfo"
chmod 755 "$ROOT/bin/vulkaninfo"

cat > "$ROOT/.PackageInfo" <<'EOF'
name			vulkan_tools
version			1.4.367-1
architecture		x86_64
summary			"vulkaninfo for the Vulkan loader"
description		"Prints the Vulkan devices the loader can open. On this image that is Lavapipe, the Mesa software driver, until a GPU ICD exists."
packager		"R5OS <r5os@localhost>"
vendor			"R5OS"
copyrights {
	"2014-2026 The Khronos Group Inc."
	"2014-2023 Valve Corporation"
	"2014-2023 LunarG, Inc."
	"2023-2024 RasterGrid Kft."
}
licenses {
	"Apache v2"
}
provides {
	vulkan_tools = 1.4.367
	cmd:vulkaninfo = 1.4.367
}
requires {
	haiku >= r1~beta5_hrev57937_5-1
	vulkan_loader == 1.4.367
}
EOF

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
"$PKG" create -C "$ROOT" "$OUT"
echo "created $OUT"
