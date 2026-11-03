# Third-party notices

Readable list for users and release builds. The rules are `docs/LICENSING.md`. This is not a complete bill of materials for the Haiku tree or for every package inside an R5OS image. Components that have not been inspected are unresolved, not MIT.

## Haiku

R5OS is an independent system based on Haiku. Haiku’s own copyright notices and licenses stay on Haiku’s files. Much of Haiku is MIT; the tree is not all MIT. This repository does not relicense it.

The preferences wrench on Settings page icons and home cards is Haiku’s unmodified `data/artwork/icons/Overlay_prefs`, copied to `distro/settings/overlay-prefs.hvif`. `data/artwork/ReadMe` releases the icons under the MIT license. About R5 does not use that wrench.

The Back, Forward, Up, and Home pictures in `libr5icons.so` are unmodified HVIF from Haiku’s `src/kits/tracker/TrackerIcons.rdef` (`R_ResBackNav`, `R_ResForwNav`, `R_ResUpNav`, `R_HomeDirIcon`). The bytes live in `distro/icons/standard_icons.inc`. The Open Tracker notice is `distro/icons/TRACKER_ICON_NOTICE.txt`. The open and closed locks are unmodified HVIF of Haiku’s `Action_Login` and `Action_Logout` artwork. The bytes live in `distro/icons/lock_icons.inc`. The MIT notice is `distro/icons/HAIKU_ICON_NOTICE.txt`. The C++ around them is GPL-3.0-or-later, and that is the license of the shared library as shipped. The picture bytes keep their own notices. Those pictures do not get the preferences wrench. Settings does not read Tracker’s private resources at run time. `r5_button_set_lock()` is the public call that puts one of those locks on a button.

## Identified in this repository

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

R5OS-owned source in this tree is GPL-3.0-or-later where the file is ours as a whole. The list of those paths, and the Haiku files that stay on their own license, is in `docs/LICENSING.md`.

### rEFInd 0.14.2

Location: `distro/system/boot/loader/refind`. Upstream sources are unmodified. The black theme and `distro/system/boot/loader/r5/refind.conf` are R5OS text, GPL-3.0-or-later.

| Piece | Project | License |
|---|---|---|
| rEFInd 0.14.2 | Roderick W. Smith. Forked from rEFIt, Christoph Pfisterer, 2006 | rEFIt: BSD-style (`LICENSE.txt`). rEFInd modifications: GPL-3.0-only (`LICENSE.txt`, `COPYING.txt`). Two Makefiles say “or later”; those files were not relabeled |
| GNU-EFI 3.0.18 | Hewlett-Packard, David Mosberger, Stephane Eranian, 1999–2007 | BSD-style, from the upstream Makefile. Linked into `refind_x64.efi`. Not stored in git |
| Icons | Mixed, see `icons/licenses/` | CC, GPL-2.0, GPL-3.0, LGPL-3.0. Separate files, shipped with those license texts |

The EFI binary is its own program. It is not linked into the kernel or the Haiku loader. The GPLv2 filesystem drivers are not built and are not on the image. The GFDL-1.3 HTML docs are not installed. `COPYING.txt`, `LICENSE.txt`, and `CREDITS.txt` are copied onto the EFI partition. Secure Boot is off.

## Built outside this repository, not in the image

### VirtualBox Guest Additions 7.2.22

Oracle VirtualBox 7.2.22 Haiku additions. License: GPL-3.0-only. Some IPRT files are GPL-3.0-only OR CDDL-1.0; we take the GPL-3.0-only choice. The Oracle tarball is not in git. The R5OS build fixes are `distro/vbox-guest/virtualbox-7.2.22-r5os.patch`. The binaries are not in git. The daily image installs them, plus the unmodified GNU GPL version 3 and that source, at `/boot/system/non-packaged/data/licenses/virtualbox`. They are not MIT.

The “or later” name on `LICENSES/GPL-3.0-or-later.txt` is the grant for new R5OS code. It is not added to the VirtualBox sources. Haiku’s MIT notices stay on the Haiku files. See `docs/LICENSING.md` and `distro/vbox-guest/README.md`.

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

The blob is unmodified. sha256 `c614d6db8056c5c67a9189b225124127d56990a190305bcb3927d50e132de7dd`. `LICENSE.amd-ucode` is the unmodified AMD text and is installed beside the blob. Redistribution of the binary is allowed when that notice stays with it. This is not an open-source license. AMD does not endorse R5OS.

## Not included

Linux DRM and `amdgpu` are not imported.

## Not done

Credits & Licenses inside About R5OS is not built. It must be generated from this inventory once the image actually contains the components it names. A full scan of `haiku/`, firmware, fonts, and package dependencies is still open.
