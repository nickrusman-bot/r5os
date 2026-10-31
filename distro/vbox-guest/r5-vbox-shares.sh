#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Mount every host shared folder onto the signed-in Desktop.
# The Desktop folder is "VBox Share". VirtualBox share names cannot contain a space.
# Exit quietly when this is not a VirtualBox guest.

home=$1
log=/boot/system/var/log/r5-vbox-shares.log

if [ -z "$home" ] || [ ! -d "$home" ]; then
	exit 0
fi
if [ ! -e /dev/misc/vboxguest ]; then
	exit 0
fi

control=/system/non-packaged/bin/VBoxControl
if [ ! -x "$control" ]; then
	echo "no VBoxControl" >> "$log"
	exit 0
fi

desktop="$home/Desktop"
mkdir -p "$desktop" || exit 0
uid=$(stat -c %u "$home" 2>/dev/null) || uid=
gid=$(stat -c %g "$home" 2>/dev/null) || gid=
note() {
	echo "$*" >> "$log"
	echo "r5-vbox-shares: $*" > /dev/dprintf 2>/dev/null || true
}

note "start $home"
list=""
try=0
while [ "$try" -lt 8 ]; do
	list=$("$control" sharedfolder list 2>>"$log") || true
	case "$list" in
	*" - "*)
		break
		;;
	esac
	try=$((try + 1))
	sleep 1
done
note "list try $try"
n=0
printf '%s\n' "$list" | while IFS= read -r line; do
	case "$line" in
	[0-9][0-9]" - "*)
		name=${line#*- }
		name=${name%% [*}
		;;
	*)
		continue
		;;
	esac
	# Trim spaces. Reject a path, a hidden name, or an empty name.
	name=$(printf '%s' "$name" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
	case "$name" in
	""|.*|*/*|*..*|-*|" ")
		note "skip $line"
		continue
		;;
	esac
	n=$((n + 1))
	if [ "$n" -eq 1 ]; then
		label="VBox Share"
	else
		label="VBox Share $n"
	fi
	dest="$desktop/$label"
	mkdir -p "$dest" || continue
	chmod 700 "$dest" || true
	if [ -n "$uid" ] && [ -n "$gid" ]; then
		chown "$uid:$gid" "$dest" || true
	fi
	if mount -t vboxsf -p "$name" "$dest" >>"$log" 2>&1; then
		note "mounted $name $dest"
	else
		note "mount failed $name"
	fi
done
exit 0
