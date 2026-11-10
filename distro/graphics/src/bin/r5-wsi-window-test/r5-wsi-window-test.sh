#!/bin/sh
# Drives VulkanTeapot --window with hey. One line per action.
APP=application/x-vnd.R5OS-VulkanTeapot
OUT=/tmp/r5-wsi-window-app.txt
TEAPOT=${TEAPOT:-VulkanTeapot}
"$TEAPOT" --window >"$OUT" 2>&1 &
pid=$!
ready=0
i=0
while [ "$i" -lt 30 ]; do
	if hey "$APP" get Title of Window 0 >/dev/null 2>&1; then
		ready=1
		break
	fi
	i=$((i + 1))
	sleep 1
done

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
