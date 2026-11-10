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

This is the comparison for point 17. No shutdown code is added here. The stop order in `r5_gfx9_kernel_stop` stays as it is. A change needs a boot on the 5625U. This is not the mode-2 reset in the section above: that reset brings the GFX block back. Shutdown leaves it off.

Haiku `system_shutdown()` (`src/system/kernel/shutdown.cpp`) sets `gKernelShutdown`, kills the other teams on a reboot, calls `sync()`, then `arch_cpu_shutdown()`. On AMD64 that is `acpi_shutdown()` (`arch_cpu.cpp`). Neither function calls a driver's uninit. `r5_gfx9_kernel_stop()` is only called from `radeon_hd_uninit()`, and Haiku keeps the driver loaded until power-off, so a normal shutdown or reboot runs none of the stop. There is no driver hook before `acpi_shutdown()`. A stop that has to run would be a new call from `system_shutdown()` after `sync()` and before `arch_cpu_shutdown()`. That call is not in the tree. The order it would use is the Linux order: interrupts off first, then the blocks in reverse, GFX, then PSP, then IH and GMC. Renoir's SMU 12 `hw_fini` is the SMU step in that reverse walk. Its source line was not checked against Linux v6.12 in this pass.

Linux v6.18 `amdgpu_device_fini_hw` (`amdgpu_device.c` line 4945) disables every interrupt source (`amdgpu_irq_disable_all`, `amdgpu_irq.c` line 124), finishes the fences, then `amdgpu_device_ip_fini_early` (line 3617) walks the IP blocks from last to first and calls each `hw_fini` (`amdgpu_ip_block_hw_fini`, line 3572). After that walk, `amdgpu_irq_fini_hw` (line 330) frees the IRQ and the IH rings.

For GC 9.3, and not a virtual function, not inside a reset, and not GC 9.4.2 or newer, `gfx_v9_0_hw_fini` (line 4048) does this:

1. Drop the privilege and bad-opcode interrupt refs (lines 4052–4056).
2. Disable the compute queues (line 4061).
3. Finish the KIQ registers (lines 4077–4085).
4. `gfx_v9_0_cp_enable(adev, false)` (line 4087). That halts the GFX command processor (`gfx_v9_0_cp_gfx_enable`, line 3241): ME, PFP, and CE halt, plus the pipe-reset and icache-invalidate bits, written to `CP_ME_CNTL` (lines 3244–3257). It also halts compute (`gfx_v9_0_cp_compute_enable`, line 3457): `CP_MEC_CNTL` gets the icache invalidate, the pipe resets, and both `MEC_ME1_HALT` and `MEC_ME2_HALT` (lines 3462–3471).
5. Stop the RLC (`gfx_v9_0_rlc_stop`, line 3119, installed at line 5221). That clears `RLC_ENABLE_F32`, turns the GUI idle interrupt off, and waits for the serdes (lines 3121–3123). GC 9.3 is below 9.4.2, so this halt is not skipped (lines 4090–4096).

GART and the VM contexts are the GMC block, not the GFX block. `gmc_v9_0_hw_fini` (line 2248) calls `gmc_v9_0_gart_disable` (line 2241). On the GFX hub, `gfxhub_v1_0_gart_disable` (line 341) writes 0 to all 16 `VM_CONTEXT*_CNTL` registers (lines 347–350), then turns the L1 TLB off and the L2 cache off (lines 357–367).

The PSP block is `psp_hw_fini` (`amdgpu_psp.c` line 3217). It calls `psp_tmr_terminate` (line 3234), which unloads the TMR (`psp_tmr_unload`, line 945, command `GFX_CMD_ID_DESTROY_TMR` at line 942), and then `psp_ring_destroy` (line 3236).

`r5_gfx9_kernel_stop` (`gfx9_k12.cpp`) already halts ME, PFP, and CE with bits 24, 26, and 28, and sets MEC bits 30 and 28. It writes the saved `VM_CONTEXT0_CNTL` when K2 stored one, issues the engine-17 invalidate and waits for the ack, clears `IH_RB_CNTL` and `CP_INT_CNTL_RING0`, removes the interrupt handler, turns MSI off, and then deletes the areas, including the carveout mapping. It does not stop the RLC, it does not write 0 to all 16 VM contexts, it does not turn the L1 TLB or the L2 cache off, and it does not send the PSP a destroy-TMR command or destroy the PSP ring. Unmapping the carveout is not that command. Those steps stay out of the code until a 5625U boot records the new stop.

## Passport

The GC 9.3 row is used when the radeon_hd device table already called the chip `RADEON_RENOIR`. That table has `15e7`, `1636`, `1638`, and `164c` (Lucienne). There is no second list of PCI ids. Discovery is 10 KiB at the end of the mapped carveout minus 64 KiB, which is Linux v6.12 `DISCOVERY_TMR_SIZE` and `DISCOVERY_TMR_OFFSET`. The signature is `0x28211407`. The byte sum after `binary_checksum` has to match, and the GC table (index 1, id `0x4347`, version 1) has its own sum. A field that disagrees with the row is logged as `passport mismatch` and the discovery value is the one reported. Shader engines, arrays, CUs per array (`2 * (wgp0 + wgp1)`), VGPRs, GL2C, the GS depths, the off-chip flag, and the wave size come from that table. The enabled render-backend count still comes from the disable registers. The mask width follows discovery when it is 1–8.

Linux tag v6.12 `amdgpu_discovery.c` `amdgpu_discovery_set_ip_blocks()` starts at line 2425 and has no `CHIP_RENOIR` case. The Raven case at line 2474 sets IP versions by hand. On the 5625U the expected line is `passport discovery missing`, and the row stays. That line is not a failure. An id the device table did not call Renoir logs `passport no row` and does not invent the row. Those lines are printed once per boot.

Clocks are the ATOM v2.1 `firmwareinfo` boot values times 10, in kHz, and min equals max. `gpu_counter_freq` for Renoir follows Linux tag v6.12 `soc15.c` `soc15_get_xclk()` lines 326-330: MP1 12.0.0 and 12.0.1 return 10000, and `amdgpu_kms.c` line 863 stores `amdgpu_asic_get_xclk()` times 10, which is 100000. The ATOM `smu_info` `core_refclk_10khz` times 10 is printed beside it (`passport counter_freq … atom …`) and is not the reported value. `generated/gpu-m4/HARDCODE.md` row 5 names the same lines. The memory type and width come from `integratedsysteminfo` v1.11, v1.12, or v2.1 through v2.3, using `convert_atom_mem_type_to_vram_type` in `amdgpu_atomfirmware.c`. LpDdr4 (atom type 30) is type 11. LpDdr5 (35) uses 32 bits per channel; the others use 64. A channel count of 0 is 1. A missing table leaves the field 0. Eight graphics contexts and a 32 KiB constant engine are `gfx_v9_0_gpu_early_init` and `gfx_v9_0_sw_init`. Firmware versions still come from the ucode headers.

This was not booted on the 5625U, so no `passport` line has been seen.
