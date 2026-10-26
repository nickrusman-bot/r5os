#!/usr/bin/env bash
# Import a Trac CSV (browser download) and refresh STATUS.md.
# Usage: scripts/tracker-sync.sh /path/to/query.csv
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CSV="${1:-}"

if [[ -z "$CSV" ]]; then
	cat <<'EOF' >&2
Haiku Trac blokkeert automatische downloads.

1. Open in je browser:
   https://dev.haiku-os.org/query?status=new&status=assigned&status=reopened&col=id&col=summary&col=status&col=type&col=priority&col=component&col=milestone&order=id&max=10000
2. Onderaan: "Comma-delimited Text" / CSV opslaan.
3. Daarna:  scripts/tracker-sync.sh ~/Downloads/query.csv
EOF
	exit 1
fi

python3 "$ROOT/scripts/tracker-import.py" "$CSV"
python3 "$ROOT/scripts/tracker-report.py"
