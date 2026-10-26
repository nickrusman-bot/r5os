#!/usr/bin/env bash
# Clone upstream Haiku + buildtools next to the R5OS overlay.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v git >/dev/null; then
	echo "git is required" >&2
	exit 1
fi

if [[ ! -d haiku/.git ]]; then
	git clone https://review.haiku-os.org/haiku
fi

if [[ ! -d buildtools/.git ]]; then
	git clone https://review.haiku-os.org/buildtools
fi

"$ROOT/scripts/configure-haiku-git.sh"

if ! command -v jam >/dev/null; then
	echo "Building jam from Haiku buildtools..."
	make -C buildtools/jam
	if [[ -d "$HOME/bin" ]] || mkdir -p "$HOME/bin"; then
		(cd buildtools/jam && ./jam0 -sBINDIR="$HOME/bin" install)
		echo "Installed jam to $HOME/bin — add that directory to PATH if needed."
	fi
fi

"$ROOT/scripts/apply-overlay.sh"
echo "R5OS sources are ready in $ROOT"
