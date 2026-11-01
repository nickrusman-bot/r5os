#!/usr/bin/env bash
# Install the R5OS Jam profile, version header, and artwork into the Haiku tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/haiku/build/jam/UserBuildConfig"
REV="$("$ROOT/scripts/r5-version.sh" --revision)"
VER="$("$ROOT/scripts/r5-version.sh")"
BUILD="$("$ROOT/scripts/r5-version.sh" --build)"

if [[ ! -d "$ROOT/haiku/build/jam" ]]; then
	echo "haiku/ is missing. Run scripts/setup-wsl.sh first." >&2
	exit 1
fi

tr -d '\r' < "$ROOT/distro/UserBuildConfig" | sed "s/^HAIKU_REVISION = .*/HAIKU_REVISION = ${REV} ;/" > "$DEST"
echo "Wrote $DEST (HAIKU_REVISION=${REV})"

hdr="$ROOT/haiku/headers/private/system/r5_os_version.h"
printf '%s\n' \
	'#ifndef R5_OS_VERSION_H' \
	'#define R5_OS_VERSION_H' \
	"#define R5_OS_VERSION \"${VER}\"" \
	"#define R5_OS_BUILD ${BUILD}" \
	'#endif' > "$hdr"
echo "Wrote $hdr (R5_OS_VERSION=${VER} R5_OS_BUILD=${BUILD})"

logoSrc="$ROOT/distro/artwork/r5-os-logo.png"
logoDst="$ROOT/haiku/data/artwork/R5OS-logo.png"
if [[ -f "$logoSrc" ]]; then
	cp "$logoSrc" "$logoDst"
	echo "Wrote $logoDst"
fi

deskSrc="$ROOT/distro/artwork/r5-os-deskbar.png"
deskDst="$ROOT/haiku/data/artwork/R5OS-deskbar.png"
if [[ -f "$deskSrc" ]]; then
	cp "$deskSrc" "$deskDst"
	echo "Wrote $deskDst"
fi

splashSrc="$ROOT/distro/artwork/r5-os-splash.png"
if [[ -f "$splashSrc" ]]; then
	cp "$splashSrc" "$ROOT/haiku/data/artwork/boot_splash/splash_logo-empty.png"
	echo "Wrote splash_logo-empty.png"
fi

python3 "$ROOT/scripts/apply-branding.py" "$ROOT/haiku"
python3 "$ROOT/scripts/apply-darkmode.py" "$ROOT/haiku"
python3 "$ROOT/scripts/apply-ui-bugs.py" "$ROOT/haiku"
python3 "$ROOT/scripts/apply-kernel-bugs.py" "$ROOT/haiku"
for patch in apply-deskbar-bugs.py apply-tracker-desktop.py apply-ui-shell.py apply-high-bugs.py apply-qemu-drivers.py apply-desktop-enh.py apply-zeroconf.py apply-airplay.py apply-wifi.py apply-vboxclip.py apply-ipv6.py apply-users.py apply-settings.py apply-login.py apply-app-server.py apply-vbox-guest.py apply-about.py apply-user-paths.py apply-debugger.py; do
	if [[ -f "$ROOT/scripts/$patch" ]]; then
		python3 "$ROOT/scripts/$patch" "$ROOT/haiku"
	fi
done
sans="$ROOT/haiku/headers/private/kernel/boot/images-sans-tm.h"
if [[ -f "$ROOT/distro/artwork/r5-os-splash.bgr" && -f "$sans" ]]; then
	python3 "$ROOT/scripts/generate_images_sans_tm.py" "$sans"
fi

gen="$ROOT/haiku/generated.x86_64/build"
if [[ -d "$gen" ]]; then
	printf '%s\n' "$REV" > "$gen/haiku-revision"
	rm -f "$gen/last-built-revision"
	echo "Wrote $gen/haiku-revision"
fi
