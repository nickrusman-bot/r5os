# R5 Operating System — Visual brand guide

This is the visual brand guide for **R5 Operating System** (short **R5**). It describes the mark, the colors, the type, and the files the generator writes.

The product name in pictures and in text is **R5 Operating System** or **R5**. The repository name is not a logo.

The only drawing of the mark is [`../distro/artwork/r5-symbol.svg`](../distro/artwork/r5-symbol.svg). [`../scripts/generate-r5-artwork.py`](../scripts/generate-r5-artwork.py) turns that file into the pictures. [`../distro/artwork/brand-kit/`](../distro/artwork/brand-kit/) is the generated kit. Do not edit the PNGs by hand.

## 1. The mark

The mark is one pebble with a wave cut out of it.

1. The outer shape is a smooth pebble. It stays recognizable at the Deskbar size and as a badge on the boot volume.
2. The inner shape is the wave. On the full mark it is the long S. At 16 and 22 pixels the same SVG uses the path `r5-small`, a thicker and simpler wave.
3. The color runs on a diagonal from 12%, 12% to 88%, 88%.

## 2. Colors

| Role | Name | Hex | RGB | Where |
|---|---|---|---|---|
| Start (0%) | Signal Blue | `#0084FF` | `rgb(0, 132, 255)` | Upper left of the mark |
| Mid (45%) | Sky Blue | `#3898FF` | `rgb(56, 152, 255)` | Left middle of the mark |
| Mid (72%) | Seafoam | `#65B891` | `rgb(101, 184, 145)` | Between blue and amber |
| End (100%) | Amber | `#FFB800` | `rgb(255, 184, 0)` | Lower right of the mark |
| Wallpaper light | Horizon Ice | `#BFD7FF` | `rgb(191, 215, 255)` | Top band of the wallpaper |
| Wallpaper mid | Horizon Sky | `#9BC0FA` | `rgb(155, 192, 250)` | Middle band of the wallpaper |
| Wallpaper deep | Horizon Cobalt | `#6F9CE6` | `rgb(111, 156, 230)` | Lower band of the wallpaper |
| Text on a light ground | Deep Slate | `#0F172A` | `rgb(15, 23, 42)` | **R5**, including the boot splash |
| Secondary text on a light ground | Muted Slate | `#334155` | `rgb(51, 65, 85)` | Operating System, including the boot splash |
| Text on a dark ground | White | `#FFFFFF` | `rgb(255, 255, 255)` | A dark ground |
| Secondary text on a dark ground | Cool Silver | `#CBD5E1` | `rgb(203, 213, 225)` | A dark ground |

## 3. Type

R5 uses Manrope (`../distro/fonts/manrope/`, OFL-1.1) for the words in the lockup.

- **R5** is Manrope Bold.
- **Operating System** is Manrope Regular, on the same baseline, at about 82–84% of the R5 size.
- The pebble is about twice the cap height of **R5**, centered on that line.

Product pictures do not contain a version or a build number. Example cards in the brand kit read `../distro/VERSION` and `../distro/BUILD` and are labeled `example`.

About R5 shows `Version YYYY.N-dev • architecture` in the header. `Build N` is smaller gray text under the system information. Both come from `r5_os_version.h` and stay visible on every tab, including What's new.

## 4. What the system uses

The image installs only these:

| File | Use |
|---|---|
| [`../distro/artwork/r5-os-deskbar.png`](../distro/artwork/r5-os-deskbar.png) | Deskbar button. The 16 pixel small pebble, centered in 28×22. |
| [`../distro/artwork/r5-os-logo.png`](../distro/artwork/r5-os-logo.png) | Horizontal lockup on a transparent ground. |
| [`../distro/artwork/r5-os-splash.png`](../distro/artwork/r5-os-splash.png) and [`../distro/artwork/r5-os-splash.bgr`](../distro/artwork/r5-os-splash.bgr) | Boot splash, 372×96, background `#FFFFFF`. Dark text, no border. |
| [`../distro/artwork/splash_icons.png`](../distro/artwork/splash_icons.png) | Boot progress, Haiku's 308×80 icons on white, with the R5 mark on the disk. |
| [`../distro/artwork/r5-symbol.png`](../distro/artwork/r5-symbol.png) | The mark in About R5. |
| [`../distro/desktop/src/kits/tracker/r5_disk_badge.h`](../distro/desktop/src/kits/tracker/r5_disk_badge.h) | The same mark as bytes, drawn on the boot volume. |

Previews (`r5-about-window-preview.png`, the lockup cards, and the system-disk mockups) stay in the brand kit. They are not installed.

`python3 scripts/generate-r5-artwork.py --check` rebuilds the product files in a temporary directory and compares hashes. `scripts/apply-overlay.sh` runs that check. Pillow 13.0.0 is required. The PNGs do not store a time.

## 5. Brand kit

| File | Size | Use |
|---|---|---|
| [`../distro/artwork/brand-kit/r5-symbol.svg`](../distro/artwork/brand-kit/r5-symbol.svg) | SVG | Copy of the source, including `r5-full` and `r5-small` |
| [`../distro/artwork/brand-kit/r5-symbol-1024.png`](../distro/artwork/brand-kit/r5-symbol-1024.png) | 1024 | Large mark |
| [`../distro/artwork/brand-kit/r5-symbol-512.png`](../distro/artwork/brand-kit/r5-symbol-512.png) | 512 | Standard mark |
| [`../distro/artwork/brand-kit/r5-symbol-256.png`](../distro/artwork/brand-kit/r5-symbol-256.png) | 256 | Same picture as the About symbol |
| [`../distro/artwork/brand-kit/r5-symbol-128.png`](../distro/artwork/brand-kit/r5-symbol-128.png) | 128 | Medium mark |
| [`../distro/artwork/brand-kit/r5-symbol-64.png`](../distro/artwork/brand-kit/r5-symbol-64.png) | 64 | Small full mark |
| [`../distro/artwork/brand-kit/r5-symbol-32.png`](../distro/artwork/brand-kit/r5-symbol-32.png) | 32 | Full mark |
| [`../distro/artwork/brand-kit/r5-symbol-22.png`](../distro/artwork/brand-kit/r5-symbol-22.png) | 22 | Thick wave |
| [`../distro/artwork/brand-kit/r5-symbol-16.png`](../distro/artwork/brand-kit/r5-symbol-16.png) | 16 | Thick wave, used by the Deskbar |
| [`../distro/artwork/brand-kit/r5-symbol-mono-white.png`](../distro/artwork/brand-kit/r5-symbol-mono-white.png) | 512 | White mark |
| [`../distro/artwork/brand-kit/r5-symbol-mono-dark.png`](../distro/artwork/brand-kit/r5-symbol-mono-dark.png) | 512 | Deep Slate mark |
| [`../distro/artwork/brand-kit/r5-os-deskbar.png`](../distro/artwork/brand-kit/r5-os-deskbar.png) | 28×22 | Same as the installed Deskbar picture |
| [`../distro/artwork/brand-kit/r5-os-logo.png`](../distro/artwork/brand-kit/r5-os-logo.png) | lockup | Same as the installed logo |
| [`../distro/artwork/brand-kit/r5-os-splash.png`](../distro/artwork/brand-kit/r5-os-splash.png) | 372×96 | Same as the installed splash |
| [`../distro/artwork/brand-kit/r5-os-splash.bgr`](../distro/artwork/brand-kit/r5-os-splash.bgr) | 107136 bytes | Same as the installed BGR |
| [`../distro/artwork/brand-kit/splash_icons.png`](../distro/artwork/brand-kit/splash_icons.png) | 308×80 | Same as the installed progress strip |
| [`../distro/artwork/brand-kit/r5-lockup-light-card.png`](../distro/artwork/brand-kit/r5-lockup-light-card.png) | example | Preview. Not installed |
| [`../distro/artwork/brand-kit/r5-lockup-dark-card.png`](../distro/artwork/brand-kit/r5-lockup-dark-card.png) | example | Preview. Not installed |
| [`../distro/artwork/brand-kit/r5-about-window-preview.png`](../distro/artwork/brand-kit/r5-about-window-preview.png) | example | Preview. Not installed |
| [`../distro/artwork/brand-kit/r5-system-disk-128.png`](../distro/artwork/brand-kit/r5-system-disk-128.png) | mockup | Preview. Not installed |
| [`../distro/artwork/brand-kit/r5-system-disk-64.png`](../distro/artwork/brand-kit/r5-system-disk-64.png) | mockup | Preview. Not installed |
| [`../distro/artwork/brand-kit/r5-system-disk-32.png`](../distro/artwork/brand-kit/r5-system-disk-32.png) | mockup | Preview. Not installed |

---

## Nederlandse versie

Dit is dezelfde gids. Het Engels hierboven is het hoofddocument.

Het product heet **R5 Operating System**, kort **R5**. De enige tekening van het merk is [`../distro/artwork/r5-symbol.svg`](../distro/artwork/r5-symbol.svg). Het script [`../scripts/generate-r5-artwork.py`](../scripts/generate-r5-artwork.py) maakt de plaatjes. De kleuren heten Signal Blue, Sky Blue, Seafoam en Amber. **R5** staat in Manrope Bold, **Operating System** in Manrope Regular. Productplaatjes hebben geen versie en geen build. Voorbeeldkaarten lezen `../distro/VERSION` en `../distro/BUILD` en dragen het label `example`.

In het image komen alleen de Deskbar-knop, het logo, de splash, de voortgangsiconen, het symbool voor About R5, en de disk-badge. De previews in de brand kit blijven erbuiten. De splash is 372×96 op wit (`#FFFFFF`), met donkere tekst en zonder rand. De voortgangsiconen houden de indeling van Haiku, op wit, met het R5-merk op de schijf. De Deskbar gebruikt de dikkere golf van 16 pixels, verticaal gecentreerd.

About R5 toont in de kop `Version YYYY.N-dev • architectuur`. `Build N` staat kleiner en grijs onder de systeeminformatie. Beide komen uit `r5_os_version.h` en blijven zichtbaar op elk tabblad, ook bij What's new.
