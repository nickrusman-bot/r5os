#!/bin/sh
# PASS or FAIL for one stick boot. Reads syslog only, plus the self-test
# file and the three Terminal captures. Boot 2 is the file that contains
# gpu_present_fail true.
log=/boot/system/var/log/syslog
test=/boot/home/config/cache/r5-gpu-selftest.txt
settings=/boot/home/config/settings/kernel/drivers/radeon_hd
wsi=/boot/home/wsi.txt
tea=/boot/home/win-teapot.txt
cube=/boot/home/win-vkcube.txt
fail=0

i=0
while [ "$i" -lt 180 ]; do
	if [ -f "$test" ] && grep -q "selftest done" "$test" \
		&& [ -f "$wsi" ] && [ -f "$tea" ] && [ -f "$cube" ]; then
		break
	fi
	i=$((i + 1))
	sleep 2
done

say() {
	if [ "$1" -eq 0 ]; then
		echo "PASS $2"
	else
		echo "FAIL $2"
		fail=1
	fi
}

boot=1
driver="AMD Radeon Graphics"
if grep -q "^gpu_present_fail true" "$settings" 2>/dev/null; then
	boot=2
	driver="llvmpipe"
fi
echo "boot $boot"

if grep -q "wsi clear ok" "$wsi" && grep -q "$driver" "$wsi"; then
	say 0 "wsi clear ok $driver"
else
	say 1 "wsi clear ok $driver"
fi

windows() {
	name=$1
	file=$2
	if grep -q "window .* FAIL" "$file" || ! grep -q "window resize ok" "$file" \
		|| ! grep -q "window workspace ok" "$file" \
		|| ! grep -q "window minimise ok" "$file" \
		|| ! grep -q "window restore ok" "$file" \
		|| ! grep -q "window close ok" "$file" \
		|| ! grep -q "device lost no" "$file"; then
		say 1 "window $name"
	else
		say 0 "window $name"
	fi
}
windows VulkanTeapot "$tea"
windows vkcube "$cube"

if [ "$boot" -eq 1 ]; then
	if grep "window driver" "$tea" | grep -q "RADV"; then
		say 0 "window driver VulkanTeapot"
	else
		say 1 "window driver VulkanTeapot"
	fi
	if grep "window driver" "$cube" | grep -q "RADV"; then
		say 0 "window driver vkcube"
	else
		say 1 "window driver vkcube"
	fi
fi

span_fail=0
spans=0
while read -r line; do
	case "$line" in
	*"cpu span ms "*)
		spans=$((spans + 1))
		num=$(printf '%s\n' "$line" | sed 's/.*cpu span ms //')
		int=${num%%.*}
		if [ -z "$int" ] || [ "$int" -ge 2000 ]; then
			span_fail=1
		fi
		;;
	esac
done < "$test"
if [ "$spans" -gt 0 ] && [ "$span_fail" -eq 0 ]; then
	say 0 "cpu span ms under 2000"
else
	say 1 "cpu span ms under 2000"
fi

eop=$(sed -n 's/.*ih msi [0-9]* early [0-9]* eop \([0-9]*\).*/\1/p' "$test" | tail -n 1)
if [ -n "$eop" ] && [ "$eop" -ge 100 ]; then
	say 0 "eop $eop"
else
	say 1 "eop ${eop:-missing}"
fi

need() {
	if grep -q "$1" "$log"; then
		say 0 "$1"
	else
		say 1 "$1"
	fi
}
need "GFX9 ring contract verified"
need "GFX9 scratch 0xdeadbeef"
need "fence landed"
need "k2 ok gart"

if [ "$boot" -eq 2 ]; then
	need "RADV view surface: no, window through Lavapipe"
fi

if [ "$fail" -eq 0 ]; then
	echo PASS
else
	echo FAIL
fi
