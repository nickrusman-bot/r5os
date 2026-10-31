#!/bin/bash
# Cross-build the VirtualBox 7.2.22 Haiku guest additions.
# See distro/vbox-guest/README.md.
set -euo pipefail

if [ -z "${R5OS_HAIKU_TREE:-}" ] || [ -z "${R5OS_VBOX_TREE:-}" ]; then
	echo "Set R5OS_HAIKU_TREE and R5OS_VBOX_TREE." >&2
	exit 1
fi
if [ ! -f "$R5OS_VBOX_TREE/env.sh" ]; then
	echo "No env.sh in $R5OS_VBOX_TREE. Run ./configure there first." >&2
	exit 1
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CROSS="$R5OS_HAIKU_TREE/generated.x86_64/cross-tools-x86_64"
SYSROOT="$CROSS/sysroot/boot/system"
PREFIX="$CROSS/bin/x86_64-unknown-haiku-"
TOOLS="$R5OS_VBOX_TREE/r5os-host-tools/bin"
WRAP="$R5OS_VBOX_TREE/r5os-cross-bin"

mkdir -p "$TOOLS"
cp -f "$ROOT/scripts/vbox-pkg-config.py" "$TOOLS/pkg-config.py"
cat > "$TOOLS/pkg-config" <<EOF
#!/bin/sh
exec python3 "$TOOLS/pkg-config.py" "\$@"
EOF
chmod 755 "$TOOLS/pkg-config"

python3 "$ROOT/scripts/vbox-additions-setup.py"

cd "$R5OS_VBOX_TREE"
# shellcheck disable=SC1091
. ./env.sh
# Host programs (yasm, xsltproc, the bldRT* tools) must see Linux gcc.
# The Haiku compiler is selected with TOOL_*_PREFIX, not by putting the
# cross wrappers ahead of /usr/bin. Those wrappers are named gcc/g++ too.
export PATH="/usr/bin:$TOOLS:$PATH:$CROSS/bin"
if [ -d "$R5OS_VBOX_TREE/../host-tools/bin" ]; then
	export PATH="$R5OS_VBOX_TREE/../host-tools/bin:$PATH"
fi
if [ -d "$R5OS_VBOX_TREE/../host-tools/lib" ]; then
	export LD_LIBRARY_PATH="$R5OS_VBOX_TREE/../host-tools/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

kmk -j"$(nproc)" \
	VBOX_ONLY_ADDITIONS=1 \
	IPRT_WITHOUT_PAM=1 \
	KBUILD_TARGET=haiku \
	KBUILD_TARGET_ARCH=amd64 \
	TOOL_GXX3PLAIN_PREFIX="$PREFIX" \
	TOOL_GCC3PLAIN_PREFIX="$PREFIX" \
	R5OS_HAIKU_DEVELOP_LIB="$SYSROOT/develop/lib" \
	R5OS_HAIKU_ICONV_INC="$R5OS_HAIKU_TREE/headers/libs/iconv" \
	R5OS_HAIKU_ICONV_LIB="$R5OS_HAIKU_TREE/generated.x86_64/objects/haiku/x86_64/release/libs/iconv/libiconv.a" \
	R5OS_HAIKU_INPUT_SERVER="$R5OS_HAIKU_TREE/generated.x86_64/objects/haiku/x86_64/packaging/packages_build/regular/hpkg_-haiku.hpkg/contents/servers/input_server" \
	R5OS_HAIKU_APP_SERVER="$R5OS_HAIKU_TREE/generated.x86_64/objects/haiku/x86_64/packaging/packages_build/regular/hpkg_-haiku.hpkg/contents/servers/app_server"

XRES="$R5OS_HAIKU_TREE/generated.x86_64/objects/linux/x86_64/release/tools/xres"
TRAY="out/haiku.amd64/release/bin/additions/VBoxTray"
RSRC="out/haiku.amd64/release/obj/VBoxTray/VBoxTray.rsrc"
if [ -x "$XRES" ] && [ -f "$TRAY" ] && [ -f "$RSRC" ]; then
	"$XRES" -o "$TRAY" "$RSRC"
	"$XRES" -l "$TRAY"
fi
