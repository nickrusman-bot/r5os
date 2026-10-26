#!/usr/bin/env bash
# haiku/ remotes: origin = R5OS (source of truth), upstream = Haiku (fetch only).
# Optional env: R5OS_HAIKU_ORIGIN
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAIKU="$ROOT/haiku"

if [[ ! -d "$HAIKU/.git" ]]; then
	echo "haiku/ is missing. Run scripts/setup-wsl.sh first." >&2
	exit 1
fi

cd "$HAIKU"
git config pull.rebase false

if git remote get-url origin >/dev/null 2>&1; then
	current="$(git remote get-url origin)"
	if [[ "$current" == *review.haiku-os.org* || "$current" == *git.haiku-os.org* ]]; then
		if ! git remote get-url upstream >/dev/null 2>&1; then
			git remote rename origin upstream
		fi
	fi
fi

if ! git remote get-url upstream >/dev/null 2>&1; then
	git remote add upstream https://review.haiku-os.org/haiku
fi

if [[ -n "${R5OS_HAIKU_ORIGIN:-}" ]]; then
	if git remote get-url origin >/dev/null 2>&1; then
		git remote set-url origin "$R5OS_HAIKU_ORIGIN"
	else
		git remote add origin "$R5OS_HAIKU_ORIGIN"
	fi
fi

if git show-ref --verify --quiet refs/heads/r5; then
	:
elif git show-ref --verify --quiet refs/remotes/origin/r5; then
	git checkout -B r5 origin/r5
else
	git checkout -B r5
fi

echo "haiku remotes:"
git remote -v
echo "Local branch: $(git rev-parse --abbrev-ref HEAD)"
echo
echo "origin is R5OS. Do not push to upstream (Haiku)."
echo "Set R5OS_HAIKU_ORIGIN to your fork URL, then re-run this script and:"
echo "  git push -u origin r5"
