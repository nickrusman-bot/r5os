# R5OS multi-user and overlay migration

Architecture and order of work. Not a claim that login or isolation ships yet.

## Current system (audit, 2026-10-29)

### Accounts and desktop

- Image user is `HAIKU_ROOT_USER_NAME = user` (`distro/UserBuildConfig`), real name `R5OS`, host `r5os`.
- Home is **`/boot/home`**. Tracker Desktop, `~/config`, mail, and `find_directory(B_USER_* )` all point there for the live session.
- `/etc/passwd` and `/etc/shadow` exist. Tools: `src/bin/multiuser` (`useradd`, `userdel`, `passwd`, `su`, `login`) and `verify_password()`.
- Graphical **Login** is on the boot path (build 10004): `data/launch/user` `run { login }` starts `/system/apps/Login` instead of `desktop`. Password check uses `verify_password`. Success starts `target desktop` in the same session (`BLaunchRoster().Target("desktop")`). Login stays running (hidden). **Log out** (build 10005) is Deskbar → Login: quit session apps, `StopTarget("desktop")`, show Sign in. `StartSession` for a different account is not wired (would start a second app_server). **Home modes** (build 10006): extra homes `0700`; `/boot/home` is `0711` with private children. Uid 0 `user` still reads them.
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

Layout target for that screen: Windows 11-like arrangement (centered person, name, password under it, power at the bottom). Haiku widgets, not a Windows chrome clone.

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
| `apply-login.py` | Login app + launch `run { login }` + Deskbar Log out; Installer must not reopen FirstBootPrompt on Log out | `distro/login` | Yes (copy-in) |
| `apply-about.py` | About version, What’s new | `distro/WHATSNEW` | Yes |

Jam/image/tracker scripts (`jam-r5-raw.sh`, `r5-version.sh`, `tracker-report.py`, QEMU/VBox runners) stay. They are not OS patches.

Do not remove an overlay until the native copy is jammed, booted, and compared.

Longer term: vendor or submodule the Haiku tree under `haiku/` in `origin` so copy-in is a no-op. That is a separate git migration, not this build.

## Version and What’s new (audit)

| Role | Source |
|---|---|
| Marketing version | `distro/VERSION` (`2026.1-dev`) |
| Build number | `distro/BUILD` (this work: **10006**) |
| Jam revision | `r5-${VERSION}` via `scripts/r5-version.sh --revision` → `UserBuildConfig` `HAIKU_REVISION` |
| About / generated header | `headers/private/system/r5_os_version.h` (`R5_OS_VERSION`, `R5_OS_BUILD`) written by `apply-overlay.sh` / `jam-r5-raw.sh` |
| What’s new text | `distro/WHATSNEW` copied to About `r5_whatsnew.txt` |
| Stale copy | `scripts/wsl-install-branding.sh` used to hardcode `10001` — must follow `r5-version.sh` |

Do not treat Haiku `hrev` as the product version.

## Implementation order

1. **Build 10002:** document the model; Users & Accounts lists `passwd`; What’s new rebuilds with the notes file.
2. **Build 10003:** add/disable/delete; new UIDs ≥ 1000 via `useradd`; group `admin`; UID 0 `user` kept; last-admin checks.
3. **Build 10004:** Login on the boot path; `target desktop` after password. Same session. Layout: centered person, password, power at the bottom.
4. **Build 10005:** Logout: Deskbar Log out, stop desktop launch jobs, then Sign in. Same session.
5. **Build 10006:** home modes. `/boot/home` is `0711`; private names under it are `0700`/`0600`; `/boot/home/users/<name>` is `0700`. Uid 0 `user` still reads everything. Same desktop session.
6. Per-user settings audit (`find_directory`).
7. Lock screen; then guest/Public.
8. Absorb kernel/UI overlays into a real in-tree fork.

## Tests for this step

VBox 2026-10-29, Europe/Amsterdam (bridged, first-boot VDI, build 10003):

1. Open Users & Accounts — listed `user` uid 0 `/boot/home` administrator (R5OS). `sshd` was not listed.
2. Add a standard user — `nicky` uid 1001, home `/boot/home/users/nicky`. Also added `nicky2`/`nicky3` uid 1002/1003 as administrators. Tracker showed those three folders.
3. Disable a non-admin — not captured in the screenshots.
4. Delete a non-admin — after delete, the list was `user` and `nicky3` only.
5. Original `user` (UID 0) refuse delete/disable — Disable and Delete stay inactive unless a removable account is selected; `user` was not removed.

Add-account label alignment is later polish. The preflet icon is the Haiku Login/Tracker people vector.

## Tests for sign-in (build 10004)

1. Boot to the sign-in screen (no Tracker/Deskbar yet). One person: `user` / R5OS.
2. Empty password + Sign in starts the desktop.
3. Wrong password stays on the sign-in screen with an error.
4. Restart and Shut down at the bottom still work.
5. Extra people from Users & Accounts appear in the list; signing in as them still opens the same `user` session until isolation exists.

VBox 2026-10-29 Europe/Amsterdam (live Try): one account showed `user`; with `nicky` the list showed both. Restart worked. Empty password signed in.

## Tests for log out (build 10005)

1. Sign in, then Deskbar leaf **Log out** → confirm → Sign in is back (no Tracker/Deskbar).
2. Sign in again reaches the desktop.
3. An open app (StyledEdit or Terminal) is gone after log out.
4. Restart / Shut down on Sign in still work.

VBox 2026-10-29 14:01 Europe/Amsterdam: Log out returned to Sign in; Login is not in the Deskbar list. StyledEdit/Terminal leftover was not rechecked on this pass.

## Tests for home modes (build 10006)

After Sign in, in Terminal:

1. `ls -ld /boot/home /boot/home/Desktop /boot/home/users` — `drwx--x--x`, `drwx------`, `drwxr-xr-x`.
2. Add `nicky`, then `su nicky -c 'ls /boot/home/Desktop'` — permission denied.
3. `su nicky -c 'ls /boot/home/users/nicky'` — that home is readable by `nicky`.
4. As `user` (uid 0), `ls /boot/home/users/nicky` still works.

Not VBox-tested yet.
