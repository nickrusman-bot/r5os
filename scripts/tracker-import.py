#!/usr/bin/env python3
"""Merge a Haiku Trac CSV export into tracker/ without clobbering R5 status."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from tracker_lib import (
    HAIKU_OPEN,
    default_r5_row,
    read_haiku_csv,
    read_r5_status,
    write_haiku_csv,
    write_r5_status,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="Trac CSV/TSV export of open tickets")
    args = parser.parse_args()
    if not args.csv.exists():
        print(f"File not found: {args.csv}", file=sys.stderr)
        return 1

    incoming = read_haiku_csv(args.csv)
    if not incoming:
        print("No tickets in that file.", file=sys.stderr)
        return 1

    write_haiku_csv(incoming)
    r5 = read_r5_status()
    added = 0
    for ticket_id in incoming:
        if ticket_id not in r5:
            r5[ticket_id] = default_r5_row(ticket_id)
            added += 1
    write_r5_status(r5)
    print(f"Imported {len(incoming)} open Haiku tickets into {HAIKU_OPEN}")
    print(f"Added {added} new rows to r5-status.tsv ({len(r5)} total)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
