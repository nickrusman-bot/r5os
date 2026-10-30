# R5OS

Independent desktop OS based on [Haiku](https://www.haiku-os.org/). The name nods to BeOS R5 as history, not as a product claim.

This is **not** the official Haiku distribution and **not** BeOS. See `distro/DISCLAIMER.txt`.

Current tree: **R5OS 2026.1-dev Build 10013** (`distro/VERSION` + `distro/BUILD`). About R5OS shows that string and a What’s new tab (`distro/WHATSNEW`).

## What is in the image now

User-visible pieces that are in this tree (limits in What’s new and `CHANGELOG.md`):

- Branding as R5OS (Deskbar, installer, splash, About).
- Dark appearance, desktop/Deskbar layout fixes.
- Wi-Fi join/remember, IPv6 SLAAC/DAD on a home LAN, AirPrint/IPP add, AirPlay 1 to speakers (not AirPlay 2 pairing).
- Shutdown no longer waits forever on IPv6 SLAAC or the VirtualBox COM1 clipboard helper.
- **Users & Accounts** opens on your own card (picture, name, username, email, password). Other people stay hidden until an administrator unlocks the list. A new account needs a password. New people get user id 1000 or higher and a folder under `/boot/home/users`. The first account stays `user` at `/boot/home`, now a normal user id (1000 or higher), not 0. Sign in still shows everyone who can sign in.
- **Printers, Repositories, and VirtualMemory** stay visible. An administrator changes them. Another account uses the lock at the bottom right. Printer queues and swap are for the computer.
- **Sign-in screen** before Tracker/Deskbar. The leaf menu says **Log out** and the account name, then returns to Sign in. **Lock screen** (Ctrl+Shift+L, or the leaf menu) covers that same session; apps stay open until you unlock with your password or log out. Restart and a cold boot also return to Sign in. After a valid password, Tracker and Deskbar run as that account: a standard person uses `/boot/home/users/<name>`, their own Trash, and their own MIME database. Folder icons (including the leaf menu) are the same yellow folders for everyone. There is still **one screen**.
- **Locked machine settings.** Preferences are not hidden by role. Network, Screen, Time, and Users stay in the list. An administrator changes them immediately. A standard account looks, then unlocks the bottom-right lock with an administrator name and password (checked by Sign in, uid 0). That lasts a few minutes and is per window. Personal prefs (Appearance, Backgrounds, …) stay with the signed-in person.

Home folders of extra accounts are private (`0700`). `user` is in group `admin` and cannot list another person’s home. Appearance/Deskbar/Tracker/background and most bundled apps already use `find_directory`. MidiPlayer, Joysticks, and PoorMan follow that in `distro/user-paths`. Printers, Repositories, and VirtualMemory are machine-wide and locked. Not done: guest and Public/Shared, `StartSession` (that would start a second app_server), one combined Preferences window. Plan: `docs/MULTIUSER.md`. Product direction: `docs/ROADMAP.md`.

## Layout

| Path | Role |
|---|---|
| `distro/` | Overlay: version, What’s new, Jam profile, branding, extra apps (Users, Wi-Fi, AirPlay, …) |
| `docs/` | Roadmap, multi-user, IPv6, VirtualBox |
| `tracker/` | Open Haiku tickets and whether R5OS already fixed them |
| `scripts/` | Clone, overlay, jam, QEMU/VBox, ticket sync |
| `haiku/` | R5OS fork of the OS sources (`r5` branch; not in *this* git) |
| `buildtools/` | Haiku build tools (after setup, fetch-only) |

`haiku/` is our fork. We merge bugfixes into `r5` and push only to our git. We do not push to Haiku: their policy refuses AI-generated or AI-modified contributions.

## First build (WSL Ubuntu)

Haiku does not build natively on Windows. In WSL:

```bash
sudo apt update
sudo apt install git nasm autoconf automake texinfo flex bison gawk \
  build-essential unzip wget zip xorriso mtools python3

cd /mnt/c/Users/nickr/Projects/R5OS
chmod +x scripts/*.sh
./scripts/setup-wsl.sh
./scripts/build-image.sh r5-raw
```

Jam AMD64 with overlays applied (`scripts/apply-overlay.sh`, then `scripts/jam-r5-raw.sh` in WSL). `jam-r5-raw.sh` does not apply overlays by itself.

LAN tests (IPv6, mDNS): VirtualBox bridged, no Guest Additions — `scripts/run-vbox-r5.ps1` and `docs/vbox.md`. Use `-ForceConvert` when the raw image is new. Text clipboard is COM1, not Guest Additions. No vboxsf, no auto-resize, no VBox drag-and-drop. QEMU usernet is not a LAN.

Then boot the image in QEMU, for example:

```bash
# WSL: scripts/run-qemu-r5.sh (TCG or KVM, IDE disk, virtio-net, USB tablet, HDA)
qemu-system-x86_64 -machine pc,accel=kvm -m 2048 -smp 2 \
  -drive file=haiku/generated.x86_64/r5-os.image,format=raw,if=ide \
  -vga std -usb -device usb-tablet \
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
  -device intel-hda -device hda-duplex
```

Profiles: `r5-raw` (QEMU/disk), `r5-anyboot` (ISO/USB), `r5-vmware`.

`--distro-compatibility compatible` stays on until we deliberately change the system API.

## Architectures

R5OS is **64-bit only**: **AMD64** first (reference machine: Ryzen 5 5625U), then **ARM64**, then **RISC-V 64**. No focus on 32-bit or the other Haiku ports. See `distro/TODO.md` and `AGENTS.md`.

Do **not** clone onto `/mnt/c/...` if you build in WSL: NTFS is case-insensitive and slow. The overlay may live on Windows; keep `haiku/` and `buildtools/` on the Linux filesystem (`~/Projects/R5OS`).

## Git: R5OS is the source of truth

```text
origin     our product repo (this tree)        push/pull
upstream   https://review.haiku-os.org/haiku   fetch only
branch     r5   (Haiku sources, separate clone)
```

GitHub pull requests on `origin` target **`main`**.

1. Create an empty repo for the Haiku-source fork if you still use one (GitHub, Codeberg, or Origin).
2. `export R5OS_HAIKU_ORIGIN=git@github.com:<you>/r5os-haiku.git`
3. `./scripts/setup-wsl.sh`, then `git -C haiku push -u origin r5`
4. Fix: `./scripts/new-bugfix.sh 12345 short-slug` → commit → merge to `r5` → `./scripts/publish-bugfix.sh` → `python3 scripts/tracker-set.py 12345 fixed --commit <sha>`
5. Optional Haiku commits: `./scripts/sync-upstream.sh`

## Tracking open Haiku bugs

Haiku’s Trac is the list of open bugs; **`tracker/`** is our workbook. Per ticket: `open`, `in-progress`, `fixed`, `wontfix`, or `not-applicable`. Import never overwrites an R5 status.

Trac blocks automated downloads. Once (and now and then) save CSV in a browser:

1. [Open tickets (CSV query)](https://dev.haiku-os.org/query?status=new&status=assigned&status=reopened&col=id&col=summary&col=status&col=type&col=priority&col=component&col=milestone&order=id&max=10000)
2. Download **Comma-delimited Text** at the bottom
3. `./scripts/tracker-sync.sh ~/Downloads/query.csv`

`tracker/STATUS.md` is the overview: how many open, in progress, fixed. We do not send patches to Haiku; their ticket number is only the id in our fork.

## Branding

Do not use Haiku or BeOS names or logos on the image or website. Our name is **R5OS**. About R5OS must show the disclaimer from `distro/DISCLAIMER.txt`.

## Version

Public releases: **R5OS 2026.1**, **2026.2**, … (year, then +1 per release; a new year starts at `.1` again).

Unreleased trees use a `-dev` suffix. About shows **R5OS YYYY.N-dev Build N**. Jam uses `HAIKU_REVISION=r5-<VERSION>`, not the build number. Bump `distro/BUILD` only when adding a What’s new section.

Open distro tasks (including leftover Haiku wording in the UI): `distro/TODO.md`.
