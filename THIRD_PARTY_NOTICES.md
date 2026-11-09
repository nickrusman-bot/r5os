# Third-party notices

Readable list for users and release builds. The rules are `docs/LICENSING.md`. This is not a complete bill of materials for the Haiku tree or for every package inside an R5 image. Components that have not been inspected are unresolved, not MIT.

## Haiku

R5 is an independent system based on Haiku. Haiku’s own copyright notices and licenses stay on Haiku’s files. Much of Haiku is MIT; the tree is not all MIT. This repository does not relicense it.

The preferences wrench on Settings page icons and home cards is Haiku’s unmodified `data/artwork/icons/Overlay_prefs`, copied to `distro/settings/overlay-prefs.hvif`. `data/artwork/ReadMe` releases the icons under the MIT license. About R5 does not use that wrench.

The seven boot-progress icons are unmodified Haiku drawings, MIT, copyright Haiku, Inc. and its contributors: Kernel, Harddisk_Development, Devices, Harddisk, Virtual_Memory, Config_Folder, and Window. The SVGs and the list are in `distro/artwork/boot_splash/icons-src/`. `SOURCES.md` names each file. The 308×80 strip is built from renders of those SVGs.

The Back, Forward, Up, and Home pictures in `libr5icons.so` are unmodified HVIF from Haiku’s `src/kits/tracker/TrackerIcons.rdef` (`R_ResBackNav`, `R_ResForwNav`, `R_ResUpNav`, `R_HomeDirIcon`). The bytes live in `distro/icons/standard_icons.inc`. The Open Tracker notice is `distro/icons/TRACKER_ICON_NOTICE.txt`. The open and closed locks are unmodified HVIF of Haiku’s `Action_Login` and `Action_Logout` artwork. The bytes live in `distro/icons/lock_icons.inc`. The MIT notice is `distro/icons/HAIKU_ICON_NOTICE.txt`. The C++ around them is GPL-3.0-or-later, and that is the license of the shared library as shipped. The picture bytes keep their own notices. Those pictures do not get the preferences wrench. Settings does not read Tracker’s private resources at run time. `r5_button_set_lock()` is the public call that puts one of those locks on a button.

## Identified in this repository

### Fault signal

Location: `distro/kernel/src/system/kernel/signal.cpp`, `distro/kernel/headers/private/kernel/ksignal.h`, and the page-fault call in `distro/kernel/src/system/kernel/vm/vm.cpp`.

The files are Haiku's. Copyright the Haiku authors named in each file. License: MIT, and NewOS for `vm.cpp`. The added blocks are haiku-pi `6e517bb52c`: a blocked or ignored fault signal is reset to the default and unblocked before it is sent. Copyright 2026 Witold Bołt. The snippets are distributed as GPL-3.0-or-later. The Haiku headers stay. ARM64 `arch_int.cpp` is not included.

### Exited thread stack

Location: `distro/kernel/src/system/kernel/thread.cpp`.

The file is Haiku's `thread.cpp`. Copyright the Haiku authors named in the file. License: MIT and NewOS. One block is haiku-pi `e09cfa1d9e`: an exited thread's stack stays while its team goes down. Copyright 2026 Witold Bołt. The snippet is distributed as GPL-3.0-or-later. The Haiku header stays.

### BFS I/O vnode release

Location: `distro/kernel/src/add-ons/kernel/file_systems/bfs/kernel_interface.cpp`.

The file is Haiku's BFS interface. Copyright the Haiku authors named in the file. License: MIT. One block is haiku-pi `44cc183c0c` (star row 57): the last reference from a finished I/O is released through `vfs_put_vnode()`. Copyright 2026 Witold Bołt. The snippet is distributed as GPL-3.0-or-later. The Haiku header stays.

### Wired range shrink

Location: `distro/kernel/src/system/kernel/vm/vm.cpp`.

The file is Haiku's `vm.cpp`. Copyright the Haiku authors named in the file. License: MIT and NewOS. One block is haiku-pi `6f810ed2d7`: a partial wire shrinks to the pages that were wired. Copyright 2026 Witold Bołt. The snippet is distributed as GPL-3.0-or-later. The Haiku header stays.

### Runtime-loader TLS generation

Location: `distro/rest/src/system/runtime_loader/elf_tls.cpp`.

The file is Haiku's `elf_tls.cpp`. Copyright 2014 Paweł Dziepak. License: MIT. One block is the haiku-pi change `5db923abc6`: a new thread's TLS vector starts at the current generation. Copyright 2026 Witold Bołt. The snippet is distributed as GPL-3.0-or-later. The Haiku header stays.

### Teapot control points

Location: `distro/apps/vulkanteapot/fg_teapot_data.h`.

The file is the freeglut 3.8.0 header `src/fg_teapot_data.h`, with the copyright line the permission text requires written above that text. The data are Martin Newell's patches as redistributed by freeglut. Copyright 1999-2000 Pawel W. Olszta. License: MIT (the X11 permission grant in the file). The control points stay under that notice. The Haiku `glut_teapot.c` file that says GPL licensing is not permitted is not used.

### AirPlay receiver

Location: `distro/airplay`.

| Piece | Project | License |
|---|---|---|
| RAOP / classic AirPlay flow | Shairport (James Laird), Shairport Sync (Mike Brady) | MIT |
| ALAC decoder | David Hammerton | MIT |
| AES-128-CBC | kokke tiny-AES-c | Unlicense |
| RSA and Apple-Challenge | OpenSSL 3 from HaikuPorts (`libcrypto`) | OpenSSL 3’s license (Apache-2.0 for OpenSSL 3) |

Copyright, URLs, and what was not copied (AirPlay 2 pairing, NQPTP, FairPlay) are in `distro/airplay/THIRD_PARTY.md`.

The MIT text used for covered files is `LICENSES/MIT.txt`. The Unlicense text is `LICENSES/Unlicense.txt`. The Apache-2.0 text is not in `LICENSES/` yet.

R5-owned source in this tree is GPL-3.0-or-later where the file is ours as a whole. The list of those paths, and the Haiku files that stay on their own license, is in `docs/LICENSING.md`.

## Built outside this repository, not in the image

### VirtualBox Guest Additions 7.2.22

Oracle VirtualBox 7.2.22 Haiku additions. License: GPL-3.0-only. Some IPRT files are GPL-3.0-only OR CDDL-1.0; we take the GPL-3.0-only choice. The Oracle tarball is not in git. The R5 build fixes are `distro/vbox-guest/virtualbox-7.2.22-r5os.patch`. The binaries are not in git. The daily image installs them, plus the unmodified GNU GPL version 3 and that source, at `/boot/system/non-packaged/data/licenses/virtualbox`. They are not MIT.

The “or later” name on `LICENSES/GPL-3.0-or-later.txt` is the grant for new R5 code. It is not added to the VirtualBox sources. Haiku’s MIT notices stay on the Haiku files. See `docs/LICENSING.md` and `distro/vbox-guest/README.md`.

### OpenGL stack on the image

Location: `distro/packages/x86_64`. Copied onto the image after jam. Mesa 22.0.5 is removed from that image.

| Piece | Project | License |
|---|---|---|
| `libglvnd` 1.7.0 (`libGL.so.1`) | NVIDIA, 2013 | MIT |
| Mesa 25.3.6 llvmpipe (`libEGL_mesa.so.0`) and Lavapipe | Brian Paul and others | MIT |
| `libLLVM.so.21.1` only | University of Illinois at Urbana-Champaign, 2003-2025 | Apache-2.0 with LLVM exception |
| Vulkan loader 1.4.367 | The Khronos Group Inc. | Apache-2.0 |
| vulkaninfo 1.4.367 (`vulkan_tools`) | The Khronos Group Inc., Valve Corporation, LunarG, Inc., RasterGrid Kft. | Apache-2.0 |

The LLVM license text shipped inside the runtime package is `LICENSES/Apache-2.0-with-LLVM-exception.txt`. These packages were not audited file by file. This is CPU drawing, not the graphics chip.

The same Mesa 25.3.6 build also ships RADV (`libvulkan_radeon.so`), Lavapipe and Zink. `src/amd/vulkan/radv_device.c` is SPDX MIT, copyright 2016 Red Hat and Bas Nieuwenhuizen, and says it is based in part on ANV, copyright 2015 Intel Corporation. `src/gallium/frontends/lavapipe/lvp_device.c` is MIT, copyright 2019 Red Hat. `src/gallium/drivers/zink/zink_context.c` is MIT, copyright 2018 Collabora Ltd. Mesa's `docs/license.rst` says the core is MIT and that individual files can differ. These three files were read. The packages were not audited file by file.

libdrm 2.4.124 is built in this tree and installed as `libdrm.so.2` and `libdrm_amdgpu.so.1`. `xf86drm.c` is MIT, copyright 1999 Precision Insight, Inc. and 2000 VA Linux Systems, Inc. `amdgpu/amdgpu.h` is MIT, copyright 2014 Advanced Micro Devices, Inc. `data/amdgpu.ids` ships unmodified. Its first lines are a syntax comment and `1.0.0`. They name no copyright holder. The libdrm tree was not audited file by file.

### Manrope

Location: `distro/fonts/manrope`. Installed on the image at `data/settings/fonts` and loaded by Settings only.

| Piece | Project | License |
|---|---|---|
| `manrope-regular.ttf`, `manrope-bold.ttf` | Manrope, Mikhail Sharanda, 2018. Mirko Velimirovic and Mikhail Sharanda made the variable family in 2019. These two files are the unmodified static fonts from `googlefonts/manrope`. | OFL-1.1 |

Copyright line in the font's license file: Copyright 2018 The Manrope Project Authors (https://github.com/googlefonts/manrope). No Reserved Font Name is declared after that line. The files are not modified, so the name stays Manrope. `OFL.txt` and `FONTLOG.txt` ship next to the fonts. The SPDX text is `LICENSES/OFL-1.1.txt`. The same files are installed under `data/fonts/ttfonts`. app_server uses Manrope Regular and Bold as the default plain and bold fonts when the user has not saved a font choice. The fixed font stays Noto Sans Mono. Noto Sans remains the fallback if Manrope is missing.

### AMD family 19h microcode

Location: `distro/firmware/amd-ucode`. Installed on the boot volume at `system/non-packaged/data/firmware/amd-ucode/`.

| Piece | Project | License |
|---|---|---|
| `microcode_amd_fam19h.bin` | Advanced Micro Devices, Inc., 2010-2025, via linux-firmware | AMD microcode license |

The blob is unmodified. sha256 `c614d6db8056c5c67a9189b225124127d56990a190305bcb3927d50e132de7dd`. `LICENSE.amd-ucode` is the unmodified AMD text and is installed beside the blob. Redistribution of the binary is allowed when that notice stays with it. This is not an open-source license. AMD does not endorse R5.

### AMD Renoir graphics firmware

Location: `distro/firmware/amdgpu`. Installed at `system/non-packaged/data/firmware/amdgpu/`.

| Piece | Project | License |
|---|---|---|
| `renoir_pfp.bin`, `renoir_me.bin`, `renoir_ce.bin`, `renoir_rlc.bin`, `renoir_mec.bin`, `green_sardine_pfp.bin`, `green_sardine_me.bin`, `green_sardine_ce.bin`, `green_sardine_rlc.bin`, `green_sardine_mec.bin`, `green_sardine_mec2.bin` | Advanced Micro Devices, Inc., 2024, via linux-firmware | AMD GPU microcode license |

The blobs are unmodified. sha256: `renoir_pfp.bin` `0c815572cc466d6c1e608e1d36ab16f35660179e19f0feeabc448b85e8158af4`, `renoir_me.bin` `38574ad3c583f3965cf1c4be84f9d0c3818511781c03733b28f07683061b1189`, `renoir_ce.bin` `b4ea0bf5d589d82ccef52e54536221f094b66b648514b14f03cf4ad50f9a4f72`, `green_sardine_pfp.bin` `94e1474b3c8d422e35661d2d584297d891464c173ebf4ad45536277bdf7bdcdc`, `green_sardine_me.bin` `671af908c8d631e659895d62db2e6bffc1d2c87d6db4c08246be0216f468fb9c`, `green_sardine_ce.bin` `3bba10cff48ec51eb63e78078456596258c9fea2e8b38d91d02be66c0aca70fd`. `renoir_rlc.bin` and `green_sardine_rlc.bin` are the same file, `66f4397c2e9d19af245f9da290c8678eb1a51cc815fa7864a9383c2cd0db319d`. `renoir_mec.bin`, `green_sardine_mec.bin` and `green_sardine_mec2.bin` are the same file, `0e4c6712f75af494c3fd70386bc07739cab3aafd77c4fe8efb46c0bc1ae4eba6`. `LICENSE.amdgpu` is the unmodified AMD text and is installed beside them. Redistribution of the binaries is allowed when that notice stays with them. This is not an open-source license. AMD does not endorse R5. Linux loads these through the PSP unless the load type is direct. RLC is header 2.1, ucode `0x3c`, 16896 bytes. PFP, ME, and CE stay header 1.0.

### AMD GFX9 clear-state table

Location: `distro/graphics/src/add-ons/accelerants/radeon_hd/clearstate_defs.h` and `clearstate_gfx9.h`. Compiled into the `radeon_hd` accelerant. Not installed as a separate file.

| Piece | Project | License |
|---|---|---|
| `clearstate_defs.h`, `clearstate_gfx9.h` | Advanced Micro Devices, Inc., 2012 and 2017, via Linux v6.12 `drivers/gpu/drm/amd/amdgpu/` | MIT |

The files are unmodified. sha256 `70eb9f9a57d2cacb2ee2d7b9f17485384019ac03c845ce4bea941acdae137285` and `e885c7f6475efd74e7729688a70992ff9543979363eb029c628bd95627fb34b5`. The permission text stays in each file. AMD does not endorse R5.

## Not included

The Linux `amdgpu` driver is not imported. The clear-state table above is the only file taken from that tree.

## Not done

Credits & Licenses inside About R5 is not built. It must be generated from this inventory once the image actually contains the components it names. A full scan of `haiku/`, firmware, fonts, and package dependencies is still open.
