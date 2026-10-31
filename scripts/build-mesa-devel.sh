#!/bin/bash
# Build mesa_devel-25.3.6-1: libglvnd link libraries plus Mesa 25 headers.
# The Haiku build feature reads develop/lib/libGL.so and develop/headers/os/opengl.
set -eu
HAIKU=/home/nickr/r5os/haiku
PKG="$HAIKU/generated.x86_64/objects/linux/x86_64/release/tools/package/package"
SRC=/home/nickr/r5os/mesa-build
OLD="$HAIKU/generated.x86_64/build_packages/mesa-22.0.5-3-x86_64/develop/headers"
ROOT=/tmp/mesa-devel-root
OUT="$(cd "$(dirname "$0")/.." && pwd)/distro/packages/x86_64/mesa_devel-25.3.6-1-x86_64.hpkg"

rm -rf "$ROOT"
mkdir -p "$ROOT/develop/lib" "$ROOT/develop/headers/os/opengl"

# Khronos / Mesa 25 headers. The compiler -I path is os/opengl.
for dir in GL EGL GLES GLES2 GLES3 KHR; do
	cp -a "$SRC/sysroot/include/$dir" "$ROOT/develop/headers/os/opengl/$dir"
done

# Haiku GL Kit headers shipped with the Mesa 22 devel package. MIT, unchanged.
cp -a "$OLD/os/opengl/GLView.h" "$ROOT/develop/headers/os/opengl/GLView.h"
cp -a "$OLD/os/opengl/GLRenderer.h" "$ROOT/develop/headers/os/opengl/GLRenderer.h"
cp -a "$OLD/opengl/OpenGLKit.h" "$ROOT/develop/headers/os/opengl/OpenGLKit.h"

# Real libglvnd binaries. Symlinks stay inside develop/lib (not ../../lib).
mkdir -p /tmp/glvnd-extract
rm -rf /tmp/glvnd-extract/*
"$PKG" extract -C /tmp/glvnd-extract "$SRC/hpkgs/libglvnd-1.7.0-1-x86_64.hpkg" lib
cp -a /tmp/glvnd-extract/lib/. "$ROOT/develop/lib/"

cat > "$ROOT/.PackageInfo" <<'EOF'
name			mesa_devel
version			25.3.6-1
architecture		x86_64
summary			"Mesa 25 and libglvnd development files"
description		"Headers and libGL.so for compiling OpenGL apps. libGL.so.1 is libglvnd. Mesa 25 is loaded at run time."
packager		"R5OS <r5os@localhost>"
vendor			"R5OS"
licenses {
	"MIT"
}
copyrights {
	"2013 NVIDIA Corporation"
	"1999-2026 Brian Paul and others"
	"2006-2013 Haiku, Inc."
	"2006 Philippe Houdoin"
}
provides {
	mesa_devel = 25.3.6
	devel:libgl = 1.0.0 compat >= 1
	devel:libegl = 1.1.0 compat >= 1
	devel:libglesv1_cm = 1.2.0 compat >= 1
	devel:libglesv2 = 2.1.0 compat >= 2
	devel:libgldispatch = 0.0.0 compat >= 0
}
requires {
	mesa == 25.3.6
	haiku >= r1~beta5_hrev57937_5-1
}
EOF

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
"$PKG" create -C "$ROOT" "$OUT"
echo "created $OUT"
"$PKG" list "$OUT" | head -30
# The link library must be libglvnd, not a symlink out of the package.
"$PKG" list "$OUT" | grep -E 'libGL.so|GLView.h|gl.h' | head
