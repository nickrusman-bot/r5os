#!/usr/bin/env bash
# Install the R5 OS Jam profile into the local Haiku tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/haiku/build/jam/UserBuildConfig"
REV="$("$ROOT/scripts/r5-version.sh" --revision)"

if [[ ! -d "$ROOT/haiku/build/jam" ]]; then
	echo "haiku/ is missing. Run scripts/setup-wsl.sh first." >&2
	exit 1
fi

tr -d '\r' < "$ROOT/distro/UserBuildConfig" | sed "s/^HAIKU_REVISION = .*/HAIKU_REVISION = ${REV} ;/" > "$DEST"
echo "Wrote $DEST (HAIKU_REVISION=${REV})"
