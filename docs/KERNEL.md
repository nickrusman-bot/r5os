# Kernel CPU readout

This is the read-only frequency snapshot for GitHub issue 86. It does not change a P-state. The Ryzen 5 5625U has not been booted with it.

## What the kernel reads

`distro/kernel/x86/r5_pstate.cpp` runs from `arch_cpu_init_post_modules()`. It prints the vendor, family, model, brand string, and `gCPUClockSpeed`, then publishes the same text at `/dev/misc/r5_pstate`. Opening that device takes a new sample on the CPU that handles the open. The write hook returns `B_NOT_ALLOWED`.

AMD and Hygon, only when the matching CPUID bit is set:

- Hardware P-state: current index from MSR `0xC0010063`, and definitions `0xC0010064`–`0xC001006B`. Family `0x17` and `0x19` report MHz as `200 * fid / did`, with the same divisor limits as `init_tsc_with_msr()` in `arch_timer.cpp`. Other families print the raw register and `mhz n/a`.
- CPPC: capability `0xC00102B0`, enable `0xC00102B1`, request `0xC00102B3`, and status `0xC00102B4`.

Intel, only when HWP is enumerated: capabilities `0x771` and enable `0x770`. The request register `0x774` is read only when the enable bit is already 1.

If CPUID says the machine is a hypervisor, none of those MSRs are read. QEMU sets that bit. A guest that is handed the feature bit without the register would otherwise take a general-protection fault on `rdmsr`.

`IA32_APERF` and `IA32_MPERF` are included when that feature bit is set and the hypervisor bit is clear. The kernel already reads those during CPU init.

## Log on a USB stick

`/system/bin/r5_pstate_log` is a job on `initial_volumes_mounted`. It waits briefly, reads the device, spends one second in a loop on that thread, reads again, and writes `/boot/home/r5-cpu.txt`. It then searches `/dev/disk` for a FAT boot sector, mounts the smallest FAT32 partition it finds, writes `r5-cpu.txt` on it, and unmounts.

The raw QEMU image has no FAT partition, so that search reports the miss. An anyboot image has the EFI system partition. After a USB boot, Windows can open that partition and read `r5-cpu.txt`. Copy the text into this file before any P-state write is attempted.

## QEMU

2026-10-31 about 16:00 Europe/Amsterdam. TCG, `-cpu qemu64`, `-display none`, serial to a file. `/dev/kvm` was not readable from this shell. The kernel line was:

`R5 CPU: cpu 1 vendor amd family 0xf model 0x6b name QEMU Virtual CPU version 2.5+`

`tsc_hz` was 2297152544. The next lines were `hypervisor bit set, hardware MSRs not read` and `no msr write`. Publish status was 0. The guest reached the log job. `bfs_shell` then showed both samples in `/boot/home/r5-cpu.txt`, plus `FAT: no FAT partition found under /dev/disk`. No panic.

`jam -q @r5-anyboot` exited 0 at 16:03 Europe/Amsterdam and wrote `generated.x86_64/r5-os-anyboot.iso`. `fdisk` shows a 2G BeFS partition and a 2.8M EFI partition, type `ef`. That file was not booted.

## Not done

- Boot the 5625U and record the idle and busy samples here.
- Write one P-state, with safemode leaving the registers alone.
- Intel HWP on hardware. That path was not executed.
