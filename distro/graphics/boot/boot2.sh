#!/bin/sh
# Boot 2 of the GPU window test. Removes the settings file and writes a new
# one, because a root-owned file cannot be overwritten. Reboots only on y.
file=/boot/home/config/settings/kernel/drivers/radeon_hd
old=$(cat "$file" 2>/dev/null || true)
rm -f "$file"
mkdir -p "$(dirname "$file")"
{
	printf '%s\n' "$old" | grep -v '^gpu_present' | grep -v '^gpu_present_fail' | grep -v '^$'
	echo "gpu_present true"
	echo "gpu_present_fail true"
} > "$file"
echo "boot 2 settings:"
cat "$file"
printf 'reboot? [y] '
read ans
if [ "$ans" = y ]; then
	shutdown -r
fi
