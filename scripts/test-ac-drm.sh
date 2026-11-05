#!/bin/bash
set -eu
ROOT=/home/nickr/r5os
LIBDRM=$ROOT/src-third/libdrm-2.4.124
MESA=$ROOT/mesa-build/mesa-25.3.6
MESON=$ROOT/mesa-build/meson-1.7.0/meson.py
WIN=/mnt/c/Users/nickr/Projects/R5OS
export PATH="$ROOT/mesa-build:$PATH"
if [[ ! -f $LIBDRM/build-host/build.ninja ]]; then
	rm -rf "$LIBDRM/build-host"
	python3 "$MESON" setup "$LIBDRM/build-host" "$LIBDRM" \
		-Dintel=disabled -Dradeon=disabled -Damdgpu=enabled \
		-Dnouveau=disabled -Dvmwgfx=disabled -Dfreedreno=disabled \
		-Dvc4=disabled -Detnaviv=disabled -Dtests=false \
		-Dman-pages=disabled -Dcairo-tests=disabled \
		-Dc_args=-DR5_HOST_TEST
fi
if [[ ! -f $LIBDRM/build-host/libdrm.so.2.124.0.p/r5_translate.c.o ]]; then
	ninja -C "$LIBDRM/build-host" || true
fi
INC=(
	-I"$MESA/src" -I"$MESA/src/amd/common" -I"$MESA/include"
	-I"$LIBDRM" -I"$LIBDRM/amdgpu" -I"$LIBDRM/include" -I"$LIBDRM/include/drm"
	-I"$LIBDRM/build-host"
)
gcc -std=c11 -O2 -DHAVE_ENDIAN_H -D_GNU_SOURCE -DR5_HOST_TEST "${INC[@]}" \
	-c -o /tmp/ac_linux_drm.o "$MESA/src/amd/common/ac_linux_drm.c"
gcc -std=c11 -O2 -DHAVE_ENDIAN_H -D_GNU_SOURCE "${INC[@]}" \
	-c -o /tmp/test-ac-drm.o "$WIN/scripts/test-ac-drm.c"
gcc -std=c11 -O2 -DR5_HOST_TEST -D_GNU_SOURCE "${INC[@]}" \
	-c -o /tmp/test-r5-libdrm.o "$WIN/scripts/test-r5-libdrm.c"
gcc -o /tmp/test-ac-drm \
	/tmp/ac_linux_drm.o /tmp/test-ac-drm.o /tmp/test-r5-libdrm.o \
	"$LIBDRM"/build-host/libdrm.so.2.124.0.p/r5_translate.c.o \
	"$LIBDRM"/build-host/amdgpu/libdrm_amdgpu.so.1.124.0.p/*.o \
	-lpthread -lm
/tmp/test-ac-drm
