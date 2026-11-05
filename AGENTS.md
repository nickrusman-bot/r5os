# R5OS

Independent Haiku-based OS. AI-assisted work is allowed here.

Haiku is a codebase and a source of fixes, not the product owner. Do not open PRs against `haiku/haiku`. Do not push to `git.haiku-os.org` or `review.haiku-os.org`. Haiku does not accept AI-generated or AI-modified contributions.

`upstream` is fetch-only (optional merge into `r5`). `origin` is source of truth.

GitHub PRs on `origin` target **`main`**. Do not stack PRs unless the user asks.

Create PRs with Cursor `ManagePullRequest`. If that fails (GitHub App missing), give the compare URL. Do not silently use `gh pr create` unless the user asks.

Roadmap (R5 Hub, Store, Vulkan, …): `docs/ROADMAP.md`. Do not implement unrelated roadmap items during a focused fix.

## GFX9 ring (Renoir)

The sequence that landed the fence is `docs/GRAPHICS.md`, section `GFX9 ring on GC 9.3 (fixed)`. Do not change that order, those registers, the PSP load, the firmware blobs, or the ring contents unless the user asks. MEC is started before the GFX ring. A change needs a boot on the 5625U that writes scratch `0xDEADBEEF`, recorded in `CHANGELOG.md`, and an update of that contract. The readback check stays. The ring stays off until the radeon_hd driver settings say `gfx9_ring true`, until the user says otherwise. The workload after the first fence (`docs/GRAPHICS.md`, `GFX9 workload on GC 9.3 (fixed)`) follows the same rule, and stays off until the settings say `gfx9_workload true`.

## Settings window

This layout is fixed. Do not move it.

- The list starts under the yellow tab and runs down to Search. The list and Search are 200 wide. A home card is at least 280 wide. The window opens at 1030 by 610. The shortest size is 910 by 560. Pages under a category are alphabetical by name. Information stays first under System. A category card and the subtitle of an open page use that page's description.
- Leave that order, and these five descriptions, as they are until the user explicitly asks to change them. Another Settings task does not include reordering the list or rewriting these lines. Information: "View system details". Virtual memory: "Extra memory stored on disk". Wallpaper: "Set the background of your desktop". Desktop and Deskbar: "Adjust desktop and Deskbar settings". Notifications: "Manage alerts and notifications". Each line is both the card and the subtitle.
- The title and the subtitle sit beside the list.
- Every page of this Settings application shows its icon at the right of that title block. A new page adds its icon in `module_icon()` in `distro/settings/SettingsWindow.cpp`. Home uses the Preferences folder icon. A category uses the icon already stored for that category.
- Those page icons, and the icons on the home cards, get the Haiku preferences wrench on top. That is `r5_settings_apply_prefs_overlay()`. Do not draw one of those icons without it. The wrench is only for this Settings application and the modules inside it. About R5 is not a settings module and does not get the wrench.
- The page content is the only part that changes. It sits under the subtitle. When a page is taller than that area, a scrollbar appears on the page. The list, the title, and the bottom row do not move.
- A box title in ordinary text is bold: the screen size (or Display), Workspaces, Applications, Menu, Window, Preview, Device, and R5 Operating System. Enable notifications and Enable virtual memory are bold because they are the box title. A menu on the box border stays plain.
- Desktop and Deskbar is a page in this window. It messages the running Deskbar as soon as a value changes, and it does not write Deskbar's settings file. The separate Deskbar preferences program stays installed. The clock is not part of that page. Where the bar sits is chosen on the screen picture.
- `r5-desktop` gives the session user `config/cache` and `config/settings`, including `settings/system`, `settings/kernel`, and `settings/kernel/drivers`, before it drops uid. The image creates those directories as root, mode 0755. Media writes the latency choice in `kernel/drivers`.
- Notifications is a page under System. It writes `~/config/settings/system/notifications` immediately, the file the notification server reads. Where a notification appears is a screen picture: the four corners, or against the Deskbar. The notification server is a system service (uid 0) and must not create that file. The separate Notifications program stays installed. Defaults restores notifications on, a timeout of 10 seconds, a width of 300, and the lower right. It does not clear the application list. Revert restores the whole page, including that list.
- Search stays under the list. The bottom row is Back, Home, the lock, Revert, and Defaults, in that order. Back, Home, and the lock are icon buttons of the same height as Revert and Defaults. Back and Home call `r5_button_set_icon()` with `R5_ICON_BACK` and `R5_ICON_HOME`. The lock calls `r5_button_set_lock()`. Closed is `R5_ICON_LOCKED` (Haiku `Action_Logout`). Open is `R5_ICON_UNLOCKED` (Haiku `Action_Login`). They do not get the wrench. A page that requires an administrator is gray, and nothing is written, until the lock is open. One unlock covers every such page for the existing grant. An administrator starts unlocked. Lists still scroll.
- Appearance is a page under Personalization. The subtitle is "Personalize how the system looks". The tabs are Fonts, Colors, Look and feel, and Antialiasing, matching the separate Appearance program. A change is applied at once. Revert and Defaults are the window buttons. The page is not locked. The separate Appearance program stays installed. Colors and fonts is not a list item, because this page was asked for in its place. That does not change the order or the five descriptions above.
- Display is a page under System. It stays gray until an administrator opens the lock. Resolution, colors, and refresh wait for Apply, then ask to keep the picture. After 12 seconds without a choice the previous picture returns. Workspace columns and rows change immediately. Revert restores the mode and the workspace layout from when the page was opened. The separate Screen program stays installed. The screen picture is `r5_screen_draw()` in `libr5ui.so`. Notifications and Desktop and Deskbar use that call.
- Virtual memory is a page under System. It writes `/boot/system/settings/kernel/drivers/virtual_memory` immediately, the file the kernel reads. A change takes effect on the next start. The separate VirtualMemory program stays installed. Defaults restores swap on, automatic size, and the boot volume. Revert restores the values from when the page was opened.
- A change is applied immediately. There is no Save button. Revert puts the page back to the values from when it was opened, and applies those. Defaults puts a page back to its factory setup, and applies that. Defaults stays disabled on a page that has no factory setup.

Do not open Tracker’s private resources, and do not read `libtracker.so`, to draw an icon. The public calls are `r5_icon_bitmap()`, `r5_vector_icon_bitmap()`, `r5_file_icon_bitmap()`, and `r5_mime_icon_bitmap()`, plus the matching `r5_button_set_*()` calls that size the button, including `r5_button_set_lock()`. Predefined icons are `R5_ICON_BACK`, `R5_ICON_FORWARD`, `R5_ICON_UP`, `R5_ICON_HOME`, `R5_ICON_UNLOCKED`, and `R5_ICON_LOCKED`. Their numbers stay fixed. A new predefined icon is added at the end of that enum, with the unmodified picture and a line in `docs/ICONS.md`. Any other icon is a vector buffer, a file path, or a MIME type passed to those calls. The library is the shared library `libr5icons.so` from `distro/icons`. The regular image and the minimum image both install it in `/system/lib`. Link `libr5icons.so`. Do not put these calls in `libbe`.

`docs/SETTINGS.md` describes the same frame. `docs/ICONS.md` is the icon contract. `docs/LIBR5.md` is the shared-library contract.

## LibR5

These libraries sit next to `libbe`. They do not replace it. A Haiku program that links only `libbe` keeps that ABI. Do not add these calls to `libbe`. Do not add a function here only to rename a `libbe` call.

A helper written for R5OS, and used by more than one program, goes in one of these libraries. Add the function at the end. Do not change a function that already shipped. Describe the call in `docs/LIBR5.md` in the same change. The C++ is GPL-3.0-or-later. A program that links one of these libraries combines with that license. A program that does not link it is unchanged.

- `libr5icons.so`, header `R5Icons.h`, source `distro/icons`. Icons. `docs/ICONS.md`.
- `libr5ui.so`, header `R5UI.h`, source `distro/ui`. The screen picture. `docs/LIBR5.md`.

The regular image and the minimum image both install them in `/system/lib`. The soname has no version suffix.

## Compatibility

Keep Haiku APIs and, where practical, ABI for the same CPU architecture. Prefer additive APIs. Do not replace a working component only to look modern.

R5OS may add APIs, services, and desktop features without upstream approval. Do not revert R5OS behavior just to match Haiku. Do not merge `upstream` blindly: check overlays, APIs, and supported arches.

If a break is required: document why, what it hits, and the migration path.

## Licensing

Full policy, inventory, and the VirtualBox 7.2.22 assessment: `docs/LICENSING.md`. User-facing list: `THIRD_PARTY_NOTICES.md`. Unmodified texts: `LICENSES/`.

R5OS is multi-license. New R5OS-owned files default to `GPL-3.0-or-later` with an SPDX header, when R5OS has the rights to that whole file. Haiku files keep their original license and copyright. Do not replace an MIT header with GPL. Do not relicense GPL-2.0-only code as GPL-3.0. Do not combine GPL-2.0-only with GPL-3.0 in one work. A separate kernel module does not by itself make GPL code independent of the kernel.

Do not assume the Haiku tree is all MIT. Do not drop a suitable driver only to keep a single license. Do not rewrite license headers in bulk. The file-by-file audit, REUSE lint, and About credits view are not done.

When a file contains both original Haiku code and R5OS changes, keep the MIT header and copyright. Mark only the R5OS portions with SPDX-Snippet tags and a GPL-3.0-or-later notice. Do not relicense the entire file. The combined binary is distributed under GPL-3.0-or-later.

Before adding a component, report project, SPDX license (`-only` or `-or-later`), copyright, integration, source-offer duties, and combination questions. Uncertain kernel integration stays out of the image until reviewed. Credit shipped third-party code from `THIRD_PARTY_NOTICES.md` (AirPlay detail: `distro/airplay/THIRD_PARTY.md`). Do not invent copyright holders.

## AI

Use AI to read code, patch, port, test, review races/overflows, and write changelog/tracker files. Generated code is not automatically correct. Verify against source, build, and boot. Prefer small diffs. No drive-by refactors.

Kernel, MMU, scheduler, drivers, filesystems: inspect the path and fail cases. Compile success is not a kernel fix.

## Architectures

Official targets are **64-bit only**:

1. **AMD64** (`x86_64`) — primary. Ryzen 5 5625U is the reference machine.
2. **ARM64** (`aarch64`) — QEMU first, then hardware.
3. **RISC-V 64** (`riscv64`, RV64GC) — after AMD64/ARM64 are usable.

Do not spend effort on 32-bit x86, ARM32, PowerPC, SPARC, or m68k except to keep the tree building. Do not drop Haiku 32-bit ports in drive-by commits.

One OS, three targets. Keep CPU-specific init, traps, page tables, and atomics in arch directories. AMD64 success does not prove ARM64 or RISC-V.

Support levels (do not skip): **buildable → bootable → functional → desktop-capable → release-supported**. Compiling is not “supported”.

## Development loop

1. Run relevant tests; inspect the diff for unrelated edits.
2. Commit locally. Ticket-status changes must include `python3 scripts/tracker-report.py` plus `tracker/r5-status.tsv` and `tracker/STATUS.md` in the same commit.
3. Apply overlays into the WSL Haiku tree (`scripts/apply-overlay.sh` or the relevant `apply-*.py`). `scripts/jam-r5-raw.sh` does not apply overlays by itself.
4. Jam AMD64 (`scripts/jam-r5-raw.sh` in WSL).
5. Boot QEMU (`scripts/run-qemu-r5.sh`).
6. For arch-specific code, test that arch (or state that it was not tested).
7. Update `CHANGELOG.md` (newest first) for user-visible behavior, chosen paths, mistakes, and limits.
8. Ask before `git push` / PR to `main`.

Never imply hardware, Wi-Fi, or LAN tests that did not run. **QEMU usernet is not a LAN** (no mDNS multicast, no Wi-Fi device, no AirPlay speakers). For LAN on Windows use VirtualBox bridged (`scripts/run-vbox-r5.ps1`); no Guest Additions (no vboxsf, no auto-resize, no VBox drag-and-drop). Text clipboard is COM1, not VBox Shared Clipboard.

If a test cannot run, say which one and why.

## Overlays and native source

R5OS is an independent Haiku fork. `origin` is the product. `upstream` is fetch-only. Do not push to Haiku remotes or open PRs against Haiku.

All OS behavior and R5OS changes belong in native source under `distro/` (copied into the Haiku tree at apply time). Do not add new overlays. Do not add Python whose purpose is a string patch of Haiku C++.

Copy relevant Haiku files into `distro/` when you need to change them. Keep the original MIT license and copyright notice. Apply R5OS changes on top and mark those changes with SPDX-Snippet tags and a GPL-3.0-or-later notice. The original Haiku portions remain MIT. The combined binary is distributed under GPL-3.0-or-later.

Existing `apply-*.py` may only copy `distro/` files, Jam `SubInclude`, launch jobs, or other idempotent hooks. Fail loudly if a required pattern is missing. New features go in `distro/users`, `distro/login`, `distro/graphics`, `distro/media`, `distro/kernel`, and the other `distro/` trees — not `replace_once`.

`replace_once` matching a short substring can insert duplicates. After apply, grep for doubled `SYSTEM_SERVERS`, `SYSTEM_PREFERENCES`, launch jobs, and includes.

Hook new `apply-*.py` into `scripts/apply-overlay.sh`. Keep upstream imports distinct from R5OS-only changes. Temporary overlays must not become silent architecture.

To retire an overlay: land the native replacement in `distro/`, jam, boot, compare behavior, then remove the old patch. Do not delete overlays wholesale. Keep Python for jam, image, tracker, and tests.

Older string patches are still in some `apply-*.py` scripts. Do not add more. Retire them by copying the file into `distro/` as above. Inventory and migration order for accounts: `docs/MULTIUSER.md`.

## Multi-user

Product accounts: one **administrator** (login name `user`, home `/boot/home`) and optional **standard** people under `/boot/home/users/<name>`. Names like `nicky` in tests are fixtures from Users & Accounts, not image users. System logins (`sshd`, `ftp`, `nobody`) are not people.

First interactive session is still that administrator, often uid 0 until migration. Sign in gates Tracker/Deskbar; Log out returns to Sign in on the same session. Extra homes are mode `0700`. Uid 0 still reads them. Do not claim a per-user desktop, or that the administrator cannot read other homes, until that is implemented **and** tested.

Keep `/boot/home` as the administrator’s home (Haiku apps and `find_directory`). Do not hardcode home paths in apps; use `find_directory` / POSIX `getpwuid`.

One graphical session at a time until fast user switching exists. Do not run the whole desktop as root to make admin UI easy. Privileged work (useradd) through a checked helper. Haiku already has `src/apps/login` (`BLaunchRoster().StartSession`) and `src/bin/multiuser` (`useradd`, `passwd`); reuse them. Ticket #8940 is the login-screen tracker item.

Password material must not appear in logs, process argv, or What’s new text.

## launch_daemon and Deskbar

- A **service** that `Quit()`s is restarted forever.
- A **job** without an `on …` event often never starts. User-session jobs belong in `data/launch/user` under `target desktop` (e.g. `on initial_volumes_mounted`, `requires` Deskbar).
- Deskbar replicants: export `instantiate_deskbar_item` **and** a non-inline `_EXPORT Class::Instantiate(BMessage*)`. Do not `load_add_on` a full server (OpenSSL, Media Kit, globals) into Deskbar. `BView::Archive` may write `class=BView` first; replace it. Dummy AddItem id `999` plus `HasItem=0` means unarchive failed.

## Networking claims

Do not claim AirPlay 2 pairing/PTP, mirroring, or LAN discovery unless implemented and tested on a real network. Credit third-party code (license + AboutSystem / `THIRD_PARTY_NOTICES.md` / `docs/LICENSING.md`).

## Tracker and changelog

Keep `tracker/STATUS.md` current. After any ticket-status change, run `python3 scripts/tracker-report.py` and commit `tracker/r5-status.tsv` with `STATUS.md`. Implemented ≠ tested ≠ verified.

`CHANGELOG.md`: problem, what changed, upstream ticket if any, limits, how/where tested. Newest first. Record mistakes and the fix. Do not claim working behavior without evidence. Distinguish compile, test, and confirmed R5OS/LAN results. Never invent test results.

After a task, report **Veranderd / Getest / Onbewezen / Documentatie**. Tracker edits: `python3 scripts/tracker-report.py` and commit `tracker/r5-status.tsv` with `STATUS.md`. Code, changelog, and tracker must not contradict each other.

GitHub issues on `origin` are the lead, including open Haiku Trac tickets. Those are labeled `haiku` and mapped in `tracker/github.tsv` (`<!-- haiku-ticket:ID -->` in the body). Multi-user: [#63](https://github.com/nickrusman-bot/r5os/issues/63). Mixxx: [#55](https://github.com/nickrusman-bot/r5os/issues/55). After a new Trac CSV: `scripts/tracker-import.py`, then `scripts/sync-github-issues.py`. The sync creates missing issues and closes ones already `fixed` on `origin/main`, with a comment that quotes the status note. It does not reopen a closed issue because Trac still lists it. When a change lands: comment on the matching issue with build, limits, and how/where it was tested; close it if that request is done. If only part shipped, comment and leave it open. Do not mark CHANGELOG/STATUS done while the GitHub issue stays open, or the reverse. What's new stays user language. The build link belongs on the GitHub issue.

## Versioning

Public: **R5OS YYYY.N** (`2026.1`, …). New year resets N to 1. Unreleased: `YYYY.N-dev` in `distro/VERSION`.

Integer **build** in `distro/BUILD` (authoritative). Sequence: `10001`, `10002`, `10003`, … Never reuse or reset. About shows `R5OS YYYY.N-dev Build N` from `r5_os_version.h` generated at apply/jam (`R5_OS_VERSION`, `R5_OS_BUILD`). Do not hardcode a build number in About, splash, or `wsl-install-branding.sh`.

Jam `HAIKU_REVISION` is `r5-` plus the VERSION string (`scripts/r5-version.sh --revision`), not the build number. Do not use Haiku `hrev` as the distro version. Keep upstream hrev only where it is still needed for debugging.

Bump `distro/BUILD` by **exactly one** when adding a **What's new** section (`distro/WHATSNEW`). Do not bump on every jam. Headings: `R5 <VERSION> Build <N> - Title`. Newest build first. Preserve older build sections; do not reassign history.

About must keep version+build visible while What’s new is selected. What’s new is for users (no tickets, hashes, filenames). `CHANGELOG.md` is technical. They must not contradict each other. Do not advertise planned work as shipping.

## Code style

Under `haiku/`: Haiku guidelines (tabs, 100 columns, InterCaps, `f`/`k`/`g`/`s`, two blank lines between functions, C++ casts, `int32`/`status_t`).

https://www.haiku-os.org/development/coding-guidelines/

Match the file you edit. Do not mix a fix with a reformat. Style-only commits say “no functional change”. Use `haiku-format` / `src/tools/checkstyle/` when available.

Desktop forms and the account card: `docs/STYLE.md`.
