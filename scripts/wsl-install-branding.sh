#!/usr/bin/env bash
set -euo pipefail
HAIKU=/home/nickr/r5os/haiku
WIN=/mnt/c/Users/nickr/Projects/R5OS
killall -q qemu-system-x86_64 || true
mkdir -p "$HAIKU/data/artwork/boot_splash" "$HAIKU/headers/private/system"
cp "$WIN/distro/artwork/r5-os-logo.png" "$HAIKU/data/artwork/R5OS-logo.png"
cp "$WIN/distro/artwork/r5-os-deskbar.png" "$HAIKU/data/artwork/R5OS-deskbar.png"
cp "$WIN/distro/artwork/r5-os-splash.png" "$HAIKU/data/artwork/boot_splash/splash_logo-empty.png"
python3 "$WIN/scripts/apply-branding.py" "$HAIKU"
python3 "$WIN/scripts/apply-darkmode.py" "$HAIKU"
python3 "$WIN/scripts/apply-ui-bugs.py" "$HAIKU"
python3 "$WIN/scripts/apply-kernel-bugs.py" "$HAIKU"
for patch in apply-deskbar-bugs.py apply-tracker-desktop.py apply-ui-shell.py apply-high-bugs.py apply-qemu-drivers.py apply-desktop-enh.py; do
	if [[ -f "$WIN/scripts/$patch" ]]; then
		python3 "$WIN/scripts/$patch" "$HAIKU"
	fi
done
python3 "$WIN/scripts/generate_images_sans_tm.py" \
	"$HAIKU/headers/private/kernel/boot/images-sans-tm.h"
cat > "$HAIKU/headers/private/system/r5_os_version.h" <<'EOF'
#ifndef R5_OS_VERSION_H
#define R5_OS_VERSION_H
#define R5_OS_VERSION "2026.1-dev"
#endif
EOF
printf '%s\n' 'r5-2026.1-dev' > "$HAIKU/generated.x86_64/build/haiku-revision"
rm -f "$HAIKU/generated.x86_64/build/last-built-revision"
tr -d '\r' < "$WIN/distro/UserBuildConfig" | sed 's/^HAIKU_REVISION = .*/HAIKU_REVISION = r5-2026.1-dev ;/' > "$HAIKU/build/jam/UserBuildConfig"
touch "$HAIKU/src/apps/aboutsystem/AboutSystem.cpp" \
	"$HAIKU/src/apps/deskbar/BarMenuBar.cpp" \
	"$HAIKU/src/apps/deskbar/icons.rdef" \
	"$HAIKU/src/apps/installer/Installer.rdef"
echo "branding installed"
ls -l "$HAIKU/data/artwork/R5OS-logo.png" "$HAIKU/data/artwork/R5OS-deskbar.png"
