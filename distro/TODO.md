# R5 OS feature / distro-todo

Eigen werk, los van de Haiku-tickettracker in `tracker/`.

## Open

- [ ] **Hardware: HP Envy (convertible) — Ryzen 5 5625U + Radeon `15e7`**  
  Referentiemachine voor AMD64, gfx, touch en tablet-mode. Doel: APU **bruikbaar** (niet alleen VESA): panel/eDP, native resolutie, HDMI/DP, backlight, geen KDL, audio, daarna power/sleep. GPU 2D/3D staat apart. Exacte Envy-SKU later in syslog vastleggen.

  **Wat het is:** Barcelo-refresh van Cezanne (Zen 3, 6c/12t, 15 W). iGPU is **Vega 7**, geen Navi/RDNA.

  Bevestigd op deze machine: **PCI `1002:15e7` (Barcelo)** — **niet** in `radeon_hd`. Verwante IDs (niet deze laptop, wel dezelfde driver-tak):
  - `1002:1638` Cezanne — staat in de tabel, maar als `RADEON_NAVI` / “Radeon Renoir” (verkeerde familie; Linux `amdgpu` houdt Renoir/Cezanne/Barcelo bij Vega + DCN 2.x).
  - `1002:164c` Lucienne staat **uit** vanwege crash in `pll_compute()` ([Haiku #17516](https://dev.haiku-os.org/ticket/17516)).

  **Huidige Haiku-stand:** `radeon_hd` is 2D/modeset, geen `amdgpu`. Raven (`15dd`/`15d8`) is de laatste nette APU-tak. Renoir/Cezanne zitten in de Navi-lijst; community-advies voor 5600G-klasse was vaak **radeon_hd blacklisten** (VESA). Verwant: [#17516](https://dev.haiku-os.org/ticket/17516), Vega/DP [#14800](https://dev.haiku-os.org/ticket/14800), [#8626](https://dev.haiku-os.org/ticket/8626), ACPI-sleep [#19487](https://dev.haiku-os.org/ticket/19487), brightness [#8258](https://dev.haiku-os.org/ticket/8258), fans [#19829](https://dev.haiku-os.org/ticket/19829). Audio op Ryzen-laptops gaat vaak via **ACP**, niet klassieke HDA.

  **Aanpak:** `0x15e7` toevoegen als Barcelo/Cezanne-APU (niet Navi). PLL/DCN voor Renoir-klasse (zelfde valkuil als #17516). eDP + backlight. Audio/ACP. Wifi hangt van de NIC in *deze* laptop af (apart). Syslog na eerste boot bewaren.

- [ ] **Branding: Haiku-OS-referenties uit de UI**  
  Zichtbare teksten, About-vensters, Welcome, Deskbar, boot/splash, AboutSystem en overige interface-strings die naar Haiku OS / haiku-os.org verwijzen, vervangen of verwijderen. Naam in de UI: **R5 OS**. Versie volgens `distro/VERSION` (nu **2026.1-dev**; releases **2026.1**, **2026.2**, …). Geen Haiku- of BeOS-merken/logo’s. Disclaimer blijft in About (`DISCLAIMER.txt`). Technische paden/API’s (`haiku/`-bron, package-namen) later; eerst wat de gebruiker ziet.

- [ ] **Architecturen: AMD64, ARM64, optioneel RISC-V 64**  
  Beleid: alleen 64-bit. Volgorde: **AMD64** (huidige `x86_64`-image, 5625U) → **ARM64** (eerst QEMU `virt`, Haiku-port ~desktop in QEMU 2026; Apple Silicon/RPi later) → **RISC-V 64** (RV64GC, Haiku heeft al een vroege QEMU-port). Geen tijd in x86-32, ARM32, PPC, SPARC, m68k. 32-bit-code in de boom niet in een drive-by slopen.

- [ ] **Touch + tablet-mode (HP Envy convertible)**  
  Testapparaat: dezelfde Envy (touchscreen + 360°-scharnier). Eersteklas vingerinput, geen muis-emulatie. Haiku heeft HID-digitizer-IDs en een tablet-pad; **multitouch ontbreekt** ([#9545](https://dev.haiku-os.org/ticket/9545)). Touchpads ≠ touchscreen. USB/I2C HID-schermen werken vaak niet ([#7481](https://dev.haiku-os.org/ticket/7481), [#15881](https://dev.haiku-os.org/ticket/15881)); Envy’s paneel is bijna zeker **I2C-HID**.

  Convertible extra: rotatie/ACPI-hinge, scherm 0/90/180/270, in tablet-mode toetsenbord+touchpad uit. Dat is de brug naar telefoon-UI.

  Volgorde: (1) I2C-HID digitizer op deze Envy, (2) contacten/IDs in `input_server`, (3) Interface Kit: tap, sleep, pinch, rotate, (4) tablet-mode (hinge + rotatie + dock-toetsenbord), (5) Deskbar/Tracker/OSK zonder muis. Telefoon-UI blijft onder Later.

## Later / lang

- [ ] **Accelerated graphics: 2D en 3D**  
  Bewust langetermijn, niet de volgende jam-cycle. Haiku heeft nu CPU-desktop (`app_server`) en Mesa **llvmpipe/lavapipe**; geen `radeonsi`/DRM voor Vega. Doel op de 5625U (`1002:15e7`): hardware-2D (blit/fill, vsync, cursor, multi-head) en daarna hardware-3D (Gallium/Mesa, OpenGL; Vulkan als die stack er is).

  Volgorde: (1) modeset op Barcelo, (2) accelerant-2D zonder KDL, (3) GPU-buffer → scherm, (4) Mesa-driver i.p.v. llvmpipe. Dat raakt kernel, drivers en waarschijnlijk een DRM-achtige API ([Haiku #9919](https://dev.haiku-os.org/ticket/9919)). Eerst de referentiemachine laten booten met beeld; daarna deze tak.

- [ ] **Telefoon-UI**  
  Compacte layout, rotatie, on-screen keyboard, statusbalk. Alleen zinvol ná touch + ARM64 (en modeset). Geen BeOS-Deskbar 1:1 op 6 inch.
