# R5Book

The developer guide to what R5 adds to Haiku for programs: LibR5, the MIDI 2 and Wi-Fi kits, Vulkan in a window, Settings modules and the additions to Haiku kits, in the layout of the Be Book and the look of the R5 website. Open `index.html` in a browser. It is static HTML: `r5book.css` for the look, `r5book.js` for the top bar, the contents and the footer, which are the same on every page.

| Page | Covers | Source of truth |
|---|---|---|
| `index.html` | What LibR5 is, linking, the rules | `docs/LIBR5.md`, `AGENTS.md` |
| `icons-kit.html` | `libr5icons.so`: predefined icons, icon bitmaps, icon buttons, line glyphs | `distro/icons/R5Icons.h`, `docs/ICONS.md` |
| `appearance-kit.html` | `libr5ui.so`: every `BAppearanceKit` class and the `R5UI.h` functions | `distro/ui/*.h`, `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md` |
| `midi2-kit.html` | MIDI 2.0 in `libmidi2.so` | `distro/media/headers/os/midi2`, `docs/MIDI2.md` |
| `wifi-kit.html` | `libr5wifi.so`: the Wi-Fi panel, its state machine, the Wi-Fi glyphs; one `BNetworkRoster` addition | `distro/wifi/lib/*.h`, `docs/LIBR5.md` |
| `vulkan.html` | Vulkan in a `BView`: `VK_RFIVE_view_surface` | `distro/vulkan/include/vulkan/vulkan_r5.h`, `docs/VULKAN-WSI.md` |
| `accounts.html` | Accounts, sessions, lock and log out; Sign in as an example, with screenshots in `img/` | `docs/MULTIUSER.md`, `distro/login` |
| `settings-modules.html` | A page in Settings as an add-on: `SettingsModule`, `SettingsContext`, entry points | `distro/settings/SettingsModule.h`, `distro/settings/ModuleAPI.md` |
| `haiku-additions.html` | Public calls R5 added to Haiku classes and modules (Locale Kit, USB module), and the decorator hooks for decorator add-ons | the `SPDX-Snippet` parts of the public headers in `distro/` |
| `guidelines.html` | How an R5 window is put together | `docs/STYLE.md`, `docs/SETTINGS.md`, `AGENTS.md` |

## Pictures

`img/` holds the screenshots from R5 (Sign in and the lock screen, QEMU, 11 Nov 2026; the Wi-Fi page). A screenshot carries the label "Screenshot". The rest of this edition draws each control as an HTML mockup with the R5 system colors at the 12 point system font. Each picture carries a label: "Mockup", or "Stand-ins" where a Haiku HVIF icon cannot be drawn in HTML yet. The glyph pictures use the same geometry as `glyph_paint()` in `R5Icons.cpp`.

The next step replaces the mockups with screenshots taken from R5 in QEMU, one per control, by a small gallery program and a script. Until then a mockup can differ from R5 in small details; the header and `docs/LIBR5.md` are the contract.

## Keeping it current

A change that adds or changes a public call in `libr5icons.so`, `libr5ui.so`, `libr5wifi.so`, the MIDI 2 headers, `vulkan_r5.h`, `SettingsModule.h`, or a public Haiku header in `distro/` updates the matching page in the same change: the entry, its members, and its picture. A new class gets an entry with the facts box (declared in, derived from, first user), what it is for and not for, its members, and an example. Add it to the contents in `r5book.js`. On a kit page (Icons Kit, Appearance Kit, Haiku kits) the entries and their contents list are in alphabetical order, Overview first. The guides (MIDI 2, Wi-Fi, Vulkan, Accounts, Settings modules, Guidelines) keep their reading order.
