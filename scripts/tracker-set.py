#!/usr/bin/env python3
"""Set R5 OS status for one Haiku ticket id."""
from __future__ import annotations

import argparse
import sys

from tracker_lib import STATUSES, default_r5_row, read_r5_status, write_r5_status


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ticket_id")
    parser.add_argument("status", choices=STATUSES)
    parser.add_argument("--branch", default="")
    parser.add_argument("--commit", default="")
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    rows = read_r5_status()
    row = rows.get(args.ticket_id, default_r5_row(args.ticket_id))
    row["r5_status"] = args.status
    if args.branch:
        row["branch"] = args.branch
    if args.commit:
        row["commit"] = args.commit
    if args.notes:
        row["notes"] = args.notes
    rows[args.ticket_id] = row
    write_r5_status(rows)
    print(
        f"#{args.ticket_id} -> {args.status}"
        + (f" branch={row['branch']}" if row["branch"] else "")
        + (f" commit={row['commit']}" if row["commit"] else "")
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
