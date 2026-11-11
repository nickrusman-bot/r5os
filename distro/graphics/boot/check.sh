#!/bin/sh
# PASS or FAIL for one stick boot. Reads syslog only, plus the self-test
# file and the three Terminal captures. Boot 2 is the file that contains
# gpu_present_fail true. Each boot keeps its own copies under ~/boot1 or
# ~/boot2, so a later boot does not replace them.
log=/boot/system/var/log/syslog
test=/boot/home/config/cache/r5-gpu-selftest.txt
settings=/boot/home/config/settings/kernel/drivers/radeon_hd
wsi=/boot/home/wsi.txt
tea=/boot/home/win-teapot.txt
cube=/boot/home/win-vkcube.txt
fail=0

boot=1
driver="AMD Radeon Graphics"
if grep -q "^gpu_present_fail true" "$settings" 2>/dev/null; then
	boot=2
	driver="llvmpipe"
fi
keep=/boot/home/boot$boot
mkdir -p "$keep"
report=$keep/check.txt
: > "$report"
emit() {
	printf '%s\n' "$1" | tee -a "$report"
}
copy_keep() {
	if [ -f "$1" ]; then
		cp "$1" "$keep/$(basename "$1")"
	fi
}

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
		emit "PASS $2"
	else
		emit "FAIL $2"
		fail=1
	fi
}

copy_keep "$wsi"
copy_keep "$tea"
copy_keep "$cube"
copy_keep "$test"
if [ -f "$keep/wsi.txt" ]; then
	wsi=$keep/wsi.txt
fi
if [ -f "$keep/win-teapot.txt" ]; then
	tea=$keep/win-teapot.txt
fi
if [ -f "$keep/win-vkcube.txt" ]; then
	cube=$keep/win-vkcube.txt
fi
if [ -f "$keep/r5-gpu-selftest.txt" ]; then
	test=$keep/r5-gpu-selftest.txt
fi
emit "boot $boot"

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
prev=
while read -r line; do
	case "$prev" in
	"radeon ms per frame "[0-9]*)
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
		;;
	esac
	prev=$line
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

lost=$(grep -n "gpu lost" "$log" | head -n 1 | cut -d: -f1)
mark=$(grep -n "r5 window tests" "$log" | head -n 1 | cut -d: -f1)
if [ -n "$lost" ] && { [ -z "$mark" ] || [ "$lost" -lt "$mark" ]; }; then
	say 1 "gpu lost before window tests"
else
	say 0 "gpu lost before window tests"
fi

if [ "$boot" -eq 2 ]; then
	need "RADV view surface: no, window through Lavapipe"
fi

if [ "$fail" -eq 0 ]; then
	emit PASS
else
	emit FAIL
fi
