#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the screenshots showcase page (`screenshots.html`) on Desktop."""
from __future__ import annotations

import os
import pathlib
from PIL import Image

DESKTOP_DIR = pathlib.Path(r"C:\Users\nickr\Desktop\r5-website")
SCREENS_DIR = DESKTOP_DIR / "screens"
OUTPUT_HTML = DESKTOP_DIR / "screenshots.html"
SITE_NAV_JS = DESKTOP_DIR / "site-nav.js"

# Known curated metadata for recognized screenshots
SCREEN_METADATA = {
    "bootscreen.png": {
        "title": "Clean White Boot Screen",
        "tag": "Boot & Kernel",
        "tag_color": "#0084ff",
        "desc": (
            "The new white boot sequence featuring the R5 Wave Pebble brand lockup "
            "and seven progressive boot stage progress icons, cleanly rendered against a pure white background."
        ),
    },
    "about_r5.png": {
        "title": "About R5 System Window",
        "tag": "System Identity",
        "tag_color": "#ffb800",
        "desc": (
            "The unified About R5 window showcasing OS release info, kernel build version, "
            "hardware specs (Ryzen 5 5625U), and interactive tabs for What's New and Licensing."
        ),
    },
    "desktop.png": {
        "title": "R5 Desktop Environment",
        "tag": "Desktop Experience",
        "tag_color": "#65b891",
        "desc": (
            "The clean, responsive R5 desktop featuring authentic BeOS styling, the streamlined Deskbar, "
            "and active Tracker workspaces."
        ),
    },
    "settings_r5.png": {
        "title": "Unified Settings Application",
        "tag": "Desktop & Preferences",
        "tag_color": "#0084ff",
        "desc": (
            "The redesigned R5 Settings window — featuring structured category cards, "
            "an alphabetical folding navigation sidebar, and instant-apply preferences without rebooting."
        ),
    },
    "settings.png": {
        "title": "Unified Settings Application",
        "tag": "Desktop & Preferences",
        "tag_color": "#0084ff",
        "desc": (
            "The redesigned R5 Settings window — featuring structured category cards, "
            "an alphabetical folding navigation sidebar, and instant-apply preferences without rebooting."
        ),
    },
    "settings_display.png": {
        "title": "Display & Workspace Settings",
        "tag": "System & Hardware",
        "tag_color": "#8b5cf6",
        "desc": (
            "Interactive display resolution, refresh rate, and multi-workspace layout controls "
            "with live preview and 12-second confirmation fallback."
        ),
    },
    "settings_systeminfo.png": {
        "title": "System Information Panel",
        "tag": "System Diagnostics",
        "tag_color": "#3898ff",
        "desc": (
            "Comprehensive hardware and kernel diagnostics in the Settings app, "
            "including CPU topology, memory layout, and driver status."
        ),
    },
}

PREFERRED_ORDER = [
    "bootscreen.png",
    "about_r5.png",
    "desktop.png",
    "settings_r5.png",
    "settings.png",
    "settings_display.png",
    "settings_systeminfo.png",
]


def screenshot_sort_key(fname: str) -> tuple[int, int, str]:
    lower = fname.lower()
    for idx, pref in enumerate(PREFERRED_ORDER):
        if lower == pref or lower.startswith(pref.split('.')[0]):
            return (0, idx, lower)
    return (1, 999, lower)


def update_site_nav():
    """Ensure site-nav.js contains the Screenshots link."""
    if not SITE_NAV_JS.is_file():
        return
    text = SITE_NAV_JS.read_text(encoding="utf-8")

    # Update active page detector
    if "screenshots" not in text:
        text = text.replace(
            "if (path.includes(\"wallpaper\")) return \"wallpapers\";",
            "if (path.includes(\"wallpaper\")) return \"wallpapers\";\n    if (path.includes(\"screen\")) return \"screenshots\";",
        )

    # Update nav links
    if 'href="screenshots.html"' not in text:
        target = '<a href="prs.html"'
        replacement = '<a href="screenshots.html" class="text-link ${active === "screenshots" ? "active" : ""}">Screenshots</a>\n      <a href="prs.html"'
        text = text.replace(target, replacement, 1)

    # Update footer links
    if '<a href="screenshots.html">Screenshots</a>' not in text:
        target_f = '<a href="prs.html">Pull Requests</a>'
        replacement_f = '<a href="screenshots.html">Screenshots</a>\n      <a href="prs.html">Pull Requests</a>'
        text = text.replace(target_f, replacement_f, 1)

    SITE_NAV_JS.write_text(text, encoding="utf-8")
    print(f"Updated {SITE_NAV_JS}")


def collect_screenshots() -> list[dict[str, str]]:
    """Scan screens/ directory for PNG, JPG, and WEBP images."""
    if not SCREENS_DIR.is_dir():
        return []

    items = []
    for fname in sorted(os.listdir(SCREENS_DIR), key=screenshot_sort_key):
        fpath = SCREENS_DIR / fname
        if not fpath.is_file():
            continue
        ext = fpath.suffix.lower()
        if ext not in (".png", ".jpg", ".jpeg", ".webp"):
            continue

        try:
            with Image.open(fpath) as img:
                w, h = img.size
        except Exception:
            w, h = 0, 0

        lower_name = fname.lower()
        meta = SCREEN_METADATA.get(lower_name, {})
        title = meta.get("title", fpath.stem.replace("-", " ").replace("_", " ").title())
        tag = meta.get("tag", "System Showcase")
        tag_color = meta.get("tag_color", "#3898ff")
        desc = meta.get(
            "desc",
            f"High-resolution screenshot from R5 Operating System ({w} × {h} pixels)."
        )

        items.append(
            {
                "filename": fname,
                "rel_path": f"screens/{fname}",
                "title": title,
                "tag": tag,
                "tag_color": tag_color,
                "desc": desc,
                "width": w,
                "height": h,
                "size_kb": fpath.stat().st_size // 1024,
            }
        )
    return items


def render_html(screenshots: list[dict[str, str]]) -> str:
    cards_html = []
    for s in screenshots:
        card = f"""
        <article class="shot-card">
          <div class="shot-preview-wrap" onclick="openLightbox('{s['rel_path']}', '{s['title']}')">
            <img src="{s['rel_path']}" alt="{s['title']}" loading="lazy" />
            <div class="shot-overlay">
              <span class="zoom-pill">
                <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
                  <circle cx="11" cy="11" r="8"></circle>
                  <line x1="21" y1="21" x2="16.65" y2="16.65"></line>
                  <line x1="11" y1="8" x2="11" y2="14"></line>
                  <line x1="8" y1="11" x2="14" y2="11"></line>
                </svg>
                View Fullscreen
              </span>
            </div>
          </div>
          <div class="shot-info">
            <div class="shot-meta-top">
              <span class="shot-tag" style="color: {s['tag_color']}; background: {s['tag_color']}14; border-color: {s['tag_color']}30;">{s['tag']}</span>
              <span class="shot-dimensions mono">{s['width']} &times; {s['height']} &bull; {s['size_kb']} KB</span>
            </div>
            <h2 class="shot-title">{s['title']}</h2>
            <p class="shot-desc">{s['desc']}</p>
            <div class="shot-actions">
              <button type="button" class="btn-action primary" onclick="openLightbox('{s['rel_path']}', '{s['title']}')">Expand Preview</button>
              <a href="{s['rel_path']}" target="_blank" download class="btn-action secondary">Download Image</a>
            </div>
          </div>
        </article>
        """
        cards_html.append(card)

    cards_joined = "\n".join(cards_html)

    return f"""<!DOCTYPE html>
<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: GPL-3.0-or-later
-->
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Screenshots — R5 Operating System</title>
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=Manrope:wght@400;500;600;700;800&display=swap" rel="stylesheet" />

  <style>
    :root {{
      --r5-blue: #0084ff;
      --r5-sky: #3898ff;
      --r5-seafoam: #65b891;
      --r5-gold: #ffb800;
      --slate-900: #0f172a;
      --slate-700: #334155;
      --slate-600: #475569;
      --slate-500: #64748b;
      --slate-400: #94a3b8;
      --slate-200: #e2e8f0;
      --slate-100: #f1f5f9;
      --bg-page: #fcfdfe;
      --bg-card: #ffffff;
      --border-card: #e2e8f0;
    }}

    * {{
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }}

    html {{
      scroll-behavior: smooth;
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
      font-family: 'JetBrains Mono', ui-monospace, monospace;
    }}

    .container {{
      max-width: 1160px;
      margin: 0 auto;
      padding: 0 32px;
    }}

    /* --- Header & Navigation --- */
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

    .brand-lockup-text {{
      font-size: 17px;
      letter-spacing: -0.015em;
    }}

    .brand-lockup-text strong {{
      font-weight: 700;
    }}

    .brand-lockup-text span {{
      font-weight: 400;
      color: var(--slate-500);
      margin-left: 3px;
    }}

    nav.nav-links {{
      display: flex;
      align-items: center;
      gap: 32px;
    }}

    nav.nav-links a.text-link {{
      text-decoration: none;
      font-size: 13.5px;
      font-weight: 600;
      color: var(--slate-600);
      transition: color 0.15s ease;
    }}

    nav.nav-links a.text-link:hover,
    nav.nav-links a.text-link.active {{
      color: var(--r5-blue);
    }}

    a.nav-pill {{
      display: inline-flex;
      align-items: center;
      padding: 7px 16px;
      border-radius: 999px;
      background-color: var(--slate-900);
      color: #ffffff;
      font-size: 13px;
      font-weight: 600;
      text-decoration: none;
      transition: background-color 0.15s ease;
    }}

    a.nav-pill:hover {{
      background-color: var(--slate-700);
    }}

    /* --- Hero Section --- */
    .hero {{
      text-align: center;
      padding: 36px 0 54px;
    }}

    .hero-jewel {{
      width: 72px;
      height: 72px;
      margin: 0 auto 24px;
      filter: drop-shadow(0 12px 24px rgba(0, 132, 255, 0.22));
    }}

    .hero h1 {{
      font-size: clamp(34px, 4.5vw, 48px);
      font-weight: 800;
      letter-spacing: -0.03em;
      color: var(--slate-900);
      margin-bottom: 14px;
      line-height: 1.15;
    }}

    .hero p.subtitle {{
      font-size: clamp(15px, 1.6vw, 17.5px);
      font-weight: 400;
      color: var(--slate-500);
      max-width: 640px;
      margin: 0 auto 28px;
    }}

    .hero-stats-bar {{
      display: inline-flex;
      align-items: center;
      gap: 16px;
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

    /* --- Screenshot Showcase Grid --- */
    .shots-grid {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(480px, 1fr));
      gap: 36px;
      padding-bottom: 80px;
    }}

    .shot-card {{
      background: var(--bg-card);
      border: 1px solid var(--border-card);
      border-radius: 16px;
      overflow: hidden;
      box-shadow: 0 4px 18px rgba(15, 23, 42, 0.04);
      display: flex;
      flex-direction: column;
      transition: transform 0.2s ease, box-shadow 0.2s ease;
    }}

    .shot-card:hover {{
      transform: translateY(-3px);
      box-shadow: 0 12px 32px rgba(15, 23, 42, 0.08);
    }}

    .shot-preview-wrap {{
      position: relative;
      background: #f8fafc;
      border-bottom: 1px solid var(--border-card);
      overflow: hidden;
      cursor: pointer;
      aspect-ratio: 16 / 11;
      display: flex;
      align-items: center;
      justify-content: center;
    }}

    .shot-preview-wrap img {{
      width: 100%;
      height: 100%;
      object-fit: cover;
      display: block;
      transition: transform 0.25s ease;
    }}

    .shot-card:hover .shot-preview-wrap img {{
      transform: scale(1.02);
    }}

    .shot-overlay {{
      position: absolute;
      inset: 0;
      background: rgba(15, 23, 42, 0.35);
      display: flex;
      align-items: center;
      justify-content: center;
      opacity: 0;
      transition: opacity 0.2s ease;
    }}

    .shot-preview-wrap:hover .shot-overlay {{
      opacity: 1;
    }}

    .zoom-pill {{
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 9px 18px;
      border-radius: 999px;
      background: #ffffff;
      color: var(--slate-900);
      font-size: 13px;
      font-weight: 700;
      box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    }}

    .shot-info {{
      padding: 24px;
      display: flex;
      flex-direction: column;
      flex-grow: 1;
    }}

    .shot-meta-top {{
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      margin-bottom: 12px;
    }}

    .shot-tag {{
      display: inline-block;
      font-size: 11.5px;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.04em;
      padding: 4px 10px;
      border-radius: 999px;
      border: 1px solid;
    }}

    .shot-dimensions {{
      font-size: 12px;
      color: var(--slate-400);
    }}

    h2.shot-title {{
      font-size: 21px;
      font-weight: 800;
      letter-spacing: -0.015em;
      color: var(--slate-900);
      margin-bottom: 8px;
    }}

    p.shot-desc {{
      font-size: 14.5px;
      color: var(--slate-600);
      line-height: 1.6;
      margin-bottom: 20px;
      flex-grow: 1;
    }}

    .shot-actions {{
      display: flex;
      align-items: center;
      gap: 12px;
      flex-wrap: wrap;
    }}

    .btn-action {{
      font-family: inherit;
      font-size: 13px;
      font-weight: 600;
      padding: 8px 16px;
      border-radius: 8px;
      cursor: pointer;
      text-decoration: none;
      transition: all 0.15s ease;
      display: inline-flex;
      align-items: center;
      justify-content: center;
    }}

    .btn-action.primary {{
      background: var(--r5-blue);
      color: #ffffff;
      border: none;
    }}

    .btn-action.primary:hover {{
      background: #0070db;
    }}

    .btn-action.secondary {{
      background: var(--slate-100);
      color: var(--slate-700);
      border: 1px solid var(--slate-200);
    }}

    .btn-action.secondary:hover {{
      background: var(--slate-200);
      color: var(--slate-900);
    }}

    /* --- Lightbox Modal --- */
    .lightbox-modal {{
      position: fixed;
      inset: 0;
      z-index: 9999;
      background: rgba(15, 23, 42, 0.92);
      backdrop-filter: blur(8px);
      display: none;
      align-items: center;
      justify-content: center;
      padding: 24px;
    }}

    .lightbox-modal.active {{
      display: flex;
    }}

    .lightbox-content {{
      max-width: 94vw;
      max-height: 90vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      position: relative;
    }}

    .lightbox-content img {{
      max-width: 100%;
      max-height: 82vh;
      border-radius: 8px;
      box-shadow: 0 20px 50px rgba(0, 0, 0, 0.5);
      object-fit: contain;
    }}

    .lightbox-caption {{
      margin-top: 14px;
      font-size: 14px;
      color: #e2e8f0;
      font-weight: 600;
      text-align: center;
    }}

    .lightbox-close {{
      position: absolute;
      top: -42px;
      right: 0;
      background: transparent;
      border: none;
      color: #ffffff;
      font-size: 32px;
      cursor: pointer;
      line-height: 1;
      padding: 4px;
      opacity: 0.8;
      transition: opacity 0.15s ease;
    }}

    .lightbox-close:hover {{
      opacity: 1;
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
      margin-left: 18px;
      transition: color 0.15s ease;
    }}

    footer.site-footer a:hover {{
      color: var(--r5-blue);
    }}

    @media (max-width: 760px) {{
      .shots-grid {{
        grid-template-columns: 1fr;
      }}
      header.site-nav {{
        flex-direction: column;
        gap: 16px;
      }}
      footer.site-footer {{
        flex-direction: column;
        gap: 12px;
        text-align: center;
      }}
      footer.site-footer a {{
        margin: 0 8px;
      }}
    }}
  </style>
</head>
<body data-page="screenshots">

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
        <a href="screenshots.html" class="text-link active">Screenshots</a>
        <a href="prs.html" class="text-link">Pull Requests</a>
        <a href="wallpapers.html" class="text-link">Wallpapers</a>
        <a href="brand-guide.html" class="text-link">Brand Guide</a>
        <a href="index.html#download" class="nav-pill">Download</a>
      </nav>
    </header>

    <!-- Hero -->
    <section class="hero">
      <div class="hero-jewel">
        <svg viewBox="0 0 100 100" width="72" height="72"><use href="#r5-wave-pebble"/></svg>
      </div>
      <h1>Screenshots.</h1>
      <p class="subtitle">
        A visual look inside R5 Operating System &mdash; from the clean white boot sequence
        to the redesigned Settings window and desktop experience.
      </p>
      <div class="hero-stats-bar">
        <span><strong>{len(screenshots)}</strong> System Screenshots</span>
        <span>&bull;</span>
        <span>Original captures from R5OS</span>
      </div>
    </section>

    <!-- Showcase Grid -->
    <main class="shots-grid">
      {cards_joined}
    </main>

    <!-- Footer -->
    <footer class="site-footer">
      <div><strong>R5 Operating System</strong> &bull; Version 2026.1-dev Build 10049</div>
      <div>
        <a href="index.html">Home</a>
        <a href="screenshots.html">Screenshots</a>
        <a href="prs.html">Pull Requests</a>
        <a href="wallpapers.html">Wallpapers</a>
        <a href="brand-guide.html">Brand Guide</a>
        <a href="https://github.com/nickrusman-bot/r5os" target="_blank" rel="noopener">GitHub</a>
      </div>
    </footer>

  </div>

  <!-- Lightbox Modal -->
  <div id="lightbox" class="lightbox-modal" onclick="closeLightbox(event)">
    <div class="lightbox-content" onclick="event.stopPropagation()">
      <button type="button" class="lightbox-close" onclick="closeLightbox(event)">&times;</button>
      <img id="lightbox-img" src="" alt="Expanded Screenshot" />
      <div id="lightbox-caption" class="lightbox-caption"></div>
    </div>
  </div>

  <!-- Shared Site Navigation JS -->
  <script src="site-nav.js"></script>

  <script>
    function openLightbox(src, title) {{
      document.getElementById('lightbox-img').src = src;
      document.getElementById('lightbox-caption').textContent = title;
      document.getElementById('lightbox').classList.add('active');
      document.body.style.overflow = 'hidden';
    }}

    function closeLightbox(e) {{
      document.getElementById('lightbox').classList.remove('active');
      document.body.style.overflow = '';
    }}

    document.addEventListener('keydown', e => {{
      if (e.key === 'Escape') closeLightbox();
    }});
  </script>
</body>
</html>
"""


def main():
    update_site_nav()
    shots = collect_screenshots()
    print(f"Found {len(shots)} screenshots in {SCREENS_DIR}")
    html_code = render_html(shots)
    OUTPUT_HTML.write_text(html_code, encoding="utf-8")
    print(f"Wrote {OUTPUT_HTML} ({len(html_code):,} bytes)")


if __name__ == "__main__":
    main()
