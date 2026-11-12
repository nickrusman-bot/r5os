#!/bin/sh
# Drives one Vulkan or OpenGL window with hey. The argument is the program.
# VulkanTeapot is the default. vkcube, GLTeapot and GLTeapot-zink-cpu are the
# others. GLTeapot-zink-cpu runs GLTeapot through Zink on Lavapipe.
OUT=/tmp/r5-wsi-window-app.txt
if [ $# -ge 1 ]; then
	prog=$1
else
	prog=${TEAPOT:-VulkanTeapot}
fi
base=$(basename "$prog")
case "$base" in
VulkanTeapot)
	APP=application/x-vnd.R5OS-VulkanTeapot
	set -- --window
	;;
vkcube)
	APP=application/x-vnd.R5OS-vkcube
	set --
	;;
GLTeapot)
	APP=application/x-vnd.Haiku-GLTeapot
	prog=/boot/system/demos/GLTeapot
	export R5_GL_TRACE=1
	set --
	;;
GLTeapot-zink-cpu)
	APP=application/x-vnd.Haiku-GLTeapot
	prog=/boot/system/demos/GLTeapot
	export R5_GL_TRACE=1 GALLIUM_DRIVER=zink LIBGL_ALWAYS_SOFTWARE=1
	set --
	;;
*)
	echo "window program FAIL"
	exit 1
	;;
esac
"$prog" "$@" >"$OUT" 2>&1 &
pid=$!
ready=0
driver=
i=0
while [ "$i" -lt 30 ]; do
	if [ "$ready" -eq 0 ] && hey "$APP" get Title of Window 0 >/dev/null 2>&1; then
		ready=1
	fi
	if [ -z "$driver" ] && [ -f "$OUT" ]; then
		driver=$(sed -n 's/^window driver //p' "$OUT" | head -n 1)
		if [ -z "$driver" ]; then
			driver=$(sed -n 's/^egl: opengl //p' "$OUT" | head -n 1)
		fi
		if [ -z "$driver" ]; then
			driver=$(sed -n 's/^Selected GPU [0-9]*: \(.*\), type: .*/\1/p' "$OUT" | head -n 1)
		fi
	fi
	if [ "$ready" -eq 1 ] && [ -n "$driver" ]; then
		break
	fi
	i=$((i + 1))
	sleep 1
done
if [ -n "$driver" ]; then
	echo "window driver $driver"
else
	echo "window driver FAIL"
fi

act() {
	name=$1
	shift
	if [ "$ready" -eq 1 ] && hey "$APP" "$@" >/dev/null 2>&1; then
		echo "window $name ok"
	else
		echo "window $name FAIL"
	fi
	sleep 1
}

act resize set Frame of Window 0 to "BRect(40.0, 40.0, 280.0, 220.0)"
act resize set Frame of Window 0 to "BRect(60.0, 50.0, 520.0, 420.0)"
act resize set Frame of Window 0 to "BRect(80.0, 80.0, 400.0, 360.0)"
act workspace set Workspaces of Window 0 to 2
act workspace set Workspaces of Window 0 to 1
act minimise set Minimize of Window 0 to true
act restore set Minimize of Window 0 to false
hey "$APP" quit >/dev/null 2>&1 || true

i=0
while kill -0 "$pid" 2>/dev/null && [ "$i" -lt 15 ]; do
	i=$((i + 1))
	sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
	kill "$pid" 2>/dev/null
	echo "window close FAIL"
	echo "device lost yes"
	exit 0
fi
wait "$pid"
status=$?
if [ "$status" -eq 0 ]; then
	echo "window close ok"
else
	echo "window close FAIL"
fi
if [ "$status" -ne 0 ] || grep -q "device lost yes" "$OUT"; then
	echo "device lost yes"
else
	echo "device lost no"
fi
