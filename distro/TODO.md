# R5 OS feature / distro-todo

Eigen werk, los van de Haiku-tickettracker in `tracker/`.

## Klaar

- [x] **Versioning**  
  Publiek: **R5 OS YYYY.N** (`2026.1`, `2026.2`, …; nieuw jaar → `.1`). Tussen builds: **`YYYY.N-dev`**. Bron: `distro/VERSION` (nu `2026.1-dev`). Jam: `HAIKU_REVISION=r5-<VERSION>`. AboutSystem toont **R5 OS 2026.1-dev**, geen Haiku-`hrev`.

- [x] **Eerste UI-naam**  
  FirstBoot Welcome/Try/Install, Deskbar **About R5 OS**, About-disclaimer, volume-label in overlay (`HAIKU_IMAGE_LABEL`). About-logo: `distro/artwork/r5-os-logo.png`.

- [x] **About-versie**  
  AboutSystem toont **R5 OS** plus `distro/VERSION` (niet de git-hash). Jam-revisiebestand wordt door `apply-overlay.sh` overschreven.

## Open

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
