# R5 Branding, Logo, Boot Splash, Deskbar Symbol, System Disk Overlay & About R5 Redesign

For Cursor. Follow `AGENTS.md` and `docs/LICENSING.md` at all times.

## Goal

Replace the temporary `R5OS` text wordmarks with the new **R5 Operating System** visual identity (**Concept 1: The Wave Pebble** + **Manrope Bold `R5` / Manrope Regular `Operating System`**), and update the **About R5** window layout so it uses the horizontal header banner and match the boot splash to it.

---

## 1. Visual Specification: Concept 1 ("The Wave Pebble") & Typography

### 1.1 The Abstract Symbol (Concept 1 — Wave Pebble)
An organic, fluid ring (viewBox `0 0 100 100`) whose inner negative space forms an S-wave / abstract `5`, colored with a diagonal gradient from BeOS/R5 Blue (`#0084FF` -> `#3898FF`) through `#65B891` to Haiku-tab Amber/Gold (`#FFB800`):

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <defs>
    <linearGradient id="r5-wave" x1="12%" y1="12%" x2="88%" y2="88%">
      <stop offset="0%" stop-color="#0084ff"/>
      <stop offset="45%" stop-color="#3898ff"/>
      <stop offset="72%" stop-color="#65b891"/>
      <stop offset="100%" stop-color="#ffb800"/>
    </linearGradient>
  </defs>
  <path d="M50,8 C74,8 92,25 92,49 C92,74 73,92 49,92 C25,92 8,75 8,50 C8,24 27,8 50,8 Z
           M54,24 C42,24 37,32 43,42 C49,51 46,62 35,66 C26,69 27,77 41,77 C64,77 77,64 77,48 C77,33 66,24 54,24 Z"
        fill="url(#r5-wave)" fill-rule="evenodd"/>
  <path d="M22,29 C30,17 44,12 58,14"
        fill="none" stroke="#ffffff" stroke-width="2.5" stroke-linecap="round" opacity="0.38"/>
</svg>
```

- Store the canonical vector source in `distro/artwork/r5-symbol.svg` with an SPDX sidecar or comment (`SPDX-FileCopyrightText: 2026 R5OS Contributors`, `SPDX-License-Identifier: GPL-3.0-or-later`).

### 1.2 Typography Rule (`AGENTS.md` § Name)
- Never render `R5OS` in user-facing artwork.
- Font: **Manrope** (`distro/fonts/manrope/`):
  - **`R5`** in **Manrope Bold** (`Manrope-Bold.ttf`);
  - **`Operating System`** in **Manrope Regular** (`Manrope-Regular.ttf`), on the same baseline right after `R5`.

---

## 2. Artwork Assets in `distro/artwork/`

Add a deterministic Python generator `scripts/generate-r5-artwork.py` (using Pillow / `resvg` / `cairosvg` or high-supersampled rasterization + Lanczos downsampling + `distro/fonts/manrope/Manrope-Bold.ttf` and `Manrope-Regular.ttf`) so every bitmap asset is reproducible from `distro/artwork/r5-symbol.svg` and the Manrope TTFs:

1. **Deskbar button icon — `distro/artwork/r5-os-deskbar.png` (symbol only, no text):**
   - The user chose **only the abstract Wave Pebble symbol** in the Deskbar leaf button (no `"R5"` or `"R5OS"` text next to it, just like Haiku shows only the leaf and air/OS shows only its mark).
   - Size: `22x22` (or centered inside `28x22` transparent RGBA PNG so the Deskbar menu title has clean left/right breathing room).
   - Verify in `distro/desktop/src/apps/deskbar/BarMenuBar.cpp` that `TBarMenuTitle` sizes nicely to the symbol width instead of forcing a wide 63px rectangle if not needed.

2. **Installer / horizontal lockup — `distro/artwork/r5-os-logo.png`:**
   - Transparent RGBA PNG showing the **Wave Pebble symbol** on the left + **`R5`** (Manrope Bold, dark slate `#0f172a`) + **`Operating System`** (Manrope Regular, `#334155`) on the right.

3. **Boot Splash — `distro/artwork/r5-os-splash.png` and `distro/artwork/r5-os-splash.bgr` (`372x96`):**
   - Must match the horizontal **About R5** lockup on a pure black (`#000000`) background:
     - Left: the **Wave Pebble symbol** (full color blue-to-gold gradient, approx. `64x64`, vertically centered at `y = 16..80`);
     - Right of the symbol:
       - Top line: **`R5`** (Manrope Bold, white `#FFFFFF`) + **`Operating System`** (Manrope Regular, light slate `#CBD5E1`) on the same baseline;
       - Optional subtle subtitle line below or keep it clean so the boot progress bar beneath the `372x96` splash aligns under the lockup.
   - Regenerate `distro/artwork/r5-os-splash.bgr` (exact `372 * 96 * 3 = 107136` bytes, 24-bit BGR raw) so `scripts/generate_images_sans_tm.py` updates `headers/private/kernel/boot/images-sans-tm.h` at apply time.
   - Remember `AGENTS.md`: **never hardcode a build number in the splash**.

---

## 3. System Disk Icon Overlay (air/OS style)

When Tracker draws the boot/system volume (`BVolume::IsBootVolume()` or device == boot volume on Desktop and in Disks / FilePanels), it should show the **R5 Wave Pebble** as a badge overlay on the system disk icon (just like air/OS badges the boot disk).

### Clean Haiku / R5 Implementation
Compare the two clean native approaches in `distro/desktop/src/kits/tracker/` and pick the cleanest one that works for both Desktop and Tracker windows at any icon size (16, 32, 64, 128 px):
- **Option A (HVIF vector icon attribute / well-known boot volume icon in Tracker):**
  Add the Wave Pebble vector paths on top of Haiku's isometric hard disk vector icon (`R_HardDiskIcon` in `TrackerIcons.rdef`) or write `BEOS:ICON` on `/` at image build time / in `WellKnowEntryList` for the boot volume.
- **Option B (Overlay in `BPose::DrawIcon` or `IconCache::Draw` in `distro/desktop/src/kits/tracker/`):**
  When `resolvedModel->IsVolume()` and its `NodeRef()->device == fs_stat_dev("/boot").dev` (cached once), draw the Wave Pebble badge (`B_OP_ALPHA`) scaled proportionally (`~42%` of `iconSize`) over the upper-right of the disk platter.
- Do **not** open `libtracker.so` from outside Tracker (`AGENTS.md` § Settings window). Keep any Tracker change inside `distro/desktop/src/kits/tracker/` wrapped in `SPDX-Snippet` (`GPL-3.0-or-later`).

---

## 4. Redesigning the `About R5` Window (`distro/rest/src/apps/aboutsystem/AboutSystem.cpp`)

The user specifically requested the **About R5** layout from the brainstorm preview, while strictly preserving all `AGENTS.md` requirements for About:

### 4.1 `AGENTS.md` Contract Checklist for `About R5` (Must Stay Intact!)
1. Window title: **`About R5`**.
2. Does **not** get the preferences wrench (`About R5 is not a settings module and does not get the wrench`).
3. Shows **`Version YYYY.N-dev`** in the header and **`Build N`** in small text under the system information, both from `<r5_os_version.h>` (`R5_OS_VERSION`, `R5_OS_BUILD`) — never hardcoded.
4. **Must keep the version and the build visible while `What's new` (or `Credits` / `Licenses`) is selected.**
5. Keeps the three tabs: **`Credits`**, **`What's new`**, and **`Licenses`** (formatted from `distro/about/licenses.txt` with `# Name` headings and clickable `Source: https://...` links).
6. Keeps the system information readout (Processor, Memory, Kernel build date/time, Time running) and `SysInfoView` replicant/dragger support intact.

### 4.2 The New `About R5` Layout
Currently, `AboutView` puts a narrow `170x110` `LogoView` above `SysInfoView` in a narrow left column, and `fTabView` on the right.

Update `AboutView` and `LogoView` in `distro/rest/src/apps/aboutsystem/AboutSystem.cpp` to a **single-column vertical layout** matching the mockup (no two-column split), and keep `scripts/apply-about.py` in sync or retire its string-patches in favor of the native copy in `distro/rest/src/apps/aboutsystem/AboutSystem.cpp` per `AGENTS.md` § Overlays and native source:

1. **Top Header Banner Card (`HeaderBannerView` / updated `LogoView`, spanning the full width at the top of `AboutView`):**
   - Document-background card (`ui_color(B_DOCUMENT_BACKGROUND_COLOR)`) with a subtle 1px border (`tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT)`) and inner padding (`inset = 14px`).
   - **Left:** Draws the **Wave Pebble** symbol (`56x56` or `64x64`, smooth `B_FILTER_BITMAP_BILINEAR` from a high-res resource `r5_symbol.png` or vector icon).
   - **Right of the symbol (vertical stack):**
     - **Line 1 (Title lockup, drawn with `BFont`):**
       - **`R5`** in `be_bold_font` (Manrope Bold, size `24pt`, `B_DOCUMENT_TEXT_COLOR`) followed immediately on the same baseline by **`Operating System`** in `be_plain_font` (Manrope Regular, size `20pt`, slightly muted `tint_color(textColor, B_LIGHTEN_1_TINT)`).
     - **Line 2 (Version + Arch, always visible across all tabs!):**
       - **`Version <R5_OS_VERSION> • <AMD64|ARM64|RISC-V 64>`** (e.g. `Version 2026.1-dev • AMD64`), in `be_plain_font`. No build number in this line.
2. **Middle Section (`SysInfoView`, spanning the full width directly under the header card):**
   - Full-width system information readout (`Processors:`, `Memory:`, `Kernel:`, `Time running:`).
   - At the bottom, **`Build <R5_OS_BUILD>`** in a smaller face, colored with `B_LIGHTEN_1_TINT`. It stays visible on every tab. A replicant has no header, so it also shows `Version <R5_OS_VERSION> • <arch>` and the same build line. Both strings come from `r5_os_version.h`.
3. **Bottom Section (`fTabView`, spanning the full width directly under `SysInfoView`):**
   - The tabs (`What's new`, `Credits`, `Licenses`) span the full width at the bottom of the window.
4. **Retire fragile `replace_once` blocks in `scripts/apply-about.py`:**
   - Per `AGENTS.md` (*"Older string patches are still in some `apply-*.py` scripts. Do not add more. Retire them by copying the file into `distro/`"*), put the complete updated `AboutSystem.cpp` and `AboutSystem.rdef` directly in `distro/rest/src/apps/aboutsystem/` (with `SPDX-Snippet` tags around all R5 blocks) and let `scripts/apply-about.py` only copy `WHATSNEW`, `licenses.txt`, the symbol/logo resource, and ensure the `Jamfile` dependencies exist.

---

## 5. Verification & Delivery Steps

1. Run `python3 scripts/generate-r5-artwork.py` to generate:
   - `distro/artwork/r5-symbol.svg`
   - `distro/artwork/r5-os-deskbar.png` (symbol only)
   - `distro/artwork/r5-os-logo.png` (Wave Pebble + Manrope Bold `R5` + Manrope Regular `Operating System`)
   - `distro/artwork/r5-os-splash.png` & `distro/artwork/r5-os-splash.bgr` (`372x96`, matching the About header lockup on black)
2. Bump `distro/BUILD` from `10048` to `10049` and prepend a new top section to `distro/WHATSNEW` (in user-facing language, no filenames or ticket numbers, per `AGENTS.md` § Versioning):
   - Heading: `R5 2026.1-dev Build 10049 - Visual Identity`
   - Body describing the new R5 icon, Deskbar button, boot screen, and About R5 window.
3. Apply overlays in WSL (`bash scripts/apply-overlay.sh`).
4. Build AMD64 image (`bash scripts/jam-r5-raw.sh`).
5. Boot in QEMU (`scripts/run-qemu-r5.sh`) and verify:
   - Boot splash shows the Wave Pebble + `R5 Operating System` lockup cleanly;
   - Deskbar top-right button shows only the Wave Pebble symbol;
   - Desktop boot volume (`R5`) shows the System Disk icon with the Wave Pebble overlay;
   - Opening **About R5** from the Deskbar menu shows the new full-width header banner (`Wave Pebble` + **R5** Bold + `Operating System` Regular + `Version ... Build ...`), `SysInfoView` full-width below it, and the tabs full-width at the bottom, keeping version+build visible when switching between `Credits`, `What's new`, and `Licenses`, with no text clipping.
6. Update `CHANGELOG.md` and `docs/LICENSING.md` if any new artwork file paths are added. Report **Veranderd / Getest / Onbewezen / Documentatie`.
