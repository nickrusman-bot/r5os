#!/bin/sh
# Cross build of Mesa 25.3.6 with RADV and Zink.
# libdrm 2.4.124 is installed in /home/nickr/r5os/r5-sysroot first
# (sha256 ac36293f61ca4aafaf4b16a2a7afff312aa4f5c37c9fbd797de9e3c0863ca379).
# sudo apt is not available here. mako 1.3.10 is unpacked into
# ~/.local/lib/python3.14/site-packages from the Ubuntu python3-mako deb.
# The llvm-config wrapper must list the bitreader component. Mesa asks fo
# it when the amd Vulkan driver is enabled. The shared libLLVM.so already
# contains that component.
# Lavapipe is built with VK_USE_PLATFORM_VIEW_RFIVE against R5's vk_icd.h.
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MESA=/home/nickr/r5os/mesa-build/mesa-25.3.6
export PATH=/home/nickr/r5os/mesa-build:/home/nickr/r5os/mesa-build/bin:/usr/bin:/bin

# Mesa's own include/vulkan/vk_icd.h is the header the compiler finds first.
cp -f "$ROOT/distro/vulkan/include/vulkan/vk_icd.h" "$MESA/include/vulkan/vk_icd.h"
cp -f "$ROOT/distro/vulkan/include/vulkan/vulkan_r5.h" "$MESA/include/vulkan/vulkan_r5.h"
cp -f "$ROOT/distro/mesa/wsi/wsi_common_haiku.cpp" "$MESA/src/vulkan/wsi/wsi_common_haiku.cpp"
sed -i 's/\r$//' "$MESA/include/vulkan/vk_icd.h" \
	"$MESA/include/vulkan/vulkan_r5.h" \
	"$MESA/src/vulkan/wsi/wsi_common_haiku.cpp"

if ! grep -q 'VK_ICD_WSI_PLATFORM_VIEW_RFIVE + 1' "$MESA/src/vulkan/wsi/wsi_common.h"; then
	patch -p1 -d "$MESA" < "$ROOT/distro/mesa/patches/haiku-view-surface.patch"
fi
if ! grep -q 'wsi_view_r5_init_wsi' "$MESA/src/amd/vulkan/radv_wsi.c" \
	|| ! grep -q 'defined(VK_USE_PLATFORM_VIEW_RFIVE)' "$MESA/src/amd/vulkan/radv_wsi.h" \
	|| ! grep -q 'ifndef VK_USE_PLATFORM_VIEW_RFIVE' "$MESA/src/amd/vulkan/radv_wsi.c"; then
	patch -p1 -d "$MESA" < "$ROOT/distro/mesa/patches/haiku-radv-view-surface.patch"
fi

cd "$MESA"
setup="python3 /home/nickr/r5os/mesa-build/meson-1.7.0/meson.py setup build-radv"
if [ -f build-radv/build.ninja ]; then
	setup="$setup --reconfigure"
fi
$setup \
	--cross-file="$ROOT/distro/mesa/haiku-cross.txt" \
	--native-file="$ROOT/distro/mesa/native.ini" \
	-Degl=enabled -Dglvnd=enabled -Dglx=disabled -Dplatforms=haiku \
	-Dgallium-rusticl=false -Dgallium-drivers=llvmpipe,zink \
	-Dvulkan-drivers=amd,swrast -Dllvm=enabled \
	-Dprefix=/home/nickr/r5os/mesa-build/sysroot -Dlibdir=lib \
	-Dbuildtype=debugoptimized
# An existing build directory keeps its old c_args. Put the platform
# define back, then reconfigure so ninja sees it.
python3 "$ROOT/distro/mesa/ensure-cargs.py"
python3 /home/nickr/r5os/mesa-build/meson-1.7.0/meson.py setup --reconfigure build-radv \
	--cross-file="$ROOT/distro/mesa/haiku-cross.txt" \
	--native-file="$ROOT/distro/mesa/native.ini" \
	-Degl=enabled -Dglvnd=enabled -Dglx=disabled -Dplatforms=haiku \
	-Dgallium-rusticl=false -Dgallium-drivers=llvmpipe,zink \
	-Dvulkan-drivers=amd,swrast -Dllvm=enabled \
	-Dprefix=/home/nickr/r5os/mesa-build/sysroot -Dlibdir=lib \
	-Dbuildtype=debugoptimized
ninja -C build-radv src/gallium/targets/lavapipe/libvulkan_lvp.so \
	src/amd/vulkan/libvulkan_radeon.so
python3 "$ROOT/distro/mesa/pack-lavapipe.py" "$ROOT"
python3 "$ROOT/distro/mesa/pack-radv.py" "$ROOT"
