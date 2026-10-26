#!/usr/bin/env bash
set -uo pipefail
HAIKU=/home/nickr/r5os/haiku
WIN=/mnt/c/Users/nickr/Projects/R5OS
export PATH=/home/nickr/bin:$PATH

tr -d '\r' < "$WIN/distro/UserBuildConfig" > "$HAIKU/build/jam/UserBuildConfig"
mkdir -p "$HAIKU/generated.x86_64/build"
printf '%s\n' 'r5-2026.1-dev' > "$HAIKU/generated.x86_64/build/haiku-revision"
rm -f "$HAIKU/generated.x86_64/build/last-built-revision"

cd "$HAIKU/generated.x86_64"
jam -q @r5-raw
echo jam_exit=$?
