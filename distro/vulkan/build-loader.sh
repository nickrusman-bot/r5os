#!/bin/bash
# Cross-build Vulkan-Loader 1.4.367 for R5 and pack vulkan_loader.
# The HaikuPorts package is left alone. This recipe pins the same tag and
# adds VK_RFIVE_view_surface the way that tag adds Metal.
# Apache-2.0, like the loader. This script compiles and packs. It does not boot.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HAIKU=/home/nickr/r5os/haiku
SRCROOT=/home/nickr/r5os/mesa-build
TARBALL="$SRCROOT/Vulkan-Loader-1.4.367.tar.gz"
URL="https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/v1.4.367.tar.gz"
SHA=f70da1e3c63b55bae070dd44dfb9ca77523dba3781b6cb8aad3a1ceef4e902b7
PRISTINE="$SRCROOT/Vulkan-Loader-1.4.367"
HEADERS="$SRCROOT/Vulkan-Headers-1.4.367"
WORK="$SRCROOT/vulkan-loader-r5"
HDR_PREFIX="$SRCROOT/vulkan-headers-r5"
CXX_BIN="$HAIKU/generated.x86_64/cross-tools-x86_64/bin"
CMAKE_HOST="$SRCROOT/cmake-3.31.8-linux-x86_64/bin/cmake"
if [[ ! -x "$CMAKE_HOST" ]]; then
	curl -L --fail -o "$SRCROOT/cmake-3.31.8-linux-x86_64.tar.gz" \
		https://github.com/Kitware/CMake/releases/download/v3.31.8/cmake-3.31.8-linux-x86_64.tar.gz
	tar -xzf "$SRCROOT/cmake-3.31.8-linux-x86_64.tar.gz" -C "$SRCROOT"
fi
export PATH="$SRCROOT/cmake-3.31.8-linux-x86_64/bin:$SRCROOT:$PATH"
PKG="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package"
STAGE="$SRCROOT/vulkan-loader-pkg"
OUT="$ROOT/distro/packages/x86_64/vulkan_loader-1.4.367-4-x86_64.hpkg"

if [[ ! -f "$TARBALL" ]]; then
	curl -L --fail -o "$TARBALL" "$URL"
fi
echo "$SHA  $TARBALL" | sha256sum -c -
if [[ ! -f "$PRISTINE/loader/wsi.c" ]]; then
	tar -xzf "$TARBALL" -C "$SRCROOT"
fi
if [[ ! -f "$HEADERS/include/vulkan/vulkan_core.h" ]]; then
	echo "missing Vulkan-Headers 1.4.367 in $HEADERS" >&2
	exit 1
fi

rm -rf "$WORK"
cp -a "$PRISTINE" "$WORK"
python3 - "$WORK" "$ROOT/distro/vulkan/patches/haiku-portability.patchset" <<'PY'
import pathlib, subprocess, sys
root = pathlib.Path(sys.argv[1])
text = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
parts = text.split("\nFrom ")
for i, part in enumerate(parts):
    if i:
        part = "From " + part
    idx = part.find("\ndiff --git ")
    if idx < 0:
        sys.exit("portability patch has no diff")
    diff = part[idx + 1 :]
    subprocess.run(["patch", "-p1", "--forward", "--batch"], input=diff, text=True, cwd=root, check=True)
print("haiku portability applied")
PY
patch -p1 --forward --batch -d "$WORK" < "$ROOT/distro/vulkan/patches/view-surface.patch"
# M9: the loader's device sort (a GPU before a CPU) is on for Linux only.
# Zink takes physical device 0, so Haiku turns it on too.
patch -p1 --forward --batch -d "$WORK" < "$ROOT/distro/vulkan/patches/haiku-device-sort.patch"

rm -rf "$HDR_PREFIX"
cmake -S "$HEADERS" -B "$HDR_PREFIX/build" -DCMAKE_INSTALL_PREFIX="$HDR_PREFIX/install" -DCMAKE_BUILD_TYPE=Release
cmake --install "$HDR_PREFIX/build"
cp -a "$ROOT/distro/vulkan/include/vulkan/vk_icd.h" "$HDR_PREFIX/install/include/vulkan/vk_icd.h"
cp -a "$ROOT/distro/vulkan/include/vulkan/vulkan_r5.h" "$HDR_PREFIX/install/include/vulkan/vulkan_r5.h"

cat > "$WORK/haiku-toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $CXX_BIN/x86_64-unknown-haiku-gcc)
set(CMAKE_CXX_COMPILER $CXX_BIN/x86_64-unknown-haiku-g++)
set(CMAKE_SYSROOT $HAIKU/generated.x86_64/cross-tools-x86_64/sysroot)
set(CMAKE_FIND_ROOT_PATH \${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

cmake -S "$WORK" -B "$WORK/build" \
	-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-toolchain.cmake" \
	-DVulkanHeaders_DIR="$HDR_PREFIX/install/share/cmake/VulkanHeaders" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_INSTALL_PREFIX="$WORK/install" \
	-DBUILD_TESTS=OFF \
	-DLOADER_CODEGEN=OFF \
	-DBUILD_WSI_XCB_SUPPORT=OFF \
	-DBUILD_WSI_XLIB_SUPPORT=OFF \
	-DBUILD_WSI_XLIB_XRANDR_SUPPORT=OFF \
	-DBUILD_WSI_WAYLAND_SUPPORT=ON \
	-DBUILD_WSI_DIRECTFB_SUPPORT=OFF \
	-DPKG_CONFIG_EXECUTABLE="$SRCROOT/bin/pkg-config" \
	-G Ninja
ninja -C "$WORK/build"
cmake --install "$WORK/build"

rm -rf "$STAGE"
mkdir -p "$STAGE/lib" "$STAGE/develop/headers/vulkan"
cp -a "$WORK/install/lib/libvulkan.so.1.4.367" "$STAGE/lib/libvulkan.so.1.4.367"
ln -s libvulkan.so.1.4.367 "$STAGE/lib/libvulkan.so.1"
cp -a "$ROOT/distro/vulkan/include/vulkan/vulkan_r5.h" "$STAGE/develop/headers/vulkan/vulkan_r5.h"
cp -a "$ROOT/distro/vulkan/include/vulkan/vk_icd.h" "$STAGE/develop/headers/vulkan/vk_icd.h"

cat > "$STAGE/.PackageInfo" <<'EOF'
name			vulkan_loader
version			1.4.367-4
architecture		x86_64
summary			"Vulkan loader with the view surface"
description		"The Khronos Vulkan loader 1.4.367, built by R5, with VK_RFIVE_view_surface and the device sort (GPU before CPU). The structure type is the Khronos value for extension 732."
packager		"R5OS <r5os@localhost>"
vendor			"R5OS"
copyrights {
	"2014-2026 The Khronos Group Inc."
	"2014-2025 Valve Corporation"
	"2014-2025 LunarG, Inc."
	"2026 R5OS Contributors"
}
licenses {
	"Apache v2"
	"MIT"
}
provides {
	vulkan_loader = 1.4.367
	lib:libvulkan = 1.4.367 compat >= 1
}
requires {
	haiku >= r1~beta5_hrev57937_5-1
}
EOF

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
"$PKG" create -C "$STAGE" "$OUT"
echo "created $OUT"
