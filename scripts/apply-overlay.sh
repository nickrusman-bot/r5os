#!/usr/bin/env bash
# Install the R5 OS Jam profile into the local Haiku tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/haiku/build/jam/UserBuildConfig"

if [[ ! -d "$ROOT/haiku/build/jam" ]]; then
	echo "haiku/ is missing. Run scripts/setup-wsl.sh first." >&2
	exit 1
fi

cp "$ROOT/distro/UserBuildConfig" "$DEST"
echo "Wrote $DEST"
