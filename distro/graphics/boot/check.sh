#!/bin/sh
# M8 stick. Boot 1 is the file with gpu_app_server true. Boot 2 is the
# CPU copy. Each boot keeps its own files under ~/boot1 or ~/boot2.
log=/boot/system/var/log/syslog
settings=/boot/home/config/settings/kernel/drivers/radeon_hd
other=0

boot=2
if grep -q "^gpu_app_server true" "$settings" 2>/dev/null; then
	boot=1
fi
keep=/boot/home/boot$boot
mkdir -p "$keep"
report=$keep/check.txt
: > "$report"
emit() {
	printf '%s\n' "$1" | tee -a "$report"
}
say() {
	if [ "$1" -eq 0 ]; then
		emit "PASS $2"
	else
		emit "FAIL $2"
		other=1
	fi
}
count_of() {
	grep -c "$1" "$bootlog" 2>/dev/null || true
}
median_field() {
	if [ ! -s "$1" ]; then
		return
	fi
	cut -d' ' -f"$2" "$1" | sort -n | awk '
		{ a[NR] = $1 }
		END { if (NR > 0) print a[int((NR + 1) / 2)] }
	'
}

# syslog_daemon keeps one older file. A long gpu_debug boot rotates the
# welcome line out of the current file. Read both, and keep the text
# after the last welcome. With no welcome, keep everything that is left.
: > "$keep/syslog.txt"
for part in "$log.1" "$log.old" "$log"; do
	if [ -f "$part" ]; then
		cat "$part" >> "$keep/syslog.txt"
	fi
done
if [ -s "$keep/syslog.txt" ]; then
	awk '
		/Welcome to the Haiku boot loader!/ { buf = ""; seen = 1 }
		{ all = all $0 "\n" }
		seen { buf = buf $0 "\n" }
		END { if (seen) printf "%s", buf; else printf "%s", all }
	' "$keep/syslog.txt" > "$keep/syslog.part"
	mv "$keep/syslog.part" "$keep/syslog.txt"
fi
bootlog=$keep/syslog.txt
emit "boot $boot"

if grep -q "Welcome to the Haiku boot loader!" "$bootlog"; then
	say 0 "boot marker"
else
	say 1 "boot marker"
fi
for line in "GFX9 scratch 0xdeadbeef" "fence landed" "k2 ok gart"; do
	n=$(count_of "$line")
	if [ "$n" -ge 1 ]; then
		say 0 "$line"
	else
		say 1 "$line"
	fi
done

on=$(count_of "gpu copy on scanout")
off=$(count_of "gpu copy off")
compared=$(count_of "copy compare ok")
mismatch=$(count_of "copy compare MISMATCH")
if [ ! -s "$bootlog" ]; then
	say 1 "no copy compare MISMATCH"
	say 1 "no gpu copy off"
elif [ "$mismatch" -eq 0 ]; then
	say 0 "no copy compare MISMATCH"
else
	say 1 "no copy compare MISMATCH"
fi
if [ -s "$bootlog" ]; then
	if [ "$off" -eq 0 ]; then
		say 0 "no gpu copy off"
	else
		say 1 "no gpu copy off"
	fi
fi

if [ "$boot" -eq 1 ]; then
	if [ "$on" -ge 1 ]; then
		say 0 "gpu copy on scanout"
	else
		say 1 "gpu copy on scanout"
	fi
	if [ "$compared" -ge 1 ]; then
		say 0 "copy compare ok"
	else
		say 1 "copy compare ok"
	fi
	grep "copy gpu median" "$bootlog" | sed -n \
		's/.*median \([0-9][0-9]*\) us cpu \([0-9][0-9]*\) ms.*/\1 \2/p' \
		> "$keep/gpu.txt"
	samples=$(wc -l < "$keep/gpu.txt" | tr -d ' ')
	if [ "$samples" -ge 3 ]; then
		say 0 "copy gpu median three times"
	else
		say 1 "copy gpu median three times"
	fi
	if [ ! -s /boot/home/boot2/cpu.txt ]; then
		emit "FAIL speed rule needs the cpu boot"
	else
		gus=$(median_field "$keep/gpu.txt" 1)
		gms=$(median_field "$keep/gpu.txt" 2)
		cus=$(median_field /boot/home/boot2/cpu.txt 1)
		cms=$(median_field /boot/home/boot2/cpu.txt 2)
		text="speed rule gpu ${gus} us / ${gms} ms against cpu ${cus} us / ${cms} ms"
		if [ -n "$gus" ] && [ -n "$gms" ] && [ -n "$cus" ] && [ -n "$cms" ] \
			&& [ $((gms * 2)) -le "$cms" ] && [ "$gus" -le "$cus" ]; then
			emit "PASS $text"
		else
			emit "FAIL $text"
		fi
	fi
else
	if [ "$on" -eq 0 ]; then
		say 0 "no gpu copy on"
	else
		say 1 "no gpu copy on"
	fi
	grep "copy cpu median" "$bootlog" | sed -n \
		's/.*median \([0-9][0-9]*\) us cpu \([0-9][0-9]*\) ms.*/\1 \2/p' \
		> "$keep/cpu.txt"
	samples=$(wc -l < "$keep/cpu.txt" | tr -d ' ')
	if [ "$samples" -ge 3 ]; then
		say 0 "copy cpu median three times"
	else
		say 1 "copy cpu median three times"
	fi
	gus=$(median_field /boot/home/boot1/gpu.txt 1)
	gms=$(median_field /boot/home/boot1/gpu.txt 2)
	cus=$(median_field "$keep/cpu.txt" 1)
	cms=$(median_field "$keep/cpu.txt" 2)
	text="speed rule gpu ${gus} us / ${gms} ms against cpu ${cus} us / ${cms} ms"
	if [ -n "$gus" ] && [ -n "$gms" ] && [ -n "$cus" ] && [ -n "$cms" ] \
		&& [ $((gms * 2)) -le "$cms" ] && [ "$gus" -le "$cus" ]; then
		emit "PASS $text"
	else
		emit "FAIL $text"
		other=1
	fi
fi

if [ "$other" -eq 0 ]; then
	emit PASS
else
	emit FAIL
fi
