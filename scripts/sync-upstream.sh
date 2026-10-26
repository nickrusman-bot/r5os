#!/usr/bin/env bash
# Merge official Haiku master into the r5 branch (fetch only; no push to Haiku).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAIKU="$ROOT/haiku"

cd "$HAIKU"
git fetch upstream
git checkout r5
git merge --no-edit upstream/master
echo "Merged upstream/master into r5. Review, then: git push origin r5"
