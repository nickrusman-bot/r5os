#!/usr/bin/env python3
"""Shared helpers for the R5 OS Haiku-ticket tracker."""
from __future__ import annotations

import csv
import io
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACKER = ROOT / "tracker"
HAIKU_OPEN = TRACKER / "haiku-open.csv"
R5_STATUS = TRACKER / "r5-status.tsv"
STATUS_MD = TRACKER / "STATUS.md"

STATUSES = ("open", "in-progress", "fixed", "wontfix", "not-applicable")
HAIKU_FIELDS = ("id", "summary", "status", "type", "priority", "component", "milestone")
R5_FIELDS = ("id", "r5_status", "branch", "commit", "notes")
TICKET_URL = "https://dev.haiku-os.org/ticket/{id}"


def ensure_tracker() -> None:
    TRACKER.mkdir(parents=True, exist_ok=True)


def _normalize_header(name: str) -> str:
    return name.strip().lstrip("\ufeff").lower()


def read_haiku_csv(path: Path) -> dict[str, dict[str, str]]:
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8-sig")
    sample = text[:4096]
    dialect = csv.Sniffer().sniff(sample, delimiters=",\t;")
    reader = csv.DictReader(io.StringIO(text), dialect=dialect)
    rows: dict[str, dict[str, str]] = {}
    for raw in reader:
        mapped = {_normalize_header(k): (v or "").strip() for k, v in raw.items() if k}
        ticket_id = mapped.get("id") or mapped.get("ticket")
        if not ticket_id:
            continue
        rows[ticket_id] = {
            "id": ticket_id,
            "summary": mapped.get("summary", ""),
            "status": mapped.get("status", ""),
            "type": mapped.get("type", ""),
            "priority": mapped.get("priority", ""),
            "component": mapped.get("component", ""),
            "milestone": mapped.get("milestone", ""),
        }
    return rows


def write_haiku_csv(rows: dict[str, dict[str, str]]) -> None:
    ensure_tracker()
    ordered = sorted(rows.values(), key=lambda r: int(r["id"]) if r["id"].isdigit() else r["id"])
    with HAIKU_OPEN.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(HAIKU_FIELDS))
        writer.writeheader()
        writer.writerows(ordered)


def read_r5_status() -> dict[str, dict[str, str]]:
    if not R5_STATUS.exists():
        return {}
    rows: dict[str, dict[str, str]] = {}
    with R5_STATUS.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for raw in reader:
            ticket_id = (raw.get("id") or "").strip()
            if not ticket_id:
                continue
            rows[ticket_id] = {field: (raw.get(field) or "").strip() for field in R5_FIELDS}
    return rows


def write_r5_status(rows: dict[str, dict[str, str]]) -> None:
    ensure_tracker()
    ordered = sorted(rows.values(), key=lambda r: int(r["id"]) if r["id"].isdigit() else r["id"])
    with R5_STATUS.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(R5_FIELDS), delimiter="\t")
        writer.writeheader()
        writer.writerows(ordered)


def default_r5_row(ticket_id: str) -> dict[str, str]:
    return {
        "id": ticket_id,
        "r5_status": "open",
        "branch": "",
        "commit": "",
        "notes": "",
    }
