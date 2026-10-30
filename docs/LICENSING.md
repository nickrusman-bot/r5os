# R5OS licensing

R5OS is an independent, multi-license operating system based on Haiku. It is not an MIT-only project. Different open-source licenses may sit in the same repository, image, and release. One license in the tree does not pull every other component under it.

This file is the policy. `AGENTS.md` points here. `THIRD_PARTY_NOTICES.md` is the readable list of components we have actually identified. `LICENSES/` holds unmodified license texts for licenses this repository declares.

A file-by-file audit of the Haiku tree, the build packages, firmware, and generated files has **not** been done. Unknown components stay unresolved. A later REUSE check would not by itself mean the distribution is compliant.

## Default for new R5OS code

New source that R5OS owns — applications, services, and drivers — defaults to **GPL-3.0-or-later**, when R5OS has the rights to license that file.

```cpp
// SPDX-FileCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
```

Use that header only for a file R5OS can license as a whole. Do not put it on a Haiku file, on third-party code, or on a file whose origin is unclear.

Existing Haiku code keeps its original license and copyright notices. Do not relicense the Haiku tree, and do not strip MIT, BSD, GPL, or other notices.

## Kernel and drivers

GPL-3.0-or-later is the default for new R5OS-owned code. It is not a rule that every kernel or driver file must use that license.

- GPL-2.0-only code stays GPL-2.0-only. Do not relicense it as GPL-3.0.
- GPL-2.0-or-later may be offered under GPL-3.0 only when that grant is what the original license actually says.
- A new R5OS component uses the license that fits a legal integration. Record which license and why.
- Do not drop a suitable driver only to keep one GPL version across the tree.
- Do not combine GPL-2.0-only code with GPL-3.0-only or GPL-3.0-or-later code into one combined work.
- GPL-2.0-only code may be shipped beside GPL-3.0 code when it stays an independent work and its own terms are met.
- A separate source file or kernel module does not by itself make a component independent of the kernel it links into.

MIT, BSD, and Apache-2.0 code can go into a GPL-3.0 combined work when their notices and other conditions stay intact. The GPL-3.0 obligations then apply to that combined work. The permissive notices still apply to those parts.

Flag an uncertain kernel integration for legal review before it is put in an image.

## How components combine

Judge the real relationship, not the directory name:

1. Independent programs distributed together (aggregation).
2. Libraries linked dynamically.
3. Libraries linked statically.
4. Source copied into another project.
5. Drivers loaded as kernel modules.
6. Code compiled into the kernel.
7. Programs that only talk over IPC or another system interface.

Starting point, not a substitute for the license text:

| License | With GPL-3.0 |
|---|---|
| MIT, BSD-2-Clause, BSD-3-Clause | Compatible, if their conditions are kept |
| BSD-4-Clause | Generally incompatible |
| Apache-2.0 | Compatible with GPL-3.0 |
| GPL-2.0-only | Not compatible in one combined work |
| GPL-2.0-or-later | May be distributed under GPL-3.0 |
| GPL-3.0-only, GPL-3.0-or-later | Compatible with GPL-3.0 |
| LGPL-2.1 | Check the linking rules in that license |
| LGPL-3.0 | Compatible with GPL-3.0 |
| MPL-2.0 | Conditional, including its secondary-license terms |
| AGPL-3.0 | Compatible in combination; network-use source duties also apply |

`OR` in an SPDX expression means the recipient may choose. `AND` means every listed license applies. Do not write `OR` to skip a question.

## Haiku files and R5OS edits

When changing an existing Haiku file:

- Keep the original copyright lines and license header.
- Do not replace an MIT header with a GPL header because R5OS code was added.
- Do not claim ownership of the Haiku work.
- Add a copyright line only where there is a real basis for it.

When a clearly separate R5OS piece is added inside a differently licensed file, mark it with the REUSE snippet tags. Example:

```cpp
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

void R5OS_NewFeature()
{
}

// SPDX-SnippetEnd
```

Use that only around code that can honestly be treated as its own contribution. If the new behavior is woven through the Haiku function, the snippet does not make it a separate work. Prefer a new file when that split is technically real. Do not use SPDX tags to imply that a GPL piece is independent of the code it is built into.

Do not rewrite Haiku headers in bulk. Do not run an automated license rewrite without a reviewed inventory. Where a header should stay untouched, use a sidecar `.license` file or `REUSE.toml` after that file has been checked.

## Third-party code

Before a component is introduced, report:

1. Project and source URL.
2. License and SPDX identifier, including `-only` or `-or-later`.
3. Copyright holders.
4. Direct dependencies that matter, and relevant transitive ones.
5. How it is integrated.
6. Whether it can sit with GPL-3.0, or which other compatibility question applies.
7. Patents, firmware, trademarks, attribution, and redistribution limits.
8. What a binary release must ship as source.
9. Whether a lawyer still needs to look at it.

Check individual files when the project is not one license. Do not relicense third-party GPL as MIT. Do not remove upstream conditions. Modifications must be allowed by that license, and they must be disclosed when the license requires it.

Firmware and other blobs are a separate question. An open driver does not license its firmware. Leave unclear redistribution rights out of official images.

## AI-assisted code

AI-assisted development is allowed. Generated code is not assumed to be public domain, and it is not assumed to be original. Review substantial generated kernel, graphics, network, and protocol code for a match to existing third-party work. If it follows an existing implementation, name that source and keep its license. Do not assign copyright to R5OS for code R5OS does not own. Do not list AI output as a human author. For new R5OS-owned work, use GPL-3.0-or-later when the rights are there. Note the origin of a large assisted implementation when that helps a later license review.

## SPDX, REUSE, and license texts

New R5OS files and other files we deliberately cover follow the [REUSE specification](https://reuse.software/).

`LICENSES/` contains the unmodified text for each license a covered file actually uses. The filenames are SPDX identifiers. Today:

- `LICENSES/GPL-3.0-or-later.txt` — GNU GPL version 3. The “or later” is the grant on new R5OS code, not a different document.
- `LICENSES/MIT.txt` — MIT, used by Haiku and by components already in this tree.

Add a text when a covered file needs it. Do not edit the official wording. A root `LICENSE` file, if one is added later, explains this policy. It does not override per-file notices.

## Notices, releases, and About

`THIRD_PARTY_NOTICES.md` lists components that have been identified: name, version, project, license, whether we changed it, how it is integrated, and what a release must ship. Unresolved stays labeled unresolved. The list is not legal approval.

For a public image, check what the image actually contains. Where the license requires it, ship the license text, copyright notices, and the corresponding source for that exact build, including scripts needed to build it. GPL-3.0 also has installation-information duties for user products. Publishing this git repository is not automatically enough. The source offer has to match the binaries people received.

About R5OS must grow a Credits & Licenses view fed from this inventory, not from a second hand-maintained list. It credits Haiku and its contributors, states that R5OS is an independent Haiku-based system, and lists third-party components with copyright, license, and source URL. What’s new, system information, and credits stay separate. That view is **not built yet**. It has not been checked against an image.

## Checks we have not wired up

Planned for the release checklist, not running now: `reuse lint`, an SPDX bill of materials, and a scan for missing notices. Those tools check metadata shape. They do not decide whether two components form one work.

No Developer Certificate of Origin is in place. Do not state that R5OS owns everything in the repository.

## Inventory

Not a file-by-file audit. Read the header of the component you touch. Haiku itself is mixed.

| Component | In this git tree? | License as documented | Notes |
|---|---|---|---|
| AirPlay receiver pieces | yes, `distro/airplay` | MIT (Shairport, ALAC), Unlicense (tiny-AES-c), OpenSSL 3 from HaikuPorts | Detail: `distro/airplay/THIRD_PARTY.md`. Unlicense and Apache-2.0 texts are not in `LICENSES/` yet |
| VirtualBox 7.2.22 Haiku Guest Additions | no | GPL-3.0-only (`COPYING` and the Haiku additions makefile). Some IPRT files are GPL-3.0-only OR CDDL-1.0. The HaikuPorts 6.1.26 recipe’s GPL-2.0 plus CDDL is an older recipe | Cross-built 2026-10-30 outside the repo (`kmk` exit 0, ELF64 AMD64). Not in the image. Userspace programs and kernel modules are assessed separately below |
| Mesa 22.0.5 | build package only | Mesa is MIT; this package was not re-audited file by file | Not a claim that the guest ships Vulkan |
| Linux DRM / `amdgpu` | no | GPL (kernel) | Not imported |

Add a row when a component is introduced. Update the row when its license or integration changes.

### VirtualBox 7.2.22, assessed against this policy

Source: Oracle VirtualBox 7.2.22, built from the Haiku guest additions in that tarball. Not from HaikuPorts 6.1.26.

The additions are GPL-3.0-only. That matches the default direction for new R5OS code. It is not MIT, and it must not be pasted into MIT sources or relicensed. Files that say GPL-3.0-only OR CDDL-1.0 can be used under the GPL-3.0-only choice; the CDDL terms are then not the ones we take.

Userspace `VBoxService`, `VBoxTray`, and `VBoxControl` are separate programs. Shipping them next to the rest of R5OS is aggregation if they are not linked into Haiku applications. A binary image that includes them must ship this GPL-3.0 text and the corresponding 7.2.22 source, including our build fixes.

Kernel modules (`vboxguest`, `vboxdev`, `vboxsf`, `vboxvideo`, and the accelerant) link into Haiku. MIT code may be combined with GPL-3.0, and the GPL-3.0 conditions apply to that combination. A separate module does not answer how far that combination reaches into the kernel. Those modules stay out of the image until that scope is reviewed. The userspace clipboard can proceed without waiting on that kernel question.

Nothing from this tarball is in git or in the current image. The COM1 clipboard is unchanged.
