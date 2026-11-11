#!/bin/sh
# Boot 2 of the GPU window test. Removes the settings file and writes a new
# one, because a root-owned file cannot be overwritten. Reboots only on y.
# Boot 1's window files stay in ~/boot1. A file already there is left as it is.
keep1=/boot/home/boot1
mkdir -p "$keep1"
keep_boot1() {
	name=$(basename "$1")
	if [ -f "$1" ] && [ ! -f "$keep1/$name" ]; then
		cp "$1" "$keep1/$name"
	fi
}
keep_boot1 /boot/home/wsi.txt
keep_boot1 /boot/home/win-teapot.txt
keep_boot1 /boot/home/win-vkcube.txt
keep_boot1 /boot/home/config/cache/r5-gpu-selftest.txt
file=/boot/home/config/settings/kernel/drivers/radeon_hd
old=$(cat "$file" 2>/dev/null || true)
rm -f "$file"
mkdir -p "$(dirname "$file")"
{
	printf '%s\n' "$old" | grep -v '^gpu_present' | grep -v '^gpu_present_fail' \
		| grep -v '^gpu_isolation_test' \
		| grep -v '^gfx9_vmids' | grep -v '^gfx9_vmid_per_file' \
		| grep -v '^gfx9_pasid_map' | grep -v '^$'
	echo "gfx9_vmids true"
	echo "gfx9_vmid_per_file true"
	echo "gfx9_pasid_map true"
	echo "gpu_present true"
	echo "gpu_present_fail true"
} > "$file"
echo "boot 2 settings:"
cat "$file"
printf 'reboot? [y] '
read ans
if [ "$ans" = y ]; then
	shutdown -
fi
