# R5OS

Independent Haiku-based OS. AI-assisted work is allowed here.

Haiku is a codebase and a source of fixes, not the product owner. Do not open PRs against `haiku/haiku`. Do not push to `git.haiku-os.org` or `review.haiku-os.org`. Haiku does not accept AI-generated or AI-modified contributions.

`upstream` is fetch-only (optional merge into `r5`). `origin` is source of truth.

GitHub PRs on `origin` target **`main`**. Do not stack PRs unless the user asks.

Create PRs with Cursor `ManagePullRequest`. If that fails (GitHub App missing), give the compare URL. Do not silently use `gh pr create` unless the user asks.

Roadmap (R5 Hub, Store, Vulkan, …): `docs/ROADMAP.md`. Do not implement unrelated roadmap items during a focused fix.

## Compatibility

Keep Haiku APIs and, where practical, ABI for the same CPU architecture. Prefer additive APIs. Do not replace a working component only to look modern.

R5OS may add APIs, services, and desktop features without upstream approval. Do not revert R5OS behavior just to match Haiku. Do not merge `upstream` blindly: check overlays, APIs, and supported arches.

If a break is required: document why, what it hits, and the migration path.

## Licensing

Full policy, inventory, and the VirtualBox 7.2.22 assessment: `docs/LICENSING.md`. User-facing list: `THIRD_PARTY_NOTICES.md`. Unmodified texts: `LICENSES/`.

R5OS is multi-license. New R5OS-owned files default to `GPL-3.0-or-later` with an SPDX header, when R5OS has the rights to that whole file. Haiku files keep their original license and copyright. Do not replace an MIT header with GPL. Do not relicense GPL-2.0-only code as GPL-3.0. Do not combine GPL-2.0-only with GPL-3.0 in one work. A separate kernel module does not by itself make GPL code independent of the kernel.

Do not assume the Haiku tree is all MIT. Do not drop a suitable driver only to keep a single license. Do not rewrite license headers in bulk. The file-by-file audit, REUSE lint, and About credits view are not done.

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

R5OS is an independent Haiku **fork**. `origin` is the product. `upstream` is fetch-only. Do not push to Haiku remotes or open PRs against Haiku.

OS behavior belongs in **native source** under `distro/` (copied into the Haiku tree at apply time) or, later, in an in-tree `haiku/` checkout. Do **not** add new overlays. Do **not** add Python whose purpose is a string patch of Haiku C++. Existing `apply-*.py` may only **copy** `distro/` files, Jam `SubInclude`, launch jobs, or other idempotent hooks. Fail loudly if a required pattern is missing. New features go in `distro/users`, `distro/login`, … — not `replace_once`.

`replace_once` matching a short substring can insert **duplicates**. After apply, grep for doubled `SYSTEM_SERVERS`, `SYSTEM_PREFERENCES`, launch jobs, and includes.

Hook new `apply-*.py` into `scripts/apply-overlay.sh`. Keep upstream imports distinct from R5OS-only changes. Temporary overlays must not become silent architecture.

To retire an overlay: land the native replacement, jam, boot, compare behavior, then remove the old patch. Do not delete overlays wholesale. Keep Python for jam/image/tracker/tests.

Inventory and migration order: `docs/MULTIUSER.md`.

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

Bump `distro/BUILD` by **exactly one** when adding a **What's new** section (`distro/WHATSNEW`). Do not bump on every jam. Headings: `R5OS <VERSION> Build <N> - Title`. Newest build first. Preserve older build sections; do not reassign history.

About must keep version+build visible while What’s new is selected. What’s new is for users (no tickets, hashes, filenames). `CHANGELOG.md` is technical. They must not contradict each other. Do not advertise planned work as shipping.

## Code style

Under `haiku/`: Haiku guidelines (tabs, 100 columns, InterCaps, `f`/`k`/`g`/`s`, two blank lines between functions, C++ casts, `int32`/`status_t`).

https://www.haiku-os.org/development/coding-guidelines/

Match the file you edit. Do not mix a fix with a reformat. Style-only commits say “no functional change”. Use `haiku-format` / `src/tools/checkstyle/` when available.

Desktop forms and the account card: `docs/STYLE.md`.
