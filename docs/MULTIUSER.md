# R5OS multi-user and overlay migration

Architecture and order of work. Not a claim that login or isolation ships yet.

## Current system (audit, 2026-10-29)

### Accounts and desktop

- Image user is `HAIKU_ROOT_USER_NAME = user` (`distro/UserBuildConfig`), real name `R5OS`, host `r5os`.
- Home is **`/boot/home`**. Tracker Desktop, `~/config`, mail, and `find_directory(B_USER_* )` all point there for the live session.
- `/etc/passwd` and `/etc/shadow` exist. Tools: `src/bin/multiuser` (`useradd`, `userdel`, `passwd`, `su`, `login`) and `verify_password()`.
- Graphical **Login** is on the boot path (build 10004): `data/launch/user` `run { login }` starts `/system/apps/Login` instead of `desktop`. Password check uses `verify_password`. Success starts `target desktop` in the same session (`BLaunchRoster().Target("desktop")`). Login stays running (hidden). **Log out** (build 10005) is Deskbar → Login: quit session apps, `StopTarget("desktop")`, show Sign in. `StartSession` for a different account is not wired (would start a second app_server). **Home modes** (build 10006): extra homes `0700`; `/boot/home` is `0711` with private children. **Build 10007:** After a valid password, Login asks the registrar to move `user` to a free uid ≥ 1000 (delete + add; uid on an existing account cannot change) and keeps a locked `root` at uid 0. Tracker/Deskbar start via `r5-desktop` (`load_image` then exit so Deskbar shows Tracker/Deskbar, not four helper names). Login stops `x-vnd.Be-TRAK` / `x-vnd.Be-TSKB` by name before that; `GetJobs("desktop")` fails without a session daemon. launch_daemon jobs are not prefixed with `r5-desktop`. app_server keeps those apps on the existing screen (uid 0 Login desktop) until `StartSession`; otherwise they open a second, invisible desktop. Do not rewrite `/etc/passwd` behind the registrar. Extra homes then apply to the person at the desktop. Sign in as someone else is still this desktop.
- Image `passwd` still ships `user` as uid 0; Login migrates that account to the first free uid ≥ 1000 before Sign in and `chown`s `/boot/home`. VBox 2026-10-29 17:14 Europe/Amsterdam: after Sign in, Terminal `id` was `uid=1001(user)` `gid=100(users)` `groups=100(users),103(admin)`. Tracker/Deskbar run as that uid (`r5-desktop`). New accounts must not be UID 0. Extra-home isolation (`ls` denied) was not tested on that VDI.
- `app_server` still has multi-user gaps (Haiku #17357, #10642).
- Personal vs system: `B_USER_SETTINGS_DIRECTORY` (`~/config/settings`) vs `B_SYSTEM_SETTINGS_DIRECTORY`. Appearance, Backgrounds, Deskbar, Tracker, and app_server `DesktopSettings` already use `find_directory`. For uid 0 that still maps to `/boot/home` (`get_user_home_path` skips `getpwuid` and uses `kUserDirectory`). After migrate, the desktop uid is ≥ 1000 so `find_directory` follows that account’s home (`/boot/home` for `user`). Sign in as someone else is still this administrator desktop (`StartSession` is not used).
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

## Who the people are

The image has **one administrator**: login name `user`, gecos R5OS, home `/boot/home`. That is who should work at the desktop after Sign in.

A **standard user** is optional: created in Users & Accounts, uid ≥ 1000, home `/boot/home/users/<name>`, group `users`, not `admin`. They should be able to work on the system **after** `StartSession`. Until then they exist in `passwd` and have a private folder; Sign in as them still opens the administrator’s desktop.

`nicky` / `nicky2` / `nicky3` were accounts added on VBox disks while testing Add/Delete. They are not in the jammed image and not a product role. Tests may create a standard user with any valid name.

`sshd` (uid 1000), `ftp`, `nobody` stay hidden. Do not reuse uid 1000 for `user`.

No new overlays for this work. Changes live in `distro/users` and `distro/login` (copy-in). No `replace_once` on libroot unless the uid-0 `get_user_home_path` skip can stay as-is (it must: launch_daemon and registrar).

## First administrator off uid 0 (plan)

**Goal:** Tracker/Deskbar run as the administrator, not root. Home stays `/boot/home`. Mode `0700` on extra homes then applies to the person at the desktop.

**Not this step:** `StartSession`, second `app_server`, hiding Network/Screen/Time, renaming `user`, moving home to `/boot/home/users/user`.

### What stays uid 0

Kernel, `launch_daemon`, registrar, net_server, package_daemon, and Login **until** the desktop starts. `get_user_home_path` still short-circuits uid 0 to `kUserDirectory` so those daemons do not call `getpwuid_r`.

### Identity after migration

| | Now | After this step |
|---|---|---|
| Login name | `user` | `user` |
| Role | administrator | administrator (`admin` / `wheel`) |
| Home | `/boot/home` | `/boot/home` (do not move) |
| uid | 0 | first **unused** uid ≥ 1000 (not `sshd`’s 1000, not an existing person) |
| gid | 0 | real group (`users` plus `admin`) |

On a disk that already has a standard user at 1001, `user` becomes e.g. 1002. Collision is worse than a gap in the number space.

Do not `chown` `/boot/home/users/<other>`.

### Both halves

**A. `passwd` + `chown` of `/boot/home` (except other people’s folders).** Alone this does nothing: Tracker stays euid 0.

**B. Desktop jobs run as `user`’s new uid** after a valid password, around `Target("desktop")`. Login may stay uid 0 so Log out and Sign in still work.

Sign in as a standard user still starts **this** administrator desktop until `StartSession`. Isolation test: that desktop cannot `ls` the standard user’s folder.

### Users & Accounts

Add/Delete/Disable currently spawn `useradd` only if `geteuid()==0`. After the drop that fails. Use registrar / `user_group` (shadow already does) or a small helper that only `admin` may call. Preflet: member of `admin`, not euid 0. Last-admin check stays. The old “uid 0 cannot be deleted” rule becomes “last administrator cannot be deleted”.

### Fresh image vs existing disk

1. Jam image: `user` is already uid ≥ 1000 in image `passwd`.
2. Existing disk: once, if `user` is still uid 0, pick a free uid, update `passwd`/`group`, `chown`, stop. Unknown layout: refuse. If uid is already ≥ 1000, do nothing.

### Tests (call it isolation only if 1–2 pass on VBox)

1. Sign in as `user`: `id` is not uid 0; `$HOME` is `/boot/home`.
2. If a standard user exists: `ls /boot/home/users/<name>` as that session is permission denied; `su <name>` can list that home; `su <name> -c 'ls /boot/home/Desktop'` is denied.
3. Sign in as the standard user still opens the administrator desktop (until `StartSession`).
4. Administrator can still add a standard user (helper).
5. Log out, Sign in again. launch_daemon still running.
6. Fresh image and a disk that still had uid 0 `user`.

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
| Build number | `distro/BUILD` (this work: **10007**) |
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
6. **Settings audit:** desktop prefs plus bundled apps. MidiPlayer, Joysticks, and PoorMan use `find_directory` via native copies in `distro/user-paths` (apply copies files). Preflet split accepted 2026-10-29: personal vs machine (Network/Screen/Time).
7. **First administrator off uid 0** (build 10007). Runtime `passwd` + `chown`; Tracker/Deskbar via `/system/apps/r5-desktop` (`load_image`, then the helper exits). Not launch wraps. Not `StartSession`.
8. **`StartSession`:** Sign in as a standard user is that uid and home. One graphical session.
9. Lock screen; then guest/Public.
10. Absorb leftover kernel/UI string overlays into native source; do not add new ones.

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

VBox 2026-10-29 14:30 Europe/Amsterdam: 1–4 confirmed (`nicky`; uid 0 `user` still listed that home).

## Tests for administrator off uid 0 (build 10007)

Not run yet. Call it isolation only if these pass on VBox:

1. Sign in as `user`: `id` is not uid 0; `$HOME` is `/boot/home`.
2. If a standard user exists: `ls /boot/home/users/<name>` is permission denied; `su <name>` can list that home; `su <name> -c 'ls /boot/home/Desktop'` is denied.
3. Sign in as the standard user still opens the administrator desktop (until `StartSession`).
4. Administrator can still add a standard user (Users talks to Login).
5. Log out, Sign in again. `launch_daemon` still running.
6. Fresh image (migrate from uid 0) and a disk that already had a standard user (new uid must not collide).

## Testing a second account (limits)

There is **no second desktop**. Sign in as `nicky` still starts Tracker/Deskbar as uid 0 `user` (`/boot/home`). Use that as the negative check, then `su` for the home that does exist.

1. Preferences → Users & Accounts: add `nicky` (standard) if missing. Home must be `/boot/home/users/nicky`.
2. Deskbar → Log out → Sign in, choose `nicky`, password, Sign in.
3. Tracker Desktop is still `/boot/home` (first account). MidiPlayer/Joysticks/PoorMan still write there. That is expected until `StartSession`.
4. Terminal, still as `user`:

```
echo $HOME
id
su nicky -c 'echo HOME=$HOME; id; ls /boot/home/users/nicky'
su nicky -c 'ls /boot/home/Desktop'
```

`$HOME` as `user` is `/boot/home`. As `nicky` it is `/boot/home/users/nicky`. Listing `/boot/home/Desktop` as `nicky` should be permission denied. Uid 0 `user` can still `ls /boot/home/users/nicky`.

Do not run MidiPlayer/Joysticks via `su nicky` and expect a separate GUI: `app_server` is still the first session.

Joysticks is on the image (Deskbar → Preferences → Joysticks). Without a stick the window should still open. After that:

```
ls -ld /boot/home/config/settings/joysticks
```

VBox 2026-10-29 15:26 Europe/Amsterdam: steps 4 confirmed (`nicky` uid 1001, own home listed, Desktop of `user` permission denied). Joysticks appeared in Preferences.

## Settings audit (Appearance, Deskbar, Tracker, background)

Haiku `find_directory(B_USER_* )` resolves `$h` via `get_user_home_path`:

- **uid ≠ 0:** `getpwuid_r` → `pw_dir` (e.g. `/boot/home/users/nicky/config/settings`).
- **uid 0:** always `kUserDirectory` (`/boot/home`). Launch_daemon/registrar must not call `getpwuid_r`.

Checked in the jammed AMD64 tree (2026-10-29):

| Surface | Path API | Hardcoded `/boot/home`? |
|---|---|---|
| Appearance | `B_USER_SETTINGS_DIRECTORY` | no |
| Backgrounds | `B_USER_SETTINGS_DIRECTORY`, `B_DESKTOP_DIRECTORY` | no |
| Deskbar | `B_USER_SETTINGS_DIRECTORY`, `B_USER_DESKBAR_DIRECTORY` | no |
| Tracker | `B_USER_SETTINGS_DIRECTORY`, `B_USER_DIRECTORY` (cwd) | comments/examples only |
| app_server colours/fonts | `DesktopSettings` / `DecorManager` `B_USER_SETTINGS_DIRECTORY` | unused `#define DECORATORS_DIR` still hardcodes a home add-on path |
| R5OS AirPlay log | now `B_USER_LOG_DIRECTORY` | was `/boot/home/airplay_deskbar.log` |
| Joysticks preflet | `B_USER_SETTINGS_DIRECTORY` / `joysticks` | native copy `distro/user-paths` |
| Users / HomeLock | first-home layout | `/boot/home` on purpose (parent of `users/`) |
| Extra-home create | `passwd` home + `config/settings` | layout only, not the live session |

Until `StartSession`, changing Appearance still writes the first home (`/boot/home`). After build 10007 that is the administrator’s uid, not 0. Jam AMD64 2026-10-29 14:42 Europe/Amsterdam compiled the AirPlay log change (`jam_exit=0`). Not VBox-tested as a second desktop.

### Bundled applications

First audit pass was desktop prefs only. 2026-10-29 14:47 Europe/Amsterdam: `src/apps` and `src/preferences` in the jammed AMD64 tree, plus R5OS `distro/` apps.

Most image apps already call `find_directory` for settings or folders (Mail, Terminal, Screenshot, ShowImage, Clock, DeskCalc, HaikuDepot, People, ProcessController, Workspaces, …). Same uid-0 limit as the desktop prefs.

Hardcoded `/boot/home` in app code, grouped:

| App | What | Role |
|---|---|---|
| CodyCam, Icon-O-Matic, TextSearch, MediaConverter | `find_directory(B_USER_DIRECTORY)` then `/boot/home` | fallback / default folder, not the settings file |
| WebPositive | image `webpositive.hpkg` | not this tree; defaults still hardcoded in that package |
| MidiPlayer | `B_USER_SETTINGS_DIRECTORY` / `MidiPlayerSettings` | `distro/user-paths` |
| PoorMan | `B_USER_DIRECTORY` / `public_html` | `distro/user-paths` |
| Joysticks | `B_USER_SETTINGS_DIRECTORY` / `joysticks` | `distro/user-paths` |
| PackageInstaller | `/boot/home/config` rewrite | BeOS path migration |
| MediaPlayer, Tracker `PROFILE` | Desktop dump paths | debug leftovers |
| Login, Users, HomeLock | `/boot/home` and `users/` | layout on purpose |
| AirPlay log | `B_USER_LOG_DIRECTORY` | R5OS overlay, already switched |

Not in this git: **Pe** and **Vision** (optional image packages). Demos without settings (GLTeapot, Mandelbrot, …) have no home path.

Native copies in `distro/user-paths` (MidiPlayer, Joysticks, PoorMan). Apply only copies files. No string patch. PackageInstaller BeOS rewrite and MediaPlayer/Tracker debug dumps stay. WebPositive stays the haikuports package.

### Preference apps (accepted 2026-10-29)

Deskbar still shows the usual Preferences list to everyone. This split is what we will follow; menus are not hidden yet. A second user does not have their own prefs until `StartSession`.

Until then, even personal preflets write `/boot/home/config/settings` because the live session is uid 0.

**Personal (already `B_USER_*`).** A normal account should keep their own:

| Preflet | In Deskbar Preferences | Notes |
|---|---|---|
| Appearance | yes | colours, fonts, antialiasing |
| Backgrounds | yes | desktop picture |
| Deskbar | yes | settings live in the Deskbar app |
| Tracker | yes | settings live in Tracker |
| Keymap | yes | |
| Input | yes | mouse, touchpad, keyboard repeat |
| ScreenSaver | yes | which saver + delay |
| Shortcuts | yes | |
| Sounds | yes | |
| Notifications | yes | |
| E-mail | yes | accounts in `~/config/settings` |
| Media | yes | |
| Locale | yes | Locale Kit catalogs, not this preflet’s own `find_directory` |
| FileTypes | yes | user overrides; system types stay in the system |
| Joysticks | yes (on the image) | `find_directory`; keep personal |
| DataTranslations | power user | window/settings file is user; add-ons mixed |
| Bluetooth | if hardware | settings file is user; the radio is the machine |

**Machine-wide. Do not make these per-user.** Visible in Preferences; they describe this computer. Later: hide or lock for a non-admin.

| Preflet | Visible today | Why stay shared | Path today |
|---|---|---|---|
| Network | yes | interfaces, DHCP, IPv6 — `net_server` | not `find_directory` in the preflet |
| Screen | yes | resolution / workspace of the display | window frame in `B_USER_SETTINGS`; mode is app_server |
| Time | yes | clock and timezone for the machine | also writes some `B_USER_SETTINGS` (NTP UI) |
| Printers | yes | queues on this host | `B_USER_PRINTERS_DIRECTORY` (odd for shared printers) |
| Repositories | yes | pkgman sources | system settings + a user file |
| VirtualMemory | power user | swap is the machine | settings file is `B_USER_SETTINGS` (wrong place) |

**Admin only:**

| Preflet | Visible today | Decision |
|---|---|---|
| Users | yes, in Preferences | administrators only; not a personal theme |

**Not this pass:** hide Network/Screen/Time from a standard user; move VirtualMemory/Printers onto `B_SYSTEM_SETTINGS`; rebuild `webpositive.hpkg`.

CodyCam / Icon-O-Matic / TextSearch / MediaConverter only fall back to `/boot/home` after `find_directory`. Leave them.
