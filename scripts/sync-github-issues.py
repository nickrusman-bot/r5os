#!/usr/bin/env python3
"""Mirror open Haiku Trac tickets into GitHub issues.

GitHub is the lead. Trac is where new upstream tickets come from.

- Every row in tracker/haiku-open.csv gets one issue, labeled ``haiku``.
  The body contains ``<!-- haiku-ticket:ID -->``.
- A ticket already marked fixed on ``origin/main`` is created closed, with a
  comment that quotes the note from that status file.
- A later run closes a still-open issue once ``origin/main`` says fixed.
  It does not reopen a closed issue because Trac still lists it.
- A GitHub close updates tracker/r5-status.tsv to ``fixed`` when that row
  was still open. Notes already stored are kept.
- Tickets that disappear from the Trac snapshot get one comment. They stay
  open here until someone closes them.

Re-run after ``scripts/tracker-import.py``. Creating the issues uses GitHub's
import API so watchers are not mailed once per ticket.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone

from tracker_lib import (
    HAIKU_OPEN,
    ROOT,
    TICKET_URL,
    read_haiku_csv,
    read_r5_status,
    write_r5_status,
)

REPO = "nickrusman-bot/r5os"
API = "https://api.github.com"
MAP_PATH = ROOT / "tracker" / "github.tsv"
MAP_FIELDS = ("id", "number", "state")
MARKER = re.compile(r"haiku-ticket:(\d+)")
PREVIEW = "application/vnd.github.golden-comet-preview+json"
# Committed locally on cursor/tracker-deskbar-batch, not on origin/main yet.
BUILD_10019 = {
    "5338", "7328", "8543", "8544", "8551", "8772", "9216", "9383",
    "11029", "11176", "11402", "11495", "12335", "13431", "15910",
    "15975", "16144", "16185", "16359", "16643", "16726", "16853",
    "18667", "19433", "19520", "19546", "19573", "19661", "19663",
    "20261",
}


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def gh_token() -> str:
    return subprocess.check_output(["gh", "auth", "token"], text=True).strip()


class GitHub:
    def __init__(self, token: str) -> None:
        self.token = token
        self.remaining = 5000
        self.reset = 0

    def request(self, method: str, url: str, body: dict | None = None,
                accept: str = "application/vnd.github+json") -> tuple[int, dict | list]:
        if self.remaining < 80 and self.reset:
            wait = max(0, self.reset - int(time.time()) + 2)
            print(f"  {self.remaining} calls left, sleeping {wait}s", flush=True)
            time.sleep(wait)
            self.remaining = 5000
        data = None if body is None else json.dumps(body).encode("utf-8")
        if url.startswith("/"):
            url = API + url
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Authorization", f"Bearer {self.token}")
        req.add_header("Accept", accept)
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        for attempt in range(8):
            try:
                with urllib.request.urlopen(req, timeout=60) as resp:
                    raw = resp.read().decode("utf-8")
                    if resp.headers.get("X-RateLimit-Remaining") is not None:
                        self.remaining = int(resp.headers["X-RateLimit-Remaining"])
                        self.reset = int(resp.headers.get("X-RateLimit-Reset") or 0)
                    return resp.status, json.loads(raw) if raw else {}
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode("utf-8", "replace")
                if exc.headers.get("X-RateLimit-Remaining") == "0":
                    self.remaining = 0
                    self.reset = int(exc.headers.get("X-RateLimit-Reset") or 0)
                    wait = max(5, self.reset - int(time.time()) + 2)
                    print(f"  rate limit empty, sleeping {wait}s", flush=True)
                    time.sleep(wait)
                    self.remaining = 5000
                    continue
                if exc.code in (403, 429, 502, 503) and attempt < 7:
                    wait = int(exc.headers.get("Retry-After") or 30)
                    print(f"  rate limit {exc.code}, sleeping {wait}s", flush=True)
                    time.sleep(wait)
                    continue
                raise SystemExit(f"{method} {url} -> {exc.code} {detail[:500]}")
        raise SystemExit(f"{method} {url} failed")


def ensure_labels(gh: GitHub) -> None:
    wanted = {
        "haiku": ("5319e7", "Open Haiku Trac ticket, mirrored here"),
        "tracker": ("1d76db", "Haiku Tracker"),
        "deskbar": ("0e8a16", "Haiku Deskbar"),
    }
    _status, existing = gh.request("GET", f"/repos/{REPO}/labels?per_page=100")
    names = {row["name"] for row in existing} if isinstance(existing, list) else set()
    for name, (color, desc) in wanted.items():
        if name in names:
            continue
        gh.request("POST", f"/repos/{REPO}/labels", {
            "name": name, "color": color, "description": desc,
        })
        print(f"label {name}")


def load_map() -> dict[str, dict[str, str]]:
    if not MAP_PATH.exists():
        return {}
    rows: dict[str, dict[str, str]] = {}
    with MAP_PATH.open(encoding="utf-8", newline="") as handle:
        for raw in csv.DictReader(handle, delimiter="\t"):
            ticket_id = (raw.get("id") or "").strip()
            if ticket_id:
                rows[ticket_id] = {
                    field: (raw.get(field) or "").strip() for field in MAP_FIELDS
                }
    return rows


def write_map(rows: dict[str, dict[str, str]]) -> None:
    ordered = sorted(
        rows.values(), key=lambda r: int(r["id"]) if r["id"].isdigit() else r["id"])
    with MAP_PATH.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(MAP_FIELDS), delimiter="\t")
        writer.writeheader()
        writer.writerows(ordered)


def scan_existing(gh: GitHub) -> dict[str, dict[str, str]]:
    found: dict[str, dict[str, str]] = {}
    page = 1
    while True:
        _status, data = gh.request(
            "GET", f"/repos/{REPO}/issues?state=all&per_page=100&page={page}")
        if not isinstance(data, list) or not data:
            break
        for issue in data:
            if "pull_request" in issue:
                continue
            match = MARKER.search(issue.get("body") or "")
            if not match:
                continue
            found[match.group(1)] = {
                "id": match.group(1),
                "number": str(issue["number"]),
                "state": issue["state"],
            }
        if len(data) < 100:
            break
        page += 1
    return found


def fixed_on_main() -> dict[str, str]:
    """ticket id -> notes, for rows already fixed on origin/main."""
    try:
        text = subprocess.check_output(
            ["git", "show", "origin/main:tracker/r5-status.tsv"],
            cwd=ROOT, text=True, encoding="utf-8")
    except subprocess.CalledProcessError:
        print("origin/main has no tracker file; nothing will be closed", file=sys.stderr)
        return {}
    notes: dict[str, str] = {}
    for raw in csv.DictReader(text.splitlines(), delimiter="\t"):
        if (raw.get("r5_status") or "").strip() != "fixed":
            continue
        ticket_id = (raw.get("id") or "").strip()
        if ticket_id:
            notes[ticket_id] = (raw.get("notes") or "").strip()
    return notes


def title_for(ticket_id: str, summary: str) -> str:
    text = " ".join((summary or "").split()) or "Haiku ticket"
    prefix = f"#{ticket_id} "
    room = 256 - len(prefix)
    if len(text) > room:
        text = text[: room - 1] + "…"
    return prefix + text


def labels_for(meta: dict[str, str]) -> list[str]:
    labels = ["haiku"]
    kind = (meta.get("type") or "").lower()
    if kind == "bug":
        labels.append("bug")
    elif kind == "enhancement":
        labels.append("enhancement")
    component = meta.get("component") or ""
    if "Tracker" in component:
        labels.append("tracker")
    if "Deskbar" in component:
        labels.append("deskbar")
    return labels


def body_for(ticket_id: str, meta: dict[str, str], r5: dict[str, str]) -> str:
    upstream = TICKET_URL.format(id=ticket_id)
    note = (r5.get("notes") or "").strip()
    lines = [
        f"Upstream: {upstream}",
        "",
        "| | |",
        "|---|---|",
        f"| Component | {meta.get('component') or '—'} |",
        f"| Type | {meta.get('type') or '—'} |",
        f"| Priority | {meta.get('priority') or '—'} |",
        f"| Haiku status | {meta.get('status') or '—'} |",
        f"| Milestone | {meta.get('milestone') or '—'} |",
        f"| R5OS status | {r5.get('r5_status') or 'open'} |",
    ]
    if note:
        lines += ["", "R5OS note: " + note]
    lines += ["", f"<!-- haiku-ticket:{ticket_id} -->"]
    return "\n".join(lines)


def close_comment(ticket_id: str, notes: str) -> str:
    how = notes or "Marked fixed on main. The status row has no longer note."
    return "\n".join([
        "Already fixed on R5OS `main`.",
        "",
        how,
        "",
        "Haiku Trac still has the report open. This issue is the lead, so it is closed here.",
        f"Upstream: {TICKET_URL.format(id=ticket_id)}",
        "",
        "<!-- r5-sync:closed -->",
    ])


def pending_comment(ticket_id: str, notes: str) -> str:
    how = notes or "The local status row has no longer note."
    return "\n".join([
        "A fix is in daily build 10019 (`bffaeb4967` on `cursor/tracker-deskbar-batch`).",
        "That commit is not on `main` yet. The image was jammed on 1 Nov 2026 and was not booted.",
        "",
        how,
        "",
        f"Upstream: {TICKET_URL.format(id=ticket_id)}",
        "",
        "<!-- r5-sync:build-10019 -->",
    ])


def start_import(gh: GitHub, ticket_id: str, meta: dict[str, str], r5: dict[str, str],
                 main_notes: dict[str, str]) -> tuple[str, str]:
    stamp = now_iso()
    issue: dict = {
        "title": title_for(ticket_id, meta.get("summary") or ""),
        "body": body_for(ticket_id, meta, r5),
        "labels": labels_for(meta),
        "created_at": stamp,
    }
    comments: list[dict] = []
    if ticket_id in main_notes:
        issue["closed"] = True
        issue["closed_at"] = stamp
        comments.append({
            "body": close_comment(ticket_id, main_notes[ticket_id]),
            "created_at": stamp,
        })
    elif ticket_id in BUILD_10019:
        comments.append({
            "body": pending_comment(ticket_id, (r5.get("notes") or "").strip()),
            "created_at": stamp,
        })
    payload: dict = {"issue": issue}
    if comments:
        payload["comments"] = comments
    _status, data = gh.request(
        "POST", f"/repos/{REPO}/import/issues", payload, accept=PREVIEW)
    if not isinstance(data, dict) or not data.get("url"):
        raise SystemExit(f"import {ticket_id} has no poll url: {data}")
    closed = "closed" if ticket_id in main_notes else "open"
    return data["url"], closed


def finish_import(gh: GitHub, ticket_id: str, poll: str, closed: str) -> dict[str, str] | None:
    """Return the map row, or None when GitHub is still working on it."""
    _status, row = gh.request("GET", poll, accept=PREVIEW)
    if not isinstance(row, dict):
        return None
    state = row.get("status")
    if state in (None, "pending"):
        return None
    if state == "failed":
        raise SystemExit(f"import {ticket_id} failed: {row.get('errors') or row}")
    if state != "imported":
        return None
    issue_url = row.get("issue_url") or ""
    number = issue_url.rstrip("/").rsplit("/", 1)[-1]
    if not number.isdigit():
        raise SystemExit(f"import {ticket_id} missing issue number in {issue_url}")
    return {"id": ticket_id, "number": number, "state": closed}


def close_merged(gh: GitHub, mapped: dict[str, dict[str, str]],
                 main_notes: dict[str, str]) -> int:
    closed = 0
    for ticket_id, note in main_notes.items():
        row = mapped.get(ticket_id)
        if row is None or row.get("state") == "closed":
            continue
        number = row["number"]
        gh.request("POST", f"/repos/{REPO}/issues/{number}/comments", {
            "body": close_comment(ticket_id, note),
        })
        gh.request("PATCH", f"/repos/{REPO}/issues/{number}", {
            "state": "closed",
            "state_reason": "completed",
        })
        row["state"] = "closed"
        closed += 1
        print(f"closed #{number} for Haiku {ticket_id}", flush=True)
    return closed


def note_upstream_gone(gh: GitHub, mapped: dict[str, dict[str, str]],
                       haiku: dict[str, dict[str, str]]) -> int:
    """One comment when Trac dropped a ticket we still have open."""
    noted = 0
    for ticket_id, row in mapped.items():
        if ticket_id in haiku or row.get("state") == "closed":
            continue
        number = row["number"]
        _status, comments = gh.request(
            "GET", f"/repos/{REPO}/issues/{number}/comments?per_page=100")
        blob = ""
        if isinstance(comments, list):
            blob = "\n".join((item.get("body") or "") for item in comments)
        if "r5-sync:upstream-closed" in blob:
            continue
        gh.request("POST", f"/repos/{REPO}/issues/{number}/comments", {
            "body": "\n".join([
                "Haiku no longer lists this ticket in the open snapshot.",
                "It stays open here until someone closes it. GitHub is the lead.",
                "",
                "<!-- r5-sync:upstream-closed -->",
            ]),
        })
        noted += 1
        print(f"upstream closed, left open #{number} Haiku {ticket_id}", flush=True)
    return noted


def github_leads(mapped: dict[str, dict[str, str]], r5: dict[str, dict[str, str]]) -> int:
    """A closed GitHub issue wins over an open local row."""
    changed = 0
    for ticket_id, row in mapped.items():
        if row.get("state") != "closed":
            continue
        local = r5.get(ticket_id)
        if local is None or local["r5_status"] in ("fixed", "wontfix", "not-applicable"):
            continue
        local["r5_status"] = "fixed"
        extra = f"Closed on GitHub #{row['number']}."
        if extra not in local["notes"]:
            local["notes"] = (local["notes"] + " " + extra).strip()
        changed += 1
    if changed:
        write_r5_status(r5)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--limit", type=int, default=0,
                        help="Import at most N new tickets (0 = all)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    haiku = read_haiku_csv(HAIKU_OPEN)
    r5 = read_r5_status()
    main_notes = fixed_on_main()
    if args.dry_run:
        print(
            f"trac {len(haiku)}  fixed on main {len(main_notes)}"
            f"  build 10019 {len(BUILD_10019)}")
        return 0

    gh = GitHub(gh_token())
    ensure_labels(gh)
    mapped = load_map()
    mapped.update(scan_existing(gh))
    write_map(mapped)

    def sort_key(ticket_id: str) -> tuple:
        return (0, int(ticket_id)) if ticket_id.isdigit() else (1, ticket_id)

    todo = [tid for tid in sorted(haiku, key=sort_key) if tid not in mapped]
    if args.limit:
        todo = todo[: args.limit]
    print(f"importing {len(todo)} (already mapped {len(mapped)})", flush=True)
    # A few imports in flight. GitHub finishes them out of order.
    flight: list[tuple[str, str, str, float]] = []
    done = 0
    started = time.monotonic()

    def pump() -> None:
        nonlocal done
        still: list[tuple[str, str, str, float]] = []
        for ticket_id, poll, closed, began in flight:
            if time.monotonic() - began > 300:
                raise SystemExit(f"import {ticket_id} still pending after 300s")
            row = finish_import(gh, ticket_id, poll, closed)
            if row is None:
                still.append((ticket_id, poll, closed, began))
                continue
            mapped[ticket_id] = row
            done += 1
            if done % 25 == 0 or done == len(todo):
                write_map(mapped)
                elapsed = int(time.monotonic() - started)
                print(f"  {done}/{len(todo)} last #{row['number']} {elapsed}s", flush=True)
        flight[:] = still

    while todo or flight:
        while todo and len(flight) < 6:
            ticket_id = todo.pop(0)
            meta = haiku[ticket_id]
            local = r5.get(ticket_id) or {"r5_status": "open", "notes": ""}
            poll, closed = start_import(gh, ticket_id, meta, local, main_notes)
            flight.append((ticket_id, poll, closed, time.monotonic()))
        pump()
        if flight:
            time.sleep(0.25)
    write_map(mapped)
    n_closed = close_merged(gh, mapped, main_notes)
    write_map(mapped)
    n_upstream = note_upstream_gone(gh, mapped, haiku)
    n_lead = github_leads(mapped, r5)
    print(
        f"mapped {len(mapped)}  newly closed {n_closed}"
        f"  upstream notes {n_upstream}  local rows updated {n_lead}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
