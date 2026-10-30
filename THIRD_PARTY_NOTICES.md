# Third-party notices

Readable list for users and release builds. The rules are `docs/LICENSING.md`. This is not a complete bill of materials for the Haiku tree or for every package inside an R5OS image. Components that have not been inspected are unresolved, not MIT.

## Haiku

R5OS is an independent system based on Haiku. Haiku’s own copyright notices and licenses stay on Haiku’s files. Much of Haiku is MIT; the tree is not all MIT. This repository does not relicense it.

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

The MIT text used for covered files is `LICENSES/MIT.txt`. The Unlicense and Apache-2.0 texts are not in `LICENSES/` yet.

## Built outside this repository, not in the image

### VirtualBox Guest Additions 7.2.22

Oracle VirtualBox 7.2.22 Haiku additions. License: GPL-3.0-only. Some IPRT files are GPL-3.0-only OR CDDL-1.0. Cross-built for AMD64 on 2026-10-30. The binaries are not in git and not in the R5OS image. They are not MIT. See `docs/LICENSING.md` for the split between userspace programs and kernel modules.

### Mesa 22.0.5

Present as a Haiku build package. Upstream Mesa is MIT. This package has not been audited file by file. The guest image is not claimed to ship Vulkan.

## Not included

Linux DRM and `amdgpu` are not imported.

## Not done

Credits & Licenses inside About R5OS is not built. It must be generated from this inventory once the image actually contains the components it names. A full scan of `haiku/`, firmware, fonts, and package dependencies is still open.
