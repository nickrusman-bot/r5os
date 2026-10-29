# R5OS feature / distro-todo

Eigen werk, los van de Haiku-tickettracker in `tracker/`.

## Klaar

- [x] **Versioning**  
  Publiek: **R5OS YYYY.N** (`2026.1`, `2026.2`, …; nieuw jaar → `.1`). Tussen builds: **`YYYY.N-dev`**. Bron: `distro/VERSION` (nu `2026.1-dev`). Buildnummer: `distro/BUILD` (nu `10010`). Jam: `HAIKU_REVISION=r5-<VERSION>`. AboutSystem toont **R5OS 2026.1-dev Build 10010**, geen Haiku-`hrev`. What's new-tab: `distro/WHATSNEW`.

- [x] **Eerste UI-naam**  
  FirstBoot Welcome/Try/Install, Deskbar **About R5OS**, About-disclaimer, volume-label in overlay (`HAIKU_IMAGE_LABEL`). About-logo: `distro/artwork/r5-os-logo.png`.

- [x] **About-versie**  
  AboutSystem toont **R5OS** plus `distro/VERSION` en `distro/BUILD` (niet de git-hash). Jam-revisiebestand wordt door `apply-overlay.sh` overschreven. What's new staat naast Credits.

- [x] **Multi-user (kern, t/m build 10010)**  
  Users & Accounts (lijst/add/disable/delete). Sign in, Log out, `Log out <naam>`. Administrator `user` is uid ≥ 1000, home `/boot/home`. Standaardaccount onder `/boot/home/users/<naam>`, eigen Trash en MIME-db, gele mappen. Eén scherm. Slot op Network, Screen, Time, Users (rechtsonder, knoppen grijs tot unlock). Autologin blijft uid 0; Restart moet Sign in halen via writable `root`-home. Plan: `docs/MULTIUSER.md`. **Niet** `StartSession` voor de aangemelde persoon (tweede `app_server`). **Niet** prefs verbergen per rol.

## Open

- [ ] **Multi-user: afronden**  
  Kern zit erin (build 10010). Volgorde, niet alles in één patch. Details: `docs/MULTIUSER.md`.

  **Eerst bevestigen**
  - Herstart ná de eerste Sign in (migrate) komt weer op Sign in, niet de splash.
  - Log out → Sign in als iemand anders; slot weer dicht na Log out.

  **Daarna, in deze volgorde**
  1. Wachtwoord wijzigen in Users & Accounts (eigen wachtwoord; administrator mag dat van een ander).
  2. Zelfde slot op Printers, Repositories, VirtualMemory; settings naar `B_SYSTEM_SETTINGS` (niet per-user laten staan).
  3. Lock screen: sessie blijft, alleen het scherm op slot.
  4. Guest-account en Public/Shared-map, pas als gewone homes privé blijven.

  **Later / niet tijdens deze lijn**
  - Eén Preferences-venster (R5 Hub).
  - WebPositive-defaults uit `webpositive.hpkg` (nu nog `/boot/home`).
  - Image-`passwd`: `user` al uid ≥ 1000, geen migrate bij eerste Sign in.
  - Add-account-dialoog uitlijnen.
  - Overlays in native bron.

  **Expres niet:** `StartSession` van de aangemelde persoon; prefs verbergen per rol; fast user switching zonder uitloggen tot er één scherm blijft.

- [ ] **Hardware: HP Envy (convertible) — Ryzen 5 5625U + Radeon `15e7`**  
  Referentiemachine voor AMD64, gfx, touch en tablet-mode. Doel: APU **bruikbaar** (niet alleen VESA): panel/eDP, native resolutie, HDMI/DP, backlight, geen KDL, audio, daarna power/sleep. GPU 2D/3D staat apart. Exacte Envy-SKU later in syslog vastleggen.

  **Wat het is:** Barcelo-refresh van Cezanne (Zen 3, 6c/12t, 15 W). iGPU is **Vega 7**, geen Navi/RDNA.

  Bevestigd op deze machine: **PCI `1002:15e7` (Barcelo)** — **niet** in `radeon_hd`. Verwante IDs (niet deze laptop, wel dezelfde driver-tak):
  - `1002:1638` Cezanne — staat in de tabel, maar als `RADEON_NAVI` / “Radeon Renoir” (verkeerde familie; Linux `amdgpu` houdt Renoir/Cezanne/Barcelo bij Vega + DCN 2.x).
  - `1002:164c` Lucienne staat **uit** vanwege crash in `pll_compute()` ([Haiku #17516](https://dev.haiku-os.org/ticket/17516)).

  **Huidige Haiku-stand:** `radeon_hd` is 2D/modeset, geen `amdgpu`. Raven (`15dd`/`15d8`) is de laatste nette APU-tak. Renoir/Cezanne zitten in de Navi-lijst; community-advies voor 5600G-klasse was vaak **radeon_hd blacklisten** (VESA). Verwant: [#17516](https://dev.haiku-os.org/ticket/17516), Vega/DP [#14800](https://dev.haiku-os.org/ticket/14800), [#8626](https://dev.haiku-os.org/ticket/8626), ACPI-sleep [#19487](https://dev.haiku-os.org/ticket/19487), brightness [#8258](https://dev.haiku-os.org/ticket/8258), fans [#19829](https://dev.haiku-os.org/ticket/19829). Audio op Ryzen-laptops gaat vaak via **ACP**, niet klassieke HDA.

  **Aanpak:** `0x15e7` toevoegen als Barcelo/Cezanne-APU (niet Navi). PLL/DCN voor Renoir-klasse (zelfde valkuil als #17516). eDP + backlight. Audio/ACP. Wifi hangt van de NIC in *deze* laptop af (apart). Syslog na eerste boot bewaren.

- [x] **Branding: Deskbar-blad, splash, Installer-logo**  
  Unofficial pad: Deskbar-blad PNG 63×22, boot-splash 372×96, Installer zelfde About-logo. Disclaimer in About blijft. Nog: Welcome-HTML (`haiku-os.org`), Deskbar-app-HVIF in Tracker.

- [ ] **Branding: rest van de strings**  
  Tracker/Welcome en overige zichtbare “Haiku”-teksten. Geen Haiku- of BeOS-merken. Technische paden/API’s later.

- [x] **Dark mode (laaghangend)**  
  [#20008](https://dev.haiku-os.org/ticket/20008) zat al in de tree. Scrollbar-trough [#20009](https://dev.haiku-os.org/ticket/20009) en BeControlLook-contrast [#19836](https://dev.haiku-os.org/ticket/19836) zitten in deze tree. [#18970](https://dev.haiku-os.org/ticket/18970) blijft een verzamelpunt.

- [x] **UI-fruit (layout + desktop)**  
  [#16838](https://dev.haiku-os.org/ticket/16838) list-clip, [#18057](https://dev.haiku-os.org/ticket/18057) scrollbar/resize-knop, [#15690](https://dev.haiku-os.org/ticket/15690) SoftwareUpdater, [#11138](https://dev.haiku-os.org/ticket/11138) Alert `_text`, [#16471](https://dev.haiku-os.org/ticket/16471) selectie op zwart desktop, [#16116](https://dev.haiku-os.org/ticket/16116) desktop 48px als er geen opgeslagen view-state is, [#1006](https://dev.haiku-os.org/ticket/1006) Backgrounds Revert-snapshot.

- [x] **Deskbar**  
  Mute [#20314](https://dev.haiku-os.org/ticket/20314), PowerStatus-maat [#18411](https://dev.haiku-os.org/ticket/18411), Appearance [#11397](https://dev.haiku-os.org/ticket/11397), selected-kleur [#18643](https://dev.haiku-os.org/ticket/18643), auto-hide-lijn [#19704](https://dev.haiku-os.org/ticket/19704), klokbreedte [#14846](https://dev.haiku-os.org/ticket/14846), menu op voorgrond [#16458](https://dev.haiku-os.org/ticket/16458), mini-mode grip [#15792](https://dev.haiku-os.org/ticket/15792)/[#15843](https://dev.haiku-os.org/ticket/15843), settings-file [#12884](https://dev.haiku-os.org/ticket/12884), drop-disabled [#3076](https://dev.haiku-os.org/ticket/3076). Overlay: `apply-deskbar-bugs.py`. Open: meerdere menu’s [#20099](https://dev.haiku-os.org/ticket/20099).

- [x] **Tracker-desktop**  
  Icon-offset/layout [#2372](https://dev.haiku-os.org/ticket/2372)/[#18514](https://dev.haiku-os.org/ticket/18514), overlap na mount/nieuw [#2864](https://dev.haiku-os.org/ticket/2864)/[#8246](https://dev.haiku-os.org/ticket/8246)/[#20363](https://dev.haiku-os.org/ticket/20363), Deskbar-marge [#3291](https://dev.haiku-os.org/ticket/3291), Arrange by [#20419](https://dev.haiku-os.org/ticket/20419), copy-statuskleur [#19070](https://dev.haiku-os.org/ticket/19070), headerkleur [#16678](https://dev.haiku-os.org/ticket/16678), count-view [#17901](https://dev.haiku-os.org/ticket/17901), Size-kolom [#18060](https://dev.haiku-os.org/ticket/18060), kolomlijn [#19084](https://dev.haiku-os.org/ticket/19084). Overlay: `apply-tracker-desktop.py`.

- [x] **UI-shell**  
  Disabled menubar [#16618](https://dev.haiku-os.org/ticket/16618), menu’s bij schermrand [#18300](https://dev.haiku-os.org/ticket/18300), live kleuren Tracker [#15541](https://dev.haiku-os.org/ticket/15541)/[#14991](https://dev.haiku-os.org/ticket/14991) (Tracker+Deskbar; niet elke app). Dark-mode-rest [#18970](https://dev.haiku-os.org/ticket/18970) deels. Overlay: `apply-ui-shell.py`. Open: menubalk-toetsen [#1852](https://dev.haiku-os.org/ticket/1852)/[#9191](https://dev.haiku-os.org/ticket/9191).

- [x] **Kernel-blocker [#20400](https://dev.haiku-os.org/ticket/20400)**  
  Early-boot divide-by-zero in TSC→`system_time` conversie: CPUID/MSR/hypervisor-leaf of PIT met frequentie 0. Guards in `arch_timer.cpp` (`scripts/apply-kernel-bugs.py`). [#18200](https://dev.haiku-os.org/ticket/18200) (swap bij veel RAM) en [#14961](https://dev.haiku-os.org/ticket/14961) (syscall-audit) zijn groter en nog open.

- [ ] **Architecturen: AMD64, ARM64, optioneel RISC-V 64**  
  Beleid: alleen 64-bit. Volgorde: **AMD64** (huidige `x86_64`-image, 5625U) → **ARM64** (eerst QEMU `virt`) → **RISC-V 64** (RV64GC). Geen focus op x86-32, ARM32, PPC, SPARC, m68k; 32-bit-code niet in een drive-by slopen.

- [ ] **Touch + tablet-mode (HP Envy convertible)**  
  Testapparaat: dezelfde Envy (touchscreen + 360°-scharnier). Eersteklas vingerinput, geen muis-emulatie. Haiku heeft HID-digitizer-IDs en een tablet-pad; **multitouch ontbreekt** ([#9545](https://dev.haiku-os.org/ticket/9545)). Touchpads ≠ touchscreen. USB/I2C HID-schermen werken vaak niet ([#7481](https://dev.haiku-os.org/ticket/7481), [#15881](https://dev.haiku-os.org/ticket/15881)); Envy’s paneel is bijna zeker **I2C-HID**.

  Convertible extra: rotatie/ACPI-hinge, scherm 0/90/180/270, in tablet-mode toetsenbord+touchpad uit. Dat is de brug naar telefoon-UI.

  Volgorde: (1) I2C-HID digitizer op deze Envy, (2) contacten/IDs in `input_server`, (3) Interface Kit: tap, sleep, pinch, rotate, (4) tablet-mode (hinge + rotatie + dock-toetsenbord), (5) Deskbar/Tracker/OSK zonder muis. Telefoon-UI blijft onder Later.

## Later / lang

- [ ] **Accelerated graphics: 2D en 3D**  
  Bewust langetermijn. Haiku: CPU-desktop + Mesa **llvmpipe/lavapipe**. Doel op de 5625U (`1002:15e7`): hardware-2D, daarna hardware-3D (Gallium/Mesa). Volgorde: modeset Barcelo → accelerant-2D → GPU-buffer → Mesa i.p.v. llvmpipe ([#9919](https://dev.haiku-os.org/ticket/9919)).

- [ ] **Telefoon-UI**  
  Compacte layout, rotatie, on-screen keyboard, statusbalk. Alleen zinvol ná touch + ARM64 (en modeset).
