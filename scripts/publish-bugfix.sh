#!/usr/bin/env bash
# Push the current haiku branch to R5OS git only. Never Haiku Gerrit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAIKU="$ROOT/haiku"

cd "$HAIKU"
branch="$(git rev-parse --abbrev-ref HEAD)"

url="$(git remote get-url origin 2>/dev/null || true)"
if [[ -z "$url" ]]; then
	echo "No origin remote. Export R5OS_HAIKU_ORIGIN and run scripts/configure-haiku-git.sh" >&2
	exit 1
fi
if [[ "$url" == *review.haiku-os.org* || "$url" == *git.haiku-os.org* ]]; then
	echo "origin still points at Haiku. Set R5OS_HAIKU_ORIGIN to the R5OS repo first." >&2
	exit 1
fi

git push -u origin "$branch"
echo "Pushed $branch to origin (R5OS)."
