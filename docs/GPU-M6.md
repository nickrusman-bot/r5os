# M6 notes

The fault notes below were written on 10 Nov 2026, before the code that reads them. The passport and the page-memory type landed after that. The fixed GFX9 contract is unchanged. Nothing here was run on the 5625U.

## Fault interrupt (stick 32)

Stick 32 programmed the context interrupt bits. The log line is `vmid fault irq cntl 007ffe07`. That value is `r5_gfx9_vmid_cntl` with `r5_gfx9_vmid_fault_irq_bits()` ORed in, and `r5_gfx9_vmid_fault_irqs()` writes it to contexts 1–15 when `gfx9_irq` and `gfx9_vmids` are on. Context 0 is left alone. Bit 7, the retry bit, stays off, so the word is `007ffe07` and not Linux's `007ffe87`.

The fault itself was visible in the hub register. `r5_gfx9_cp_dump()` reads `VM_L2_PROTECTION_FAULT_STATUS` and prints it as `fault %08lx`. Stick 32's value `00900c10` is that line, not a `vm fault` line. The same dump prints the address registers beside it. No `radeon_hd: vm fault` line reached the syslog.

`r5_gfx9_fault_decode()` does not read a VMID out of that status word. CID is bits 9–17 and RW is bit 18. The recorded VMID 9 is the high byte of `00900c10` (bits 24 and up). The decoder never prints it.

### Path on R5

1. The VM context bits above ask the hub to raise an interrupt. `ih_on()` has already started the IH ring and set `CP_INT_CNTL_RING0` for the EOP and privilege sources. There is no separate UTCL2 bit in that CP register. EOP interrupts have been seen on earlier sticks, so the IH ring and `ih_handler()` do run.
2. `ih_handler()` parses the 32-byte IV with `r5_gfx9_ih_parse()`: client bits 7:0, source bits 15:8, ring bits 23:16. It queues a fault only when the client is `0x12` (VMC) or `0x1b` (UTCL2) and the source is 0. The address comes from dwords 4 and 5 through `r5_gfx9_fault_address()`, which is the same shift Linux uses. The VMID comes from dword 0 bits 27:24. The PASID comes from dword 3 bits 15:0. Any other IV is not queued. It is printed as `ih client` only when `r5_gpu_log_error()` allows a line.
3. The retire thread calls `gpu_fault_retire()`, which prints `vm fault` only for a queued note. An empty queue prints nothing.

### Linux v6.12 `gmc_v9_0.c`

`gmc_v9_0_process_interrupt` starts at line 545.

- Lines 564–565 build the byte address from `src_data[0]` and the low 4 bits of `src_data[1]`, shifted up by 12. R5's `r5_gfx9_fault_address()` matches that.
- Lines 567–582 treat client VMC as MMHUB and every other client, including UTCL2, as GFXHUB. The function does not test `src_id == 0` again. The IRQ table already selected this function.
- Lines 585–624 are the retry path (`src_data[1]` bit 7). R5 leaves retry off, so stick 32 is the no-retry case. That part of Linux does not apply.
- Lines 661–665, for a GFXHUB fault on a chip older than GC 9.4.2, do one dummy read of `VM_L2_PROTECTION_FAULT_STATUS`, then the real read. Renoir is GC 9.3, so the dummy read applies. R5 never reads this register in the handler.
- Lines 675–676 clear the fault by writing bit 0 of `VM_L2_PROTECTION_FAULT_CNTL` (`WREG32_P(..., 1, ~1)`). R5 never clears it. A later fault can sit in `MORE_FAULTS` and not raise a new interrupt.
- Lines 680–682 print `VM_L2_PROTECTION_FAULT_STATUS`. That is the line stick 32 did not have.

### What is missing

The hub recorded the fault, and the context interrupt bits were on. The `vm fault` line is printed only from the IH queue, and that queue accepts only two client ids with source 0. Nothing in that path reads or clears `VM_L2_PROTECTION_FAULT_STATUS`. A fault that is visible to `r5_gfx9_cp_dump()` therefore never becomes the line `r5-gpu-isolation` searches for.

The code change, not done in this note: on a GFXHUB fault IV, do the dummy read, read the status, print `vm fault` with the address and the VMID, then clear bit 0 of the fault control. Keep the retry bit off. Do not change the ring, the page tables, or the invalidate word. The next stick shows whether an IV arrives at all. If the status is non-zero and no IV arrives, the interrupt is not reaching the IH, which is a separate gap from the missing read and the missing clear.

## `print_fault()`

`r5-gpu-isolation` `print_fault()` scans syslog for `vm fault` and the address and prints any matching line. It prints `vm fault addr` only when a syslog line containing both `vm fault` and the address was found. When none was found it prints `vm fault not in syslog` and the address, so a missing driver fault stays visible. The interrupt status read and the GPU reset did not land.

## Reset

A guilty job hangs the GFX ring. `gpu_watchdog()` calls `gpu_lost()`, which sets the lost flag, drops the job list, and wakes waiters. It does not halt the command processor and it does not reset the GPU. The ring stays where it stopped until the next boot. That is the current hang policy.

Linux, after a job timeout, calls `amdgpu_device_gpu_recover`. On this APU the soft reset is mode 2 through the PSP (`AMD_RESET_METHOD_MODE2`): the CPU stays up, and the GFX block (CP, MEC, RLC, the rings) comes back uninitialized. A mode-1 reset is the full chip and is the wrong tool here.

A reset on R5 would have to do all of the following, in this order. This note does not add that code. It belongs in the hang policy, not in M6, unless that choice changes.

1. Stop new submits. The lost flag already does this.
2. Halt the ME, PFP, and CE with the mask already used for that (bits 24, 26, and 28), and halt the MEC.
3. Send the PSP mode-2 reset. The PSP is already loaded for the GFX start. This is not a PCI reset.
4. Start the GPU again only in the fixed order: MEC before the GFX ring, the same ring words, scratch readback `0xDEADBEEF`, then the K2 sequence when those settings are on. The stop order, the page-table base, the PTE flags, the invalidate word, and the ring-index formula stay as they are.
5. Clear the lost flag only after that fence lands. Until then the render node stays refused.

## Shutdown

The comparison with Linux `amdgpu_device_fini_hw` is round 3, point 17. It is not in this note.

## Passport

`r5_passport.cpp` keeps one GC 9.3 row for PCI `15e7`, `1636`, `1638`, and `164c`. Discovery is 10 KiB at the end of the mapped carveout minus 64 KiB, which is Linux v6.12 `DISCOVERY_TMR_SIZE` and `DISCOVERY_TMR_OFFSET`. The signature is `0x28211407`. The byte sum after `binary_checksum` has to match, and the GC table (index 1, id `0x4347`, version 1) has its own sum. A field that disagrees with the row is logged as `passport mismatch` and the discovery value is the one reported. Shader engines, arrays, CUs per array (`2 * (wgp0 + wgp1)`), VGPRs, GL2C, the GS depths, the off-chip flag, and the wave size come from that table. The enabled render-backend count still comes from the disable registers. The mask width follows discovery when it is 1–8.

If the carveout is not mapped, or the signature or a sum fails, the log is `passport discovery missing` and the row stays. An id with no row logs `passport no row` and does not invent the row.

Clocks are the ATOM v2.1 `firmwareinfo` boot values times 10, in kHz, and min equals max. `gpu_counter_freq` is `smu_info` `core_refclk_10khz` times 10, for format 3 or 4. The memory type and width come from `integratedsysteminfo` v1.11, v1.12, or v2.1 through v2.3. LpDdr4 (atom type 30) is type 11. LpDdr5 (35) uses 32 bits per channel; the others use 64. A channel count of 0 is 1. A missing table leaves the field 0. There is no constant 100000 and no Mesa 1800000/1333000 record. Firmware versions still come from the ucode headers.

This was not booted on the 5625U, so no `passport` line has been seen.
