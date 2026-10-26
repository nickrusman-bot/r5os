#!/usr/bin/env bash
# Start a fix branch from r5 and mark the Haiku ticket in-progress.
# Usage: scripts/new-bugfix.sh <ticket-id> <short-slug>
#        scripts/new-bugfix.sh <short-slug>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAIKU="$ROOT/haiku"
TICKET=""
SLUG=""

if [[ "${1:-}" =~ ^[0-9]+$ ]]; then
	TICKET="$1"
	SLUG="${2:-}"
else
	SLUG="${1:-}"
fi

if [[ -z "$SLUG" ]]; then
	echo "Usage: $0 [<ticket-id>] <short-slug>" >&2
	exit 1
fi

if [[ ! -d "$HAIKU/.git" ]]; then
	echo "haiku/ is missing. Run scripts/setup-wsl.sh first." >&2
	exit 1
fi

cd "$HAIKU"
if git show-ref --verify --quiet refs/remotes/origin/r5; then
	git fetch origin
	git checkout -b "r5/${SLUG}" origin/r5
elif git show-ref --verify --quiet refs/heads/r5; then
	git checkout -b "r5/${SLUG}" r5
else
	echo "No r5 branch yet. Run scripts/configure-haiku-git.sh" >&2
	exit 1
fi

if [[ -n "$TICKET" ]] && command -v python3 >/dev/null; then
	python3 "$ROOT/scripts/tracker-set.py" "$TICKET" in-progress --branch "r5/${SLUG}"
	python3 "$ROOT/scripts/tracker-report.py"
fi

echo "Branch r5/${SLUG} — merge back to r5 when the fix is done."
