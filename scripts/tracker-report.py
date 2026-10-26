#!/usr/bin/env python3
"""Write tracker/STATUS.md from haiku-open.csv + r5-status.tsv."""
from __future__ import annotations

from collections import Counter

from tracker_lib import (
    HAIKU_OPEN,
    STATUS_MD,
    TICKET_URL,
    read_haiku_csv,
    read_r5_status,
)


def md_cell(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def main() -> int:
    haiku = read_haiku_csv(HAIKU_OPEN)
    r5 = read_r5_status()
    counts = Counter(row["r5_status"] or "open" for row in r5.values())
    still_open_upstream = set(haiku)
    missing_status = [tid for tid in haiku if tid not in r5]

    lines = [
        "# R5OS ticket status",
        "",
        "Gegenereerd door `scripts/tracker-report.py`. Niet met de hand bewerken.",
        "",
        "| R5-status | Aantal |",
        "|---|---|",
    ]
    for status in ("open", "in-progress", "fixed", "wontfix", "not-applicable"):
        lines.append(f"| {status} | {counts.get(status, 0)} |")
    extra = sum(v for k, v in counts.items() if k not in {
        "open", "in-progress", "fixed", "wontfix", "not-applicable"
    })
    if extra:
        lines.append(f"| overig | {extra} |")
    lines += [
        f"| **totaal in tracker** | **{len(r5)}** |",
        f"| open bij Haiku (laatste import) | {len(haiku)} |",
        "",
    ]

    if missing_status:
        lines.append(f"Nog zonder R5-rij: {len(missing_status)} (opnieuw importeren).")
        lines.append("")

    remaining_ids = [
        tid
        for tid, row in r5.items()
        if row["r5_status"] in ("open", "in-progress") and tid in still_open_upstream
    ]
    remaining = {tid: haiku[tid] for tid in remaining_ids if tid in haiku}

    def count_table(title: str, field: str, col: str, limit: int | None = None) -> None:
        tallies = Counter(row.get(field) or "" for row in remaining.values())
        items = tallies.most_common(limit)
        lines.append(f"## {title}")
        lines.append("")
        lines.append(f"| {col} | Aantal |")
        lines.append("|---|---|")
        for key, n in items:
            lines.append(f"| {md_cell(key) or '—'} | {n} |")
        lines.append("")

    type_counts = Counter(row.get("type") or "" for row in remaining.values())
    lines += [
        f"Nog open in R5OS: **{type_counts.get('bug', 0)}** bugs, "
        f"**{type_counts.get('enhancement', 0)}** enhancements, rest overig "
        f"(van {len(remaining)} open/bezig).",
        "",
    ]
    count_table("Type", "type", "Type")
    count_table("Priority", "priority", "Priority")
    count_table("Milestone", "milestone", "Milestone")
    count_table("Component (top 20)", "component", "Component", limit=20)

    def section(title: str, ticket_ids: list[str], limit: int = 80) -> None:
        lines.append(f"## {title}")
        lines.append("")
        if not ticket_ids:
            lines.append("_geen_")
            lines.append("")
            return
        lines.append("| Ticket | Haiku | Component | Samenvatting | Branch |")
        lines.append("|---|---|---|---|---|")
        for ticket_id in ticket_ids[:limit]:
            meta = haiku.get(ticket_id, {})
            local = r5.get(ticket_id, {})
            url = TICKET_URL.format(id=ticket_id)
            summary = md_cell(meta.get("summary") or local.get("notes") or "")
            component = md_cell(meta.get("component") or "")
            haiku_state = md_cell(meta.get("status") or ("gesloten/niet in snapshot" if ticket_id not in still_open_upstream else ""))
            branch = md_cell(local.get("branch") or "")
            lines.append(
                f"| [#{ticket_id}]({url}) | {haiku_state} | {component} | {summary} | {branch} |"
            )
        if len(ticket_ids) > limit:
            lines.append(f"| … | | | *{len(ticket_ids) - limit} meer in r5-status.tsv* | |")
        lines.append("")

    in_progress = sorted(
        (tid for tid, row in r5.items() if row["r5_status"] == "in-progress"),
        key=lambda t: int(t) if t.isdigit() else t,
    )
    fixed = sorted(
        (tid for tid, row in r5.items() if row["r5_status"] == "fixed"),
        key=lambda t: int(t) if t.isdigit() else t,
    )
    open_ids = sorted(
        (tid for tid, row in r5.items() if row["r5_status"] == "open" and tid in still_open_upstream),
        key=lambda t: int(t) if t.isdigit() else t,
    )

    section("Bezig", in_progress)
    section("Gefixt in R5OS", fixed)
    section("Nog open (eerste 80)", open_ids, limit=80)

    STATUS_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {STATUS_MD} ({len(r5)} tickets, {counts.get('fixed', 0)} fixed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
