// SPDX-FileCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * Shared Header and Footer Component for R5 Operating System Website.
 * Ensures 100% pixel-perfect consistency across all website pages.
 */
(function () {
  function getActivePage() {
    if (document.body && document.body.dataset && document.body.dataset.page) {
      return document.body.dataset.page;
    }
    const path = (window.location.pathname || "").toLowerCase();
    if (path.includes("wallpaper")) return "wallpapers";
    if (path.includes("prs") || path.includes("pull")) return "prs";
    if (path.includes("brand")) return "brand-guide";
    return "home";
  }

  const active = getActivePage();

  const navHtml = `
    <a href="index.html" class="brand-lockup">
      <svg viewBox="0 0 100 100" width="28" height="28">
        <defs>
          <linearGradient id="r5-shared-nav-grad" x1="12%" y1="12%" x2="88%" y2="88%">
            <stop offset="0%" stop-color="#0084ff"/>
            <stop offset="45%" stop-color="#3898ff"/>
            <stop offset="72%" stop-color="#65b891"/>
            <stop offset="100%" stop-color="#ffb800"/>
          </linearGradient>
        </defs>
        <path d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z
                 M54,24 C42,24 37,32 43,42 C49,51 46,62 35,66 C26,69 27,77 41,77 C64,77 77,64 77,48 C77,33 66,24 54,24 Z"
              fill="url(#r5-shared-nav-grad)" fill-rule="evenodd"/>
        <path d="M22,29 C30,17 44,12 58,14"
              fill="none" stroke="#ffffff" stroke-width="2.5" stroke-linecap="round" opacity="0.38"/>
      </svg>
      <div class="brand-lockup-text"><strong>R5</strong><span>Operating System</span></div>
    </a>

    <nav class="nav-links">
      <a href="index.html#overview" class="text-link ${active === "home" ? "active" : ""}">Overview</a>
      <a href="index.html#beyond-haiku" class="text-link">Features</a>
      <a href="prs.html" class="text-link ${active === "prs" ? "active" : ""}">Pull Requests</a>
      <a href="wallpapers.html" class="text-link ${active === "wallpapers" ? "active" : ""}">Wallpapers</a>
      <a href="brand-guide.html" class="text-link ${active === "brand-guide" ? "active" : ""}">Brand Guide</a>
      <a href="index.html#download" class="nav-pill">Download</a>
    </nav>
  `;

  const footerHtml = `
    <div><strong>R5 Operating System</strong> &nbsp;&bull;&nbsp; Version 2026.1-dev Build 10049</div>
    <div>
      <a href="index.html">Home</a>
      <a href="prs.html">Pull Requests</a>
      <a href="wallpapers.html">Wallpapers</a>
      <a href="brand-guide.html">Visual Brand Guide</a>
      <a href="https://github.com/r5-operating-system/r5os" target="_blank" rel="noopener">GitHub</a>
    </div>
  `;

  function renderSharedElements() {
    const headerEl = document.querySelector("header.site-nav");
    if (headerEl) {
      headerEl.innerHTML = navHtml;
    }
    const footerEl = document.querySelector("footer.site-footer");
    if (footerEl) {
      footerEl.innerHTML = footerHtml;
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", renderSharedElements);
  } else {
    renderSharedElements();
  }
})();
