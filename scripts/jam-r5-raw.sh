#!/usr/bin/env bash
set -uo pipefail
HAIKU=/home/nickr/r5os/haiku
WIN=/mnt/c/Users/nickr/Projects/R5OS
export PATH=/usr/bin:/bin:/home/nickr/bin:$PATH

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

cd "$HAIKU/generated.x86_64"
jam -q @r5-raw
echo jam_exit=$?
