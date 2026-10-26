#!/usr/bin/env bash
# Marketing version from distro/VERSION, or jam HAIKU_REVISION (--revision).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ver="$(sed -n '/^[[:space:]]*[^#[:space:]]/p' "$ROOT/distro/VERSION" | head -1 | tr -d '\r' | tr -d '[:space:]')"
if [[ -z "$ver" ]]; then
	echo "distro/VERSION is empty" >&2
	exit 1
fi

case "${1:-}" in
	--revision)
		echo "r5-${ver}"
		;;
	"")
		echo "$ver"
		;;
	*)
		echo "usage: $0 [--revision]" >&2
		exit 1
		;;
esac
