# Kernel and CPU

Audit of the AMD64 kernel and boot loader that R5OS actually builds. Source of the limits is the Haiku tree at `/home/nickr/r5os/haiku`, plus the R5OS boot-loader copy `distro/boot/bios_ia32/smp.cpp` installed by `scripts/apply-kernel-bugs.py`. This is source inspection and the boots already recorded in `CHANGELOG.md` and `docs/vbox.md`. It is not a 64-CPU test, a 128-CPU test, or a bare-metal boot of the Ryzen 5 5625U.

No kernel source was changed for this audit. The read-only snapshot
further down was added later. It does not write a P-state.

## What already works

R5OS AMD64 defaults to the BIOS boot platform and also builds the EFI loader.

`build/jam/ArchitectureRules` (x86_64 case): `HAIKU_KERNEL_PLATFORM ?= bios_ia32`, and boot targets `bios_ia32`, `efi`, `pxe_ia32`. The kernel sources live under `src/system/kernel/arch/x86`.

`scripts/run-qemu-r5.sh` starts QEMU with `-machine pc` (SeaBIOS), `-smp 2`, and `-cpu host` when KVM is available, otherwise `-cpu qemu64`. `scripts/run-vbox-r5.ps1` defaults to `--cpus 2` and `--firmware bios`. Those two scripts never reach the EFI loader.

CPU identification in `src/system/kernel/arch/x86/arch_cpu.cpp` reads the vendor string, family, extended family, model, extended model, and stepping from leaf 1, then the brand string from leaves `0x80000002`–`0x80000004` (around lines 1215–1297). Named feature bits include x2APIC (`FEATURE_EXT` / `EXT_X2APIC`, line 445), Intel HWP (lines 471–483), hybrid (`FEATURE_7_EDX` / `HYBRID_CPU`, line 530), AMD hardware P-states, and invariant TSC (lines 541–542). A per-CPU feature mismatch with the boot CPU is detected only inside the optional feature dump (`sameAsBoot`, around line 388). That dump does not change scheduling.

Topology:

- AMD: `detect_amd_cpu_topology()` (line 591) uses leaf 1 for the logical-ID width and leaf `0x80000008` for the core-ID width, then builds SMT / core / package masks.
- Intel: `detect_intel_cpu_topology_x2apic()` (line 668) prefers leaf `0x1F`, then leaf `0xB`. It records level type 1 (SMT) and type 2 (core). The initial x2APIC ID comes from leaf 11 `EDX` (`get_intel_cpu_initial_x2apic_id`, line 659).

The kernel local-APIC code (`src/system/kernel/arch/x86/apic.cpp`) can run in x2APIC mode. `apic_init()` (line 285) sets that mode when leaf 1 reports x2APIC and either the hypervisor bit is set or `IA32_APIC_BASE` already has the x2APIC enable bit. Inter-processor interrupts then write the full 32-bit destination through the MSR (`apic_set_interrupt_command`, line 159, destination in bits 63:32). Unicast IPI uses the physical APIC ID stored at boot (`arch_smp.cpp` `arch_smp_send_ici`, line 317). With x2APIC off, the same function shifts that ID by 24 into the legacy ICR, so only the low 8 bits are delivered.

The EFI loader (`src/system/boot/platform/efi/arch/x86/arch_smp.cpp`) parses both MADT local-APIC entries and `ACPI_MADT_PROCESSOR_LOCAL_X2_APIC` (line 224), stores the 32-bit `x2apic_id`, and sends startup IPIs through the same MSR path when x2APIC is already enabled.

`cpu_ent gCPU[SMP_MAX_CPUS]` is the scheduler-facing CPU table (`src/system/kernel/cpu.cpp` line 30). `CPUSet` is a bitmap of `SMP_MAX_CPUS` bits (`headers/private/kernel/smp.h`, `kArraySize`). At 64 CPUs that is two `uint32` words. There is no separate public `B_MAX_CPU_COUNT`.

## Three different limits

These are not the same capability.

| Limit | Where it is enforced | What it allows |
| --- | --- | --- |
| 64 logical CPUs | `SMP_MAX_CPUS` in the BIOS and EFI platform headers | At most 64 entries in `gCPU`, `cpu_kstack`, and `cpu_apic_id` |
| 8-bit APIC ID | BIOS loader MADT parse and legacy ICR `<< 24` | IDs 0–255. CPU 64 can still have ID 0–255 |
| x2APIC / ID above 255 | EFI loader and the kernel MSR path, not the BIOS loader | 32-bit IDs, only if that path is the one that boots |

A 6-core / 12-thread Ryzen 5 5625U sits inside all three, if its firmware leaves xAPIC mode on and lists ordinary local-APIC MADT entries. That machine has not been booted for this audit. A 128-vCPU QEMU guest does not.

## Findings

### 1. BIOS SMP startup could wait forever — fixed in the R5OS loader, seen on 2 vCPUs

Severity: was a boot hang. The current distro copy bounds it. User impact on the old loader: cold start with 2 CPUs stays on gray splash icons. Implementation risk of the existing fix: low. It is already in tree.

`distro/boot/bios_ia32/smp.cpp` `smp_boot_other_cpus()` (line 496) tries each CPU three times. `start_application_cpu()` waits about a second (`cpu_relax` loop at line 480) instead of spinning with no timeout. If that CPU never answers, `num_cpus` is cut to the CPUs that did start (line 537) so the kernel does not wait on a hole. `scripts/apply-kernel-bugs.py` `install_smp_boot()` copies this file over the Haiku BIOS loader.

Seen: VirtualBox, firmware BIOS, 2 CPUs, 2026-10-30 13:18 Europe/Amsterdam, image `generated/r5-os-smpfix.vdi`. Serial showed `smp: booting 1 other cpu`, then kernel `CPU 1` and `scheduler_init: found 2 logical cpus`. The give-up path did not run. The daily VDI was not that image. Details: `CHANGELOG.md`, `docs/vbox.md`.

Not seen: the retry under load, a CPU that fails and is dropped, reboot/shutdown stress, or more than 2 CPUs.

### 2. `SMP_MAX_CPUS` is 64 on both AMD64 boot platforms

Severity: hard cap. No effect on the reference laptop or the current 2-vCPU VM. Breaks any machine or VM with 65 or more logical processors. Risk of raising it: medium. Every array sized from the macro, and the boot loader and kernel, must move together.

Authoritative defines:

- `headers/private/kernel/boot/platform/bios_ia32/platform_kernel_args.h` line 18: `#define SMP_MAX_CPUS 64`
- `headers/private/kernel/boot/platform/efi/platform_kernel_args.h` line 21: `#define SMP_MAX_CPUS 64`
- amiga, atari, open firmware, u-boot, and routerboard headers: `1` (those platforms are not R5OS targets)

Consumers that grow with the constant (39 files under `src/` and `headers/` reference the macro):

- `kernel_args.h` line 67: `addr_range cpu_kstack[SMP_MAX_CPUS]`
- `arch_kernel_args.h` lines 36–37: `uint32 cpu_apic_id[SMP_MAX_CPUS]` and `cpu_apic_version[SMP_MAX_CPUS]`
- `cpu.cpp` line 30: `cpu_ent gCPU[SMP_MAX_CPUS]`
- `arch_smp.cpp` lines 46–47: `sCPUAPICIds` and `sAPICVersions`
- `CPUSet::kArraySize` in `smp.h`
- `scheduler_profiler.h` lines 78–79: per-CPU profiler stacks

The BIOS MADT walk (`distro/boot/bios_ia32/smp.cpp` line 278) stops storing CPUs at 64. The message is a `TRACE`, so a normal boot is silent. Further MADT entries are skipped, not started. The EFI walk does the same check (line 186).

`CPUSet::SetBit` does not check the index. A CPU number past the bitmap writes off the end. Today the cap at enumeration keeps `num_cpus` inside the array. Raising the constant without rebuilding both sides of `kernel_args` would desynchronize the boot loader and the kernel.

Minimal fix, after a test, not before: one new constant used by the BIOS and EFI headers, rebuild loader and kernel together, and `dprintf` when a processor entry is dropped. Do not treat “64” as “APIC IDs above 255 work”.

### 3. The BIOS loader cannot see x2APIC processors or APIC IDs above 255

Severity: high on hardware that only publishes x2APIC MADT entries, or whose firmware has already turned x2APIC on. Low on the 5625U and on today’s QEMU and VirtualBox scripts, which use BIOS and a small CPU count. Risk: medium. The EFI loader already has the MSR path to copy. The BIOS trampoline and the “current CPU” lookup have to agree with it.

`distro/boot/bios_ia32/smp.cpp` MADT switch (line 276) handles `ACPI_MADT_LOCAL_APIC` and `ACPI_MADT_IO_APIC` only. The local APIC id stored at line 292 is the 8-bit MADT field. There is no `ACPI_MADT_PROCESSOR_LOCAL_X2_APIC` case. Unknown entry types fall through `default` and are ignored.

Startup IPIs (lines 443, 452, 469) write `cpu_apic_id[cpu] << 24` through the MMIO ICR. Bits above 7 are shifted out of the 32-bit register. `smp_get_current_cpu()` (line 329) reads the id as `uint8`. Two CPUs that differ only above bit 7 look like the same CPU, and the function returns 0 when nothing matches.

The file still has a safemode item “Disable X2APIC” (line 567) when CPUID says the feature exists. That flag is for the kernel. The loader itself never programs x2APIC.

The kernel will turn x2APIC on later if the hypervisor bit is set (`apic.cpp` line 285), which QEMU and VirtualBox do. That happens after the BIOS loader has already started application processors with MMIO. It works when every ID fit in 8 bits and the firmware left xAPIC mode on. It does not recover processors the loader never stored.

Intel SDM: once `IA32_APIC_BASE` x2APIC is enabled, the MMIO register block is gone. A firmware that enables x2APIC before the OS would make these MMIO INIT/SIPI writes miss. That case is not reproduced here.

Minimal fix: in the BIOS loader, mirror the EFI loader. If the enable bit is set, parse type-9 MADT entries, keep the 32-bit id, and send INIT/SIPI with the MSR ICR. If the bit is clear, keep today’s MMIO path for IDs 0–255. Print a line when an entry is skipped because the id does not fit the mode in use.

### 4. Logical destination is programmed only for CPUs 0–7 in xAPIC mode

Severity: low for inter-processor interrupts. Possible for anything that still uses the logical destination register. Not reproduced.

`apic_per_cpu_init()` (`apic.cpp` line 327) writes the flat logical id `1 << cpu` only when x2APIC is off and `cpu < 8`. CPUs 8–63 keep whatever the firmware left in the LDR.

Kernel multicast IPI does not use that register in xAPIC mode. `arch_smp_send_multicast_ici()` (`arch_smp.cpp` line 258) calls `send_multicast_ici_physical()` unless x2APIC is on. Physical mode uses `sCPUAPICIds[i]` and the 8-bit ICR. So 9–64 CPUs with IDs that fit in 8 bits can still take an IPI.

In x2APIC mode the same function sends one logical-mode IPI per CPU, using `gCPU[i].arch.logical_apic_id` from the LDR MSR (`apic_logical_apic_id`, line 115). That path is not exercised by the 2-vCPU boots.

Minimal fix: leave this alone until a >8 CPU xAPIC boot shows a lost interrupt. If it does, send those IPIs in physical mode only, which is already what xAPIC multicast does.

### 5. AMD core-count fallback reads the wrong register — possible, dormant on Zen

Severity: possible wrong SMT/core masks. Not reproduced. The 5625U is Zen 3; if leaf `0x80000008` ECX bits 15:12 are non-zero, this line never runs.

`detect_amd_cpu_topology()` (`arch_cpu.cpp` around lines 602–607): when the 4-bit `ApicIdCoreIdSize` field is 0, the core count is `next_power_of_2((cpuid.regs.edx & 0xf) + 1)`. AMD documents that count in ECX bits 7:0 of the same leaf (`NC`), not in the low nibble of EDX. EDX on that leaf is a feature word on current AMD. The fallback would then see at most 16 cores and would often see 1.

There is no read of leaf `0x8000001E` (extended APIC id / threads per core / cores per compute unit). Zen topology is whatever `0x80000008` plus the leaf-1 logical count produce.

Minimal fix: use ECX bits 7:0 for the legacy count, and prefer leaf `0x8000001E` when the extended leaf allows it. Confirm on the 5625U by printing the masks before changing them. This belongs in the kernel topology code, not in a driver.

### 6. Intel hybrid CPUs are named and then ignored

Severity: none on the 5625U (one core type). On Alder Lake and newer, thread placement can prefer an efficiency core with no policy saying otherwise. Not reproduced. R5OS has no hybrid test machine in the logs.

`get_hybrid_cpu_type()` (line 1170) returns 0 when the hybrid feature bit is clear. Otherwise it shifts CPUID leaf `0x1A` EAX by 24. `get_hybrid_cpu_type_string()` maps that to a name such as `"Atom"`. `cpu->arch.hybrid_type` is stored and printed (lines 1357–1366). A tree search finds `hybrid` only in `arch_cpu.cpp`. The scheduler does not read it.

Leaf `0x1F` level types other than SMT and core (module, tile, die) are ignored by the switch in `detect_intel_cpu_topology_x2apic()`. The loop stops when both SMT and core have been seen, or when the level type is 0, so a die-only leaf does not spin forever. The package mask can still be wrong. Not reproduced.

Where a later change belongs: scheduler (which core receives a thread). Reporting the type to userspace is a small kernel export plus a tool. No userspace program can see `hybrid_type` today.

### 7. No NUMA

Severity: none on a single-CCX laptop. Real on multi-socket or multi-CCD parts, where every allocation is treated as local. Not reproduced.

Search of `src/system` for `SRAT`, `SLIT`, and `numa` found no parser and no node map. Earlier hits on similar names (`kNumAsids`, `numArgs`) were unrelated. Memory and the scheduler are UMA.

Where it belongs: kernel (ACPI SRAT/SLIT, per-node free lists, scheduler domain). A userspace tool can only print what the kernel exports.

### 8. Power and frequency are detected more than they are controlled

Severity: laptops run at firmware-chosen P-states. No confirmed bug. Not a scalability bug.

`r5_pstate.cpp` reads AMD hardware P-states and CPPC, and Intel HWP capabilities, when those CPUID bits are set and the hypervisor bit is clear. It does not write an MSR. The write hook on `/dev/misc/r5_pstate` returns `B_NOT_ALLOWED`. Nothing in this tree programs `IA32_PM_ENABLE`. `arch_timer.cpp` reads AMD `MSR_F10H_PSTATEDEF(0)` to estimate TSC frequency. `apply-kernel-bugs.py` `patch_tsc_div0()` guards the CPUID TSC conversion against a zero divisor (Haiku #20400). That is a panic fix in the timer, not a power driver.

Invariant TSC is a named feature bit. C-state policy and a userspace power preference are not in this tree as an R5OS component.

Where it belongs: MSRs and idle (kernel). A “balanced / performance” control, if one is added later, is a preference program talking to the kernel, not a new APIC path.

### 9. This pass did not certify the kernel free of races

The SMP boot stall in finding 1 is the CPU bug reproduced on R5OS. The TSC divisor guard is in the apply script so a zero CPUID frequency does not divide. No other race, deadlock, leak, or bad error path was executed for this audit. Absence from this document is not a clean bill of health.

## Test record

Executed:

- VirtualBox, BIOS firmware, 2 CPUs, SMP loader timeout in place, 2026-10-30 13:18 Europe/Amsterdam. Both logical CPUs entered the kernel. Serial only. Screen not watched. Give-up path not hit. Daily VDI not updated in that run.
- QEMU script remains `-smp 2`. No serial from a QEMU SMP run is cited here.

Not executed, and not claimed:

- QEMU `-smp` 8, 16, 64, or 128
- APIC IDs above 255, x2APIC left on by firmware, MADT type 9 only
- Topology printout on the Ryzen 5 5625U
- Intel hybrid, NUMA, HWP
- Parallel stress, reboot, and shutdown across those CPU counts
- EFI boot of an R5OS image (the scripts force BIOS)

Proposed, in order, before any constant change:

1. QEMU KVM, `-smp 8`, BIOS, serial captured. Expect `scheduler_init: found 8 logical cpus`, eight `CPU n` lines, and a reboot that reaches the same count.
2. Same with `-smp 64`. Expect 64. Watch for a silent stop at 64 if the guest has more.
3. `-smp 128`. Expect the current loader to keep 64 and ignore the rest. Record the APIC ids QEMU chose. If they are all below 256, this tests the cap, not x2APIC.
4. QEMU `-device intel-iommu` is unnecessary. For IDs above 255 use a CPU model and topology that QEMU places in x2APIC (often `sockets`/`cores`/`threads` high enough, or a host with those ids). Boot once with the BIOS loader and once with an EFI firmware (`OVMF`) so the two loaders are compared on the same topology.
5. VirtualBox stays at 2 CPUs until QEMU 8 and 64 are boring. Then raise that VM’s CPU count only on a throwaway VDI. Do not attach `generated/r5-os.vdi`.
6. On the 5625U, after a normal boot: serial lines for vendor, brand, family, model, stepping, `found N logical cpus` with N = 12, and one IPI-heavy workload (build). That is the first hardware claim this document would accept.

## Roadmap

Correctness, already in the tree or small and local:

1. Keep the BIOS SMP timeout. Do not put the unbounded wait back.
2. Keep the TSC zero check.
3. When a CPU entry is dropped, print it at `dprintf` level (count, APIC id, reason). That is the whole change until a >64 or x2APIC boot exists.

Scalability and hardware, after the QEMU steps above:

4. BIOS loader: x2APIC MADT entries and MSR startup IPIs, matching the EFI loader. Still cap at 64 until step 5.
5. Raise `SMP_MAX_CPUS` from 64 to 128 in the BIOS and EFI headers together. Rebuild loader and kernel. Re-run QEMU 64 and 128. Stop if `kernel_args` layout or a `CPUSet` user assumes 64 by a private array.
6. AMD topology: ECX `NC` and leaf `0x8000001E`, checked on the 5625U before it is trusted.

Later modernization, not required for the reference machine:

7. Scheduler input from `hybrid_type` (Intel only; no R5OS machine yet).
8. SRAT/SLIT and a scheduler domain, for multi-CCD and multi-socket.
9. HWP or AMD P-state requests. The frequency readout is the snapshot below. Requests are still not written. Safemode must leave the registers alone.

Do not start that work until this report has been reviewed. Do not import a Linux CPU driver to get there.

## Read-only frequency snapshot

This is the readout for GitHub issue 86. It does not change a P-state. The Ryzen 5 5625U has not been booted with it.

`distro/kernel/x86/r5_pstate.cpp` runs from `arch_cpu_init_post_modules()`. It prints the vendor, family, model, brand string, and `gCPUClockSpeed`, then publishes the same text at `/dev/misc/r5_pstate`. Opening that device takes a new sample on the CPU that handles the open. The write hook returns `B_NOT_ALLOWED`.

AMD and Hygon, only when the matching CPUID bit is set:

- Hardware P-state: current index from MSR `0xC0010063`, and definitions `0xC0010064`–`0xC001006B`. Family `0x17` and `0x19` report MHz as `200 * fid / did`, with the same divisor limits as `init_tsc_with_msr()` in `arch_timer.cpp`. Other families print the raw register and `mhz n/a`.
- CPPC: capability `0xC00102B0`, enable `0xC00102B1`, request `0xC00102B3`, and status `0xC00102B4`.

Intel, only when HWP is enumerated: capabilities `0x771` and enable `0x770`. The request register `0x774` is read only when the enable bit is already 1.

If CPUID says the machine is a hypervisor, none of those MSRs are read. QEMU sets that bit. A guest that is handed the feature bit without the register would otherwise take a general-protection fault on `rdmsr`.

`IA32_APERF` and `IA32_MPERF` are included when that feature bit is set and the hypervisor bit is clear. The kernel already reads those during CPU init.

`/system/bin/r5_pstate_log` is a job on `initial_volumes_mounted`. It waits briefly, reads the device, spends one second in a loop on that thread, reads again, and writes `/boot/home/r5-cpu.txt`. It then searches `/dev/disk` for a FAT boot sector, mounts the smallest FAT32 partition it finds, writes `r5-cpu.txt` on it, and unmounts.

The raw QEMU image has no FAT partition, so that search reports the miss. An anyboot image has the EFI system partition. After a USB boot, Windows can open that partition and read `r5-cpu.txt`. Copy the text into this file before any P-state write is attempted.

QEMU, 2026-10-31 about 16:00 Europe/Amsterdam. TCG, `-cpu qemu64`, `-display none`, serial to a file. `/dev/kvm` was not readable from this shell. The kernel line was `R5 CPU: cpu 1 vendor amd family 0xf model 0x6b name QEMU Virtual CPU version 2.5+`. `tsc_hz` was 2297152544. The next lines were `hypervisor bit set, hardware MSRs not read` and `no msr write`. Publish status was 0. `bfs_shell` then showed both samples in `/boot/home/r5-cpu.txt`, plus `FAT: no FAT partition found under /dev/disk`. No panic. `jam -q @r5-anyboot` exited 0 at 16:03 Europe/Amsterdam and wrote `generated.x86_64/r5-os-anyboot.iso`. `fdisk` shows a 2G BeFS partition and a 2.8M EFI partition, type `ef`. That file was not booted.

Still open: boot the 5625U and record the idle and busy samples. Write one P-state only after that, with safemode leaving the registers alone. Intel HWP on hardware was not executed.

