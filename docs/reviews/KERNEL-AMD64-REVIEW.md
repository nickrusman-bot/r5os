<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: GPL-3.0-or-later
-->
# Review of the AMD64 kernel (12 Nov 2026)

For Cursor. Read-only review of what the kernel does on the Ryzen 5 5625U, and what is still missing. Sources: `origin/main` at `d988322049` (`docs/KERNEL.md`, `docs/ROADMAP.md`, the stick logs under `generated/boot-5625u-*`) and the WSL Haiku tree at `4aae8640c9`, which already contains the R5 `r5_pstate` publish and the TSC guards. Stick 33 is the boot that still has the early CPU lines. Stick 41 is the frequency sample. Sticks 42 and later are mentioned in `CHANGELOG.md`; their log folders are not in `origin/main`, so they are not used here. Nothing was built or booted for this review.

The other forks were not cloned. What they already have is taken from `docs/ROADMAP.md` and from `generated/haiku-pi/REVIEW-1.md`. haiku-pi’s shared fixes are correctness (VM, locks, signals, BFS). air/OS and RenkuOS are the ones with laptop power and idle policy. None of the notes in this tree show an AVX2 `memcpy`, PCID, or a Zen SMT fix in those forks.

**Verdict: the 12 threads are up, and one busy thread already runs at about 4341 MHz. The scheduler is told that those 12 threads are 12 separate cores.** That is the change that can make this laptop faster. Copies and the syscall path come after it. Raising `SMP_MAX_CPUS` and teaching the BIOS loader x2APIC does not change this machine: the log shows APIC ids 0 through 11.

`docs/KERNEL.md` already records the 64-CPU cap, the BIOS x2APIC gap, NUMA, hybrid CPUs, and the read-only P-state snapshot. Those are not repeated as new findings. One correction: AMD CPPC is written by `amd_pstates`, which that document does not cover.

Line numbers are from the WSL tree at `4aae8640c9`.

## AMD and Intel

These are not all general AMD64 fixes. The shared x86_64 code also runs on Intel. The AMD CPUID leaves and MSRs do not.

General AMD64, so also Intel:

- **Copies (finding 2).** `memcpy` and `memset` for x86_64 are one implementation for both vendors. AVX2 and ERMS exist on Intel too (the X1 Carbon Gen 8 has AVX2). The `setjmp` around every kernel copy from user memory applies to every 64-bit build. The 32-bit kernel uses a different path.
- **Syscall clear (finding 3).** `vzeroall` on the fast return is chosen when the XSAVE mask stops at AVX. That is this Ryzen, and an Intel CPU without AVX-512. An Intel CPU with AVX-512 takes the other patch (`xrstor`).
- **Page tables (finding 4).** Rewriting CR3, no PCID, and ordinary areas in 4 KB pages live in the shared 64-bit paging code. 2 MB pages would help both. PCID is usually present on Intel. On the 5625U the feature line did not list `pcid`, so that work waits until the CPU reports the bit.
- **Idle (finding 6).** `x86_cstates` uses MONITOR/MWAIT. That instruction is Intel’s; AMD implements it too. A cap on deep C-states applies to both. The module already has an Intel exception (Skylake C5/C6).
- **`launch_speedup`, the syscall filter, UMIP and PKU.** These do not depend on the vendor. Enable UMIP and PKU only when the feature bit is set. Both vendors have those bits on recent CPUs.

AMD only:

- **SMT map (finding 1).** The wrong masks come from `detect_amd_cpu_topology()` (leaf `0x80000008`; leaf `0x8000001E` is never read). Intel uses leaf `0xB` or `0x1F`. The scheduler is shared: it trusts the map the architecture code hands it. An AMD leaf does not change the X1 Carbon. The open point there is a different one: hybrid cores are named and then ignored (`docs/KERNEL.md` finding 6).
- **Frequency (finding 5).** `amd_pstates` writes AMD CPPC and returns at once when the vendor is not AMD. Intel HWP is a different set of MSRs. The 4341 MHz on the 5625U says nothing about the X1.
- **IOMMU (finding 7).** `1022:1631` is the AMD IOMMU. Intel is VT-d, a different device and a different driver.
- **IBPB and SSBD.** The feature bit exists on both. The MSR to write does not match. One call that ignores the vendor hits the wrong register on the other CPU.

## 1. Every logical CPU is its own core (this machine)

`arch_cpu.cpp:605-633` and `:857-875`. `detect_amd_cpu_topology()` builds the SMT and core masks from leaf 1 and leaf `0x80000008`. Leaf `0x8000001E` is never read. The printed ids are `topology_id[]`, and the scheduler groups CPUs with those same ids (`scheduler.cpp:661-694`, `:735-744`).

Stick 33, `generated/boot-5625u-33/r5-syslog.txt`:

- `CPU 0: apic id 0, package 0, core 0, smt 0` through `CPU 11: apic id 11, package 0, core 11, smt 0`
- `scheduler_init: found 12 logical cpus and 3 cache levels`
- `scheduler switches: single core: false, cpu load tracking: false, core load tracking: true` (twice, lines 689 and 1870)

The cache ids on that same boot are the real pairs. L1 and L2 are shared by 0–1, 2–3, 4–5, 6–7, 8–9 and 10–11. L3 id is 0 on all of them. So the cache walk (leaf `0x8000001D`) sees six cores, and the topology walk sees twelve.

`scheduler_init()` counts a core for every CPU whose SMT id is 0 (`scheduler.cpp:675-678`). That count is 12, so there are 12 `CoreEntry`s of one thread each. Low-latency mode, which this boot selects, spreads runnable threads across cores. With this map it will put two threads on a hardware pair while another core is idle, because it has no pairs.

`docs/KERNEL.md` finding 5 calls the EDX fallback dormant on Zen and asks for a mask print before any change. The print now exists, and the masks are wrong on the reference laptop. The fallback line is not what the log proves. The result of the whole function is.

Fix: take threads-per-core from leaf `0x8000001E` when that leaf exists, and boot the 5625U again. The pass is twelve lines with `smt` 0 and 1 inside each L1 pair, and `scheduler_init` still saying 12 logical CPUs. haiku-pi, air/OS and RenkuOS are not recorded here as already having that leaf.

## 2. Copies stop at SSE, on a CPU that has AVX2 and ERMS

`libroot/.../x86_64/memcpy.cpp:142-155` and `memset.cpp:73-86`. The kernel links the same two files (`kernel/lib/arch/x86_64/Jamfile`). Length up to 16 bytes is integer copies, up to 2048 bytes is `movdqu` (16 bytes), and above that is `rep movsb` / `rep stosb`.

Stick 33 lists `avx2`, `erms`, `clflushopt` and `clwb` on CPU 0. `rep movsb` above 2048 bytes matches ERMS, so the large path is the right instruction. Width 16 bytes leaves AVX2 unused for the copies app_server, the media kit and the file cache actually do. There is also no non-temporal store for a copy that is larger than the cache. The write-combined framebuffer alias is already described in `docs/KERNEL.md` under “Memory types”; this is the CPU side of the same copies.

AMD64 user copies go through `headers/private/kernel/arch/generic/user_memory.h:78-82`: one `setjmp` per `memcpy`. The 32-bit kernel uses a fault pointer in assembly (`arch/user_memory.h:14-32`) and does not take that path. Every `read` and `write` on this OS pays the `setjmp`.

Fix: an AVX2 body for the middle sizes, and a timed copy on the 5625U before and after. Keep `rep movsb` for the large sizes while ERMS is set. The `setjmp` user-copy change is its own boot, because it is a fault path. Both changes are general AMD64.

## 3. The fast syscall return clears the whole vector file

`interrupts.S:120-138` and `:458-483`. The fast path saves no XSAVE state. It runs `CLEAR_FPU_STATE()` and then `sysretq`. XMM and YMM are caller-saved across a Haiku syscall, so wiping them is allowed. The cost is on every syscall.

Stick 33: `enable XSAVEC 0x7 832`. Mask `0x7` is x87, SSE and AVX, which is the case `arch_cpu.cpp:1717-1721` patches with `vzeroall` (3 bytes). `arch_altcodepatch.cpp:54-63` fills the rest of the site with nops, so the old `pxor` list does not also run. The instruction that remains on the fast path is `vzeroall`.

Interrupts are a different path: `interrupts.S:274-280` writes the 832-byte XSAVE area on the way in. That part matches the CPU.

Fix only after a measurement: a tight `gettimeofday` or `null` syscall loop on the 5625U, cycles per call, with the clear left in and with it patched out. If the difference is noise, leave it. The clear is also what stops kernel vector data reaching the caller. The same fast path runs on Intel.

## 4. TLB flushes and page size

`arch_cpu.cpp:1784-1816`. A full flush writes CR3. A range flush is `invlpg` one page at a time. PCID and INVPCID are named (`arch_cpu.cpp:454`, `:507`) and have no other use.

Stick 33’s feature line includes `invpcid` and does not include `pcid`. Leaf 1 ECX bit 17 was clear in the word this kernel stored. INVPCID without PCID does nothing useful. A raw leaf 1 dump on the next stick comes before any PCID work. The flush code is the same either way: a context switch rewrites CR3.

`pdpe1gb` is set on that same line. `X86PagingMethod64Bit.cpp:384-387` refuses a large page for a normal address. Large pages stay on the physical map only. User and kernel heap mappings are 4 KB, so a big anonymous area walks the TLB 512 times more often than a 2 MB page would.

Fix: 2 MB pages for large anonymous areas are the one with a visible effect on this laptop (16 GB class, one CCD). The same 2 MB path helps Intel. PCID waits on the leaf 1 dump. 1 GB pages and 5-level paging (`la57` is a flag and stays off) do not change this machine.

## 5. Boost already happens; the scheduler cannot ask

`amd_pstates.cpp:43-52` returns `B_NOT_SUPPORTED` for both increase and decrease. `cpu.cpp:321-326` then leaves load tracking off, which is the stick 33 line `cpu load tracking: false`.

The same module does write CPPC. `init_pstates()` (`amd_pstates.cpp:85-102`) forces low-latency mode, enables CPPC, and writes min, max and EPP. Stick 33: `using AMD P-States (capabilities: 0xba572a10ba572a10)`.

Stick 41, `generated/boot-5625u-41/r5-cpu.txt`, after one second busy on that thread:

- hardware P-state index stayed 0, the 2300 MHz definition
- CPPC request: max 181, min 42, desired 0, EPP 0
- `no msr write` is the snapshot device, which still refuses writes
- APERF delta `4340932228`, MPERF delta `2295743344`, ratio 1.891, about **4341 MHz** against the 2296 MHz TSC

Desired 0 with a min and a max is hardware-autonomous. EPP 0 is the performance bias this module writes in low-latency mode. Single-thread boost is already there. What is missing is a preference the user can set, and any reaction to load inside the scheduler. `docs/KERNEL.md` finding 8 describes the snapshot only.

air/OS `power_daemon` (lid, AC, battery, suspend to idle) and RenkuOS’s “stay out of a deep idle state when a driver asks” are the fork pieces `docs/ROADMAP.md` already lists under power. They are not in this kernel. This finding is AMD CPPC. Intel HWP is a separate module.

## 6. Idle is MWAIT, and deep states stay available in low-latency mode

`x86_cstates.cpp:77-104`. Idle picks a deeper MWAIT state as the idle time grows. Low-latency mode only changes the step (500 µs against 125 µs) and turns off `mwait` in the spin wait. It does not cap the deepest state.

Stick 33: `found cpuidle module: power/cpuidle/x86_cstates/v1`. The following `using x86 C-States:` line is not in that file, so the exact C-states accepted on that boot are not on record. CPU 0 does list `monitor` and `mwaitx`. The idle path uses `monitor`/`mwait`, not `mwaitx`.

`arch_cpu.cpp:1477-1481` is `hlt`, used when no idle module is loaded. On this boot the module loaded.

This is the RenkuOS gap: a driver that cannot tolerate a deep C-state has no way to say so. Audio and NVMe on this laptop are the reason to care. A cap, or a per-driver hold, wants a stick where the C-state line is actually in the syslog. The module is shared with Intel.

## 7. Functions the hardware log shows, and the kernel does not use

Stick 41 `r5-hardware.txt` lists the Renoir/Cezanne IOMMU, device `1022:1631`. A search of `src/add-ons/kernel` finds no IOMMU driver. DMA from a device is not isolated. That is a missing function, not a speed change. An AMD driver does not cover Intel VT-d.

Stick 33 line 1791: `module: Search for file_cache/launch_speedup/v1 failed.` The module is `src/add-ons/kernel/file_cache/launch_speedup.cpp`. It prefetches the files a previous boot or a previous application launch opened. The image this stick booted does not contain it. Boot and application start are the cost. That module is not CPU-specific.

Named on CPU 0 and unused outside the feature dump (`arch_cpu.cpp` feature table only):

- `ibpb`, `amd_ssbd` — no write of those MSRs on a context switch. The Intel MSRs are different.
- `umip`, `pku` — no CR4 enable. The CR4 bits are the same instruction on both vendors when the feature is present.
- `sha_ni`, `vaes`, `vpclmulqdq` — userspace can use them; the kernel crypto paths were not checked for these instructions in this pass.

RenkuOS `restrict_syscalls()` is the per-team filter in `docs/ROADMAP.md`. It is not in this tree. haiku-pi’s starred kernel fixes are a separate list; R5 has already taken a set of them (`docs/LICENSING.md`). This review did not re-audit that set.

## What is already in good shape on this CPU

- Twelve threads, APIC ids 0–11, one package, one L3. The 64-CPU cap and the BIOS x2APIC gap do not show up.
- SMEP, SMAP and XSAVEC are on (`enable SMEP`, `enable SMAP`, `enable XSAVEC 0x7 832`). PAT is on.
- The TSC sample used later is `tsc_hz` 2295697015 on stick 41, in line with the 2300 MHz P-state. The line `TSC frequency calibrated via HPET: 1870961` is the conversion factor (`arch_timer.cpp:570-571`), not the frequency.
- The syscall instruction itself is `syscall` / `sysretq` on the fast path.
- One busy thread reached about 4341 MHz without the snapshot device writing an MSR.

## Order

1. Finding 1, alone. One 5625U boot. The syslog has to show SMT ids inside the six L1 pairs. No other kernel change in that boot. This one is AMD only.
2. Finding 2, memcpy only, with a timed buffer on the same machine. The `setjmp` user-copy change is its own boot, because it is a fault path. Both are general AMD64.
3. Finding 3 only if a syscall loop shows a cost worth the risk. General AMD64.
4. 2 MB pages (finding 4) when a large-area workload exists. PCID after a raw leaf 1 line that either shows the bit or explains its absence. General AMD64.
5. Power preference and idle cap (findings 5 and 6) as the laptop-power track already in the roadmap, next to air/OS and RenkuOS, not as a new CPU driver. CPPC is AMD. The idle cap is shared.
6. `launch_speedup` in the image is a package choice. The IOMMU driver is a separate project, per vendor.

Per `AGENTS.md`, a later change still needs a build, a boot, and a `CHANGELOG.md` line that says where it was tested. This review did not boot.
