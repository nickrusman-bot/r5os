# R5OS multi-user and overlay migration

Architecture and order of work. Not a claim that login or isolation ships yet.

## Current system (audit, 2026-10-29)

### Accounts and desktop

- Image user is `HAIKU_ROOT_USER_NAME = user` (`distro/UserBuildConfig`), real name `R5OS`, host `r5os`.
- Home is **`/boot/home`**. Tracker Desktop, `~/config`, mail, and `find_directory(B_USER_* )` all point there for the live session.
- `/etc/passwd` and `/etc/shadow` exist. Tools: `src/bin/multiuser` (`useradd`, `userdel`, `passwd`, `su`, `login`) and `verify_password()`.
- Graphical **Login** already exists: `src/apps/login`. After `crypt`/`shadow` check it calls `BLaunchRoster().StartSession(login)` (Haiku #8940). It is **not** the default boot path. `target desktop` in `data/launch/user` starts Tracker and Deskbar on `initial_volumes_mounted` with no login.
- Default desktop user is often **UID 0** on Haiku images. GUI restrictions would not be security. New accounts must not be UID 0.
- `app_server` still has multi-user gaps (Haiku #17357, #10642).
- Personal vs system: `B_USER_SETTINGS_DIRECTORY` (`~/config/settings`) vs `B_SYSTEM_SETTINGS_DIRECTORY`. Many apps already use `find_directory`. Hardcoded `/boot/home` is the bug to fix when found.
- Packages: system-wide in `/boot/system`. Per-user config in `~/config`. Do not invent a second package format.

### Chosen home layout

Keep **`/boot/home` as the first account’s home** so existing installs and Haiku apps keep working.

Additional users: **`/boot/home/users/<username>`** (Desktop, Documents, Pictures, Videos, Downloads, Music, `config`). Do not nest extra users as siblings of `Desktop` without the `users/` prefix (that mixes people with folders).

Session home must come from `passwd` + `find_directory`, not copied path strings.

### Login and sessions (later)

Reuse `src/apps/login`. One graphical session at a time. Logout must tear down Tracker/Deskbar/user jobs (`data/launch/user`) so the next login does not inherit processes or `~/`. Lock screen is a later feature (keep the session). Guest and Public/Shared folders after permissions work.

Admin vs standard: POSIX groups (not only a checkbox). Privileged work through a checked helper, not “run Deskbar as root”. Never delete or demote the last administrator without a recovery path.

### Migration of existing disks

Detect actual layout. Typical live disk: one `user` and `/boot/home` already full of files. Map that identity to the first administrator. Back up before chown/move. Fresh install: first-boot create-admin is a later step. If the layout is unknown, refuse destructive changes.

## Overlay inventory

`scripts/apply-*.py` plus `scripts/apply-overlay.sh`. Python here is a **checkout installer**, not the OS feature.

| Script | Purpose | Native home | Keep as tool? |
|---|---|---|---|
| `apply-branding.py` | Deskbar leaf, unofficial branding | `distro/artwork` + branding patches | Yes, until `haiku/` is in this git tree |
| `apply-darkmode.py` | Appearance / system colors | stay in appearance until absorbed | Yes |
| `apply-ui-bugs.py` | UI kit bugs | absorb per fix | Yes |
| `apply-kernel-bugs.py` | e.g. TSC #20400 | kernel source | Yes |
| `apply-deskbar-bugs.py` | Deskbar | Deskbar source | Yes |
| `apply-tracker-desktop.py` | Tracker desktop | Tracker source | Yes |
| `apply-ui-shell.py` | lists, alerts, scroll | Interface Kit | Yes |
| `apply-high-bugs.py` | high-priority tickets | per ticket | Yes |
| `apply-qemu-drivers.py` | virtio on AMD64 | drivers | Yes |
| `apply-desktop-enh.py` | desktop polish | mixed | Yes |
| `apply-zeroconf.py` | mDNS / #1042 | `distro/zeroconf` | Yes (copy-in) |
| `apply-airplay.py` | AirPlay 1 | `distro/airplay` | Yes (copy-in) |
| `apply-wifi.py` | Wi-Fi window | `distro/network/WiFiWindow.*` | Yes (copy-in) |
| `apply-vboxclip.py` | COM1 clipboard | `distro/vboxclip` | Yes (copy-in) |
| `apply-ipv6.py` | SLAAC/DAD/DHCPv6 + kernel IPv6 | `distro/network` + kernel patches | Yes; kernel hunks are the riskiest to absorb |
| `apply-users.py` | Users & Accounts preflet | `distro/users` | Yes (copy-in) |
| `apply-about.py` | About version, What’s new | `distro/WHATSNEW` | Yes |

Jam/image/tracker scripts (`jam-r5-raw.sh`, `r5-version.sh`, `tracker-report.py`, QEMU/VBox runners) stay. They are not OS patches.

Do not remove an overlay until the native copy is jammed, booted, and compared.

Longer term: vendor or submodule the Haiku tree under `haiku/` in `origin` so copy-in is a no-op. That is a separate git migration, not this build.

## Version and What’s new (audit)

| Role | Source |
|---|---|
| Marketing version | `distro/VERSION` (`2026.1-dev`) |
| Build number | `distro/BUILD` (this work: **10003**) |
| Jam revision | `r5-${VERSION}` via `scripts/r5-version.sh --revision` → `UserBuildConfig` `HAIKU_REVISION` |
| About / generated header | `headers/private/system/r5_os_version.h` (`R5_OS_VERSION`, `R5_OS_BUILD`) written by `apply-overlay.sh` / `jam-r5-raw.sh` |
| What’s new text | `distro/WHATSNEW` copied to About `r5_whatsnew.txt` |
| Stale copy | `scripts/wsl-install-branding.sh` used to hardcode `10001` — must follow `r5-version.sh` |

Do not treat Haiku `hrev` as the product version.

## Implementation order

1. **Build 10002:** document the model; Users & Accounts lists `passwd`; What’s new rebuilds with the notes file.
2. **Build 10003:** add/disable/delete; new UIDs ≥ 1000 via `useradd`; group `admin`; UID 0 `user` kept; last-admin checks.
3. Put Login on the boot path; stop auto-starting `target desktop` until authenticated.
4. Logout: stop user launch jobs, then Login.
5. Permission isolation tests (home not world-readable).
6. Per-user settings audit (`find_directory`).
7. Lock screen; then guest/Public.
8. Absorb kernel/UI overlays into a real in-tree fork.

## Tests for this step

Open Users & Accounts. Add a standard user (UID ≥ 1000, home under `/boot/home/users`). Disable and delete a non-admin test user. The original `user` (UID 0) must refuse delete/disable. Login as that person is **out of scope**.
