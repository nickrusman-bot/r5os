#!/usr/bin/env bash
# Build an R5OS image. Usage: scripts/build-image.sh [r5-raw|r5-anyboot|r5-vmware]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROFILE="${1:-r5-raw}"
GEN="$ROOT/haiku/generated.x86_64"

"$ROOT/scripts/apply-overlay.sh"

if [[ ! -d "$GEN" ]]; then
	mkdir -p "$GEN"
	(
		cd "$GEN"
		../configure \
			--cross-tools-source ../../buildtools \
			--build-cross-tools x86_64 \
			--distro-compatibility compatible
	)
fi

REV="$("$ROOT/scripts/r5-version.sh" --revision)"
cd "$GEN"
jam -sHAIKU_REVISION="$REV" -q "@$PROFILE"
echo "Build finished for @$PROFILE in $GEN ($REV)"
