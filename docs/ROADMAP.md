# R5OS roadmap

Product direction. Not a license to implement everything in one patch. Agents: follow `AGENTS.md`; pick work from the current user request.

R5OS modernizes a Haiku-based desktop without waiting on upstream schedules. Compatibility with existing Haiku apps stays a design goal (`AGENTS.md`).

## Near term (user-visible)

Order is impact, not ticket number.

1. **Hardware on the Ryzen 5 5625U** — Wi-Fi, GPU, backlight, sleep/resume, HiDPI without breaking layout.
2. **Browser** — a current WebKit/Qt browser in the image. WebPositive is the weak daily app.
3. **Network** — remaining items after Wi-Fi join/remember/tray:
   1. IPv6 in apps (SLAAC + DAD + DHCPv6 in net_server; still needs app/DNS checks and more LAN tests)
   2. DNS that uses DHCP servers and still resolves `.local`
   3. Captive portal
   4. SMB in Tracker
   5. Simple firewall (incoming closed by default)
   6. WireGuard
   7. LAN tests: VirtualBox bridged (no Guest Additions) or QEMU bridged/tap (usernet is not a LAN)
4. **Dark mode** through system apps and Deskbar (Haiku #18970).
5. **Polish** — notifications, screenshot, emoji/fonts, image clipboard.

Zeroconf, AirPrint, AirPlay 1, and the NetworkStatus tray are started; they are not “done” until hardware/LAN tests exist.

## Named products (later)

Do not start these during an unrelated bugfix.

| Name | Intent |
|---|---|
| **R5 Hub** | One place for settings: devices, display, audio, network, printers, updates |
| **R5 Quicklaunch** | Keyboard launch, app/file/system search |
| **R5 Connect** | Device discovery (AirPrint, AirPlay, later SMB/printers) |
| **R5 Store** | Software install on Haiku packages |
| **R5 SafeUpdate** | Validated updates and recovery *before* disruptive changes |
| **R5 Doctor** | Logs, crashes, drivers, common failures |

## Graphics and media

Investigate GPU acceleration, compositing, and Vulkan **without** destabilizing `app_server`. Improve audio, streaming, and device interop. Incremental replacements only.

## Architectures

| Target | Current bar |
|---|---|
| AMD64 | First: QEMU + Ryzen reference |
| ARM64 | After AMD64 is usable on hardware; QEMU then boards |
| RISC-V 64 | After AMD64 and ARM64 |

Levels: buildable → bootable → functional → desktop-capable → release-supported. See `AGENTS.md`.

## Upstream

Fetch `upstream` when a Haiku fix is useful. Inspect overlays and R5OS behavior before merge. Record imports, local patches, and intentional differences in `CHANGELOG.md`.
