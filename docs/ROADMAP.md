# R5OS roadmap

Product direction. Not a license to implement everything in one patch. Agents: follow `AGENTS.md`; pick work from the current user request.

R5OS modernizes a Haiku-based desktop without waiting on upstream schedules. Compatibility with existing Haiku apps stays a design goal (`AGENTS.md`).

## Near term (user-visible)

Order is impact, not ticket number.

1. **Multi-user** — real accounts, login/logout, per-user settings. Plan: `docs/MULTIUSER.md`. Remaining work: [#63](https://github.com/nickrusman-bot/r5os/issues/63) (lock screen [#64](https://github.com/nickrusman-bot/r5os/issues/64) is build 10013, seen in VBox 2026-10-30; the shortcut is [#72](https://github.com/nickrusman-bot/r5os/issues/72); next open product item is guest [#65](https://github.com/nickrusman-bot/r5os/issues/65)). Do not skip isolation or treat a list of names as “done”.
2. **Hardware on the Ryzen 5 5625U** — Wi-Fi, GPU, backlight, sleep/resume, HiDPI without breaking layout.
3. **Browser** — a current WebKit/Qt browser in the image. WebPositive is the weak daily app.
4. **Network** — remaining items after Wi-Fi join/remember/tray:
   1. IPv6 in apps (SLAAC + DAD + DHCPv6 in net_server; Network IPv6 Mode; still needs more LAN tests)
   2. DNS that uses DHCP servers and still resolves `.local`
   3. Captive portal
   4. SMB in Tracker
   5. Simple firewall (incoming closed by default)
   6. WireGuard
   7. LAN tests: VirtualBox bridged (no Guest Additions) or QEMU bridged/tap (usernet is not a LAN)
5. **Dark mode** through system apps and Deskbar (Haiku #18970).
6. **Polish** — notifications, screenshot, emoji/fonts, image clipboard. Rounder windows and Deskbar: `docs/LOOK.md` (not while multi-user is open).

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

GPU and Vulkan stay in `docs/GRAPHICS.md`. The desktop still paints on the CPU. Do not destabilize `app_server`, and do not call VESA, `vboxvideo`, or Lavapipe hardware acceleration. Improve audio, streaming, and device interop. Incremental replacements only. Mixxx is a side quest ([#55](https://github.com/nickrusman-bot/r5os/issues/55), `docs/MIXXX.md`), not an image app and not while multi-user is open.

## Architectures

| Target | Current bar |
|---|---|
| AMD64 | First: QEMU + Ryzen reference |
| ARM64 | After AMD64 is usable on hardware; QEMU then boards |
| RISC-V 64 | After AMD64 and ARM64 |

Levels: buildable → bootable → functional → desktop-capable → release-supported. See `AGENTS.md`.

## Upstream

Fetch `upstream` when a Haiku fix is useful. Inspect overlays and R5OS behavior before merge. Record imports, local patches, and intentional differences in `CHANGELOG.md`.
