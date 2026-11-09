# R5 licensing

R5 is an independent, multi-license operating system based on Haiku. It is not an MIT-only project. Different open-source licenses may sit in the same repository, image, and release. One license in the tree does not pull every other component under it.

This file is the policy. `AGENTS.md` points here. `THIRD_PARTY_NOTICES.md` is the readable list of components we have actually identified. `LICENSES/` holds unmodified license texts for licenses this repository declares.

A file-by-file audit of the Haiku tree, the build packages, firmware, and generated files has **not** been done. Unknown components stay unresolved. A later REUSE check would not by itself mean the distribution is compliant.

## Default for new R5 code

New source that R5 owns — applications, services, and drivers — defaults to **GPL-3.0-or-later**, when R5 has the rights to license that file.

A library or public header that applications link or include is MIT when R5 holds the rights to all of it. GPL-3.0-or-later stays the default for the rest of the OS, and GPL code stays GPL.

```cpp
// SPDX-FileCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
```

Use that header only for a file R5 can license as a whole. Do not put it on a Haiku file, on third-party code, or on a file whose origin is unclear.

Existing Haiku code keeps its original license and copyright notices. Do not relicense the Haiku tree, and do not strip MIT, BSD, GPL, or other notices.

The public MIDI kit (the headers plus `libmidi2.so`) is MIT. That is an exception to the GPL default, like the R5 GPU interface and the R5 DRM library, because every MIDI program links it. The R5 parts of the USB MIDI driver and of the MIDI server stay GPL-3.0-or-later.

## Kernel and drivers

GPL-3.0-or-later is the default for new R5-owned code. It is not a rule that every kernel or driver file must use that license.

- GPL-2.0-only code stays GPL-2.0-only. Do not relicense it as GPL-3.0.
- GPL-2.0-or-later may be offered under GPL-3.0 only when that grant is what the original license actually says.
- A new R5 component uses the license that fits a legal integration. Record which license and why.
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

## Haiku files and R5 edits

When changing an existing Haiku file:

- Keep the original copyright lines and license header.
- Do not replace an MIT header with a GPL header because R5 code was added.
- Do not claim ownership of the Haiku work.
- Add a copyright line only where there is a real basis for it.

When a clearly separate R5 piece is added inside a differently licensed file, mark it with the REUSE snippet tags. Example:

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

AI-assisted development is allowed. Generated code is not assumed to be public domain, and it is not assumed to be original. Review substantial generated kernel, graphics, network, and protocol code for a match to existing third-party work. If it follows an existing implementation, name that source and keep its license. Do not assign copyright to R5 for code R5 does not own. Do not list AI output as a human author. For new R5-owned work, use GPL-3.0-or-later when the rights are there. Note the origin of a large assisted implementation when that helps a later license review.

## SPDX, REUSE, and license texts

New R5 files and other files we deliberately cover follow the [REUSE specification](https://reuse.software/).

`LICENSES/` contains the unmodified text for each license a covered file actually uses. The filenames are SPDX identifiers. Today:

- `LICENSES/GPL-3.0-or-later.txt` — GNU GPL version 3. The “or later” is the grant on new R5 code, not a different document.
- `LICENSES/MIT.txt` — MIT, used by Haiku and by components already in this tree.
- `LICENSES/OFL-1.1.txt` — SIL Open Font License 1.1, used by Manrope. The font directory also keeps the upstream `OFL.txt`, which adds the copyright line.

Add a text when a covered file needs it. Do not edit the official wording. A root `LICENSE` file, if one is added later, explains this policy. It does not override per-file notices.

## Notices, releases, and About

`THIRD_PARTY_NOTICES.md` lists components that have been identified: name, version, project, license, whether we changed it, how it is integrated, and what a release must ship. Unresolved stays labeled unresolved. The list is not legal approval.

For a public image, check what the image actually contains. Where the license requires it, ship the license text, copyright notices, and the corresponding source for that exact build, including scripts needed to build it. GPL-3.0 also has installation-information duties for user products. Publishing this git repository is not automatically enough. The source offer has to match the binaries people received.

About has a Licenses tab beside Credits and What's new. The words on that tab are `distro/about/licenses.txt`. Each part is one block, and a new part uses the same block:

```
# Name
One short sentence.

License: SPDX-ID
Source: https://example.com
```

A line that starts with `# ` is the heading. `Source: ` is followed by one URL. This is the list for that tab, not a second copy of every package, and it is not the file-by-file audit. Credits still names people and projects. System information stays on the logo page. The window title is About R5. The tab has not been checked on a booted screen.

## Checks we have not wired up

Planned for the release checklist, not running now: `reuse lint`, an SPDX bill of materials, and a scan for missing notices. Those tools check metadata shape. They do not decide whether two components form one work.

No Developer Certificate of Origin is in place. Do not state that R5 owns everything in the repository.

## Inventory

Not a file-by-file audit. Read the header of the component you touch. Haiku itself is mixed.

| Component | In this git tree? | License as documented | Notes |
|---|---|---|---|
| AirPlay receiver pieces | yes, `distro/airplay` | MIT (Shairport, ALAC), Unlicense (tiny-AES-c), OpenSSL 3 from HaikuPorts. `AirPlayDeskbar` is a separate GPL-3.0-or-later program | Detail: `distro/airplay/THIRD_PARTY.md`. Unlicense text: `LICENSES/Unlicense.txt`. Apache-2.0 text is not in `LICENSES/` yet |
| VirtualBox 7.2.22 Haiku Guest Additions | patch in `distro/vbox-guest`; tarball and binaries copied onto the image at apply time, not stored in git | GPL-3.0-only (`COPYING` and the Haiku additions makefile). Some IPRT files are GPL-3.0-only OR CDDL-1.0. The HaikuPorts 6.1.26 recipe’s GPL-2.0 plus CDDL is an older recipe | The image ships `COPYING` (the unmodified GNU GPL version 3), `VirtualBox-7.2.22.tar.bz2`, and `virtualbox-7.2.22-r5os.patch` under `/boot/system/non-packaged/data/licenses/virtualbox`. Apply stops if that tarball or the built ELFs are missing |
| libglvnd 1.7.0, Mesa 25.3.6 (llvmpipe and Lavapipe), LLVM 21.1.8 runtime, Vulkan loader 1.4.367 | hpkgs in `distro/packages/x86_64`. `mesa_devel` is libglvnd plus Mesa 25 headers. The image solver uses a catalog with Mesa 22 removed | libglvnd and Mesa: MIT. LLVM: Apache-2.0 with LLVM exception. Vulkan loader: Apache-2.0 (Khronos). Not re-audited file by file | OpenGL apps compile against libglvnd. Mesa 25 is the EGL vendor at run time. Drawing is llvmpipe. Lavapipe is installed and was not what the OpenGL probe loaded. Not a GPU driver |
| Manrope Regular and Bold | yes, `distro/fonts/manrope`. Unmodified static TTF from `googlefonts/manrope`, plus that project's `OFL.txt` and the published `FONTLOG.txt` | OFL-1.1. Copyright 2018 The Manrope Project Authors. Designed by Mikhail Sharanda, 2018 | Installed in `data/fonts/ttfonts`. Default plain and bold UI fonts when no font settings are saved. Fixed font stays Noto Sans Mono. No Reserved Font Name is declared. The font is not relicensed. `LICENSES/OFL-1.1.txt` is the SPDX text |
| libglvnd 1.7.0, Mesa 25.3.6 (llvmpipe and Lavapipe), LLVM 21.1.8 runtime, Vulkan loader 1.4.367, vulkaninfo 1.4.367 (`vulkan_tools`) | hpkgs in `distro/packages/x86_64`. `mesa_devel` is libglvnd plus Mesa 25 headers. The image solver uses a catalog with Mesa 22 removed | libglvnd and Mesa: MIT. LLVM: Apache-2.0 with LLVM exception. Vulkan loader and vulkaninfo: Apache-2.0 (Khronos, and for vulkaninfo also Valve, LunarG, RasterGrid). Not re-audited file by file | OpenGL apps compile against libglvnd. Mesa 25 is the EGL vendor at run time. Drawing is llvmpipe. Lavapipe is installed and was not what the OpenGL probe loaded. vulkaninfo is packed and has not been run. Not a GPU driver |
| Linux DRM / `amdgpu` driver | no | GPL (kernel) | The driver is not imported. The GFX9 clear-state table below is a separate MIT file from that tree |
| libdrm 2.4.124 | built from `src-third` at image time, not stored as an hpkg in git | MIT on the files that were read: `xf86drm.c` (Precision Insight, 1999; VA Linux Systems, 2000) and `amdgpu/amdgpu.h` (Advanced Micro Devices, 2014). Not re-audited file by file | Installed as `libdrm.so.2` and `libdrm_amdgpu.so.1`. `amdgpu.ids` is the unmodified data file. Its header names no copyright holder |
| Mesa 25.3.6 RADV, Lavapipe and Zink | hpkgs built from the Mesa recipe and copied onto the image | MIT on the files that were read: `radv_device.c` (Red Hat and Bas Nieuwenhuizen, 2016; based in part on ANV, Intel, 2015), `lvp_device.c` (Red Hat, 2019), `zink_context.c` (Collabora, 2018). `docs/license.rst` says the core is MIT and that other files can differ. Not re-audited file by file | RADV is `libvulkan_radeon.so`. Lavapipe is the software Vulkan device. Zink is the OpenGL-on-Vulkan path and is not the default |

Add a row when a component is introduced. Update the row when its license or integration changes.

### R5 files in `distro/`

Checked file by file on 2026-10-31. A header was added only where the file is one work we can name. Haiku copies keep the copyright and license already in the file. R5 edits that run through those functions were not wrapped in SPDX snippets and were not relicensed.

| Path | SPDX | Why |
|---|---|---|
| `distro/ui/` | MIT | `libr5ui.so` and `<R5UI.h>`. Copyright 2026 R5OS Contributors. Applications link this library, so the C++ is MIT |
| `distro/icons/` except `standard_icons.inc`, `lock_icons.inc`, `TRACKER_ICON_NOTICE.txt`, and `HAIKU_ICON_NOTICE.txt` | MIT | `libr5icons.so` and `<R5Icons.h>`. Copyright 2026 R5OS Contributors. Applications link this library, so the C++ is MIT. The picture bytes are not relicensed |
| `distro/icons/standard_icons.inc`, `TRACKER_ICON_NOTICE.txt` | Open Tracker | Unmodified Tracker HVIF. The notice file stays |
| `distro/icons/lock_icons.inc`, `HAIKU_ICON_NOTICE.txt` | MIT | Unmodified Haiku `Action_Login` and `Action_Logout`. The notice file stays |
| `distro/login/LockWindow.*`, `R5Desktop.cpp`, `SessionLog.h`, `TrayApp.cpp`, `default_deskbar_items.sh` | GPL-3.0-or-later | New R5 files. They link into Login. The other Login sources stay MIT (François Revol, 2008). The Login binary is one combined work; the MIT notices stay on those files |
| `distro/login/Login*.cpp`, `Login*.h`, `DesktopWindow.*`, `main.cpp`, `Login.rdef` | MIT, unchanged | Haiku Login |
| `distro/users/` | GPL-3.0-or-later | New Users & Accounts preference |
| `distro/network/` | GPL-3.0-or-later | New IPv6 and Wi-Fi code. Four headers had said “Copyright 2026, R5OS” and MIT. That was R5’s own grant; those files are now GPL-3.0-or-later. They are compiled into `net_server`. Haiku’s `net_server` files stay MIT. The linked server is one combined work |
| `distro/print/airprint/` | GPL-3.0-or-later | New print add-on, loaded by the print server |
| `distro/vboxclip/` | GPL-3.0-or-later | Separate COM1 clipboard program |
| `distro/zeroconf/` | GPL-3.0-or-later | New mDNS code |
| `distro/airplay/` except `AirPlayDeskbar.cpp`, `AirPlayDeskbar.rdef`, and `third_party/` | MIT | RAOP flow follows Shairport. Not relicensed to GPL |
| `distro/airplay/AirPlayDeskbar.cpp`, `AirPlayDeskbar.rdef` | GPL-3.0-or-later | Separate Deskbar program. It includes the MIT AirPlay headers |
| `distro/airplay/third_party/alac.*` | MIT | David Hammerton, 2005. The permission text in `alac.c` stays |
| `distro/airplay/third_party/aes.*` | Unlicense | kokke tiny-AES-c |
| `distro/app_server/`, `distro/boot/`, `distro/debugger/`, `distro/user-paths/` | unchanged | Haiku files. Headers were not rewritten |
| `distro/kernel/x86/r5_pstate.cpp`, `r5_pstate.h` | MIT | New readout, compiled into the kernel. The kernel is a combined work with Haiku's MIT sources. A GPL-3.0 kernel combination was not reviewed, so this file stays MIT |
| `distro/cpu/r5_pstate_log.cpp` | GPL-3.0-or-later | Separate log program. It only reads `/dev/misc/r5_pstate` |
| `distro/graphics/` `radeon_hd` `accelerant.cpp`, `connector.cpp`, `display.cpp`, `gpu.cpp`, `mode.cpp`, `pll.cpp`, `driver.cpp`, `radeon_hd.cpp` | MIT, with GPL-3.0-or-later snippets | Copies of Haiku’s accelerant and kernel driver. Copyright Haiku, Inc., MIT header kept. R5 edits (Barcelo object table, EDID fallback, PLL 2.2, VRAM size, DCN init that leaves the firmware scanout, DPMS skip, PSP load, opt-in GFX9 start, MEC start, scratch test, and the GFX ring submission) are `SPDX-Snippet` blocks, copyright 2026 R5OS Contributors, GPL-3.0-or-later. The copied file is one combined work; shipping it follows GPL-3.0-or-later and the MIT notices stay. The kernel add-on `radeon_hd` contains GPL-3.0-or-later R5 code (`gfx9_start.cpp`, by the user's decision on 5 Nov 2026). The combined add-on follows GPL-3.0-or-later. The Haiku parts stay MIT. `1002:15e7` is claimed. DCN does not run the DCE encoder, clock, AUX, modeset, or encoder DPMS. Renoir may program the GFX ring for the clear-state preamble and one scratch packet, and restores those registers if the packet does not land |
| `distro/graphics/` `radeon_hd` `clearstate_defs.h`, `clearstate_gfx9.h` | MIT | Unmodified from Linux v6.12 `drivers/gpu/drm/amd/amdgpu/`. Copyright 2012 and 2017 Advanced Micro Devices, Inc. The permission text stays in the files. sha256 `70eb9f9a57d2cacb2ee2d7b9f17485384019ac03c845ce4bea941acdae137285` and `e885c7f6475efd74e7729688a70992ff9543979363eb029c628bd95627fb34b5`. `gfx9_start.cpp` includes them. The combined accelerant follows GPL-3.0-or-later and these MIT notices stay. AMD does not endorse R5 |
| `distro/graphics/third_party/linux-v6.18/amdgpu_ids.c`, `amdgpu_vm.c` | MIT | Unmodified Linux v6.18 `drivers/gpu/drm/amd/amdgpu/`. `amdgpu_ids.c` copyright 2017 Advanced Micro Devices, Inc. `amdgpu_vm.c` copyright 2008 Advanced Micro Devices, Inc., 2008 Red Hat Inc., and 2009 Jerome Glisse. The permission text stays in the files. sha256 `90583999baf5b8b1195f3ddfdd1839fb14db4019354be5699c1f925769b6a7ae` and `799e6a31a822707fa345c7a89cf790c519c0ea6d92563466f234d081bbf56ba5`. Not compiled and not in the image. The driver uses the portable forms in `r5_vmid.h` and `r5_vm.h`. AMD does not endorse R5 |
| `distro/apps/vulkanteapot/fg_teapot_data.h` | MIT | freeglut 3.8.0 `src/fg_teapot_data.h`. Copyright 1999-2000 Pawel W. Olszta. The X11 permission text stays in the file. The control points are Martin Newell's patches as freeglut ships them. Haiku `glut_teapot.c` is not used |
| `distro/firmware/amd-ucode/microcode_amd_fam19h.bin` and `LICENSE.amd-ucode` | AMD microcode license (binary redistribution) | Unmodified container from linux-firmware. Copyright 2010-2025 Advanced Micro Devices, Inc. Not an open-source license. The license text ships next to the blob. Installed at `system/non-packaged/data/firmware/amd-ucode/` so the bootloader can read it |
| `distro/firmware/amdgpu/renoir_pfp.bin`, `renoir_me.bin`, `renoir_ce.bin`, `renoir_rlc.bin`, `renoir_mec.bin`, `green_sardine_pfp.bin`, `green_sardine_me.bin`, `green_sardine_ce.bin`, `green_sardine_rlc.bin`, `green_sardine_mec.bin`, `green_sardine_mec2.bin`, and `LICENSE.amdgpu` | AMD GPU microcode license (binary redistribution) | Unmodified blobs from linux-firmware. `1636` and `164c` use the `renoir` set. Barcelo `15e7` and Cezanne `1638` use `green_sardine`. Copyright 2024 Advanced Micro Devices, Inc. Not an open-source license. The license text ships next to the blobs. Installed at `system/non-packaged/data/firmware/amdgpu/`. Linux sends these through the PSP |
| `distro/kernel/src/system/kernel/arch/x86/arch_timer.cpp` and `arch_cpu.cpp` | MIT and NewOS, with GPL-3.0-or-later snippets | Copies of the Haiku kernel files. Original copyright and MIT/NewOS headers stay. R5 edits (TSC divide guards, Haiku #20400, `r5_pstate_publish`, microcode `installed_cpu` match) are `SPDX-Snippet` blocks, copyright 2026 R5OS Contributors, GPL-3.0-or-later. `scripts/apply-kernel-bugs.py` copies the files. The combined kernel follows GPL-3.0-or-later and the original notices stay |
| `distro/kernel/src/system/kernel/thread.cpp` | MIT and NewOS, with a GPL-3.0-or-later snippet | Copy of Haiku's `thread.cpp`. The dying-team stack is haiku-pi `e09cfa1d9e`, copyright 2026 Witold Bołt, in a GPL-3.0-or-later `SPDX-Snippet`. `scripts/apply-kernel-bugs.py` copies it |
| `distro/kernel/src/system/kernel/vm/vm.cpp` | MIT and NewOS, with GPL-3.0-or-later snippets | Copy of Haiku's `vm.cpp`. The wired-range shrink is haiku-pi `6f810ed2d7`. A protection change skips copy-on-write for a file cache, haiku-pi `894c4abe7f` (star row 21). Copyright 2026 Witold Bołt, in GPL-3.0-or-later `SPDX-Snippet` blocks. `scripts/apply-kernel-bugs.py` copies it |
| `distro/kernel/src/system/kernel/vm/vm.cpp` | MIT and NewOS, with a GPL-3.0-or-later snippet | Copy of Haiku's `vm.cpp`. The wired-range shrink is haiku-pi `6f810ed2d7`, copyright 2026 Witold Bołt, in a GPL-3.0-or-later `SPDX-Snippet`. `scripts/apply-kernel-bugs.py` copies it |
| `distro/kernel/src/system/kernel/debug/user_debugger.cpp` and `distro/kernel/src/system/kernel/scheduler/scheduler.cpp` | MIT, and NewOS for the scheduler, with GPL-3.0-or-later snippets | Copies of Haiku's user debugger and scheduler. The blocks are haiku-pi `5180e96119` (star row 23): a profiling timer is cancelled when the thread exits or is switched away, and it is unlinked before it is armed again. Copyright 2026 Witold Bołt. The snippets are GPL-3.0-or-later. `scripts/apply-kernel-bugs.py` copies the files |
| `distro/kernel/src/system/kernel/device_manager/IORequest.cpp` and `IORequest.h` | MIT, with a GPL-3.0-or-later snippet | Copies of Haiku's I/O request. One block is haiku-pi `90fa3bc401` (star row 19): iteration runs in a loop instead of by recursion. Copyright 2026 Witold Bołt. The snippet is GPL-3.0-or-later. `scripts/apply-kernel-bugs.py` copies the files |
| `distro/kernel/src/system/kernel/vm/vm_page_writer.cpp` | MIT and NewOS, with a GPL-3.0-or-later snippet | Copy of Haiku's page writer. One block is haiku-pi `1539d29eee` (star row 18): after the flush wait times out, the writer still writes the modified pages it has. Copyright 2026 Witold Bołt. The snippet is GPL-3.0-or-later. `scripts/apply-kernel-bugs.py` copies it |
| `distro/kernel/src/add-ons/kernel/bus_managers/pci/pci.cpp`, `distro/kernel/src/add-ons/kernel/busses/pci/ecam/ECAMPCIControllerACPI.cpp`, and `distro/kernel/src/add-ons/kernel/busses/pci/x86/` | MIT, with GPL-3.0-or-later snippets | Copies of the Haiku PCI host sources. Copyright 2022 Haiku, Inc., and for `pci.cpp` also the earlier Haiku authors in that file. Segment 0 is unchanged. The other firmware segments are copyright 2026 R5OS Contributors, GPL-3.0-or-later. `scripts/apply-kernel-bugs.py` copies the files |
| `distro/kernel/src/system/runtime_loader/arch/x86_64/arch_relocate.cpp`, `distro/kernel/src/system/runtime_loader/export.cpp`, `distro/kernel/headers/private/runtime_loader/runtime_loader.h`, `distro/rest/src/system/runtime_loader/elf_tls.h`, `distro/rest/src/system/runtime_loader/r5_tpoff.h`, and the three thread entry files under `distro/kernel/src/system/libroot/` | MIT, and NewOS for the runtime-loader header, with GPL-3.0-or-later snippets | Copies of the Haiku sources. The initial-exec TLS offset is copyright 2026 R5OS Contributors, GPL-3.0-or-later. `scripts/apply-kernel-bugs.py` copies the kernel-tree files. `scripts/apply-rest-native.py` copies the files under `distro/rest/` |
| `distro/kernel/src/system/kernel/signal.cpp` and `distro/kernel/headers/private/kernel/ksignal.h` | MIT, with a GPL-3.0-or-later snippet | Copies of Haiku's signal sources. `send_fault_signal()` is haiku-pi `6e517bb52c`, copyright 2026 Witold Bołt, in a GPL-3.0-or-later `SPDX-Snippet`. The page fault in `vm.cpp` calls it. ARM64 `arch_int.cpp` is not copied. `scripts/apply-kernel-bugs.py` copies the files |
| `distro/artwork/r5-symbol.svg`, `r5-symbol.png`, `r5-os-deskbar.png`, `r5-os-logo.png`, `r5-os-splash.png`, `r5-os-splash.bgr`, `distro/artwork/brand-kit/`, and `distro/desktop/src/kits/tracker/r5_disk_badge.h` | GPL-3.0-or-later | New R5 mark, the Wave Pebble. Copyright 2026 R5OS Contributors. `scripts/generate-r5-artwork.py` writes them from the SVG. The words are set in Manrope, which stays OFL-1.1 and is not relicensed. The badge header is the same picture as bytes so Tracker can draw it on the boot disk |
| `distro/artwork/boot_splash/icons-src/` | MIT | Seven unmodified Haiku icons: Kernel, Harddisk_Development, Devices, Harddisk, Virtual_Memory, Config_Folder, and Window. Copyright Haiku, Inc. and its contributors. `SOURCES.md` names each file. `png/1.png`–`7.png` are 160 px renders of those SVGs. `splash_icons.png` and `splash_icons.bgr` are the 308×80 strip built from the PNGs |
| `distro/desktop/` interface, Tracker, Deskbar, locale, control look, preferences, `Desktop.cpp`, `DefaultDecorator.cpp` | MIT, with GPL-3.0-or-later snippets | Copies of the Haiku sources. Copyright and MIT header stay. R5 edits are `SPDX-Snippet` blocks, copyright 2026 R5OS Contributors, GPL-3.0-or-later. `scripts/apply-desktop-native.py` copies them. The combined libraries and programs follow GPL-3.0-or-later and the MIT notices stay |
| `distro/rest/` except `getaddrinfo.c`, `ping6.c`, `elf_tls.cpp`, and `r5_tpoff.h` |
| `distro/rest/src/system/runtime_loader/elf_tls.cpp` | MIT, with GPL-3.0-or-later snippets | Copy of Haiku's runtime-loader TLS. Copyright 2014 Paweł Dziepak. The new-thread generation line is haiku-pi `5db923abc6`, copyright 2026 Witold Bołt, in a GPL-3.0-or-later `SPDX-Snippet`. The initial-exec placement of startup TLS templates is copyright 2026 R5OS Contributors, GPL-3.0-or-later. `scripts/apply-rest-native.py` copies it. This row is the exception to the `distro/rest/` row | MIT, or MIT and NewOS, with GPL-3.0-or-later snippets | Copies of the remaining Haiku sources: network stack, net server, package volume, input, DriveSetup, accounts, locked preferences, LaunchDaemon, Installer, boot prompt, About, Deskbar `icons.rdef`, the home desktop resource, `ServerApp.cpp`, `runtime_loader.cpp`, and the interface and locale headers. Original copyright and MIT/NewOS headers stay. `runtime_loader.cpp` also keeps the NewOS notice. R5 edits are `SPDX-Snippet` blocks, copyright 2026 R5OS Contributors, GPL-3.0-or-later. `scripts/apply-rest-native.py` copies them. The combined binaries follow GPL-3.0-or-later and the original notices stay. The IPP sources had no license header; the Takagi date line stays |
| `distro/rest/src/system/libnetwork/netresolv/net/getaddrinfo.c` and `src/bin/network/ping/ping6.c` | BSD-3-Clause, with GPL-3.0-or-later snippets | WIDE Project copyright, and for `ping6.c` also the Regents of the University of California. Those license texts stay. R5 edits are `SPDX-Snippet` blocks, copyright 2026 R5OS Contributors, GPL-3.0-or-later. The combined `libnetwork.so` and `ping` follow GPL-3.0-or-later and the BSD notices stay |
| `distro/package/.../CommitTransactionHandler.cpp` | MIT, with GPL-3.0-or-later snippets | Haiku package daemon, copyright Haiku, Inc. The two script-start and script-finish log lines are R5 snippets |

`scripts/` is R5 build tooling and still has no SPDX headers. This was not a pass over the Haiku tree, the packages, or the image.

### VirtualBox 7.2.22, assessed against this policy

Source: Oracle VirtualBox 7.2.22, built from the Haiku guest additions in that tarball, plus `distro/vbox-guest/virtualbox-7.2.22-r5os.patch`. Not from HaikuPorts 6.1.26. The tarball itself is not in git. How to build: `distro/vbox-guest/README.md`.

The additions are GPL-3.0-only. That matches the default direction for new R5 code. It is not MIT, and it must not be pasted into MIT sources or relicensed. Files that say GPL-3.0-only OR CDDL-1.0 can be used under the GPL-3.0-only choice; the CDDL terms are then not the ones we take.

Userspace `VBoxService`, `VBoxTray`, and `VBoxControl` are separate programs. Shipping them next to the rest of R5 is aggregation if they are not linked into Haiku applications. A binary image that includes them must ship this GPL-3.0 text and the corresponding 7.2.22 source, including our build fixes.

Kernel modules (`vboxguest`, `vboxdev`, `vboxsf`, `vboxvideo`, and the accelerant) load into the kernel and call kernel exports. MIT allows that kernel to be distributed in a GPL-3.0 combination. An image that contains one of these modules ships under GPL-3.0-only for the combination: the unmodified GNU GPL version 3 in `LICENSES/GPL-3.0-or-later.txt`, and the corresponding 7.2.22 source, including the build fixes. The “or later” grant on that filename is not added to the VirtualBox sources. Haiku's copyright lines and MIT notices stay on the Haiku files. Those files are not relicensed, and the modules are not pasted into them.

The combined work is the kernel image that loads the module. `vboxguest` and `vboxdev` may be shipped on that basis. On the throwaway disk, `vboxvideo` painted the desktop and followed one host size hint, and `vboxsf` copied one small file each way from a shell. Tracker was not opened on the share.

The daily image now installs those modules, the accelerant, `VBoxService`, `VBoxTray`, and `VBoxControl`, together with the unmodified GNU GPL version 3 and the 7.2.22 source plus this patch. The mouse filter is not installed. The shipped clipboard is still COM1. VirtualBox drag-and-drop stays off. The binaries and the tarball stay out of git. `scripts/apply-vbox-guest.py` copies them onto the image and fails if they are missing. A boot of that daily image is a separate test from the throwaway.
