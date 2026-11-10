#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the paginated R5 Operating System Merged Pull Requests page (`prs.html`)."""
from __future__ import annotations

import datetime
import html
import json
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEBSITE_OUT = ROOT / "docs" / "website" / "prs.html"


def classify_pr(title: str, branch: str, summary: str) -> tuple[str, str]:
    """Return (category_name, accent_color_hex) for a PR."""
    blob = f"{title} {branch} {summary}".lower()
    if any(k in blob for k in ("gpu", "vulkan", "radv", "gfx9", "teapot", "mesa", "radeon", "gart", "drm", "zink")):
        return ("GPU & Vulkan", "#ffb800")
    if any(k in blob for k in ("audio", "midi", "media", "sound", "airplay", "surround", "usb", "hdmi", "latency")):
        return ("Audio & Media", "#3898ff")
    if any(k in blob for k in ("user", "login", "sign in", "lock", "kernel", "boot", "vbox", "virtualbox", "tsc", "hpet", "ipv6", "wifi")):
        return ("System & Multi-User", "#65b891")
    if any(k in blob for k in ("settings", "deskbar", "tracker", "wallpaper", "notification", "display", "appearance", "about", "icon", "brand", "look", "visual")):
        return ("Desktop & Settings", "#0084ff")
    return ("Architecture & Docs", "#3898ff")


def parse_shortstat(stat_str: str) -> tuple[int, int, int]:
    """Return (files_changed, insertions, deletions) from git diff --shortstat."""
    files = ins = dels = 0
    mf = re.search(r"(\d+)\s+files?\s+changed", stat_str)
    mi = re.search(r"(\d+)\s+insertions?\(\+\)", stat_str)
    md = re.search(r"(\d+)\s+deletions?\(-\)", stat_str)
    if mf:
        files = int(mf.group(1))
    if mi:
        ins = int(mi.group(1))
    if md:
        dels = int(md.group(1))
    return files, ins, dels


def clean_body_text(text: str) -> str:
    lines: list[str] = []
    for raw_ln in text.splitlines():
        ln = raw_ln.strip()
        if not ln:
            continue
        low = ln.lower()
        if low.startswith(("signed-off-by:", "co-authored-by:", "change-id:", "reviewed-on:", "reviewed-by:")):
            continue
        # Strip leading markdown bullet
        ln = re.sub(r"^[-*•]\s+", "", ln)
        lines.append(ln)
    return " ".join(lines)


def build_pr_intro(sha: str, title: str) -> tuple[str, int]:
    """Return (2-3 line intro summary, commit_count) for a merge commit."""
    raw_commits = subprocess.check_output(
        [
            "git",
            "log",
            f"{sha}^1..{sha}^2",
            "--first-parent",
            "--no-merges",
            "--reverse",
            "--format=%s%x1f%b%x1e",
        ],
        encoding="utf-8",
        errors="replace",
    )
    commits = [c.strip() for c in raw_commits.split("\x1e") if c.strip()]
    commit_count = max(1, len(commits))

    subjects: list[str] = []
    bodies: list[str] = []
    for c in commits:
        parts = c.split("\x1f")
        s = parts[0].strip()
        b = clean_body_text(parts[1]) if len(parts) > 1 else ""
        if s and s.lower() != title.lower() and s not in subjects:
            subjects.append(s)
        if b and b not in bodies:
            bodies.append(b)

    if bodies:
        # Combine the clearest body sentences up to ~280 chars
        combined = " ".join(bodies)
        combined = re.sub(r"\s+", " ", combined).strip()
        if len(combined) > 290:
            combined = combined[:287].rsplit(" ", 1)[0].rstrip(".,;:") + "…"
        return combined, commit_count

    if subjects:
        joined = " ".join(s if s.endswith(".") else s + "." for s in subjects[:4])
        if len(subjects) > 4:
            joined += f" (+{len(subjects) - 4} more commits)."
        if len(joined) > 290:
            joined = joined[:287].rsplit(" ", 1)[0].rstrip(".,;:") + "…"
        return joined, commit_count

    files = subprocess.check_output(
        ["git", "diff", "--name-only", f"{sha}^1...{sha}^2"],
        encoding="utf-8",
        errors="replace",
    ).strip().splitlines()
    if files:
        shown = ", ".join(files[:3])
        more = f" and {len(files) - 3} more files" if len(files) > 3 else ""
        return f"Updates {shown}{more}.", commit_count
    return title, commit_count


def format_date(iso_date: str) -> str:
    try:
        dt = datetime.date.fromisoformat(iso_date)
        return dt.strftime("%b %d, %Y").replace(" 0", " ")
    except ValueError:
        return iso_date


def parse_iso_timestamp(ts: str) -> float:
    """Parse an ISO-8601 timestamp into a unix float for accurate merge-date sorting."""
    if not ts:
        return 0.0
    try:
        clean = ts.replace("Z", "+00:00")
        dt = datetime.datetime.fromisoformat(clean)
        return dt.timestamp()
    except Exception:
        return 0.0


CACHE_FILE = ROOT / "scripts" / "github-prs-cache.json"


def load_github_prs() -> dict[int, dict[str, str]]:
    """Fetch or load merged PR metadata from GitHub CLI or local cache."""
    prs_by_num: dict[int, dict[str, str]] = {}

    # Try fetching fresh data via gh CLI if available
    try:
        raw_json = subprocess.check_output(
            [
                "gh",
                "pr",
                "list",
                "--repo",
                "r5-operating-system/r5os",
                "--state",
                "merged",
                "--limit",
                "500",
                "--json",
                "number,title,body,mergedAt,url,headRefName",
            ],
            encoding="utf-8",
            errors="replace",
            timeout=15,
        )
        gh_data = json.loads(raw_json)
        CACHE_FILE.write_text(json.dumps(gh_data, indent=2, ensure_ascii=False), encoding="utf-8")
    except Exception:
        # Fall back to existing cached file
        gh_data = []
        if CACHE_FILE.is_file():
            try:
                gh_data = json.loads(CACHE_FILE.read_text(encoding="utf-8"))
            except Exception:
                gh_data = []

    for item in gh_data:
        num = item.get("number")
        if isinstance(num, int):
            prs_by_num[num] = item

    return prs_by_num


def clean_github_body(body: str, title: str) -> str:
    """Format and clean a GitHub PR description into a readable summary."""
    if not body:
        return title
    # Remove HTML comments
    clean = re.sub(r"<!--.*?-->", "", body, flags=re.S)
    # Remove HTML tags
    clean = re.sub(r"<[^>]+>", "", clean)
    # Remove robot / generator footers
    clean = re.sub(r"🤖\s*Generated with.*", "", clean, flags=re.S)

    lines: list[str] = []
    for raw_ln in clean.splitlines():
        ln = raw_ln.strip()
        if not ln:
            continue
        low = ln.lower()
        if any(
            low.startswith(p)
            for p in (
                "signed-off-by:",
                "co-authored-by:",
                "reviewed-by:",
                "do not merge",
                "nothing was copied",
                "host test:",
                "row ",
            )
        ):
            continue
        # Strip leading markdown bullets
        ln = re.sub(r"^[★*•-]\s*", "", ln)
        if ln:
            lines.append(ln)

    summary = " ".join(lines)
    if len(summary) > 280:
        cut = summary[:280]
        last_dot = cut.rfind(".")
        if last_dot > 180:
            summary = cut[: last_dot + 1]
        else:
            summary = cut.rsplit(" ", 1)[0] + "..."
    return summary or title


def collect_prs() -> list[dict[str, object]]:
    github_prs = load_github_prs()
    raw = subprocess.check_output(
        [
            "git",
            "log",
            "origin/main",
            "--grep=Merge pull request #",
            "--format=%H%x1f%cI%x1f%s%x1f%b%x1e",
        ],
        encoding="utf-8",
        errors="replace",
    )
    entries = [e.strip() for e in raw.split("\x1e") if e.strip()]
    prs: list[dict[str, object]] = []
    seen_pr_numbers: set[int] = set()

    for entry in entries:
        parts = entry.split("\x1f")
        sha, committer_iso, subj = parts[0], parts[1], parts[2]
        date_str = committer_iso[:10] if len(committer_iso) >= 10 else ""
        merge_ts = parse_iso_timestamp(committer_iso)
        body_raw = parts[3].strip() if len(parts) > 3 else ""
        title = body_raw.splitlines()[0].strip() if body_raw else subj

        m = re.search(r"Merge pull request #(\d+) from [^\s]+/([^\s]+)", subj)
        pr_num = int(m.group(1)) if m else 0
        branch = m.group(2) if m else ""

        stat_str = subprocess.check_output(
            ["git", "diff", "--shortstat", f"{sha}^1...{sha}^2"],
            encoding="utf-8",
            errors="replace",
        ).strip()
        files, ins, dels = parse_shortstat(stat_str)
        intro, commit_count = build_pr_intro(sha, title)

        # Override title, intro, and exact merge timestamp from GitHub PR metadata if available
        if pr_num in github_prs:
            gh_item = github_prs[pr_num]
            gh_title = gh_item.get("title", "").strip()
            if gh_title:
                title = gh_title
            gh_body = clean_github_body(gh_item.get("body", ""), title)
            if gh_body and gh_body != title:
                intro = gh_body
            if gh_item.get("headRefName"):
                branch = gh_item["headRefName"]
            gh_merged_at = gh_item.get("mergedAt")
            if gh_merged_at:
                gh_ts = parse_iso_timestamp(gh_merged_at)
                if gh_ts > 0:
                    merge_ts = gh_ts
                    date_str = gh_merged_at[:10]

        cat, color = classify_pr(title, branch, intro)

        prs.append(
            {
                "number": pr_num,
                "sha": sha[:10],
                "date": format_date(date_str),
                "iso_date": date_str,
                "merge_ts": merge_ts,
                "branch": branch,
                "title": title,
                "intro": intro,
                "files": files,
                "insertions": ins,
                "deletions": dels,
                "commits": commit_count,
                "category": cat,
                "color": color,
            }
        )
        if pr_num:
            seen_pr_numbers.add(pr_num)

    # Also include any merged PRs from GitHub that may not have appeared in git log yet
    missing_gh = [p for num, p in github_prs.items() if num not in seen_pr_numbers]
    for gh_item in missing_gh:
        pr_num = gh_item["number"]
        title = gh_item.get("title", f"PR #{pr_num}")
        branch = gh_item.get("headRefName", "")
        intro = clean_github_body(gh_item.get("body", ""), title)
        cat, color = classify_pr(title, branch, intro)
        merged_at = gh_item.get("mergedAt", "")
        date_str = merged_at[:10] if merged_at else ""
        merge_ts = parse_iso_timestamp(merged_at)

        prs.append(
            {
                "number": pr_num,
                "sha": "",
                "date": format_date(date_str),
                "iso_date": date_str,
                "merge_ts": merge_ts,
                "branch": branch,
                "title": title,
                "intro": intro,
                "files": 1,
                "insertions": 0,
                "deletions": 0,
                "commits": 1,
                "category": cat,
                "color": color,
            }
        )

    # Sort descending by merge timestamp (most recently merged PR first)
    prs.sort(key=lambda p: (float(p.get("merge_ts") or 0.0), int(p["number"])), reverse=True)
    return prs


def render_html(prs: list[dict[str, object]]) -> str:
    total_prs = len(prs)
    total_ins = sum(int(p["insertions"]) for p in prs)
    total_dels = sum(int(p["deletions"]) for p in prs)
    prs_json = json.dumps(prs, ensure_ascii=False)

    return f"""<!DOCTYPE html>
<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: GPL-3.0-or-later
-->
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>What have we been up to? — R5 Operating System</title>
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@500;600&amp;family=Manrope:wght@400;500;600;700;800&amp;display=swap" rel="stylesheet" />
  <style>
    :root {{
      --r5-blue: #0084ff;
      --r5-blue-hover: #0070db;
      --r5-sky: #3898ff;
      --r5-seafoam: #65b891;
      --r5-gold: #ffb800;
      --slate-900: #0f172a;
      --slate-700: #334155;
      --slate-500: #64748b;
      --slate-400: #94a3b8;
      --slate-200: #e2e8f0;
      --slate-100: #f1f5f9;
      --bg-page: #fcfdfe;
      --green-add: #16a34a;
      --red-del: #dc2626;
    }}

    * {{
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }}

    html {{
      scroll-behavior: smooth;
      overflow-y: scroll;
    }}

    body {{
      font-family: 'Manrope', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      background-color: var(--bg-page);
      color: var(--slate-900);
      line-height: 1.5;
      -webkit-font-smoothing: antialiased;
      overflow-x: hidden;
    }}

    .mono {{
      font-family: 'JetBrains Mono', ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    }}

    .container {{
      max-width: 1120px;
      margin: 0 auto;
      padding: 0 32px;
    }}

    /* --- Whisper-quiet Minimal Navigation --- */
    header.site-nav {{
      padding: 28px 0;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }}

    a.brand-lockup {{
      display: inline-flex;
      align-items: center;
      gap: 11px;
      text-decoration: none;
      color: var(--slate-900);
    }}

    a.brand-lockup svg {{
      width: 28px;
      height: 28px;
      flex-shrink: 0;
    }}

    .brand-lockup-text {{
      font-size: 17px;
      letter-spacing: -0.015em;
    }}

    .brand-lockup-text strong {{
      font-weight: 700;
      color: var(--slate-900);
    }}

    .brand-lockup-text span {{
      font-weight: 400;
      color: var(--slate-500);
      margin-left: 3px;
    }}

    nav.nav-links {{
      display: flex;
      align-items: center;
      gap: 36px;
    }}

    nav.nav-links a.text-link {{
      text-decoration: none;
      font-size: 13.5px;
      font-weight: 500;
      color: var(--slate-500);
      transition: color 0.15s ease;
    }}

    nav.nav-links a.text-link:hover,
    nav.nav-links a.text-link.active {{
      color: var(--slate-900);
    }}

    nav.nav-links a.nav-pill {{
      text-decoration: none;
      font-size: 12.5px;
      font-weight: 700;
      color: #ffffff;
      background-color: var(--slate-900);
      padding: 7px 18px;
      border-radius: 999px;
      transition: transform 0.15s ease, background-color 0.15s ease;
    }}

    nav.nav-links a.nav-pill:hover {{
      background-color: var(--slate-700);
      transform: translateY(-1px);
    }}

    /* --- Centered Serene Hero --- */
    section.hero {{
      text-align: center;
      padding: 36px 0 68px;
    }}

    .hero-jewel {{
      width: 72px;
      height: 72px;
      margin: 0 auto 24px;
      filter: drop-shadow(0 12px 24px rgba(0, 132, 255, 0.22));
    }}

    .hero h1 {{
      font-size: clamp(32px, 4.2vw, 44px);
      font-weight: 800;
      letter-spacing: -0.025em;
      color: var(--slate-900);
      margin-bottom: 12px;
      line-height: 1.15;
    }}

    .hero p.subtitle {{
      font-size: clamp(15px, 1.6vw, 17.5px);
      font-weight: 400;
      color: var(--slate-500);
      max-width: 660px;
      margin: 0 auto 28px;
    }}

    .hero-stats-bar {{
      display: inline-flex;
      align-items: center;
      gap: 18px;
      padding: 8px 20px;
      border-radius: 999px;
      background-color: var(--slate-100);
      font-size: 13px;
      color: var(--slate-700);
    }}

    .hero-stats-bar strong {{
      font-weight: 700;
      color: var(--slate-900);
    }}

    /* --- Filter & Search Bar --- */
    .controls-row {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      flex-wrap: wrap;
      padding-bottom: 24px;
      border-bottom: 1px solid var(--slate-200);
    }}

    .category-pills {{
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
    }}

    .cat-btn {{
      font-family: inherit;
      font-size: 12.5px;
      font-weight: 600;
      color: var(--slate-500);
      background: transparent;
      border: 1px solid transparent;
      padding: 6px 13px;
      border-radius: 999px;
      cursor: pointer;
      transition: all 0.15s ease;
    }}

    .cat-btn:hover {{
      color: var(--slate-900);
      background-color: var(--slate-100);
    }}

    .cat-btn.active {{
      color: #ffffff;
      background-color: var(--slate-900);
    }}

    .search-input {{
      font-family: inherit;
      font-size: 13px;
      color: var(--slate-900);
      background-color: #ffffff;
      border: 1px solid var(--slate-200);
      border-radius: 999px;
      padding: 7px 16px;
      width: 240px;
      outline: none;
      transition: border-color 0.15s ease;
    }}

    .search-input:focus {{
      border-color: var(--r5-blue);
    }}

    /* --- PR Stream (Borderless, Serene Typographic Rows) --- */
    .pr-list {{
      display: flex;
      flex-direction: column;
    }}

    .pr-item {{
      position: relative;
      padding: 28px 0 28px 24px;
      border-bottom: 1px solid var(--slate-200);
    }}

    .pr-accent-bar {{
      position: absolute;
      left: 0;
      top: 31px;
      width: 5px;
      height: 24px;
      border-radius: 3px;
    }}

    .pr-top-row {{
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      gap: 16px;
      flex-wrap: wrap;
      margin-bottom: 6px;
    }}

    .pr-title-group {{
      display: flex;
      align-items: baseline;
      gap: 10px;
      flex-wrap: wrap;
    }}

    a.pr-number {{
      font-family: 'JetBrains Mono', monospace;
      font-size: 13px;
      font-weight: 600;
      color: var(--r5-blue);
      text-decoration: none;
      background: rgba(0, 132, 255, 0.08);
      padding: 2px 8px;
      border-radius: 6px;
      transition: background 0.15s ease;
    }}

    a.pr-number:hover {{
      background: rgba(0, 132, 255, 0.16);
    }}

    h2.pr-title {{
      font-size: 19px;
      font-weight: 700;
      letter-spacing: -0.015em;
      color: var(--slate-900);
      line-height: 1.3;
    }}

    .pr-stats {{
      display: inline-flex;
      align-items: center;
      gap: 10px;
      font-size: 12px;
      flex-shrink: 0;
    }}

    .stat-add {{
      color: var(--green-add);
      font-weight: 600;
    }}

    .stat-del {{
      color: var(--red-del);
      font-weight: 600;
    }}

    .stat-meta {{
      color: var(--slate-400);
      font-size: 12px;
    }}

    p.pr-intro {{
      font-size: 14.5px;
      color: var(--slate-500);
      line-height: 1.6;
      max-width: 820px;
      margin-bottom: 8px;
    }}

    .pr-submeta {{
      display: flex;
      align-items: center;
      gap: 14px;
      font-size: 12px;
      color: var(--slate-400);
    }}

    .pr-cat-label {{
      font-weight: 600;
      color: var(--slate-700);
    }}

    /* --- Pagination Controls --- */
    .pagination-bar {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 36px 0 64px;
      gap: 16px;
      flex-wrap: wrap;
    }}

    .page-info {{
      font-size: 13.5px;
      color: var(--slate-500);
    }}

    .page-buttons {{
      display: flex;
      align-items: center;
      gap: 6px;
      flex-wrap: wrap;
    }}

    .page-btn {{
      font-family: inherit;
      font-size: 13px;
      font-weight: 600;
      min-width: 36px;
      height: 36px;
      padding: 0 12px;
      border-radius: 999px;
      border: 1px solid var(--slate-200);
      background-color: #ffffff;
      color: var(--slate-700);
      cursor: pointer;
      transition: all 0.15s ease;
    }}

    .page-btn:hover:not(:disabled) {{
      border-color: var(--slate-400);
      color: var(--slate-900);
    }}

    .page-btn.active {{
      background-color: var(--r5-blue);
      border-color: var(--r5-blue);
      color: #ffffff;
    }}

    .page-btn:disabled {{
      opacity: 0.4;
      cursor: default;
    }}

    /* --- Minimal Footer --- */
    footer.site-footer {{
      border-top: 1px solid var(--slate-200);
      padding: 28px 0 40px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 13px;
      color: var(--slate-500);
    }}

    footer.site-footer a {{
      color: var(--slate-700);
      text-decoration: none;
      font-weight: 600;
      margin-left: 24px;
    }}

    footer.site-footer a:hover {{
      color: var(--r5-blue);
    }}

    @media (max-width: 760px) {{
      .controls-row {{
        flex-direction: column;
        align-items: stretch;
      }}
      .search-input {{
        width: 100%;
      }}
      nav.nav-links {{
        gap: 16px;
      }}
    }}
  </style>
</head>
<body data-page="prs">

  <!-- Master Inline SVG Definition -->
  <svg width="0" height="0" style="position:absolute">
    <defs>
      <linearGradient id="r5-wave-grad" x1="12%" y1="12%" x2="88%" y2="88%">
        <stop offset="0%" stop-color="#0084ff"/>
        <stop offset="45%" stop-color="#3898ff"/>
        <stop offset="72%" stop-color="#65b891"/>
        <stop offset="100%" stop-color="#ffb800"/>
      </linearGradient>
      <g id="r5-wave-pebble">
        <path d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z
                 M54,24 C42,24 37,32 43,42 C49,51 46,62 35,66 C26,69 27,77 41,77 C64,77 77,64 77,48 C77,33 66,24 54,24 Z"
              fill="url(#r5-wave-grad)" fill-rule="evenodd"/>
        <path d="M22,29 C30,17 44,12 58,14"
              fill="none" stroke="#ffffff" stroke-width="2.5" stroke-linecap="round" opacity="0.38"/>
      </g>
    </defs>
  </svg>

  <div class="container">

    <!-- Top Navigation -->
    <header class="site-nav">
      <a href="index.html" class="brand-lockup">
        <svg viewBox="0 0 100 100"><use href="#r5-wave-pebble"/></svg>
        <div class="brand-lockup-text"><strong>R5</strong><span>Operating System</span></div>
      </a>

      <nav class="nav-links">
        <a href="index.html#overview" class="text-link">Overview</a>
        <a href="index.html#beyond-haiku" class="text-link">Features</a>
        <a href="prs.html" class="text-link active">Pull Requests</a>
        <a href="wallpapers.html" class="text-link">Wallpapers</a>
        <a href="brand-guide.html" class="text-link">Brand Guide</a>
        <a href="index.html#download" class="nav-pill">Download</a>
      </nav>
    </header>

    <!-- Serene Hero -->
    <section class="hero">
      <div class="hero-jewel">
        <svg viewBox="0 0 100 100" width="72" height="72"><use href="#r5-wave-pebble"/></svg>
      </div>
      <h1>What have we been up to?</h1>
      <p class="subtitle">
        Fresh from the workbench. Every fix and feature, in the order it shipped.
      </p>
      <div class="hero-stats-bar">
        <span><strong>{total_prs}</strong> Merged PRs</span>
        <span>&bull;</span>
        <span class="mono" style="color: var(--green-add); font-weight: 600;">+{total_ins:,}</span>
        <span class="mono" style="color: var(--red-del); font-weight: 600;">&minus;{total_dels:,}</span>
        <span>lines across R5</span>
      </div>
    </section>

    <!-- Category Filter Pills & Search -->
    <div class="controls-row">
      <div class="category-pills" id="category-pills">
        <button type="button" class="cat-btn active" data-cat="all">All ({total_prs})</button>
        <button type="button" class="cat-btn" data-cat="Desktop & Settings">Desktop &amp; Settings</button>
        <button type="button" class="cat-btn" data-cat="GPU & Vulkan">GPU &amp; Vulkan</button>
        <button type="button" class="cat-btn" data-cat="Audio & Media">Audio &amp; Media</button>
        <button type="button" class="cat-btn" data-cat="System & Multi-User">System &amp; Multi-User</button>
        <button type="button" class="cat-btn" data-cat="Architecture & Docs">Architecture &amp; Docs</button>
      </div>

      <input
        type="search"
        id="pr-search"
        class="search-input"
        placeholder="Search PR #, title, topic…"
        aria-label="Search pull requests"
      />
    </div>

    <!-- Paginated PR List -->
    <div id="pr-list" class="pr-list"></div>

    <!-- Pagination Footer -->
    <div class="pagination-bar">
      <div class="page-info" id="page-info"></div>
      <div class="page-buttons" id="page-buttons"></div>
    </div>

    <!-- Minimal Footer -->
    <footer class="site-footer">
      <div><strong>R5 Operating System</strong> &nbsp;&bull;&nbsp; Version 2026.1-dev Build 10049</div>
      <div>
        <a href="index.html">Home</a>
        <a href="prs.html">Pull Requests</a>
        <a href="wallpapers.html">Wallpapers</a>
        <a href="brand-guide.html">Visual Brand Guide</a>
        <a href="https://github.com/r5-operating-system/r5os">GitHub</a>
      </div>
    </footer>

  </div>

  <script src="site-nav.js"></script>
  <script>
    const ALL_PRS = {prs_json};
    const PER_PAGE = 15;
    let currentCat = 'all';
    let currentQuery = '';
    let currentPage = 1;

    function escapeHtml(str) {{
      return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
    }}

    function getFilteredPRs() {{
      const q = currentQuery.trim().toLowerCase();
      return ALL_PRS.filter(p => {{
        if (currentCat !== 'all' && p.category !== currentCat) return false;
        if (!q) return true;
        const hay = (`#${{p.number}} ${{p.title}} ${{p.intro}} ${{p.branch}} ${{p.category}}`).toLowerCase();
        return hay.includes(q);
      }});
    }}

    function renderPRs() {{
      const filtered = getFilteredPRs();
      const total = filtered.length;
      const totalPages = Math.max(1, Math.ceil(total / PER_PAGE));
      if (currentPage > totalPages) currentPage = totalPages;

      const startIdx = (currentPage - 1) * PER_PAGE;
      const pageItems = filtered.slice(startIdx, startIdx + PER_PAGE);

      const listEl = document.getElementById('pr-list');
      if (pageItems.length === 0) {{
        listEl.innerHTML = '<div style="padding: 48px 0; text-align: center; color: var(--slate-500);">No pull requests match your filter.</div>';
      }} else {{
        listEl.innerHTML = pageItems.map(p => {{
          const ins = Number(p.insertions).toLocaleString('en-US');
          const dels = Number(p.deletions).toLocaleString('en-US');
          const filesLabel = p.files === 1 ? '1 file' : `${{p.files}} files`;
          const commitsLabel = p.commits === 1 ? '1 commit' : `${{p.commits}} commits`;
          return `
            <article class="pr-item">
              <span class="pr-accent-bar" style="background-color: ${{p.color}};"></span>
              <div class="pr-top-row">
                <div class="pr-title-group">
                  <a class="pr-number" href="https://github.com/r5-operating-system/r5os/pull/${{p.number}}" target="_blank" rel="noopener">#${{p.number}}</a>
                  <h2 class="pr-title">${{escapeHtml(p.title)}}</h2>
                </div>
                <div class="pr-stats mono">
                  <span class="stat-add">+${{ins}}</span>
                  <span class="stat-del">&minus;${{dels}}</span>
                  <span class="stat-meta">&bull; ${{filesLabel}}</span>
                </div>
              </div>
              <p class="pr-intro">${{escapeHtml(p.intro)}}</p>
              <div class="pr-submeta">
                <span class="pr-cat-label">${{escapeHtml(p.category)}}</span>
                <span>&bull;</span>
                <span>Merged ${{escapeHtml(p.date)}}</span>
                <span>&bull;</span>
                <span>${{commitsLabel}}</span>
                <span>&bull;</span>
                <span class="mono">${{escapeHtml(p.branch)}}</span>
              </div>
            </article>
          `;
        }}).join('');
      }}

      const infoEl = document.getElementById('page-info');
      if (total === 0) {{
        infoEl.textContent = 'Showing 0 pull requests';
      }} else {{
        infoEl.textContent = `Showing ${{startIdx + 1}}–${{Math.min(startIdx + PER_PAGE, total)}} of ${{total}} merged pull requests (Page ${{currentPage}} of ${{totalPages}})`;
      }}

      const btnsEl = document.getElementById('page-buttons');
      let btnsHtml = `<button type="button" class="page-btn" ${{currentPage <= 1 ? 'disabled' : ''}} onclick="goToPage(${{currentPage - 1}})">&larr; Prev</button>`;
      for (let i = 1; i <= totalPages; i++) {{
        btnsHtml += `<button type="button" class="page-btn ${{i === currentPage ? 'active' : ''}}" onclick="goToPage(${{i}})">${{i}}</button>`;
      }}
      btnsHtml += `<button type="button" class="page-btn" ${{currentPage >= totalPages ? 'disabled' : ''}} onclick="goToPage(${{currentPage + 1}})">Next &rarr;</button>`;
      btnsEl.innerHTML = btnsHtml;
    }}

    function goToPage(p) {{
      currentPage = p;
      renderPRs();
      window.scrollTo({{ top: 220, behavior: 'smooth' }});
    }}

    document.querySelectorAll('.cat-btn').forEach(btn => {{
      btn.addEventListener('click', () => {{
        document.querySelectorAll('.cat-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentCat = btn.getAttribute('data-cat');
        currentPage = 1;
        renderPRs();
      }});
    }});

    document.getElementById('pr-search').addEventListener('input', ev => {{
      currentQuery = ev.target.value;
      currentPage = 1;
      renderPRs();
    }});

    renderPRs();
  </script>
</body>
</html>
"""


def main() -> int:
    prs = collect_prs()
    html_out = render_html(prs).replace('website.html', 'index.html')
    WEBSITE_OUT.parent.mkdir(parents=True, exist_ok=True)
    WEBSITE_OUT.write_text(html_out, encoding="utf-8")
    print(f"Generated {WEBSITE_OUT} with {len(prs)} merged PRs across {(len(prs) + 14) // 15} pages.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
