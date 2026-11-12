<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: MIT
-->
# Review: Moving R5 from GCC 13.3.0 to GCC 16 (and Binary Compatibility) (12 Nov 2026)

Read-only investigation of what would be required to build R5 Operating System with **GCC 16** (16.2, released August 2026) instead of the current **GCC 13.3.0** cross-toolchain, while keeping binary compatibility with existing HaikuPorts packages.

**Recommendation: Do not do this now.** Stay on GCC 13.3.0 until a concrete dependency (such as a newer Mesa or C++23/26 application) requires a newer compiler, or until upstream Haiku transitions. If an upgrade becomes necessary sooner, **GCC 15.3** is a much lower-risk intermediate target because HaikuPorts already has a `gcc-15.3.0_2026_10_11.recipe` and patchset.

---

## 1. Current Baseline in R5

- **Cross-compiler:** `x86_64-unknown-haiku-gcc (GCC) 13.3.0` (built from `buildtools/gcc`, paired with `binutils 2.46.1`).
- **Runtime & Devel Syslibs:** `haiku/build/jam/repositories/HaikuPorts/x86_64` pins:
  - `gcc_syslibs-13.3.0_2023_08_10-6` (`libstdc++.so.6`, `libgcc_s.so.1`, `libsupc++.so`)
  - `gcc_syslibs_devel-13.3.0_2023_08_10-6` (`libgcc*.a`, `libstdc++.a`, `libsupc++*.a`, C++ headers)
- **HaikuPorts reference:** `haikuports/sys-devel/gcc/gcc-15.3.0_2026_10_11.recipe` with `gcc-15.3.0_2026_10_11.patchset` (469 KB, `libstdcxxLibVersion="6.0.34"`) already exists as a reference for GCC 15.

---

## 2. What Must Change for GCC 16

### A. Toolchain (`buildtools/gcc` + `build_cross_tools_gcc4`)

In `buildtools/gcc/`, Haiku carries **64 modified files** on top of upstream GCC 13.3.0 (`4023 insertions, 1482 deletions`). All of these must be ported and validated against GCC 16.2:

- **Target and OS definitions:** `gcc/config.gcc`, `gcc/config.host`, `gcc/config/haiku.{h,c,opt}`, `haiku-stdint.h`, and architecture headers (`i386/haiku64.h`, `aarch64/aarch64-haiku.h`, `riscv/haiku.h`, etc.).
- **Runtime configuration (`libgcc`):** `libgcc/config.host`, `libgcc/config/t-haiku`, `libgcc/config/aarch64/haiku-unwind.h`, `crtstuff.c`.
- **C++ standard library (`libstdc++-v3`):** `config/os/haiku/{ctype_base.h,ctype_configure_char.cc,ctype_inline.h,error_constants.h,os_defines.h}`, `configure.host`, `crossconfig.m4`, `libsupc++/{new_opa.cc,tinfo.cc}`.
- **Haiku-specific fixes:** `-rdynamic` accepted as a no-op, frame pointers enabled by default (`--enable-frame-pointer`), `c++tools` linked against `-lnetwork`, `dl_iterate_phdr` disabled in `libbacktrace`, and `libiberty/pex-unix.c` (`posix_spawn`).
- **Cross-tools:** Rebuild cross-tools via `haiku/build/scripts/build_cross_tools_gcc4` for all official 64-bit R5 targets (`x86_64`, `aarch64`, `riscv64`).

### B. Binary Compatibility (`gcc_syslibs` and `libstdc++.so.6`)

`haiku/build/jam/BuildFeatures` and `SystemLibraryRules` pull `libstdc++.so.6`, `libgcc_s.so.1`, and static kernel/boot libraries (`libgcc-kernel.a`, `libsupc++-kernel.a`, etc.) from `gcc_syslibs` and `gcc_syslibs_devel`.

1. **Shipping GCC 13 `gcc_syslibs` with GCC 16-compiled R5 binaries will fail at runtime:**
   - Binaries compiled by GCC 16 (`libbe.so`, `app_server`, `Tracker`, `libr5ui.so`) can reference newer symbol versions (`GLIBCXX_3.4.34+` / `CXXABI_1.3.15+`) that do not exist in `gcc_syslibs-13.3.0`.
2. **Keeping backwards compatibility with existing HaikuPorts packages requires:**
   - Building and pinning our own **`gcc_syslibs-16.2.0`** and **`gcc_syslibs_devel-16.2.0`** `.hpkg` packages (in `r5-haikuports.repo`, similar to how `scripts/pin-mesa-packages.sh` pins Mesa 25).
   - Preserving the Haiku OS ABI in `libstdc++-v3/config/os/haiku/*` and `libsupc++/tinfo.cc`, as well as `compat >= 7` (`gcc_syslibs`) and `compat >= 6` (`libstdc++.so.6`).
   - Because `libstdc++.so.6` uses additive symbol versioning, existing GCC 13 packages (`pe`, `vision`, `WebPositive`, `mesa`, `vulkanteapot`, `Mixxx`) continue to load against `gcc_syslibs-16.2.0`.
3. **GCC 16 C++20 ABI note:**
   - GCC 16 adjusted the internal value of `std::partial_ordering::unordered` (`<compare>`). Existing C++17 packages are unaffected, but C++20 libraries passing `std::partial_ordering` across a shared library boundary must be built with matching GCC versions.

### C. Language Standard Defaults (C23 and C++20)

Unlike GCC 13 (`-std=gnu17` and `-std=gnu++17`), GCC 16 changes **both** defaults:

- **C defaults to `-std=gnu23` (since GCC 15):**
  - `bool`, `true`, and `false` are keywords. (`SupportDefs.h` already includes `<stdbool.h>` for C, but older drivers or third-party C code defining their own `bool` / `enum { false, true }` will fail.)
  - Empty parameter lists `int foo();` strictly mean `int foo(void)`. Legacy C code declaring functions without `(void)` and calling them with arguments or mismatched function pointers will error.
- **C++ defaults to `-std=gnu++20` (new in GCC 16):**
  - Older C++ code relying on pre-C++20 behavior or removed library/language features can fail to build unless updated or pinned with `-std=gnu++17`.
- **Practical mitigation:** Set `-std=gnu17` and `-std=gnu++17` explicitly as the baseline in `haiku/build/jam/ArchitectureRules` first, then modernize per module.

### D. Stricter Warnings and `-Werror`

Haiku/R5 compiles large parts of the tree with `-Werror`, with explicit suppressions in `haiku/build/jam/ArchitectureRules` (`-Wno-error=unused-but-set-variable`, `-Wno-error=address-of-packed-member`, `-Wno-error=stringop-overread`, `-Wno-error=array-bounds`, `-Wno-error=cast-align`, `-Wno-error=format-truncation`).

Moving from GCC 13 to GCC 16 introduces additional hard errors and warnings:
- **GCC 14:** `-Wimplicit-function-declaration` and `-Wincompatible-pointer-types` are errors by default.
- **GCC 15:** `-Wunterminated-string-initialization` catches fixed-size char arrays without a trailing `\0`.
- **GCC 16:** Expanded `-Wunused-but-set-variable` flags compound assignments and `++`/`--` on variables that are never read.

---

## 3. Pros and Cons

### Pros
- **Modern C++/C support:** Unlocks C++20/23/26 features if future versions of Mesa, LLVM, or applications (e.g. Mixxx) require a newer minimum GCC.
- **Better optimization & CPU tuning:** Improved auto-vectorization and codegen for AMD Zen 3 (Ryzen 5 5625U), ARM64, and RISC-V.
- **Better diagnostics:** Catches real latent bugs earlier via tighter static checks.
- **Binary compatibility preserved:** Backwards-compatible `libstdc++.so.6` symbol versioning keeps existing HaikuPorts packages working when paired with `gcc_syslibs-16.2.0`.

### Cons
- **Minimal user-visible benefit:** Typical desktop/OS speedups are minor (often ~1–3% in CPU-bound paths); real R5 gains come from GPU, audio, and driver work.
- **Self-maintained `gcc_syslibs`:** R5 can no longer rely on HaikuPorts for `gcc_syslibs` / `gcc_syslibs_devel` and must build, pin, and maintain those `.hpkg` packages itself.
- **Upstream merge friction:** While upstream Haiku stays on GCC 13, incoming Haiku commits may break under GCC 16 (`-Werror`, C23, C++20).
- **Multi-arch verification:** R5 targets `x86_64`, `aarch64`, and `riscv64`; a custom toolchain upgrade must eventually build and boot on all three.

---

## 4. Effort Estimate

### Option A — Direct port to GCC 16.2 (from GCC 13.3.0)
| Task | Estimate |
|---|---|
| Port Haiku `buildtools/gcc` patches (64 files) to GCC 16.2 & build cross-tools | **2 – 4 days** |
| Build, test, and pin `gcc_syslibs-16.2.0` & `gcc_syslibs_devel-16.2.0` `.hpkg` | **1 – 2 days** |
| Fix `-Werror` & C23/C++20 issues across the R5/Haiku tree | **2 – 5 days** *(shorter if `-std=gnu17`/`gnu++17` is forced)* |
| Full verification (QEMU, 5625U, HaikuPorts apps, Mesa 25, ARM64) | **1 – 2 days** |
| **Total** | **~6 – 13 working days (1.5 – 2.5 weeks)** + ongoing merge maintenance |

### Option B — Intermediate step to GCC 15.3 (using existing HaikuPorts recipe)
- Reusing `haikuports/sys-devel/gcc/gcc-15.3.0_2026_10_11.recipe` and its existing 469 KB Haiku patchset eliminates most toolchain porting work.
- **Total:** **~2 – 4 working days**.
