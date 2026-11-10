#!/bin/bash
# Cross-build vkcube from Vulkan-Tools 1.4.367 and put it in vulkan_tools.
# Also refreshes r5-wsi-window-test.sh inside the vulkanteapot package.
# Apache-2.0 for cube.c. The window file is GPL-3.0-or-later.
set -eu
HAIKU=/home/nickr/r5os/haiku
SRCROOT=/home/nickr/r5os/mesa-build
REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HAIKU/generated.x86_64/cross-tools-x86_64/bin"
CC="$BIN/x86_64-unknown-haiku-gcc"
CXX="$BIN/x86_64-unknown-haiku-g++"
STRIP="$BIN/x86_64-unknown-haiku-strip"
PKG="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package"
TARBALL="$SRCROOT/Vulkan-Tools-1.4.367.tar.gz"
CUBE_SRC="$SRCROOT/Vulkan-Tools-1.4.367/cube"
SYS="$SRCROOT/sysroot"
HEADERS="$SRCROOT/vulkan-headers-r5/install/include"
WORK="$SRCROOT/vkcube-r5"
STAGE="$SRCROOT/vkcube-out"
TOOLS="$REPO/distro/packages/x86_64/vulkan_tools-1.4.367-1-x86_64.hpkg"
TEAPOT="$REPO/distro/packages/x86_64/vulkanteapot-1.0.0-1-x86_64.hpkg"

if [[ ! -f "$CUBE_SRC/cube.c" ]]; then
	if [[ ! -f "$TARBALL" ]]; then
		curl -L --fail -o "$TARBALL" \
			"https://github.com/KhronosGroup/Vulkan-Tools/archive/refs/tags/v1.4.367.tar.gz"
	fi
	tar -xzf "$TARBALL" -C "$SRCROOT"
fi

rm -rf "$WORK"
mkdir -p "$WORK" "$STAGE"
cp -a "$CUBE_SRC/." "$WORK/"
python3 "$REPO/scripts/patch-vkcube.py" "$WORK"

cp -a "$REPO/distro/vulkan/include/vulkan/vulkan_r5.h" "$HEADERS/vulkan/vulkan_r5.h"
"$CC" -std=c11 -O2 -DVK_USE_PLATFORM_VIEW_RFIVE \
	-fmacro-prefix-map="$WORK/"= \
	-I"$HEADERS" -I"$SYS/develop/headers" -I"$WORK" \
	-c -o "$STAGE/cube.o" "$WORK/cube.c"
"$CXX" -std=c++17 -O2 \
	-fmacro-prefix-map="$REPO/"= \
	-I"$HEADERS" -I"$SYS/develop/headers" \
	-c -o "$STAGE/vkcube_r5.o" "$REPO/distro/vulkan/vkcube_r5.cpp"
"$CXX" -o "$STAGE/vkcube" "$STAGE/cube.o" "$STAGE/vkcube_r5.o" \
	-L"$SYS/develop/lib" -lvulkan -lbe
"$STRIP" "$STAGE/vkcube"
if strings "$STAGE/vkcube" | grep -F -q /home/nickr; then
	echo "vkcube contains a host path" >&2
	exit 1
fi

repack() {
	local hpkg="$1"
	local root="$2"
	local hook="$3"
	rm -rf "$root"
	mkdir -p "$root"
	"$PKG" extract -C "$root" "$hpkg"
	"$hook" "$root"
	rm -f "$hpkg"
	"$PKG" create -C "$root" "$hpkg"
}

add_vkcube() {
	mkdir -p "$1/bin"
	cp -a "$STAGE/vkcube" "$1/bin/vkcube"
	chmod 755 "$1/bin/vkcube"
	python3 - "$1/.PackageInfo" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
text = path.read_text()
old = 'Prints the Vulkan devices the loader can open. On this image that is Lavapipe, the Mesa software driver, until a GPU ICD exists.'
new = 'vulkaninfo prints the Vulkan devices the loader can open. vkcube draws in an R5 window through VK_RFIVE_view_surface and keeps vkcube\'s own device choice.'
if old in text:
    text = text.replace(old, new, 1)
if "cmd:vkcube" not in text:
    text = text.replace("\tcmd:vulkaninfo = 1.4.367\n",
                        "\tcmd:vulkaninfo = 1.4.367\n\tcmd:vkcube = 1.4.367\n", 1)
path.write_text(text)
PY
}

add_script() {
	mkdir -p "$1/bin"
	python3 - "$REPO/distro/graphics/src/bin/r5-wsi-window-test/r5-wsi-window-test.sh" "$1/bin/r5-wsi-window-test.sh" <<'PY'
import pathlib, sys
text = pathlib.Path(sys.argv[1]).read_text().replace("\r\n", "\n").replace("\r", "\n")
pathlib.Path(sys.argv[2]).write_text(text)
PY
	chmod 755 "$1/bin/r5-wsi-window-test.sh"
}

repack "$TOOLS" "$SRCROOT/vulkan-tools-pkg" add_vkcube
repack "$TEAPOT" "$SRCROOT/teapot-pkg" add_script
echo "packed vkcube and the window script"
