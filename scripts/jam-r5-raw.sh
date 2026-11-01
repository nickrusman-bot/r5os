#!/usr/bin/env bash
set -uo pipefail
HAIKU=/home/nickr/r5os/haiku
WIN="$(cd "$(dirname "$0")/.." && pwd)"
export PATH=/usr/bin:/bin:/home/nickr/bin:$PATH

"$WIN/scripts/pin-mesa-packages.sh"

ver="$(sed -n '/^[[:space:]]*[^#[:space:]]/p' "$WIN/distro/VERSION" | head -1 | tr -d '\r' | tr -d '[:space:]')"
build="$(sed -n '/^[[:space:]]*[^#[:space:]]/p' "$WIN/distro/BUILD" | head -1 | tr -d '\r' | tr -d '[:space:]')"
rev="r5-${ver}"
tr -d '\r' < "$WIN/distro/UserBuildConfig" | sed "s/^HAIKU_REVISION = .*/HAIKU_REVISION = ${rev} ;/" > "$HAIKU/build/jam/UserBuildConfig"
mkdir -p "$HAIKU/generated.x86_64/build" "$HAIKU/headers/private/system"
printf '%s\n' "$rev" > "$HAIKU/generated.x86_64/build/haiku-revision"
printf '%s\n' \
	'#ifndef R5_OS_VERSION_H' \
	'#define R5_OS_VERSION_H' \
	"#define R5_OS_VERSION \"${ver}\"" \
	"#define R5_OS_BUILD ${build}" \
	'#endif' > "$HAIKU/headers/private/system/r5_os_version.h"
rm -f "$HAIKU/generated.x86_64/build/last-built-revision"

# About bakes What's new into a resource. The resource rule follows
# AboutSystem.rdef, not the text file, so touch the rdef after the copy.
sed 's/\r$//' "$WIN/distro/WHATSNEW" > "$HAIKU/src/apps/aboutsystem/r5_whatsnew.txt"
touch "$HAIKU/src/apps/aboutsystem/AboutSystem.rdef"

cd "$HAIKU/generated.x86_64"
jam -q @r5-raw
jam_exit=$?
echo jam_exit=$jam_exit
if [[ "$jam_exit" -eq 0 ]]; then
	if ! "$WIN/scripts/install-mesa-packages.sh"; then
		echo mesa_exit=1
		exit 1
	fi
	echo mesa_exit=0
fi
exit "$jam_exit"
