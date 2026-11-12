<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: MIT
-->
# Review of the AMD64 kernel (12 Nov 2026)

For Cursor. Read-only review of what the kernel does on the Ryzen 5 5625U, and what is still missing. Sources: `origin/main` at `d988322049` (`docs/KERNEL.md`, `docs/ROADMAP.md`, the stick logs under `generated/boot-5625u-*`) and the WSL Haiku tree at `4aae8640c9`, which already contains the R5 `r5_pstate` publish and the TSC guards. Stick 33 is the boot that still has the early CPU lines. Stick 41 is the frequency sample. Sticks 42 and later are mentioned in `CHANGELOG.md`; their log folders are not in `origin/main`, so they are not used here. Nothing was built or booted for this review.

The other forks were not cloned. The first version took what they have from `docs/ROADMAP.md` and `generated/haiku-pi/REVIEW-1.md` only, and said none of them had a Zen SMT fix. That was wrong. On review, the commit history of the same kernel files was read on GitHub (12 Nov 2026), see “What the other forks already have” below. air/OS has fixes for findings 1, 2, 5 and 6. RenkuOS has the deep-idle hold. haiku-pi has no change to these files beyond upstream.

**Verdict: the 12 threads are up, and one busy thread already runs at about 4341 MHz. The scheduler is told that those 12 threads are 12 separate cores.** That is the change that can make this laptop faster. Copies and the syscall path come after it. For battery, the larger gap is idle: every core stops at C1 (finding 6). air/OS already has fixes for the SMT map, the deep idle and one `memcpy` slowdown; those are imports, not new work. Raising `SMP_MAX_CPUS` and teaching the BIOS loader x2APIC does not change this machine: the log shows APIC ids 0 through 11.

`docs/KERNEL.md` already records the 64-CPU cap, the BIOS x2APIC gap, NUMA, hybrid CPUs, and the read-only P-state snapshot. Those are not repeated as new findings. One correction: AMD CPPC is written by `amd_pstates`, which that document does not cover.

Line numbers are from the WSL tree at `4aae8640c9`.

**Status (build 10059):** findings 1 and 6 are taken from air/OS, together with the scheduler commits for SMT and cores of several speeds, so Intel hybrid and Zen 5c are covered too. Generic, not for this CPU only. The RenkuOS deep-idle hold, the `memcpy` aliasing fix, and findings 3, 4, 5 and 7 are still open. Details and limits are in `CHANGELOG.md`. Not booted on the 5625U yet.

**Status (build 10060):** stick 47 confirmed the SMT map on the 5625U. The topology line now also prints leaf 1 ECX (the PCID question), `amd_pstates` keeps its CPPC values per CPU and logs them, and the air/OS `memcpy` aliasing fix is in. `launch_speedup` does not compile any more and stays out. Still open: findings 3, 4 (beyond the PCID print) and 7, the AVX2 copy, the `setjmp` user copy. Stick 49 answered the PCID question: bit 17 is clear in the raw CPUID word on the 5625U, so the CPU or firmware does not report PCID; PCID work stops for this laptop. Deep idle runs on `x86_acpi_cstates` with C1–C3, and the per-CPU CPPC values hold.

## AMD and Intel

These are not all general AMD64 fixes. The shared x86_64 code also runs on Intel. The AMD CPUID leaves and MSRs do not.

General AMD64, so also Intel:

- **Copies (finding 2).** `memcpy` and `memset` for x86_64 are one implementation for both vendors. AVX2 and ERMS exist on Intel too (the X1 Carbon Gen 8 has AVX2). The `setjmp` around every kernel copy from user memory applies to every 64-bit build. The 32-bit kernel uses a different path.
- **Syscall clear (finding 3).** `vzeroall` on the fast return is chosen when the XSAVE mask stops at AVX. That is this Ryzen, and an Intel CPU without AVX-512. An Intel CPU with AVX-512 takes the other patch (`xrstor`).
- **Page tables (finding 4).** Rewriting CR3, no PCID, and ordinary areas in 4 KB pages live in the shared 64-bit paging code. 2 MB pages would help both. PCID is usually present on Intel. On the 5625U the feature line did not list `pcid`, so that work waits until the CPU reports the bit.
- **Idle cap (finding 6).** A hold on deep C-states for a driver applies to both vendors. The `x86_cstates` module is shared and already has an Intel exception (Skylake C5/C6).
- **`launch_speedup`, the syscall filter, UMIP and PKU.** These do not depend on the vendor. Enable UMIP and PKU only when the feature bit is set. Both vendors have those bits on recent CPUs.

AMD only:

- **SMT map (finding 1).** The wrong masks come from `detect_amd_cpu_topology()` (leaf `0x80000008`; leaf `0x8000001E` is never read). Intel uses leaf `0xB` or `0x1F`. The scheduler is shared: it trusts the map the architecture code hands it. An AMD leaf does not change the X1 Carbon. The open point there is a different one: hybrid cores are named and then ignored (`docs/KERNEL.md` finding 6).
- **Frequency (finding 5).** `amd_pstates` writes AMD CPPC and returns at once when the vendor is not AMD. Intel HWP is a different set of MSRs. The 4341 MHz on the 5625U says nothing about the X1. Preferred cores and the shared-globals race in that module are AMD only too.
- **Deep idle (finding 6).** MWAIT on Zen reports C1 only; the deep state is reached through ACPI `_CST`. Intel reports its deep states through MWAIT, so this gap is AMD only.
- **IOMMU (finding 7).** `1022:1631` is the AMD IOMMU. Intel is VT-d, a different device and a different driver.
- **IBPB and SSBD.** The feature bit exists on both. The MSR to write does not match. One call that ignores the vendor hits the wrong register on the other CPU.

## What the other forks already have

Added on review. Read from the commit history of each file on GitHub, not built here. `docs/ROADMAP.md` (“Other Haiku forks as sources”) says R5 takes what already works there. All of these are MIT, like the files they change.

| Finding | Fork | Commit | What it does |
|---|---|---|---|
| 1 | air/OS | `49eda6ac0` | Zen topology from leaf `0x8000001E` (threads per core, extended APIC id). Also stops multiplying the `0x8000001D` cache sharing by the SMT threads. Without that second part the cache ids go wrong as soon as the SMT ids are right. |
| 1 | air/OS | `3fbd41c84` | Scheduler, low-latency placement on SMT cores: a thread moves off a core whose sibling is busy. Measured with the topology fix. |
| 2 | air/OS | `6178b691d` | `memcpy` leaves `rep movsb` when source and destination are less than 16 bytes apart modulo 4 KB. There `rep movsb` was about 14 times slower. This hits app_server bitmap rows. |
| 5 | air/OS | `8d12a3ae3`, `5e303bb5a`, `252a1c6ed` | `amd_pstates` power-saving mode: boost stays allowed, EPP `0x80` (balance_performance). Measured on Zen 5. |
| 6 | air/OS | `2c48d1ae0`, `32a420032`, `e24004dc2` | `x86_acpi_cstates` for AMD: ACPI0007 processor devices, no `_OSC`/`_PDC` needed, ranks above `x86_cstates` on AMD when a state deeper than C1 exists, picks the state by idle time. air/OS puts it in the image (`build/jam/packages/Haiku:117`). |
| 6 | RenkuOS | `a432cb265`, `3fc13fe60`, `72cee9ae2`, `0aed7ba4c` | `cpu_block_deep_idle()`: a driver keeps every CPU out of deep idle. Changes both idle modules. |

air/OS also has `7abf76e98` and later scheduler commits for cores of several speeds (hybrid x86). That is `docs/KERNEL.md` finding 6, the X1 Carbon, not this machine.

The air/OS and RenkuOS changes to `acpi_cpuidle.cpp` were made apart from each other. Taking both is a merge by hand.

Nothing found in the forks for: the `setjmp` user copy, the syscall clear, PCID, 2 MB pages, the range-flush limit, preferred cores, the `amd_pstates` globals race, the AMD IOMMU, or `launch_speedup` in the image.

## 1. Every logical CPU is its own core (this machine)

`arch_cpu.cpp:605-633` and `:857-875`. `detect_amd_cpu_topology()` builds the SMT and core masks from leaf 1 and leaf `0x80000008`. Leaf `0x8000001E` is never read. The printed ids are `topology_id[]`, and the scheduler groups CPUs with those same ids (`scheduler.cpp:661-694`, `:735-744`).

Stick 33, `generated/boot-5625u-33/r5-syslog.txt`:

- `CPU 0: apic id 0, package 0, core 0, smt 0` through `CPU 11: apic id 11, package 0, core 11, smt 0`
- `scheduler_init: found 12 logical cpus and 3 cache levels`
- `scheduler switches: single core: false, cpu load tracking: false, core load tracking: true` (twice, lines 689 and 1870)

The cache ids on that same boot are the real pairs. L1 and L2 are shared by 0–1, 2–3, 4–5, 6–7, 8–9 and 10–11. L3 id is 0 on all of them. So the cache walk (leaf `0x8000001D`) sees six cores, and the topology walk sees twelve.

`scheduler_init()` counts a core for every CPU whose SMT id is 0 (`scheduler.cpp:675-678`). That count is 12, so there are 12 `CoreEntry`s of one thread each. Low-latency mode, which this boot selects, spreads runnable threads across cores. With this map it will put two threads on a hardware pair while another core is idle, because it has no pairs.

`docs/KERNEL.md` finding 5 calls the EDX fallback dormant on Zen and asks for a mask print before any change. The print now exists, and the masks are wrong on the reference laptop. The fallback line is not what the log proves. The result of the whole function is.

Cause (added on review, from the source): the CmpLegacy check at `arch_cpu.cpp:624-629` sets `maxCoreID = maxLogicalID` whenever leaf `0x80000001` ECX bit 1 is set, and Zen sets it. `compute_cpu_hierarchy_masks()` (`:577-594`) then gets `kMaxSMTID = maxLogicalID / maxCoreID = 1`, so the SMT mask is 0 and every CPU gets SMT id 0. That bit meant "no SMT, the logical CPUs are cores" on the K8 dual-cores this code was written for. Without the CmpLegacy line the answer would very likely be the same: on Zen, `ApicIdCoreIdSize` in leaf `0x80000008` sizes the APIC ids of the whole package, threads included. The raw words are not in the log, so that second point is not proven on this machine.

Fix: import air/OS `49eda6ac0` instead of writing it again. It does what this finding asks: threads per core from leaf `0x8000001E` EBX bits 15:8, the core width as the logical width divided by that, Zen only (`IA32_FEATURE_AMD_EXT_TOPOLOGY` and the leaf present), so the K8 path stays. Take its cache-sharing change in the same import. Add one print of leaf 1 EBX, `0x80000008` ECX and `0x8000001E` EBX on CPU 0. Boot the 5625U again. The pass is twelve lines with `smt` 0 and 1 inside each L1 pair, the L1 and L2 ids still in the same six pairs, and `scheduler_init` still saying 12 logical CPUs. The air/OS scheduler placement (`3fbd41c84`) is a second step with its own boot.

## 2. Copies stop at SSE, on a CPU that has AVX2 and ERMS

`libroot/.../x86_64/memcpy.cpp:142-155` and `memset.cpp:73-86`. The kernel links the same two files (`kernel/lib/arch/x86_64/Jamfile`). Length up to 16 bytes is integer copies, up to 2048 bytes is `movdqu` (16 bytes), and above that is `rep movsb` / `rep stosb`.

Stick 33 lists `avx2`, `erms`, `clflushopt` and `clwb` on CPU 0. `rep movsb` above 2048 bytes matches ERMS, so the large path is the right instruction, except when source and destination are less than 16 bytes apart modulo 4 KB. There `rep movsb` is about 14 times slower (air/OS `6178b691d`, measured on Zen 1). That case is common in app_server: bitmap rows copied a few pixels to the side. Width 16 bytes leaves AVX2 unused for the copies app_server, the media kit and the file cache actually do. There is also no non-temporal store for a copy that is larger than the cache. The write-combined framebuffer alias is already described in `docs/KERNEL.md` under “Memory types”; this is the CPU side of the same copies.

AMD64 user copies go through `headers/private/kernel/arch/generic/user_memory.h:78-82`: one `setjmp` per `memcpy`. The 32-bit kernel uses a fault pointer in assembly (`arch/user_memory.h:14-32`) and does not take that path. Every `read` and `write` on this OS pays the `setjmp`.

Fix: first import air/OS `6178b691d` with its `memcpy_test`; that is a correctness-tested change already. Then an AVX2 body for the middle sizes, and a timed copy on the 5625U before and after. Keep `rep movsb` for the large sizes while ERMS is set, outside the aliasing window. The `setjmp` user-copy change is its own boot, because it is a fault path. Both changes are general AMD64.

## 3. The fast syscall return clears the whole vector file

`interrupts.S:120-138` and `:458-483`. The fast path saves no XSAVE state. It runs `CLEAR_FPU_STATE()` and then `sysretq`. XMM and YMM are caller-saved across a Haiku syscall, so wiping them is allowed. The cost is on every syscall.

Stick 33: `enable XSAVEC 0x7 832`. Mask `0x7` is x87, SSE and AVX, which is the case `arch_cpu.cpp:1717-1721` patches with `vzeroall` (3 bytes). `arch_altcodepatch.cpp:54-63` fills the rest of the site with nops, so the old `pxor` list does not also run. The instruction that remains on the fast path is `vzeroall`.

Interrupts are a different path: `interrupts.S:274-280` writes the 832-byte XSAVE area on the way in. That part matches the CPU.

Fix only after a measurement: a tight `gettimeofday` or `null` syscall loop on the 5625U, cycles per call, with the clear left in and with it patched out. If the difference is noise, leave it. The clear is also what stops kernel vector data reaching the caller. The same fast path runs on Intel.

## 4. TLB flushes and page size

`arch_cpu.cpp:1784-1816`. A full flush writes CR3. A range flush is `invlpg` one page at a time, with no limit after which it falls back to one CR3 write, so unmapping a large area costs one `invlpg` per page. PCID and INVPCID are named (`arch_cpu.cpp:454`, `:507`) and have no other use.

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

Added on review: the cores do not all report the same highest performance. Stick 33 prints CPPC capabilities `0xba572a10` (highest 186) from the CPU that ran `init_pstates()`. Stick 41 reads `cppc_cap 0xb5572a10` (highest 181) on CPU 3. These are AMD preferred cores: some cores boost higher than others. The scheduler does not know this and puts a single busy thread on any core. Using that ranking is AMD only, and needs a print of CAP1 on all twelve CPUs first.

Also added on review: `set_normal_pstate()` (`amd_pstates.cpp:66-82`) runs on all CPUs at the same time through `call_all_cpus_sync()`, and writes the CAP1 fields into the shared globals `sHWPHighest` and the others before it reads them back for its own request. One CPU can therefore write another CPU’s highest value as its own max. Stick 41 shows CPU 3 with max 181, matching its own cap, so the log does not show the race. It is a read of the code. The fix is local variables. The check is a request dump on every CPU.

air/OS `power_daemon` (lid, AC, battery, suspend to idle) and RenkuOS’s “stay out of a deep idle state when a driver asks” are the fork pieces `docs/ROADMAP.md` already lists under power. They are not in this kernel. air/OS also changed `amd_pstates` for power-saving mode (see the table): boost stays on and EPP is `0x80`. R5 forces low-latency mode, so that only matters once the user can choose. This finding is AMD CPPC. Intel HWP is a separate module.

## 6. Idle stops at C1 on this machine

Corrected on review. The first version said the C-state line was missing from stick 33 and that deep states stay available. The line is there: stick 33 line 1871, `using x86 C-States: C0, C1`, right before `found cpuidle module: power/cpuidle/x86_cstates/v1`.

`x86_cstates.cpp:128-190` builds its list from CPUID leaf 5 (MWAIT sub-states). On this Zen 3 that leaf reports C1 only, so the module has one state, and every idle is `mwait` with hint 0, which is C1. `x86_cstates.cpp:77-104` picks a deeper state as the idle time grows, but there is no deeper state to pick. Low-latency mode changes the step and the spin wait, and with one state that makes no difference.

AMD reaches its deep core state (CC6) through the ACPI `_CST` table, usually an I/O port read, not through MWAIT. The tree has a module for that, `src/add-ons/kernel/power/cpuidle/x86_acpi_cstates/acpi_cpuidle.cpp`, and it is built. The image does not contain it: `build/jam/packages/Haiku:114` lists only `x86_cstates`. It has not been tried on this laptop. Its quirks are written for Intel.

So an idle 5625U stays in C1 on every core. That costs battery and heat; it does not slow anything down. This is the biggest power gap in this review, and it is AMD only: Intel reports its deep states in leaf 5, so the X1 should get them from `x86_cstates`.

`arch_cpu.cpp:1477-1481` is `hlt`, used when no idle module is loaded. On this boot the module loaded. CPU 0 lists `monitor` and `mwaitx`. The idle path uses `monitor`/`mwait`, not `mwaitx`.

air/OS has already done this work (`2c48d1ae0`, `32a420032`, `e24004dc2`): the ACPI module for AMD, ranked above `x86_cstates` when a state deeper than C1 exists, in their image. Their commit message records the same C1-only idle on a Ryzen 7640U, 7 W while idle. Import those three, add `x86_acpi_cstates` to the image, and boot the 5625U once. The pass is a syslog line with the `_CST` states it accepts, `found cpuidle module: power/cpuidle/x86_acpi_cstates/v1`, and a desktop that stays responsive.

RenkuOS has the hold for a driver that cannot tolerate a deep C-state (`cpu_block_deep_idle()`, see the table). It only matters once a deep state exists. It changes the same file as the air/OS commits, so it comes after them, merged by hand, with audio and NVMe as the reason.

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

1. Finding 1, alone: air/OS `49eda6ac0`. One 5625U boot. The syslog has to show SMT ids inside the six L1 pairs. No other kernel change in that boot. This one is AMD only.
2. Finding 2: air/OS `6178b691d` first, then the AVX2 memcpy, with a timed buffer on the same machine. The `setjmp` user-copy change is its own boot, because it is a fault path. Both are general AMD64.
3. Finding 3 only if a syscall loop shows a cost worth the risk. General AMD64.
4. 2 MB pages (finding 4) when a large-area workload exists. PCID after a raw leaf 1 line that either shows the bit or explains its absence. General AMD64.
5. Deep idle on the 5625U (finding 6): the three air/OS `x86_acpi_cstates` commits and the module in the image, one boot, no other change. Then the RenkuOS hold. AMD only. This is the battery item; it can run in parallel with steps 2–4 because it touches no shared code.
6. Power preference, preferred cores and idle cap (findings 5 and 6) as the laptop-power track already in the roadmap, next to air/OS and RenkuOS, not as a new CPU driver. The shared-globals fix in `amd_pstates` is small, and its check is a request dump on every CPU. CPPC is AMD. The idle cap is shared.
7. `launch_speedup` in the image is a package choice. The IOMMU driver is a separate project, per vendor.

## Not proven

- The raw CPUID words behind finding 1. The log has the result, not the input.
- That the air/OS commits apply to this tree as they are, and work on the 5625U. They were read, not built. Their measurements are from a Threadripper 1950X, a Ryzen 7640U and a Ryzen AI 5 340, not from this laptop.
- The `amd_pstates` race (finding 5). It is a read of the code.
- The cost of the syscall clear and of the `setjmp` per user copy. Nothing was timed.
- What the 5625U firmware puts in `_CST`.

Per `AGENTS.md`, a later change still needs a build, a boot, and a `CHANGELOG.md` line that says where it was tested. This review did not boot.
