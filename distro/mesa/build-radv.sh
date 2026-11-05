#!/bin/sh
# Cross build of Mesa 25.3.6 with RADV and Zink.
# libdrm 2.4.124 is installed in /home/nickr/r5os/r5-sysroot first
# (sha256 ac36293f61ca4aafaf4b16a2a7afff312aa4f5c37c9fbd797de9e3c0863ca379).
# sudo apt is not available here. mako 1.3.10 is unpacked into
# ~/.local/lib/python3.14/site-packages from the Ubuntu python3-mako deb.
# The llvm-config wrapper must list the bitreader component. Mesa asks for
# it when the amd Vulkan driver is enabled. The shared libLLVM.so already
# contains that component.
set -e
MESA=/home/nickr/r5os/mesa-build/mesa-25.3.6
export PATH=/home/nickr/r5os/mesa-build:/home/nickr/r5os/mesa-build/bin:/usr/bin:/bin
cd "$MESA"
python3 /home/nickr/r5os/mesa-build/meson-1.7.0/meson.py setup build-radv \
	--cross-file=/mnt/c/Users/nickr/Projects/R5OS/distro/mesa/haiku-cross.txt \
	--native-file=/mnt/c/Users/nickr/Projects/R5OS/distro/mesa/native.ini \
	-Degl=enabled -Dglvnd=enabled -Dglx=disabled -Dplatforms=haiku \
	-Dgallium-rusticl=false -Dgallium-drivers=llvmpipe,zink \
	-Dvulkan-drivers=amd,swrast -Dllvm=enabled \
	-Dprefix=/home/nickr/r5os/mesa-build/sysroot -Dlibdir=lib \
	-Dbuildtype=debugoptimized \
	-Dc_args='-I/mnt/c/Users/nickr/Projects/R5OS/distro/mesa/haiku-extra' \
	-Dcpp_args='-I/mnt/c/Users/nickr/Projects/R5OS/distro/mesa/haiku-extra'
ninja -C build-radv
