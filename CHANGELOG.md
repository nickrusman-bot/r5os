# R5OS changelog (lokaal)

### M4 review: the interface is still 1.0 and has not shipped

API 1.0 has not been booted, so `r5_gpu.h` can still change. This pass is the review of the render node. It has not been booted.

Every ioctl copies with `user_memcpy` after `is_user_address_range`. The kernel does not dereference a user pointer. The fourth ioctl argument is checked against the struct `size` field. `GET_INFO` still accepts a short buffer. The AMD block that follows the common info is `drm_amdgpu_info_device` from libdrm 2.4.124, then 32 firmware slots and 10 `HW_IP` slots. `family` is `AMDGPU_FAMILY_RV` (142). `external_rev` comes from NBIO `RCC_DEV0_EPF0_STRAP0` (`nbio` segment 2, index `0x000f`, mask `0x0F000000`), the same register as `nbio_v7_0_get_rev_id`. Barcelo and Cezanne (`0x15e7`, `0x1638`) add `0xa1`. Renoir (`0x1636`) adds `0x91`. The PCI revision is stored separately and is not that input. Firmware versions are 0: this driver does not read a PSP version yet. GFX and compute `HW_IP` are 9.3, with IB start alignment 32 and size alignment 4.

`CS_SUBMIT` takes user lists of IBs, wait syncobjs and signal syncobjs. The whole IB must already be mapped. `IB_BASE_HI` is `upper_32_bits & 0xffff`. The `RELEASE_MEM` address is the kernel MC address of the fence page plus 2048. That page is not mapped in VMID 1. The end-of-pipe interrupt signals the job's syncobjs and wakes one condition variable. `FENCE_WAIT`, the ring-full wait and syncobj waits loop on that variable and compare the sequence or the point. A timeout of 0 does not mark the device lost. A fence timeout still does, and does not reset the GPU.

`VA_MAP` and `VA_UNMAP` take a BO offset and a size. Each BO records its ranges and rejects an overlap. A GTT BO is a kernel area (`B_FULL_LOCK`, kernel read/write, cloneable) and `BO_MAP` clones it into the caller, the same way as VRAM. User VRAM comes from a free list. Kernel page tables stay on the bump allocator. `BO_FREE` and the last close wait for that client's last sequence, clear the PTEs, flush the TLB, and only then free the pages. The lock is created in `init_driver`. Syncobjs belong to the client and are dropped on close. VMID 1 is still built once and reused.

The host test printed `ok gpu api 1.0 walk`, including the IB high mask `0x8000`, the strap revision, the free list and the overlap check. `jam -q radeon_hd` exited 0. The libdrm Haiku edits are `distro/graphics/libdrm-r5/patches/haiku-xf86drm.patch`. The Mesa `unistd.h` include is a unified diff in `distro/mesa/patches/haiku-os-drm-ioctl.patch`. AMDGPU ioctl translation is not done. This has not been booted. VirtualBox was not started.

### M4 programs VMID 1 and does not ship RADV yet

`origin/main` contains M3 (`71d5144efd`, pull request #4098) and `claude/m4-plan` (`d6b1bc09c6`, pull request #4099).

The user address space for GC 9.3 is the Linux v6.12 layout: low range `0x10000`–`0x800000000000`, high range `0xffff800000000000`–`0xffffffffffbfe000`. The walk is four levels with 9-bit indexes. That section in `docs/GRAPHICS.md` is not fixed. No boot has programmed VMID 1.

The render node name is `gpu/radeon_hd/0`. `GET_INFO` reads the shader-array registers, masks the inactive CUs to 8, and logs the bitmap. It does not hardcode 7. Without `gpu_render true` every other call returns `B_NOT_ALLOWED`. With that key, creating a context builds the VMID 1 tables: root in `VM_CONTEXT1_PAGE_TABLE_BASE_ADDR`, depth 3, block size 0, end page `2^36-1`, and an empty identity aperture. `SYSTEM_ACCESS_MODE` is not written. The TLB flush uses engine 0. `CS_SUBMIT` appends an indirect buffer with VMID 1 in bits 24–27 and a 64-bit `RELEASE_MEM` with `INT_SEL` 2. `FENCE_WAIT` sleeps on the end-of-pipe semaphore. A timeout marks the device lost and does not reset the GPU. `BO_EXPORT` and `BO_IMPORT` return `B_UNSUPPORTED`.

The teapot control points are freeglut 3.8.0 `src/fg_teapot_data.h`. The file's permission text is the X11/MIT grant. Copyright 1999-2000 Pawel W. Olszta. The header is `distro/apps/vulkanteapot/fg_teapot_data.h`. Haiku `glut_teapot.c` is not used. VulkanTeapot itself is not built.

libdrm 2.4.124, sha256 `ac36293f61ca4aafaf4b16a2a7afff312aa4f5c37c9fbd797de9e3c0863ca379`, is installed in `/home/nickr/r5os/r5-sysroot`. The Haiku edit defines `makedev` and opens `/dev/gpu/radeon_hd/<n>`. AMDGPU ioctls are not translated. Mesa 25.3.6 configured with Vulkan `amd,swrast` and Gallium `llvmpipe,zink`, and found libdrm 2.4.124. The compile stops in two Haiku gaps. `radv_physical_device.c:2143` calls `major` and `minor`, which Haiku does not declare. `libsync.h:57` uses `_IOWR`, which the Haiku headers do not define, while compiling `radv_amdgpu_cs.c`. RADV is not in this image. VulkanTeapot is not built.

The host test printed `ok gpu api 1.0 walk`. The kernel driver, `r5gpuinfo` and `r5-gpu-selftest` linked. `jam -q @r5-anyboot` exited 0. The anyboot image is 2156724224 bytes, sha256 `06ed2b06583fe314a71f0eef7d3dc453c8ee22a079b512b3a6231fb69d69c216`. The stick copy, with `gfx9_ring`, `gfx9_kernel`, `gfx9_irq`, `gfx9_gart`, `gpu_render` and `gpu_selftest` set, is sha256 `19b5d9d9b9194ad186b5bb4b4ee949b95a04a3c60a01c9702124aaaea29c6d3c`. It was written at 17:24 Europe/Amsterdam on 5 Nov 2026. Read-back matched at 0, 6291456 and 2153775104. Disk 0 was not opened. This image has not been booted. VirtualBox was not started. VulkanTeapot is not in the image.

### M3 is the fixed kernel sequence

The user shut down the 15:29 session from the desktop and reported no panic. The syslog exported while that desktop was up still ends before `radeon_hd_uninit`, so the stop registers are not in that file. Milestone M3 in `docs/GPU-ARCHITECTURE.md` is done. The sequence is `GFX9 kernel on GC 9.3 (fixed)` in `docs/GRAPHICS.md`, and `AGENTS.md` locks it the same way as the ring, the workload and compute. `gfx9_kernel`, `gfx9_irq` and `gfx9_gart` stay off until the settings file says so. GC 9.3 is not the default. The next milestone is M4.

### The GART DMA and the GART indirect buffer landed

The stick written at 15:29 Europe/Amsterdam on 5 Nov 2026, sha256 `7f137a27d566fa2493648b1852e92620baf1c404cad43f8f71da98a1e4e85d47`, booted on the 5625U. The desktop came up. The syslog has no panic. Tracker logged `R5 tracker: Tracker from distro` and Deskbar logged `R5 deskbar: BarApp from distro`. The accelerant logged `GFX9 owned by the kernel`. The log ends on the running desktop. There is no `radeon_hd_uninit`, no `GFX9 kernel start already done this boot`, and no shutdown. MSI vector 61 is not freed. The stop order and a second open in the same boot were not exercised.

K0 and K1 landed again. The syslog lines are `scratch deadbeef`, `rptr 00000392`, `dwords 914`, `fence landed`, `ib_seq 1 seen 1`, `ib_rptr 0000039e wptr 0000039e`, `compute_c0 ok seq 2 seen 2`, `compute_c1 ok bad 0`, `compute_c2 ok bad 0 ticks 16656`, `compute_c3 ok bad 0 ticks 63750`, `compute_ratio 3.8`, `dma_1m_bad 0 ticks_1m 8464`, `dma_8m_bad 0 ticks_8m 54854`, `dma_ratio 6.4`, `k0 ok fence`, `k1 ok msi irq 61 eop 1 client 20 src 181 ring 0`, and `k1_wait_us 1132`.

K2 programmed the table, the DMA matched `kK2Pattern`, and the indirect buffer in the GART area completed. The syslog lines are:

```
k2 agp bot 00000000 top 00000000 0000000000000000-0000000001000000
k2 map count 8 pages 256
k2 pte last 060000001b6cc077 got 060000001b6cc077 tail 0600000812c98077
k2 ptb 0000000812c18001 got 12c18001 00000008
k2 inv 007c0001
k2 ok gart 000000f3f0000000-000000f400000000 pte0 060000001b78d077
k2 dma a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5
k2 dma pattern 262144 cafe 0 other 0
k2_ib wb deadbeef seq 10 seen 10
```

`k2 ok` is logged only when the DMA submission returned and every one of the 262144 dwords is `0xA5A5A5A5`, and the GART indirect buffer submission returned. `wb deadbeef` is the word that buffer writes. Sequence 10 is that buffer's fence, and `seen 10` is the same value read back. There is no `k2 fault` line and no `GFX9 workload failed` line. The command processor was not halted. The contract update is the section above.

### The GFX ring wraps and the kernel stops the GPU before it unmaps

K2 still uses one constant, `kK2Pattern` (`0xA5A5A5A5`), for the DMA packet and the readback. The log is the first four dwords, then `k2 dma pattern`, `cafe` and `other`. The first dword that is neither the pattern nor `0xCAFEDEAD` is logged with its index. The GART indirect buffer runs only when the submission landed and every dword matches.

Later submissions no longer refuse when the write pointer plus the packet would pass 1024. `r5_gfx9_ring_space` is `ringDwords - (wptr - rptr) - 1`. The read pointer comes from the 64-bit writeback at the fence plus 256 when that word is non-zero, otherwise from `CP_RB0_RPTR`. Each dword is stored at `(wptr + i) & (dwords - 1)`. The doorbell and the poll slot receive the write pointer that keeps counting. The first fill of 914 dwords is the same packet. The host test checks the twelve indexes from 1022: 1022, 1023, then 0 through 9. It printed `ok dwords 73 submit 12` and `ok pte gart ih`.

`radeon_hd_uninit` calls `r5_gfx9_kernel_stop` before any `delete_area`. Stop halts the command processor (ME, PFP and CE) and the MEC, writes the saved `VM_CONTEXT0_CNTL` when K2 stored one, issues the same TLB invalidate as K2, clears `IH_RB_CNTL` and `CP_INT_CNTL_RING0`, removes the interrupt handler, turns MSI off, and then deletes the system area and the pool mapping. A second open in the same boot logs `GFX9 kernel start already done this boot` and does not start the GPU again. K0 and K1 packets are unchanged.

`jam -q @r5-anyboot` exited 0 (1189 targets). The kernel driver contains `k2 dma pattern`, `k2 dma other` and `GFX9 kernel start already done this boot`. The image was written to the General UDisk at 15:29 Europe/Amsterdam on 5 Nov 2026, sha256 `7f137a27d566fa2493648b1852e92620baf1c404cad43f8f71da98a1e4e85d47`. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The settings are `gfx9_ring`, `gfx9_workload`, `gfx9_compute`, `gfx9_kernel`, `gfx9_irq` and `gfx9_gart`. The boot is the section above. The syslog from that boot has no shutdown.

### K2 checks the same pattern the DMA packet writes

The K2 readback compared with `0x5A5A5A5A` while `radeon_gfx9_dma_ib` writes `0xA5A5A5A5`. Both the K2 packet and the K2 check now use `kK2Pattern`, which is `0xA5A5A5A5`. The K0 calls keep the previous packet bytes. After the DMA, the log is the first four dwords, then the count equal to the pattern, the count still `0xCAFEDEAD`, and the count of anything else. A value that is neither logs its first index and the dword. The GART indirect buffer runs when the submission landed and every dword matches the pattern. K0 and K1 are unchanged.

`jam -q @r5-anyboot` exited 0. The kernel driver contains `k2 dma pattern`. The image was written to the General UDisk at 15:14 Europe/Amsterdam on 5 Nov 2026, sha256 `fc3ae076f41407362fbbd555de7867521b9c4c5dc056fe1b877b9dc143ef4575`. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The settings are `gfx9_ring`, `gfx9_workload`, `gfx9_compute`, `gfx9_kernel`, `gfx9_irq` and `gfx9_gart`. This image has not been booted.

### The GART DMA fence landed and the dword check missed

The stick written at 14:56 Europe/Amsterdam on 5 Nov 2026, sha256 `313e46d73a6dccdd85734a9ca01f2a662c8d7d6d7dcb2f88dc8664cd79ada770`, booted on the 5625U. The desktop came up. There was no panic. Tracker logged `R5 tracker: Tracker from distro` and Deskbar logged `R5 deskbar: BarApp from distro`. The accelerant logged `GFX9 owned by the kernel`.

K0 and K1 landed again. The syslog lines are `scratch deadbeef`, `rptr 00000392`, `dwords 914`, `ib_seq 1 seen 1`, `ib_rptr 0000039e wptr 0000039e`, `compute_c0 ok seq 2 seen 2`, `compute_c1 ok bad 0`, `compute_c2 ok bad 0 ticks 16682`, `compute_c3 ok bad 0 ticks 58876`, `compute_ratio 3.5`, `dma_1m_bad 0 ticks_1m 8468`, `dma_8m_bad 0 ticks_8m 48908`, `dma_ratio 5.7`, `k0 ok fence`, `k1 ok msi irq 61 eop 1 client 20 src 181 ring 0`, and `k1_wait_us 1130`.

K2 programmed the table and the DMA submission completed. The syslog lines are:

```
k2 agp bot 00000000 top 00000000 0000000000000000-0000000001000000
k2 map count 8 pages 256
k2 pte last 060000001b74f077 got 060000001b74f077 tail 0600000812c98077
k2 ptb 0000000812c18001 got 12c18001 00000008
k2 inv 007c0001
k2 fail gart 000000f3f0000000-000000f400000000 pte0 060000001b810077
k2_dma_bad 262144
k2_ib wb deadbeef seq 0 seen 9
k2 fault 00000000 MORE_FAULTS 0 WALKER_ERROR 0 PERMISSION_FAULTS 0 MAPPING_ERROR 0 CID 0 RW 0 addr 0000000000000000 cntl 007ffe01 cp 00000000 grbm 00003028
```

The page-table base read back as written: physical `0x812c18000` with the valid bit. The last PTE read back as written, including `MTYPE_UC` and the executable bit. The invalidate request was `0x007c0001`. There is no `GFX9 workload failed` line for this DMA. `seen 9` is the fence sequence, so the release completed. The fault status is zero. `k2_dma_bad 262144` is every dword of the 1 MiB buffer. The DMA packet writes `0xA5A5A5A5`, which is the value the K0 check accepts. The K2 check compares with `0x5A5A5A5A`. The log does not record the dword the CPU read, so a buffer that stayed `0xCAFEDEAD` is still possible. The GART indirect buffer was not submitted. The failure path then halted the command processor. `docs/GRAPHICS.md`, `AGENTS.md` and the M3 row stay as they are.

### The page table base is the framebuffer offset

The 14:19 boot faulted a write on the first GART page with `WALKER_ERROR` and `MAPPING_ERROR`. `VM_L2_PROTECTION_FAULT_ADDR` holds a page number, so the byte address is that register shifted left by 12. For `0x0f3f0000` that is the GART start `0xf3f0000000`. The walker did not read the table. K2 now writes `VM_CONTEXT0_PAGE_TABLE_BASE_ADDR` as `(MC_VM_FB_OFFSET << 24) + tableOffset`, then `AMDGPU_PTE_VALID`, the same value as `amdgpu_gmc_pd_addr` plus `gmc_v9_0_get_vm_pde`. The physical address is rejected when it fails the Linux `BUG_ON` mask `0xFFFF00000000003F`. The written value and the read-back are logged as `k2 ptb`.

`gmc_v9_0_gart_init` sets `gart_pte_flags` to `AMDGPU_PTE_MTYPE_VG10(0, MTYPE_UC) | AMDGPU_PTE_EXECUTABLE`. `MTYPE_UC` is 3 in `vega10_enum.h` and the field sits at bit 57 in `amdgpu_vm.h`. A system page is now `0x0600000123450077` for physical `0x123450000`. After the table is filled, K2 issues the same `sfence` as `radeon_visible_barrier` and reads the last mapped PTE and the tail PTE back before `VM_CONTEXT0_CNTL` and before the TLB flush.

`VM_INVALIDATE_ENG0_REQ` in `gc_9_0_sh_mask.h` has `FLUSH_TYPE` as two bits at 16, then `INVALIDATE_L2_PTES`, `INVALIDATE_L2_PDE0`, `INVALIDATE_L2_PDE1`, `INVALIDATE_L2_PDE2` and `INVALIDATE_L1_PTES`. `gmc_v9_0_get_invalidate_req` for VMID 0 and flush type 0 is `0x007c0001`. K2 logs that value as `k2 inv`. A fault log now names `MORE_FAULTS`, `WALKER_ERROR`, `PERMISSION_FAULTS`, `MAPPING_ERROR`, `CID` and `RW`, and prints the byte address. The host test covers the PTE, the alignment, the invalidate word and the previous fault status `0x00040d5b`. K0 and K1 are unchanged.

The host test printed `ok dwords 73 submit 12` and `ok pte gart ih`. Jam `@r5-anyboot` exited 0. The driver inside `haiku-r1~beta6_r5_2026.1_dev-1-x86_64.hpkg` contains `k2 fail ptb readback`. The image was written to the General UDisk at 14:56 Europe/Amsterdam on 5 Nov 2026, sha256 `313e46d73a6dccdd85734a9ca01f2a662c8d7d6d7dcb2f88dc8664cd79ada770`. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The settings are `gfx9_ring`, `gfx9_workload`, `gfx9_compute`, `gfx9_kernel`, `gfx9_irq` and `gfx9_gart`. The boot is the section above.

### The GART DMA faulted at the start of the window

The stick written at 14:19 Europe/Amsterdam on 5 Nov 2026, sha256 `d022bbed0832034b0c39698b4bed89ec5e0648b12d74c90f278e2a4f34896ca5`, booted on the 5625U. The desktop came up. There was no panic. Tracker logged `R5 tracker: Tracker from distro` and Deskbar logged `R5 deskbar: BarApp from distro`. The accelerant logged `GFX9 owned by the kernel`.

K0 and K1 match the previous boot. The syslog lines are `scratch deadbeef`, `rptr 00000392`, `dwords 914`, `fence landed`, `ib_seq 1 seen 1`, `ib_rptr 0000039e wptr 0000039e`, `compute_c0 ok seq 2 seen 2`, `compute_c1 ok bad 0`, `compute_c2 ok bad 0 ticks 16680`, `compute_c3 ok bad 0 ticks 60033`, `compute_ratio 3.5`, `dma_ratio 7.9`, `k0 ok fence`, `k1 ok msi irq 61 eop 1 client 20 src 181 ring 0`, and `k1_wait_us 1129`.

K2 programmed the page table and then the DMA fence did not land. The syslog lines are:

```
k2 agp bot 00000000 top 00000000 0000000000000000-0000000001000000
k2 map count 8 pages 256
GFX9 workload failed: fence CP_STAT 0x80018200 GRBM_STATUS 0xa0003028 rptr 0x3fe fence 0x0 wb 0x0
GFX9 stall grbm 0xa0003028 grbm2 0x50000008 cp 0x80018200 cpc 0x0 int 0x0 vm 0x40d5b vmlo 0xf3f0000 vmhi 0x0
k2 fail gart 000000f3f0000000-000000f400000000 pte0 000000001b755067
k2_dma_bad 4294967295
k2_ib wb deadbeef seq 0 seen 0
k2 fault 00040d5b addr 0f3f0000 00000000 cntl 007ffe01 cp 80018200 grbm a0003028
```

`0xf3f0000` is the GART start `0xf3f0000000` shifted by 16. The first page-table entry is physical `0x1b755000` with valid, system, snooped, readable and writable. The command processor had consumed the packet (`rptr 0x3fe`) and the fence writeback stayed 0. The failure path halted the command processor. The indirect buffer in the GART area was not submitted. `k2_dma_bad 4294967295` is that sentinel, not a dword count. `docs/GRAPHICS.md`, `AGENTS.md` and the M3 row stay as they are.

### The kernel asks for the GART translation count

K2 on the 5625U stopped at `k2 fail memory map` because `get_memory_map` returns `B_OK` (0) on success. The next image calls `get_memory_map_etc(B_CURRENT_TEAM, …, &count)` and walks exactly `count` entries. It logs `k2 map count` and `pages`. `MC_VM_AGP_BOT` and `MC_VM_AGP_TOP` are 16 MiB units and `TOP` is inclusive, so bot 0 and top 0 are `0`-`0x1000000`. The log prints that window, or `off` when bot is above top. The registers are not written. `r5_gfx9_gart_range` refuses a GART block that overlaps that window. The host test printed `ok dwords 73 submit 12` and `ok pte gart ih`, including the 16 MiB window and an overlap refusal. K0 and K1 are the same packets.

`jam -q @r5-anyboot` for AMD64 finished at 14:13 Europe/Amsterdam on 5 Nov 2026 with exit 0 (1189 targets updated). The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `deecfb928e6f74d806cf2d45dd58f6a866e1caf6915558d11e5dea9282796984`. The stick copy is the same size, sha256 `d022bbed0832034b0c39698b4bed89ec5e0648b12d74c90f278e2a4f34896ca5`. The kernel driver contains `k2 map count`, `k2 agp bot`, `k0` and `k1_wait_us`. It was written raw to the General UDisk at 14:19 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The boot is the section above.

### The kernel GFX9 start reached the desktop

The stick written at 13:46 Europe/Amsterdam on 5 Nov 2026, sha256 `1e98f708cb8036b84410e041a764c6cb70a46193b1474fe1a20e7e31a963712b`, booted on the 5625U. There was no panic. Tracker logged `R5 tracker: Tracker from distro` and Deskbar logged `R5 deskbar: BarApp from distro`. The accelerant logged `GFX9 owned by the kernel` and did not start the command processor again.

The pool mapping is `gfx9 pool 0xfce07e9000+0xf817000`. K0 matches the earlier fence, workload and compute. The syslog lines are `scratch deadbeef`, `rptr 00000392`, `dwords 914`, `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed`, `ib_seq 1 seen 1`, `ib_rptr 0000039e wptr 0000039e`, `compute_c0 ok seq 2 seen 2`, `compute_c1 ok bad 0`, `compute_c2 ok bad 0 ticks 16652`, `compute_c3 ok bad 0 ticks 59194`, `compute_ratio 3.5`, `dma_1m_bad 0 ticks_1m 8468`, `dma_8m_bad 0 ticks_8m 67216`, `dma_ratio 7.9`, and `k0 ok fence`.

K1 arrived on MSI. The syslog lines are `allocate_io_interrupt_vectors: allocated 1 vectors starting from 61`, `msi enabled: 0x0085`, `k1 ok msi irq 61 eop 1 client 20 src 181 ring 0`, and `k1_wait_us 1130`. Client 20 is `0x14` and source 181 is the CP end-of-pipe on ring 0. The wait was 1130 µs.

K2 stopped before a page table or a DMA. The syslog lines are `k2 agp bot 00000000 top 00000000 on` and `k2 fail memory map`. `get_memory_map` returns `B_OK`, which is 0, and ends the list with a zero entry. The check treats 0 as failure, so the translation was dropped. AGP bot and top are both 0. `MC_VM_AGP_TOP` is inclusive and the unit is 16 MiB, so that register pair is the window `0`-`0x1000000`, not a zero-length aperture. The boot logged `on` because the check was only `bot > top`, and the range check did not look at AGP. The command processor was not halted. `docs/GRAPHICS.md`, `AGENTS.md` and the M3 row in `docs/GPU-ARCHITECTURE.md` stay as they are until K2 lands.

### The kernel writes the GFX9 ring through a kernel mapping

The stick from 13:11 Europe/Amsterdam on 5 Nov 2026 reached the desktop loader and then stopped in the kernel debugger. The panic is `SMAP violation: user-mapped address`, in `r5_gfx9_prepare`, called from `r5_gfx9_kernel_start` and `radeon_hd_init` while `app_server` opened the device. The visible VRAM window is mapped with `B_READ_AREA | B_WRITE_AREA`, so the page has the user bit set. The accelerant writes that pointer from user mode. The kernel store of the ring fill hits SMAP. No K0, K1 or K2 line was reached. The syslog was not read; the machine was still in the debugger and the stick was not on this host.

`radeon_hd_init` does not map a second window. After `gfx9_ring` and `gfx9_kernel` are both on, and the PCI id is `15e7`, `1636` or `1638`, `r5_gfx9_kernel_start` maps only the pool, from `vram_window_phys + vram_pool_offset` for `vram_pool_bytes`, with `B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA` and write combining. The kernel allocator returns `kernel_window + (aligned - vram_pool_offset)`. `r5_gfx9_kernel_stop` deletes that area. With `gfx9_kernel` off, the driver maps nothing extra. A refused or failed map leaves `gfx9_owner` unset and does not start the command processor. The cloneable window is unchanged.

`jam -q @r5-anyboot` for AMD64 finished at 13:42 Europe/Amsterdam on 5 Nov 2026 with exit 0 (7041 targets updated). The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `34ef7d6e47c6baa6349b5f7e46cfc3af7168b0907af535d4fb3b199b9dbb5929`. The stick copy is the same size, sha256 `1e98f708cb8036b84410e041a764c6cb70a46193b1474fe1a20e7e31a963712b`. It was written raw to the General UDisk at 13:46 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The kernel driver contains `gfx9 pool`, `k0`, `k1_wait_us`, `k2_dma_bad` and `k2_ib`. The boot of this stick is the section above.

### The kernel starts the GFX9 command processor

This is milestone M3 for R5. The stick below was booted and panicked in `r5_gfx9_prepare` before the command processor started. See the section above.

The fixed sequences now live in one file, `gfx9_start.cpp`, and both the accelerant and the kernel driver link it. The packet bytes are the ones that landed the fence, the indirect buffer, the DMA fill and the compute dispatches. The user chose GPL-3.0-or-later for that file on 5 Nov 2026. `docs/LICENSING.md` says the kernel add-on contains that code, the combined add-on follows GPL-3.0-or-later, and the Haiku parts stay MIT. The `r5_pstate` kernel line is unchanged.

The doorbell clone, the visible-pool allocator, the firmware read and the MMIO size check stay in each host. `radeon_gpu_ring_boot` stays in the accelerant: it reads the settings and calls the shared start. No function that mixes Haiku and R5 code was moved. The kernel does not write a file. It stores the same lines in `radeon_shared_info`, and the accelerant writes `radeon-gfx.txt` from that text. When `gfx9_owner` is the kernel, the accelerant logs `GFX9 owned by the kernel` and does not start the GPU again.

Both hosts read one settings file, `/boot/home/config/settings/kernel/drivers/radeon_hd`. `gfx9_kernel`, `gfx9_irq` and `gfx9_gart` default to false. This stick sets `gfx9_ring`, `gfx9_workload`, `gfx9_compute`, `gfx9_kernel`, `gfx9_irq` and `gfx9_gart` to true.

The host test printed `ok dwords 73 submit 12` and `ok pte gart ih`. The page-table check expects `0x12345000` with valid, system, snooped, readable and writable, and with the Vega MTYPE clear. Linux v6.12 `amdgpu_vm.h` puts that MTYPE at bits 58:57. `gmc_v9_0.c` uses `MTYPE_NC` (0) for GC 9.3 system memory, so those bits stay clear. The range under framebuffer `0xf400000000` is `0xf3f0000000`-`0xf400000000`. It does not overlap the system aperture `0xf400000000`-`0xf420000000` (`0x3d0000`-`0x3d0800` in 256 KiB units). The interrupt entry matches `amdgpu_ih_decode_iv_helper`: client in bits 7:0, source in 15:8, ring in 23:16. CP end-of-pipe is client `0x14` (`SOC15_IH_CLIENTID_GRBM_CP`) and source 181 (`GFX_9_0__SRCID__CP_EOP_INTERRUPT`).

`soc15d.h` defines `DATA_SEL(x)` as `x << 29` and `INT_SEL(x)` as `x << 24`. The K1 release writes `INT_SEL(2)` into that same dword. The K0 packets are unchanged. The interrupt ring is the Vega10 ring 0: 64 KiB, writeback on, rings 1 and 2 left off. OSSSYS base `0x10A0`, GC base `0x2000`, NBIO segment 2 `0xD20`. MSI uses `pci_module_info` through `B_PCI_MODULE_NAME`, the same `get_msi_count`, `configure_msi` and `enable_msi` the HDA driver uses. This tree has no `pci_x86` type with that signature. A miss turns the ring and `CP_INT_CNTL_RING0` off and removes the handler. The GART test still runs from the K0 fence, whether or not the interrupt landed.

The ring holds 1024 dwords. K0 with the workload and the compute path writes 998. The interrupt submission and the GART DMA submission fit (1022). The following indirect buffer, the one that lives in the GART area, does not. That submit is refused before it touches the command processor. That is the existing ring limit.

Both hosts built with `-Wall -Werror`. The only warning is the existing unused `igpLaneInfo` in `connector.cpp`. `jam -q @r5-anyboot` for AMD64 finished at 13:06 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8392 targets). The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `e51c404cef57f5bd4fcb8e53e9539e717876eff0a16e39fdfc7d4913a6dfb8c8`. The stick copy is the same size, sha256 `df6b5cbf887cb4d36162e11c14b2137d530bd2d0d3f0ace7fe1eeaeab7c991a6`. It was written raw to the General UDisk at 13:11 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The kernel driver contains `k0`, `k1`, `k1_wait_us`, `k2`, `k2_dma_bad` and `k2_ib`. The accelerant contains `GFX9 owned by the kernel` and `fence landed`.

What's new and `distro/BUILD` are unchanged. `docs/GRAPHICS.md`, `AGENTS.md` and `docs/GPU-ARCHITECTURE.md` stay as they are until a boot confirms the result.

### The GFX9 command processor dispatches compute

This is milestone M2 for R5. The fence and the indirect-buffer check stay, and the check still runs first. After that fence, and only when the same `radeon_hd` settings file says `gfx9_compute true`, the command processor runs four compute dispatches. The image does not ship the file. With the ring on and this key absent, the log line is `GFX9 compute off`. This stick sets `gfx9_ring true` and `gfx9_compute true`. It does not set `gfx9_workload`, so the DMA fill does not run. If both keys are set, the fill runs only after the four dispatches and uses sequences 6 and 7. The workload-only path still uses sequences 2 and 3, and its packets are unchanged.

Stage P writes `0xDEADBEEF` and waits for sequence 1. C0 dispatches one group of `r5_empty` (`s_endpgm`) as sequence 2. C1 dispatches one group of `r5_fill`, 64 threads, into a 4 KiB buffer the CPU filled with `0xCAFEDEAD`, sequence 3. C2 dispatches 16384 groups (1,048,576 threads, 4 MiB) as sequence 4. C3 dispatches 65536 groups (4,194,304 threads, 16 MiB) as sequence 5. A thread `i` stores `i * 3 + 7`. The rest of the 4 KiB buffer must stay `0xCAFEDEAD`. A miss logs the stage, the sequence, and the value seen, plus `CP_STAT`, `GRBM_STATUS`, `GRBM_STATUS2`, `GRBM_STATUS_SE0`, `CP_STALLED_STAT1`, `CP_STALLED_STAT2`, `CP_CPF_STATUS`, `CP_RB0_RPTR`, `CP_ROQ_RB_STAT`, `GRBM_READ_ERROR`, `GRBM_READ_ERROR2`, and the VM fault status and address. It also logs the `COMPUTE_*` readback. Then it writes `CP_ME_CNTL` with the halt mask and does not run a later stage. It does not restore the stage-1 registers. `GRBM_GFX_INDEX` is not read over MMIO. The command processor writes it, broadcast `0xe0000000`, with `SET_UCONFIG_REG` offset `0x200`.

`GRBM_STATUS` `SPI_BUSY` is bit 22 (`0x00400000`). `GRBM_STATUS_SE0` `SPI_BUSY` is bit 27 (`0x08000000`).

The shader source is `gfx9_compute.s`. The `r5_fill` instructions are unchanged. Ubuntu LLVM 18.1.8 assembled it with `llvm-mc -triple=amdgcn-amd-amdhsa -mcpu=gfx90c -mattr=-xnack -filetype=obj gfx9_compute.s -o gfx9_compute.o`. The disassembly matches the source instruction for instruction. `r5_empty` is the 4 bytes `0xbf810000`. `r5_fill` is 36 bytes. Kernel-descriptor byte 48 is `COMPUTE_PGM_RSRC1` `0x000c0000` and byte 52 is `COMPUTE_PGM_RSRC2` `0x00000084`, for both kernels. That RSRC2 is `USER_SGPR` 2, `TGID_X_EN` 1 and `TIDIG_COMP_CNT` 0. `gfx9_compute_shader.h` is generated from that object. The object file is not committed.

The indirect buffer is 73 dwords and fits in the 4 KiB buffer. The host test compared one group and 16384 groups dword for dword, checked every `SET_SH_REG` offset against `index - 0x0c00` and against `(0x2000 + index) - 0x2c00`, and checked that the shader address is 256-byte aligned and that `COMPUTE_PGM_LO` is that address shifted right by 8. It printed `ok dwords 73 submit 12`. The submission is the same 12 dwords as the workload, including `INDIRECT_BUFFER_VALID`. Linux `gfx_v9_0_ring_emit_ib_gfx` still does not set that bit. The named constants match Linux v6.12 `soc15d.h`, `gfx_v9_0.c`, `gc_9_0_offset.h`, `gc_9_0_sh_mask.h` and `vega10_enum.h`: `PACKET3_DISPATCH_DIRECT` `0x15`, `PACKET3_EVENT_WRITE` `0x46`, `CS_PARTIAL_FLUSH` 7 with `EVENT_INDEX` 4, `PACKET3_ACQUIRE_MEM` `0x58` with coherency `0x28c40000`, and `PACKET3_SET_SH_REG` `0x76`. No header value differed from the brief.

The ring holds 1024 dwords. This path writes 914 + 12 + four times 12 = 974. With the DMA fill as well, that is 998. The existing check still refuses a wrap.

What's new and `distro/BUILD` are unchanged.

`jam -q @r5-anyboot` for AMD64 finished at 11:58 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8389 targets). The linked accelerant contains `GFX9 compute off`, `GFX9 compute failed:`, `compute_c0`, `compute_c1`, `compute_c2`, `compute_c3`, `compute_ratio`, `compute_regs`, `GFX9 workload off`, `fence landed` and `GFX9 ring contract verified`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `7e2ca7807c8a952a940a4d955f9d3f1053853f463c8f074e025469dd01229891`. A copy gained `home/config/settings/kernel/drivers/radeon_hd` with `gfx9_ring true` and `gfx9_compute true`. That file does not contain `gfx9_workload`. That stick image is 2156724224 bytes, sha256 `e978a0c67f23d3d3104c1ad641e55e906373f5d6a53cdf87a334fe0a4f5de410`. It was written to the General UDisk at 12:03 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. The settings text read back from the stick at file offset 1515616256. Disk 0 was not opened.

That stick reached the desktop on the Ryzen 5 5625U. One `Welcome to the Haiku boot loader!`. `GFX9 ring contract verified`. `CP_MEC_CNTL` went from `0x50000000` to `0x0`. The log says `GFX9 workload off`. The lines are `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed`, `GFX9 workload ib wb 0xdeadbeef seq 1 seen 1 rptr 0x39e wptr 0x39e`, `GFX9 compute_c0 ok seq 2 seen 2`, `GFX9 compute_regs pgm f4010030 00000000 rsrc 000c0000 00000084 threads 64 user 01004000 000000f4 se0 ffffffff`, `GFX9 compute_c1 ok bad 0 first 0 got 00000000 want 00000000`, `GFX9 compute_c2 ok bad 0 ticks 16664`, `GFX9 compute_c3 ok bad 0 ticks 65426` and `GFX9 compute_ratio 3.9`. `radeon-gfx.txt` has the same `ib_*` and `compute_*` lines. Every checked dword was correct. The 16 MiB dispatch took 3.9 times the 4 MiB dispatch, inside the expected range of about 3 to 4. Between those two lines the USB stick logged `operation 0x35 failed` with sense `Invalid field in CDB`. The next compute stage still completed. Tracker started. There is no panic. The scanout was not changed. That sequence is now `GFX9 compute on GC 9.3 (fixed)` in `docs/GRAPHICS.md`. A boot with `gfx9_compute` absent was not run. Other AMD GPUs were not tested. VirtualBox was not tested. ARM64 and RISC-V were not tested. QEMU does not have this GPU. The plan's milestone table still says M2 is "brief ready". That file was not edited.

### The GFX9 command processor fills memory

The indirect-buffer check stays, and it still runs first. After that fence, the command processor fills memory with `DMA_DATA`. The CPU writes zeros into an 8 MiB buffer. The first submission fills 1 MiB with `0xA5A5A5A5` and takes a GPU counter before and after. The second submission fills all 8 MiB the same way, as eight 1 MiB packets. Linux v6.12 `soc15d.h` says `DATA_SEL(3)` sends the 64-bit GPU counter. `PACKET3_DMA_DATA` is `0x50`. `SRC_SEL(2)` is the constant. `DST_SEL(0)` is the destination address. The byte count is bits 20:0, and `0x100000` fits. The header calls the high destination address bits 7:0. On this machine that half is `0xf4`, so the stored dword is inside those bits. The packet does not set `CMD_DAS`.

What's new and `distro/BUILD` are unchanged.

`jam -q @r5-anyboot` for AMD64 finished at 10:54 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8390 targets). The linked accelerant contains `GFX9 workload dma`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `31cd94cbd1719a28e3fff8e3631704d60fb4c3cd0607e84cbb0287abedcb790d`. A copy gained `home/config/settings/kernel/drivers/radeon_hd` with `gfx9_ring true` and `gfx9_workload true`. That stick image is 2156724224 bytes, sha256 `221a4837006941b4f23981db9ef35bb831bb05404a1b70ebb247fba1f252e9a4`. It was written to the General UDisk at 10:59 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0 was not opened.

That stick reached the desktop on the Ryzen 5 5625U. One `Welcome to the Haiku boot loader!`. `GFX9 ring contract verified`. `CP_MEC_CNTL` went from `0x50000000` to `0x0`. The lines are `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed`, `GFX9 workload ib wb 0xdeadbeef seq 1 seen 1 rptr 0x39e wptr 0x39e` and `GFX9 workload dma 1m bad 0 ticks 8470 8m bad 0 ticks 67209 ratio 7.9`. `radeon-gfx.txt` says `ib_wb deadbeef`, `ib_seq 1 seen 1`, `ib_rptr 0000039e wptr 0000039e`, `dma_1m_bad 0 ticks_1m 8470`, `dma_8m_bad 0 ticks_8m 67209` and `dma_ratio 7.9`. Both fills are clean. The 8 MiB fill took 7.9 times the 1 MiB fill, so the time grew with the work. Tracker started. There is no panic. The scanout stayed the firmware picture. That sequence is now `GFX9 workload on GC 9.3 (fixed)` in `docs/GRAPHICS.md`. A boot with the ring on and `gfx9_workload` absent was not repeated after this fill. `gfx9_paint` is not implemented. Other AMD GPUs were not tested. VirtualBox was not tested. ARM64 and RISC-V were not tested. QEMU does not have this GPU.

### A GPU write after the GFX9 fence

The fence that landed is a scratch word the command processor stores. This step asks the GPU to write ordinary memory. It runs only after that fence is `0xdeadbeef`, and only when the same `radeon_hd` settings file also says `gfx9_workload true`. The image does not ship the file. With the ring on and this key absent, the log line is `GFX9 workload off` and the first ring is unchanged.

The submission is 12 dwords at the write pointer 914: `INDIRECT_BUFFER` of a 5-dword `WRITE_DATA`, then `RELEASE_MEM`. The indirect buffer writes `0xdeadbeef` at fence+1024. The release writes the sequence at fence+1088. The CPU stores `0xcafedead` and 0 in those slots first. The sequence starts at 1. The new write pointer is 926 (`0x39e`). A miss writes `CP_ME_CNTL` with the halt mask and does not restore the stage-1 registers. The scanout stays the firmware picture.

The packet names match Linux v6.12 `soc15d.h` and `vega10_enum.h`. `CACHE_FLUSH_AND_INV_TS_EVENT` is `0x14`. `PACKET3_WRITE_DATA` is `0x37`, `PACKET3_INDIRECT_BUFFER` is `0x3f`, and `PACKET3_RELEASE_MEM` is `0x49`. Linux `gfx_v9_0_ring_emit_ib_gfx` does not set `INDIRECT_BUFFER_VALID`. This packet does, because that is the bit the test sequence asks for. The high half of the indirect-buffer address is kept in 16 bits, the same mask as the existing writeback. On this machine that half is `0xf4`, so the stored value is the full address.

What's new and `distro/BUILD` are unchanged.

`jam -q @r5-anyboot` for AMD64 finished at 09:57 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8390 targets). The linked accelerant contains `GFX9 workload off`, `GFX9 workload failed:` and `GFX9 workload ib`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `a15c2db4d8df82e6e837b138a504d72ae81248302b13779f305f3df7468d407a`. A copy gained `home/config/settings/kernel/drivers/radeon_hd` with `gfx9_ring true` and `gfx9_workload true`. That stick image is 2156724224 bytes, sha256 `2a5cb8dbc5cfcd52af5d74a6e1149b0f563ba313876ce3ff6a2909786ff6295a`. It was written to the General UDisk at 10:05 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0 was not opened. The settings file read back as those two lines.

That stick reached the desktop on the Ryzen 5 5625U. One `Welcome to the Haiku boot loader!`. `GFX9 ring contract verified`. `CP_MEC_CNTL` went from `0x50000000` to `0x0`. The lines are `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed` and `GFX9 workload ib wb 0xdeadbeef seq 1 seen 1 rptr 0x39e wptr 0x39e`. `radeon-gfx.txt` says `ib_wb deadbeef`, `ib_seq 1 seen 1` and `ib_rptr 0000039e wptr 0000039e`. Tracker started. There is no panic. The scanout stayed the firmware picture. Other AMD GPUs were not tested. VirtualBox was not tested. ARM64 and RISC-V were not tested. QEMU does not have this GPU.

### The first GFX9 packet lands on the 5625U

The GFX ring fetched 152 dwords and stopped, scratch stayed `0xcafedead`, and `CP_MEC_CNTL` was `0x50000000`. Linux starts the MEC in `gfx_v9_0_kiq_resume` before the GFX ring. Writing `CP_MEC_CNTL` to 0 and waiting 50 µs, only after both MEC PSP items return status 0, makes the PFP idle before the doorbell. The 914-dword clear-state stream then runs, and scratch becomes `0xdeadbeef`.

The same 08:11 image was booted a second time with no code change and reproduced that result. The phase snapshot that read `GRBM_GFX_INDEX` through segment `0xA000` is gone, so that `0xc200` read error is gone with it. The short test rings are gone. The clear-state stream, the scratch packet, the MEC start, and the stall note on a miss stay. `MC_VM_MX_L1_TLB_CNTL` still keeps `ECO_BITS`. The type-2 NOP padding is unchanged and is not executed. The aperture high check now refuses a value that would overflow after adding 1.

The ring is off unless `~/config/settings/kernel/drivers/radeon_hd` says `gfx9_ring true`. Userland looks up `B_USER_SETTINGS_DIRECTORY` and then `/kernel/drivers/radeon_hd`. The image does not ship that file. Tonga through Vega 20 are not claimed. The IP-discovery row is not compiled in. A DCN chip with no boot framebuffer is not claimed, and `app_server` moves on to VESA or the framebuffer once it asks past the published devices. Before the ME is unhalted the driver reads the ring setup back and logs `GFX9 ring contract verified`, or logs every mismatch and restores the ring. The apply scripts refuse a firmware blob or a clear-state header whose sha256 is not the one in `THIRD_PARTY_NOTICES.md`.

What's new and `distro/BUILD` are unchanged. Nothing here is on by default, and the panel is still the firmware picture.

The 08:11 stick, sha256 `73a9a97a5cc06a789baec24befa83cf8523be2a43c8d3209914a426fe22cc1c2`, reached the desktop on the Ryzen 5 5625U twice. Tracker started. There is no panic. Scratch was `0xdeadbeef` and the read pointer was `0x392` both times.

`jam -q @r5-anyboot` for AMD64 finished at 08:54 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8389 targets). The apply printed the eleven firmware sha256 lines and the two clear-state header lines, and each matched `THIRD_PARTY_NOTICES.md`. A copy of `renoir_pfp.bin` with the first byte flipped, kept under `/tmp`, made `apply-amdgpu-fw.py` exit before copying. The linked accelerant contains `GFX9 ring off`, `fence landed`, `GFX9 ring contract verified` and `GFX9 ring contract:`. It does not contain `GFX9 phase` or `try writedata`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `1a1e8379d10461d0d2fb98aa1eef47f3f2033d1f704382ff284060287ea0776d`. Boot 1 used that image plus `home/config/settings/kernel/drivers/radeon_hd` containing `gfx9_ring true`. That stick, sha256 `406a47c457d912c490999094c0ac7f4715690e7eecf033aa43cfac4529620fdd`, was written at 09:12 Europe/Amsterdam on 5 Nov 2026 and booted once on the 5625U. One `Welcome to the Haiku boot loader!`. `GFX9 ring contract verified`. `CP_MEC_CNTL` went from `0x50000000` to `0x0`. The line is `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed`. `radeon-gfx.txt` says `scratch deadbeef` and `packet yes`. Tracker started. There is no panic. The scanout stayed the firmware picture. No `GFX9 ring contract:` mismatch. Boot 2 used the image without that settings file, sha256 `1a1e8379d10461d0d2fb98aa1eef47f3f2033d1f704382ff284060287ea0776d`, written at 09:21 Europe/Amsterdam on 5 Nov 2026. One boot-loader welcome. The line is `GFX9 ring off, set gfx9_ring in the radeon_hd driver settings`. There is no mode 2, no PSP load, and no `radeon-gfx.txt`. Tracker started. There is no panic. The scanout stayed the firmware picture. Other AMD GPUs were not tested. Polaris and Vega are now not claimed. A hybrid laptop was not tested. VirtualBox was not tested. ARM64 and RISC-V were not tested. QEMU does not have this GPU. `scripts/run-qemu-r5.sh` boots `r5-os.image`, which is still the file from 3 Nov 2026, 19:24 Europe/Amsterdam. That process was started and then stopped. The new anyboot ISO was not booted in QEMU.

### The MEC is started before the GFX ring

Linux v6.12 `gfx_v9_0_cp_compute_enable(true)` writes `CP_MEC_CNTL` to 0 and waits 50 µs. `mmCP_MEC_CNTL` is `0x008d`, base index 0. `MEC_ME1_HALT` is bit 30 and `MEC_ME2_HALT` is bit 28. The accelerant does that write after the `rlc` snapshot and before the ring registers, and only when both MEC PSP items (type 4 and type 5) returned status 0. There is no KIQ queue and no MQD. A failure path writes that saved value back before `radeon_gfx9_restore_saved`. Two such calls sit after the new code. The log line is `GFX9 CP_MEC_CNTL`.

The phase snapshot also reads `GRBM_READ_ERROR` (`0x0016`), `GRBM_READ_ERROR2` (`0x0017`), `GRBM_GFX_INDEX` (`0x2200`, base index 1), `CP_MEC_CNTL`, `CP_CPC_STATUS` (`0x0084`) and `RLC_GPM_STAT` (`0x4c40`, base index 1). A seventh point, `mec`, follows the write. The file line adds `rerr`, `rerr2`, `gfxidx`, `mec`, `cpc` and `rlcstat`.

`GRBM_READ_ERROR` in `gc_9_0_sh_mask.h`: `READ_ERROR` is bit 31, `READ_MEID` is bits 23:22, `READ_PIPEID` is bits 21:20, and `READ_ADDRESS` is bits 17:2 (mask `0x0003fffc`, shift 2). The dword offset is that field shifted right by 2. `RLC_GPM_STAT` has `GFX_POWER_STATUS` at bit 1 and `GFX_CLOCK_STATUS` at bit 2. `GRBM_GFX_INDEX` broadcast is `SE_BROADCAST_WRITES`, `INSTANCE_BROADCAST_WRITES` and `SH_BROADCAST_WRITES`, together `0xe0000000`.

From the previous boot, now named from that header: `CP_STALLED_STAT2` `0x20` is `PFP_RCIU_READ_PENDING` (bit 5). `CP_STALLED_STAT1` `0xc00` is `ME_HAS_ACTIVE_CE_BUFFER_FLAG` (bit 10) and `ME_HAS_ACTIVE_DE_BUFFER_FLAG` (bit 11). `CP_CPF_STATUS` `0x10000000` is `GRBM_CPF_STAT_BUSY` (bit 28). `GRBM_STATUS` bit 5 is `RSMU_RQ_PENDING`. It is already set in the `boot` value `0x3028`, with the command processor halted, so it does not explain the stall. `CP_PFP_HEADER_DUMP` is not a header readout. The earlier conclusion that `0xdef0def0` showed the absence of a PM4 header does not hold. The PFP is already busy at `unhalt`, before a packet is fetched.

`jam -q @r5-anyboot` for AMD64 finished at 08:06 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8389 targets). The linked accelerant contains `GFX9 CP_MEC_CNTL` and `rerr`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `73a9a97a5cc06a789baec24befa83cf8523be2a43c8d3209914a426fe22cc1c2`. The build package is 41073319 bytes, sha256 `9074df93b7730b3e155c348832e03a6cfdda0deae314d6fd543ef67f46d13017`. It was written raw to the General UDisk at 08:11 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop on the Ryzen 5 5625U. There is no panic. Tracker started. The scanout stayed the firmware picture. `CP_MEC_CNTL` went from `0x50000000` to `0x0`. At `unhalt`, before the doorbell, `CP_STAT` is `0x0`, `CP_RB0_RPTR` is `0` and `CP_ROQ_RB_STAT` is `0`. The PFP is idle there. After the doorbell the read pointer and the writeback both reach `0x392`, the same value as the 914-dword write pointer. The syslog line is `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed`. `radeon-gfx.txt` says `scratch deadbeef` and `packet yes`. `writedata` and `full2` were not submitted.

`GRBM_READ_ERROR` is `0` at `boot` and `0x80030800` from `mode2` through `bell`. Bit 31 is `READ_ERROR`. `READ_MEID` and `READ_PIPEID` are 0. `READ_ADDRESS` bits 17:2 give dword offset `0xc200`. `GRBM_READ_ERROR2` is `0x20000`, which is `READ_REQUESTER_RSMU`. `RLC_GPM_STAT` is `0xf40016` at `boot` and `mode2`, then `0xb40016`. Bits 1, 2 and 4 stay set: `GFX_POWER_STATUS`, `GFX_CLOCK_STATUS` and `GFX_PIPELINE_POWER_STATUS`. Bit 22, `RLC_EXEC_ROM_CODE`, is set only in the first two samples.

The same 08:11 image was booted again on the 5625U, with no code change. The second syslog starts at a second `Welcome to the Haiku boot loader!`. It reproduced on a second boot. The lines are again `GFX9 scratch 0xdeadbeef rptr 0x392 dwords 914, fence landed`, `GRBM_READ_ERROR` `0` at `boot`, and `0x80030800` at `mode2`. Tracker started. There is no panic.

`0x80030800` is not a register the PFP was waiting on. Linux v6.12 `gc_9_0_offset.h` defines `mmGRBM_GFX_INDEX` as `0x2200` with base index 1. The phase snapshot read that index through `kGfx9Seg1` (`0xA000`). `radeon_gfx9_byte` adds the segment and the index, which is `0xc200`, and that is the address in the error. The read runs at `boot`, before `mode2`, on the CPU path. The value `0` stored for `gfxidx` is not the register. It has nothing to do with the stall.

### The Renoir golden list writes GCEA_PROBE_MAP

Linux v6.12 `gfx_v9_0.c` defines `mmGCEA_PROBE_MAP` as `0x070c` with base index 0. `golden_settings_gc_9_1_rn` programs that register with mask `0xffffffff` and value `0x0000cccc`. The accelerant now saves, writes and restores that row with the other golden registers. The log line is `GFX9 golden GCEA_PROBE_MAP`.

The same start also reads `CP_STAT`, `GRBM_STATUS`, `GRBM_STATUS2`, `CP_STALLED_STAT1`, `CP_STALLED_STAT2`, `CP_CPF_STATUS`, `CP_PFP_HEADER_DUMP`, `CP_ROQ_RB_STAT`, `CP_RB0_RPTR`, `CP_ME_CNTL` and `RLC_CNTL` at six points: `boot`, `mode2`, `psp`, `rlc`, `unhalt` and `bell`. `gc_9_0_offset.h` defines `mmCP_PFP_HEADER_DUMP` as `0x01a2` and does not say that a read consumes a header. The stall line already read it once. These six reads use that same offset. The syslog line is `GFX9 phase`. `radeon-gfx.txt` stores one `phase` line per point.

The load order from the previous stick is unchanged. The ring is unchanged. `jam -q @r5-anyboot` for AMD64 finished at 00:40 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8389 targets). The linked accelerant contains `GFX9 phase` and `GFX9 golden GCEA_PROBE_MAP`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `6bbb653c3bcce41ea2f1cb07f0a1ed427cf71bebda9927f9e167a5d0e5bbb639`. The build package is 41077907 bytes, sha256 `06453be2e0a07926d674dbfb54e223ff2f82993c12ed4fed351cd1931986103d`. It was written raw to the General UDisk at 07:39 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop on the Ryzen 5 5625U. There is no panic. Tracker started. The scanout stayed the firmware picture. `GCEA_PROBE_MAP` read `0` and read back `0xcccc`. Scratch stayed `0xcafedead`. `CP_RB0_RPTR` stopped at `0x98`. The `bell` line matches the 00:15 stall: `CP_STAT` `0x84808200`, `CP_ROQ_RB_STAT` `0x180019`, `CP_CPF_STATUS` `0x94000023`. The stall read one step later is `CP_STAT` `0x84008200`. `RLC_GPM_GENERAL_6` is again `0x59` after `green_sardine_rlc.bin` and `0` after the F32 enable.

`CP_STAT` is `0` at `boot`, `mode2`, `psp` and `rlc`. `CP_ME_CNTL` is `0x15000000` at those four points, so the halt bits are still set and the firmware did not leave the command processor running. At `unhalt`, before the doorbell, `CP_STAT` is `0x80008000` (bit 31 and bit 15, the PFP), `CP_RB0_RPTR` is still `0` and `CP_ROQ_RB_STAT` is still `0`. `GRBM_STATUS` is `0xa0003028`, `CP_STALLED_STAT1` is `0xc00` and `CP_STALLED_STAT2` is `0x20`. The fetch of the ring starts only at the doorbell.

`CP_PFP_HEADER_DUMP` is `0xdef0def0` at `boot` and `mode2`. After the PSP load each later read is the next value: `0xdef1def1`, `0xdef2def2`, `0xdef3def3`, `0xdef4def4`, then `0xdef5def5` on the stall line. The offset header does not document a side effect. This sequence is what a read that advances a counter looks like, and it is not a PM4 header. The step from the `rlc` line to the `unhalt` line is that same step, so it does not by itself show that the PFP took a packet. `writedata` and `full2` again logged a rptr of `0x98` and were not submitted.

### The PSP loads the CP firmware before it loads the RLC

On the 23:22 stick the PSP accepted RLC before PFP, CE, ME and MEC. Linux `psp_load_non_psp_fw` sends CE, PFP, ME, MEC and the MEC jump table first, then the three RLC lists, and RLC_G last. Its comment says the RLC autoload starts after the PSP has the rest of the GFX firmware. This stick uses that order. The ring, the GFXHUB setup and the clear-state stream are unchanged.

After `SETUP_TMR`, and after every PSP command, the log reads `RLC_CNTL` and `RLC_GPM_GENERAL_6`. Before F32 is enabled, `RLC_GPM_GENERAL_6` is cleared. The log then records the value before the clear, the value read back, the value 50 µs after the enable, and the value 10 ms later. `radeon-gfx.txt` stores those four as `rlc_gen6`. The file's ucode version is `0x3c`. The dword `0x59` is not in the file.

`jam -q @r5-anyboot` for AMD64 finished at 00:07 Europe/Amsterdam on 5 Nov 2026 with exit 0 (8389 targets). `gpu.o` was rebuilt at 00:04. The linked accelerant contains `PSP after` and `RLC GENERAL_6`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `a5dd01984580d3f9934283a3d1500260723483ab196e0aadcfff6c5480017a6f`. The build package is 41073241 bytes, sha256 `647a6061ae074d7557ea4c802eaf7e6d95c0d8b97da965dfbe3849433d22b4fd`. It was written raw to the General UDisk at 00:15 Europe/Amsterdam on 5 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop on the Ryzen 5 5625U. There is no panic. Tracker started. The scanout stayed the firmware picture. Every PSP command returned status `0x0`, in this order: CE, PFP, ME, MEC body, MEC jump table, `rlc-cntl`, `rlc-gpm`, `rlc-srm`, RLC_G. `RLC_CNTL` and `RLC_GPM_GENERAL_6` stayed `0` through the jump table and the three lists. The line after `green_sardine_rlc.bin` is `RLC_CNTL 0x1 GENERAL_6 0x59`. The clear read back `0`. Fifty microseconds and 10 ms after the F32 enable, `GENERAL_6` was still `0`. Scratch stayed `0xcafedead`. `CP_RB0_RPTR` stopped at `0x98`. `CP_STAT` is `0x84808200`. `CP_PFP_HEADER_DUMP` is `0xdef0def0`. `CP_ROQ_RB_STAT` is `0x180019`. `CP_STALLED_STAT2` is `0` on this boot; it was `0x20` on the 23:22 stick. The other stall registers match that stick.

### The GFXHUB TLB and L2 cache are turned on before the ring

The 22:46 stick reached the desktop. `SETUP_TMR` and the MEC body returned status `0x0`. `CP_STAT` changed from `0x80808200` to `0x80008200`: bit 23, `RCIU_BUSY`, is clear. `PFP_BUSY` and `ROQ_RING_BUSY` stayed set, and the read pointer still stopped at `0x98`.

`gfxhub_v1_0_init_tlb_regs` and `gfxhub_v1_0_init_cache_regs` now run after the system aperture. `MC_VM_MX_L1_TLB_CNTL` enables the L1 TLB, sets `SYSTEM_ACCESS_MODE` to 3, sets `MTYPE` to uncached and sets `ATC_EN`. `SYSTEM_APERTURE_UNMAPPED_ACCESS` stays clear. `VM_L2_CNTL` enables the L2 cache and fragment processing, sets context-1 identity mode to 1, and clears the PDE0 tag mode, the PDE fault class and the identity fragment size. `VM_L2_CNTL2` invalidates the L1 TLBs and the L2 cache. `VM_L2_CNTL3` is `mmVM_L2_CNTL3_DEFAULT` (`0x80100007`) with `BANK_SELECT` 9 and `L2_CACHE_BIGK_FRAGMENT_SIZE` 6, which is `0x80130009`. The four registers are restored with the rest of the snapshot. The log records the old and new L1 and `VM_L2_CNTL` values.

The stall line now also reads `CP_CPF_STATUS`, `CP_CPF_BUSY_STAT`, `CP_CPF_STALLED_STAT1`, `CP_STALLED_STAT1`, `CP_STALLED_STAT2`, `CP_BUSY_STAT`, `CP_PFP_HEADER_DUMP` and `CP_ROQ_RB_STAT`. The first submitted ring is the 914-dword clear-state stream. The write-data ring and a second clear-state ring (`full2`) run only if that read pointer can be forced back to 0. The uconfig ring stays compiled and is not submitted.

`jam -q @r5-anyboot` for AMD64 finished at 23:08 Europe/Amsterdam on 4 Nov 2026 with exit 0 (8389 targets). `gpu.o` was rebuilt at 23:05. The linked accelerant contains `GFX9 L1_TLB` and `GFX9 stall cpf`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `70ad36ea6d46f78085ce108a2fe955a2155663282d03c2f6a63d57799d3d91df`. The system package inside it is the build package, 41073644 bytes, sha256 `ad63f232bcd8939f75b1b18f5b9a7d076d87e5957e7293e5cc12616c6c7a9b55`, at offset 1684320256. A first raw write stopped at the EFI partition, offset 2153775104, because Windows had mounted that partition. The partition table was cleared, the rest of the image was written, and the boot sector was restored. The finished stick was written at 23:22 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop on the Ryzen 5 5625U. There is no panic. Tracker started, and the first-login script ran. The scanout stayed the firmware picture. `L1_TLB` went from `0x2501` to `0x3d59` and `L2_CNTL` from `0x80602` to `0x80603`. `SETUP_TMR`, the RLC lists, RLC, PFP, CE, ME, the MEC body and the MEC jump table all returned status `0x0`. Scratch stayed `0xcafedead`. `CP_RB0_RPTR` went from `0` to `0x80` at the doorbell and then `0x98`, with the write pointer at 914. `CP_STAT` is `0x84808200`: the previous `0x80008200` plus bit 23, `RCIU_BUSY`, and bit 26. `CP_CPF_STALLED_STAT1` is `0x1`, `RING_FETCHING_DATA`. The UTCL2 and GFXHUB translation-wait bits in that register are clear. `writedata` and `full2` did not start, because the read pointer stayed `0x98`. The registers were restored.

### The GFX9 ring uses the firmware MC window, not the UMA offset

On the 5625U the 22:03 stick logged `MC_VM_FB_OFFSET 0x810` and `aperture low 0x3d0000 -> 0x20400`. `0x3d0000 << 18` is `0xF400000000`. That is `MC_VM_FB_LOCATION_BASE << 24`, the firmware MC window. `0x810 << 24` is `0x810000000`, `MC_VM_FB_OFFSET`, the physical UMA carveout. Linux `gmc_v9_0_vram_gtt_location` keeps those apart. `amdgpu_gmc_vram_mc2pa` is `mc_addr - vram_start + vram_base_offset`.

The accelerant was using the offset as `vram_start`. It wrote `MC_VM_FB_LOCATION_BASE` to `0x810` and left `MC_VM_FB_LOCATION_TOP` at the firmware top, and it moved the system aperture onto the physical pages. The ring, the clear-state buffer and the PSP buffers were programmed at `0x810000000 + offset`.

`FB_LOCATION` is no longer written. The system aperture is widened only when it does not already cover the MC window. The ring, writeback, poll, clear-state buffer and PSP command buffers use `FB_LOCATION_BASE << 24` plus the byte offset. `MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR` uses the physical carveout. `SETUP_TMR` is sent again: command 5, `virt_phy_addr` set, `buf_phy_addr` the MC address, `system_phy_addr` the carveout. The region is 4 MiB, naturally aligned. A missing page or a finished non-zero status is logged and the GFX loads still run. A PSP fence timeout still stops the start.

`RLC_CNTL` was `0x1` during that PSP load, and `RLC_GPM_GENERAL_6` stayed `0x59` after a file whose header version is `0x3c`. F32 is now halted, and the serdes is polled, before `LOAD_IP_FW`. If the load or the ring test fails, `RLC_CNTL` and `RLC_CGCG_CGLS_CTRL` are written back to their values from before the halt.

`radeon-gfx.txt` records `fb_location`, `fb_top`, `fb_offset`, `ring_mc` and `ring_pa`.

`jam -q @r5-anyboot` for AMD64 finished at 22:41 Europe/Amsterdam on 4 Nov 2026 with exit 0 (8401 targets). `gpu.o` was rebuilt at 22:38. The linked accelerant contains `FB_LOCATION`, `RLC halted before PSP`, and `SETUP_TMR mc`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `3bfb8941aa5874e8b28f066dcc19b461dc4c9db0e204363aa310e268b18d4484`. The system package inside it is the build package, 41067950 bytes, sha256 `7bead33cab3ea49d1f544458ebf6a301c01177b5d0b430b49d8c7ad0a58c92b9`. It was written raw to the General UDisk at 22:46 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed up. `FB_LOCATION` is `0xf400-0xf41f` and `FB_OFFSET` is `0x810`. The ring MC address is `0xf4007e9000` and the physical address is `0x8107e9000`. The system aperture stayed `0x3d0000` to `0x3d0800`. The default page is physical `0x8107eb000`. `SETUP_TMR` finished with status `0x0`, MC `0xf400c00000`, physical `0x810c00000`, fence `0x1`. The MEC body, type 4, finished with status `0x0`. On the previous stick that body was `0x80000203`. RLC, PFP, CE and ME were also `0x0`. `RLC_CNTL` was already `0` before the PSP load. After the restart `RLC_GPM_GENERAL_6` still read `0x59`. Scratch stayed `0xcafedead`. `CP_RB0_RPTR` went from `0` to `0x98`. `CP_STAT` is `0x80008200`. The previous stall was `0x80808200`, so bit 23, `RCIU_BUSY`, is clear. `GRBM_STATUS` is still `0xa0003028` and `GRBM_STATUS2` is still `0x50000008`. The later rings did not start, because the read pointer stayed `0x98`. The registers were restored.

### The SMU mailbox is MP1 base 0x16000 plus C2PMSG_90

`MP1_BASE.instance[0].segment[0]` in `vega10_ip_offset.h` is `0x00016000`. `mmMP1_SMN_C2PMSG_90` is already `0x029a`, so the dword is `0x1629a` and the byte offset is `0x58a68`. The previous code added `0x029a` to `0x00016200` and read dword `0x1649a`. That read was 0, so mode 2 was not sent and the RLC lists never ran. The 21:27 stick did that. The accelerant now logs `SMU mailbox byte`.

The stick written at 22:03 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. `SMU mailbox byte` is `0x58a68`. Mode 2 answered `1` both before and after the message. The PSP accepted `rlc-cntl` type 22, `rlc-gpm` type 20, `rlc-srm` type 21 and `RLC_G` type 8, status `0x0`, fences `0x1` through `0x4`. PFP, CE and ME were also `0x0`. The MEC body was again `0x80000203`. `RLC_CNTL` stayed `0x1`. The clear-state buffer was programmed with `srm 0x3`. Scratch stayed `0xcafedead`. `CP_RB0_RPTR` went from `0x0` to `0x98`. The stall line is `grbm a0003028 grbm2 50000008 cp 80808200 cpc 0 int 0 vm 0`. The later writedata and full rings did not start, because the read pointer stayed `0x98`. The registers were restored.

`jam -q @r5-anyboot` for AMD64 finished at 21:59 Europe/Amsterdam on 4 Nov 2026 with exit 0 (8401 targets). The linked accelerant contains `SMU mailbox byte`, `SMU mailbox initial 0`, and `rlc-cntl`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `12032b0cecd85982f957d46c0da92ae2621b4b0cc58f519be82ef8dc70fa3ba8`. The system package inside it is the build package, 41067049 bytes, at offset 1684320256. It was written raw to the General UDisk at 22:03 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### The first SMU message is sent when the mailbox is still 0

`smu_cmn_send_smc_msg_with_param` polls `C2PMSG_90` only after the SMU has left `SMU_FW_INIT`. The comment there is "Ignore initial smu response register value". Power-on leaves that register at 0. A later 0 means the previous command is still running. The 21:27 stick hit the power-on case: `mode2_before` was 0, the message was not written, and the ring was not programmed. The first mode-2 message is now sent when the register is still 0. A later call still waits. The ring is programmed only when the answer is `1`. This has not been booted.

### The 21:27 stick reached the desktop, and the ring was not started

The stick written at 21:27 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. `radeon-gfx.txt` says `device 15e7`, `psp no`, `rlc no`, `scratch 00000000`, `packet no`, `mode2 no`, `mode2_before 00000000`, `mode2_after ffffffff`. The syslog line is `SMU mailbox stayed 0, mode 2 not sent`. The response register was still 0 after 100 ms, so the reset message was not written and the command processor was left alone. Scratch was not tested.

The same boot does not change the agreed bookkeeping. `sRlcRestoreLists` is cleared at the start of every ring start. The poll slot is flushed after the write pointer is stored. A later ring try that stores `0xdeadbeef` is the result that decides whether the ring stays. CSIB and SRM are written back while RLC F32 is stopped, and the enable bit is restored after that. The register snapshot is taken before the mode-2 message. The framebuffer map size is a 64-bit byte count, so a 4 GiB BAR does not wrap to 0. `gfxinfo` prints the record, up to 16 KiB. This correction has not been built.

### RLC 2.1 save/restore lists go to the PSP before RLC_G

`green_sardine_rlc.bin` and `renoir_rlc.bin` are the same 39928-byte header 2.1. The ucode is 16896 bytes at offset 256. `rlc_firmware_header_v2_1` then holds three lists: CNTL 592 bytes at 26832, GPM 2560 bytes at 27424, SRM 9944 bytes at 29984. Linux `amdgpu_gfx_rlc_init_microcode_v2_1` registers those as PSP types 22, 20 and 21. The ucode id order places them before `RLC_G` (type 8). This driver sent only type 8 and then set `RLC_SRM_CNTL` `SRM_ENABLE`. `gfx_v9_0_init_pg` sets that bit for every RLC 2.1, and on GC 9.3 it does not write the lists through MMIO. The PSP load is how Renoir gets them.

The three lists are now staged from those header offsets and sent before type 8. A non-zero status on a list stops the ring. `SRM_ENABLE` is set only after the three lists were accepted. `RLC_PG_CNTL` bit 15, `CP_PG_DISABLE`, is set with the other power-gating enables cleared. `RLC_JUMP_TABLE_RESTORE` stays unset: Linux writes it only when GFXOFF power-gating flags are on, and this path does not send GFXOFF.

`jam -q @r5-anyboot` for AMD64 finished at 21:22 Europe/Amsterdam on 4 Nov 2026 with exit 0 (8401 targets). The linked accelerant contains `rlc-cntl`, `rlc-gpm` and `rlc-srm`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `75eb2cd83576234c7b73013f23f7280a8f40debec372e1b27391aa50593e7cfd`. The system package inside it is the build package, 41066214 bytes, at offset 1684320256. It was written raw to the General UDisk at 21:27 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. The boot of that stick is the section above.

### Renoir gets the Linux mode-2 reset before the ring

`soc15_need_reset_on_init` in Linux 6.12 is true for `CHIP_RENOIR` and false for Raven and Picasso. `soc15_asic_reset` returns before a reset on those Raven chips. PCI `15e7`, `1636`, `1638` and `164c` are `CHIP_RENOIR`. MP1 12.0.1 selects mode 2. `smu_v12_0_mode2_reset` is `PPSMC_MSG_GfxDeviceDriverReset` (`0x1E`) with argument `2` on the MP1 mailbox: message `C2PMSG_66` at dword index `0x16000+0x282`, argument `C2PMSG_82` at `0x16000+0x292`, response `C2PMSG_90` at `0x16000+0x29a`. `PPSMC_Result_OK` is `1`. The same function is the decision for this series. Raven and Picasso do not take the message. Lucienne `164c` is in the set and does not get the message, because its firmware stays unloaded and DCN is not programmed again afterwards.

The ring start sends it before any command-processor or aperture write. The mailbox must already hold a non-zero response. That wait is 100 ms. The reset wait is 2 seconds, `usec_timeout * 20` at the Linux default. A timeout does not send the message again. A response other than `1` does not program the ring. `radeon-gfx.txt` records `mode2`, `mode2_before` and `mode2_after`. This is not a mode-1 reset and it is not `GRBM_SOFT_RESET`. The firmware scanout is not rewritten. Whether the panel stays up, and whether scratch then becomes `0xdeadbeef`, has not been booted.

The stall line from the previous stick was misread. `GRBM_STATUS2` `0x50000008` is the ME0 pipe 1 fifo at 8, `CPF_BUSY` and `CPG_BUSY`. `RLC_BUSY` is bit 24 (`0x01000000`). `ME1PIPE2_RQ_PENDING` is bit 8 (`0x100`). Both were clear. The pending RSMU request is the graphics register write. The refused MEC body is not that client.

### VMID 0 shader memory is set before the ring

The ring of `SET_UCONFIG` plus NOPs stopped at read pointer `0x98` with scratch still `cafedead`, and the read pointer could not be cleared afterwards. That is the same stop as the clear-state stream, so the dword in that stream is not the cause. The micro-engine did not retire the first packet. `gfx_v9_0_constants_init` writes VMID 0 shader memory before any PM4, and this path did not. `GRBM_GFX_CNTL` is now forced to 0, which is ME 0, pipe 0, queue 0 and VMID 0. `SH_MEM_CONFIG` is set to `0x1018`: alignment unaligned is 3 on GFX9, shifted by 3, and `RETRY_DISABLE` is set because GC 9.3 noretry is 1. `SH_MEM_BASES` is 0. Both are restored if scratch stays `cafedead`.

After the wait, and before a later ring is attempted, the log records `GRBM_STATUS`, `GRBM_STATUS2`, `CP_STAT`, `CP_CPC_STATUS`, `CP_INT_STATUS`, `VM_L2_PROTECTION_FAULT_STATUS` and its address, `RLC_SAFE_MODE`, `GRBM_GFX_CNTL` and `SH_MEM_CONFIG`. In `CP_STAT`, bit 14 is the VM walker, bit 15 the PFP and bit 17 the ME. In `CP_INT_STATUS`, bit 24 is an opcode error and bit 23 a privileged register. A non-zero VM fault status is a translation fault. `GCEA_PROBE_MAP` from the Renoir golden list is still not programmed. Its offset is not in the GC 9.0 header. `jam -q @r5-anyboot` for AMD64 finished at 19:20 Europe/Amsterdam on 4 Nov 2026 with exit 0. The linked accelerant contains `GFX9 stall`. The image is `r5-os-anyboot.iso`, 2156724224 bytes, sha256 `67b8ecbf5bc5481ad894ba98108edfb630f9261a8d203030635fea880821b4df`, the same file as the build. The package inside it contains `GFX9 stall`. It was written raw to the General UDisk at 20:22 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop. There is no panic. Tracker started. `GRBM_GFX_CNTL` was already `0x0`. `SH_MEM_CONFIG` read back `0x1018`. Scratch stayed `cafedead` and the read pointer stopped at `0x98`. The stall line is `grbm a0003028 grbm2 50000008 cp 80808200 cpc 0 int 0 vm 0`. `CP_STAT` is `CP_BUSY`, `RCIU_BUSY`, `PFP_BUSY` and `ROQ_RING_BUSY`. The ME bit is clear. `CP_INT_STATUS` is `0`, so there is no opcode error and no privileged-register interrupt. The VM fault status and address are `0`. `RLC_SAFE_MODE` is `0`. `GRBM_STATUS` is `GUI_ACTIVE`, `CP_BUSY`, clean color and depth, command-fifo level 8, and `RSMU_RQ_PENDING`. `GRBM_STATUS2` is `CPG_BUSY`, `RLC_BUSY` and `ME1PIPE2_RQ_PENDING`. The shader blocks are idle. The PFP is waiting on a register write that the RSMU has not completed. MEC body type 4 is still status `0x80000203`. The registers were restored.

### Three rings in one boot

A ring of three or eight dwords reaches its write pointer while `SCRATCH_REG0` stays `0xcafedead`. The read pointer on those rings is the fetcher. One fetch block is 128 dwords, so a short ring cannot show whether the micro-engine retired the packet. This image runs three rings in one boot. Each ring has its own buffer. The calls stay inside `REMOVE-GFX9-SCRATCH-TEST`.

The first ring is `SET_UCONFIG` of `SCRATCH_REG0` to `0xDEADBEEF`, then type-2 NOPs. The doorbell publishes write pointer 256. The second is `PACKET3` `WRITE_DATA` (opcode `0x37`, count 3) with engine 0, destination a register, `WR_CONFIRM`, register `0xC040`, and the same value, then NOPs, write pointer 256. The third is the 914-dword clear-state stream. Between rings the command processor is halted, the write pointer is cleared, and `CP_RB0_RPTR` is forced to 0 through `RB_RPTR_WR_ENA`. A doorbell of 0 is written only when that readback is 0. If the reset readback is not 0, that ring is logged and the next ring is still attempted. `radeon-gfx.txt` gains one `try` line per ring: name, write pointer, the reset read pointer, the read pointer just after the doorbell, the read pointer after the wait, scratch, and `CP_ME_CNTL`. The first line is `uconfig` and its reset field is 0 because that ring is the one programmed at start. The syslog line is `GFX9 ring tests`.

Linux runs `gfx_v9_0_ring_test_ring` only after `gfx_v9_0_cp_gfx_start` has finished the clear-state. That clear-state is the stream that stops here. This boot does not copy the rest of `gfx_v9_0_hw_init`. `SH_MEM`, the tiling table, and the RLC CP jump table are still not programmed. The Renoir golden `GB_ADDR_CONFIG` value `0x24000042` is already written.

`jam -q @r5-anyboot` for AMD64 finished at 18:18 Europe/Amsterdam on 4 Nov 2026 with exit 0. The linked accelerant contains `GFX9 ring tests`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. The boot that followed the 18:23 write still logged `GFX9 preamble scratch test`, write pointer 8, and packet `0xc0004a00`. That is the previous accelerant. The ISO that is on disk was checked after that: `haiku-r1~beta6_r5_2026.1_dev-1-x86_64.hpkg` inside it is 41071194 bytes, sha256 `42c442f21ff9000878740a564b22a78e45ac68114ff0906dc4ca727e6923fa91`, the same file as the build package, and the accelerant extracted from that package contains `GFX9 ring tests`. That ISO was written again to the General UDisk at 18:46 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop. There is no panic. Tracker started. The log line is `GFX9 ring tests`. The first packet dword is `0xc0017900`, `SET_UCONFIG`. The `uconfig` line is write pointer 256, read pointer `0x80` immediately after the doorbell and `0x98` after the wait, scratch `cafedead`, `CP_ME_CNTL` `0x0`. Forcing the read pointer back to 0 left it at `0x98`, so `writedata` and `full` were not submitted. After that halt `CP_ME_CNTL` was `0x15000000`. The registers were restored. The stop at dword 152 is the same on a ring of `SET_UCONFIG` plus NOPs as on the clear-state stream. The micro-engine did not retire the first packet, and the command processor then would not take a new read pointer.

### The scratch packet follows the preamble

The three-dword scratch packet was fetched and `SCRATCH_REG0` stayed `0xcafedead`. Linux submits that packet from `gfx_v9_0_ring_test_ring` only after `gfx_v9_0_cp_gfx_start` has sent the preamble and context control. This ring is those five dwords and then the same three-dword `SET_UCONFIG`. The 212 context registers are not in the buffer. One doorbell publishes write pointer 8 and is not written again. The calls stay inside `REMOVE-GFX9-SCRATCH-TEST`.

`jam -q @r5-anyboot` for AMD64 finished at 17:55 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 17:53. The linked accelerant contains `GFX9 preamble scratch test`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 17:59 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop. There is no panic. Tracker started. The log line is `GFX9 preamble scratch test`. The first packet dword is `0xc0004a00`, the preamble. The doorbell control was `0x40000458` and the poll slot read back `0x8`. Immediately after the doorbell `CP_RB0_RPTR` was `0x8`, and it was still `0x8` after the wait. `CP_ME_CNTL` was `0x0` before the doorbell. Scratch stayed `0xcafedead`. The writeback stayed `0x0`. The aperture went from `0x0` to `0x1` and the four doorbell ranges still read `0x0`. The registers were restored. All eight dwords were fetched. The preamble and context control did not make `SET_UCONFIG` change `SCRATCH_REG0`.

### The ring carries only the scratch packet

The previous probes wrote a doorbell of 0 while the read pointer was already 3, and the 1024-dword ring wrapped. Those probes are gone. This boot submits the three dwords from `gfx_v9_0_ring_test_ring`: `SET_UCONFIG` of `SCRATCH_REG0` to `0xDEADBEEF`. The 914-dword clear-state stream is not written into the ring buffer. One doorbell publishes write pointer 3 and is not written again. The temporary calls are marked `REMOVE-GFX9-SCRATCH-TEST` and go away when this test has answered. `radeon_gfx9_fill_start` stays in the file for that removal.

The first jam failed because `radeon_gfx9_fill_start` was unused under `-Werror`. A count-only call keeps it compiled and does not store those dwords. `jam -q @r5-anyboot` for AMD64 then finished at 17:33 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 17:31. The linked accelerant contains `GFX9 scratch test only`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 17:37 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop. There is no panic. Tracker started. The log line is `GFX9 scratch test only`. The first packet dword is `0xc0017900`, which is `SET_UCONFIG` with count 1. The doorbell control was `0x40000458` and the poll slot read back `0x3`. Immediately after the doorbell `CP_RB0_RPTR` was `0x3`, and it was still `0x3` after the wait. `CP_ME_CNTL` was `0x0` before the doorbell. Scratch stayed `0xcafedead`. The writeback stayed `0x0`. The aperture went from `0x0` to `0x1` and the four doorbell ranges still read `0x0`. The registers were restored. The three dwords were fetched and `SCRATCH_REG0` did not change. Linux runs this same packet in `gfx_v9_0_ring_test_ring` only after `gfx_v9_0_cp_gfx_start` has already submitted the clear-state stream.

### Short rings measure the stalled dword

The 914-dword clear-state stream stops inside the first `SET_CONTEXT`. This image does not guess which dword that is. Before the registers are restored it submits separate rings. Each one halts the command processor, forces `CP_RB0_RPTR` to 0, writes a doorbell of 0, and then rings the real write pointer.

The first ring is only the scratch packet. The second is the preamble and context control, then that scratch packet. The third adds one `SET_CONTEXT` value, `DB_RENDER_CONTROL`. When that scratch read is `0xDEADBEEF` and the read pointer has passed the packet, the same preamble is repeated with more values from the first extent, bisecting between 1 and 212. The last ring is the original 914-dword stream. `radeon-gfx.txt` gains one `probe` line per ring: name, register count, write pointer, the MMIO read pointer just after the doorbell, the read pointer after the wait, scratch, and both writeback samples. The syslog lines start with `GFX9 probe`.

`jam -q @r5-anyboot` for AMD64 finished at 17:02 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 17:00. The linked accelerant contains `GFX9 probe`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 17:07 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick reached the desktop. There is no panic. The scanout stayed up and Tracker started. The aperture went from `0x0` to `0x1` and the four doorbell ranges still read `0x0`. The scratch probe published write pointer 3. Both read-pointer samples are `0x3`. Scratch stayed `0xcafedead`. The preamble probe published write pointer 8 and the read pointer is `0x98`. The one-register probe published 11 and the read pointer is still `0x98`. The 914-dword probe is also `0x98`, and scratch is still `0xcafedead`. A read pointer past its own write pointer is not the progress of that short ring. Between probes the code writes a doorbell of 0 while the read pointer is already 3, and the ring is 1024 dwords, so that write pointer wraps. After the preamble sample the forced read-pointer clear does not move `CP_RB0_RPTR` again. The short rings were not isolated, and the stalled context register is still not identified. The registers were restored.

### The doorbell fetch stops at the same dword

The stick written at 16:22 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. Tracker started. `RCC_DOORBELL_APER_EN` read `0x0` and then `0x1`. `BIF_SDMA0_DOORBELL_RANGE`, `BIF_SDMA1_DOORBELL_RANGE`, `BIF_IH_DOORBELL_RANGE` and `BIF_MMSCH0_DOORBELL_RANGE` all read `0x0`, so none of those ranges covered the GFX doorbell. The doorbell control was `0x40000458` and the poll slot read back `0x392`. Immediately after the doorbell the MMIO read pointer was `0x80`. After 100 ms it was `0x98`. The writeback at the fence plus 256 stayed `0x0` both times. Scratch stayed `0xcafedead`. The registers were restored.

The 15:56 stick, with the aperture clear, left the read pointer at `0x0`. The 15:19 stick, writing `CP_RB0_WPTR` with the doorbell disabled, also stopped at `0x98`. The aperture lets the doorbell start the fetch. The command processor still stops 152 dwords in, inside the first `SET_CONTEXT`. The scratch packet at dword 911 is not reached.

### The NBIO doorbell aperture is enabled before the ring

Linux 6.12 `soc15_common_hw_init` enables the doorbell aperture before the CP ring test. GC 9.3.0, which is Renoir, Barcelo and Cezanne, uses `soc15_asic_funcs`. An APU binds `nbio_v7_0`. `nbio_v7_0_enable_doorbell_aperture` sets `RCC_DOORBELL_APER_EN` bit 0. In `vega10_ip_offset.h` NBIO segment 2 is `0xD20`, and `mmRCC_DOORBELL_APER_EN` is `0xC0` in that segment, so the byte offset is `0x3780`. `nbio_v7_0_init_registers` is empty. `nbio_v7_0_enable_doorbell_selfring_aperture` is empty. The same init then programs the SDMA doorbell ranges and says a write inside the SDMA, IH or multimedia range is not routed to the CP. Those ranges are not written here. They are read and logged: `BIF_SDMA0_DOORBELL_RANGE` at byte `0x3BC0`, SDMA1 at `0x3BC4`, IH at `0x3BC8`, and `BIF_MMSCH0_DOORBELL_RANGE` at `0x3BCC`. A read of `0xffffffff` means the register did not decode, and the aperture bit is left alone. After the doorbell store the log records the MMIO read pointer and the writeback at once, and again after the 100 ms wait. A missed scratch test restores the aperture register.

The stick written at 15:56 Europe/Amsterdam on 4 Nov 2026 had the doorbell control at `0x40000458` and the poll slot at `0x392`, and both pointers stayed `0x0`. This image has not been booted.

`jam -q @r5-anyboot` for AMD64 finished at 16:18 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 16:16. The linked accelerant contains `NBIO aperture`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 16:22 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

### The doorbell was programmed and the ring did not fetch

The stick written at 15:56 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. Tracker started. PCI BAR 2 is `0xfcf0000000`, `0x200000` bytes, and the accelerant cloned it. `CP_RB_DOORBELL_CONTROL` read back `0x40000458`: enable set, offset `0x458`. The poll slot read back `0x392`. `CP_ME_CNTL` was `0x0`. Both the writeback and `CP_RB0_RPTR` stayed `0x0`. Scratch stayed `0xcafedead`. The registers were restored. `packet` is `no`.

The 15:19 stick, with the doorbell enable clear, fetched 152 dwords after `CP_RB0_WPTR`. This stick did not write that register at the commit, matching `gfx_v9_0_ring_set_wptr_gfx`. The command processor did not move. The doorbell write did not start a fetch. `nbio_v7_0_enable_doorbell_aperture` sets `RCC_DOORBELL_APER_EN` before any doorbell store. This driver does not. PSP, the Green Sardine loads, the MEC body status `0x80000203`, the quiet serdes, and the clear-state buffer are unchanged.

### The GFX ring publishes its write pointer through the doorbell

The stick written at 15:19 Europe/Amsterdam on 4 Nov 2026 reached the desktop and stopped at the same dword with both command-processor images. `CP_RB0_RPTR` went from `0x0` to `0x98`. Scratch stayed `0xcafedead`. Linux `gfx_v9_0` sets `use_doorbell` on every GFX9 graphics ring, including Renoir. `gfx_v9_0_ring_set_wptr_gfx` stores the 64-bit count in the poll slot and then calls `WDOORBELL64`. It does not write `CP_RB0_WPTR` when the doorbell is enabled. `gfx_v9_0_ring_get_rptr_gfx` reads the writeback, not `mmCP_RB0_RPTR`. Renoir uses `vega10_doorbell_index_init`. `AMDGPU_DOORBELL64_GFX_RING0` is qword `0x8b`. The dword index stored on the ring is `0x116`. The byte offset in PCI BAR 2 is `0x458`. `CP_RB_DOORBELL_RANGE_LOWER` uses that same index. The previous image cleared the enable bit, left the poll slot at 0, and judged progress only from the MMIO read pointer.

The kernel driver now maps PCI BAR 2 when the part is not DCE, MMIO is BAR 5, and the BAR is between `0x460` bytes and 16 MiB. The accelerant clones that area. The start writes the 64-bit count into the poll slot, issues an `sfence`, and rings doorbell `0x458`. The enable bit is set only when that mapping exists. The range lower register is the GFX index either way. A missing BAR still writes the poll slot and falls back to `CP_RB0_WPTR`. The log records the doorbell control, the poll read-back, the writeback at the fence plus 256, and the MMIO read pointer. `SETUP_TMR` stays skipped. `15e7` and `1638` still load `green_sardine`. The scanout is untouched.

`jam -q @r5-anyboot` for AMD64 finished at 15:52 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 15:49 and `radeon_hd.o` at 15:48. The linked accelerant contains `GFX9 doorbell`. The kernel driver contains `doorbell BAR2`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 15:56 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### Green Sardine still stops the ring at dword 152

The stick written at 15:19 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. Tracker started. PSP accepted `green_sardine_rlc.bin` (`0x3c`, 16896 bytes), `green_sardine_pfp.bin` (`0xc5`, 85504 bytes), `green_sardine_ce.bin` (`0x50`, 36352 bytes), and `green_sardine_me.bin` (`0xa7`, 69120 bytes), status `0x0`, fences `0x1` through `0x4`. The MEC body was again `0x80000203` and the jump table was again status `0x0`. The serdes was quiet. `CP_RB0_RPTR` was `0x0` before the write pointer and `0x98` after 100 ms. Scratch stayed `0xcafedead`. The read pointer stopped at the same dword as with the Renoir PFP, ME, and CE. Those files were not the cause. The registers were restored.

### Barcelo loads the Green Sardine command processor

The stick written at 14:53 Europe/Amsterdam on 4 Nov 2026 fetched 152 dwords and stopped inside the first `SET_CONTEXT`. Scratch stayed `0xcafedead`. The files were `renoir_pfp.bin` version `0xc3` (21376 bytes), `renoir_me.bin` version `0xa7` (17280 bytes), and `renoir_ce.bin` version `0x50` (9088 bytes). `CP_RB0_RPTR` moved from `0x0` to `0x98` with the doorbell enable clear, so the write-pointer register is enough to start the fetch. This image does not switch the ring to the doorbell.

Linux `amdgpu_device_init_apu_flags` marks `1636` and `164c` as `AMD_APU_IS_RENOIR` and `15e7` and `1638` as `AMD_APU_IS_GREEN_SARDINE`. Barcelo now loads `green_sardine_pfp.bin` (`0xc5`, 85504 bytes), `green_sardine_me.bin` (69120 bytes), and `green_sardine_ce.bin` (36352 bytes). RLC and MEC are the same bytes in both sets. `1636` is named Renoir. Lucienne `164c` still does not start the command processor. `SETUP_TMR` stays skipped, and a finished MEC refusal still does not stop the GFX ring.

`jam -q @r5-anyboot` for AMD64 finished at 15:16 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 15:13. The linked accelerant contains `green_sardine`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 15:19 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### The GFX ring fetched 152 dwords and then stopped

The stick written at 14:53 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. Tracker started. `SETUP_TMR` was skipped. PSP accepted RLC, PFP, CE, and ME with status `0x0` and fences `0x1` through `0x4`. The loaded files were still `renoir_rlc.bin` (`0x3c`), `renoir_pfp.bin` (`0xc3`, 21376 bytes), `renoir_ce.bin` (`0x50`, 9088 bytes), and `renoir_me.bin` (`0xa7`, 17280 bytes). The MEC body, type 4, finished with fence `0x5` and status `0x80000203`, and the start continued. The MEC jump table, type 5, finished with fence `0x6` and status `0x0`. The RLC serdes read CU `0x0` and non-CU `0x0` on the first poll. The clear-state buffer was programmed at GPU `0x810845000`, 904 dwords. `RLC_CNTL` stayed `0x1`. `RLC_GPM_GENERAL_6` read `0x59`.

`CP_RB0_RPTR` was `0x0` before the write pointer moved, including after the force to 0. The write pointer was 914. After 100 ms the read pointer was `0x98` and scratch was still `0xcafedead`. The command processor fetched 152 dwords and stopped inside the first `SET_CONTEXT` packet. The scratch packet is dword 911. The registers were restored. `packet` is `no`. The doorbell enable stayed clear, so this fetch used `CP_RB0_WPTR`.

### The GFX ring no longer waits on SETUP_TMR

The stick written at 14:31 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. The aperture low page was again `0x3d0000` against a framebuffer page of `0x20400`. The high page `0x3d0800` already covers `0x20800`. The GPCOM ring came ready as `0x80020000`. The original `SETUP_TMR` at GPU `0x81fc00000`, 4 MiB, finished with fence `0x1` and status `0xffff0006`. The retry with `virt_phy_addr` and CPU physical address `0xfcefc00000` finished with fence `0x2` and the same status. That is `TEE_ERROR_BAD_PARAMETERS` for both forms. No IP firmware was sent. The ring was not submitted. Scratch was not tested. The last ring submit is still the 10:57 stick.

The flag and the choice between the MC address and the BAR physical address are not the cause. Linux can place that 4 MiB region in stolen memory above the visible BAR. This pool cannot, and another `SETUP_TMR` would only stop the start again. The command is no longer sent. The top 4 MiB of the BAR is left in the pool. RLC, PFP, CE, and ME stay required. The MEC body, type 4, and its jump table, type 5, are still sent. A finished non-zero MEC status is logged and the GFX ring still starts. A PSP fence timeout still restores the registers and does not program the ring. After F32 is halted, the start waits up to 100 ms for `RLC_SERDES_CU_MASTER_BUSY` to read 0 and for the non-CU busy bits of SE, GC, TC0, and TC1 to clear, then programs the clear-state buffer. A serdes that stays busy is logged and does not abort. The current GRBM index is not rewritten.

`jam -q @r5-anyboot` for AMD64 finished at 14:50 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 14:47. The linked accelerant contains `PSP SETUP_TMR skipped`, `MEC left unloaded`, and `RLC serdes`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 14:53 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### The Renoir TMR command was a bad parameter

The stick written at 14:09 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. `SETUP_TMR` finished with fence `0x1` and status `0xffff0006`. That is `TEE_ERROR_BAD_PARAMETERS`. The command had set `virt_phy_addr` and had passed the MC address as the system address. The region was `0x810c00000`, 4 MiB. No IP firmware was sent after that. The ring was not submitted.

The original `SETUP_TMR` is the GPU address and the size. The 4 MiB region is now the top of the visible BAR, naturally aligned, and the visible pool stops before it. If that command is refused, one retry sets `virt_phy_addr` and uses the CPU physical address of the same page, the framebuffer BAR plus the offset. A timeout is not retried. `CP_RB_DOORBELL_RANGE_LOWER` and `CP_RB_DOORBELL_RANGE_UPPER` are written the way `gfx_v9_0_cp_gfx_resume` does, and restored when the ring test fails. The MEC split is unchanged.

`jam -q @r5-anyboot` for AMD64 finished at 14:27 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 14:25. The linked accelerant contains `PSP TMR legacy`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 14:31 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### Renoir reserves a PSP trusted memory region before MEC

The stick written at 13:48 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. The split was loaded: `renoir_mec.bin` body 267072 bytes, jump table 896 bytes. PSP accepted RLC, PFP, CE, and ME with status `0x0` and fences `0x1` through `0x4`. The MEC body, type 4, finished with fence `0x5` and status `0x80000203` again. The jump table was not the refusal. The ring was not submitted.

That status is the same `LOAD_IP_FW` failure Linux logs when `SETUP_TMR` has not reserved a region. PSP v12 for Renoir (`MP0` 11.0.3 and 12.0.1) sets `boot_time_tmr` and `autoload_supported` to false, so `psp_hw_start` calls `psp_tmr_load` before any IP firmware. The accelerant now does that. The region is `PSP_TMR_SIZE`, 4 MiB, naturally aligned, in the visible pool past the scanout. `virt_phy_addr` is set. `amdgpu_gmc_vram_mc2pa` is the identity on this APU, so the system address is the same MC address. A non-zero TMR status restores the registers and does not program the ring. The MEC split is unchanged.

`jam -q @r5-anyboot` for AMD64 finished at 14:05 Europe/Amsterdam on 4 Nov 2026 with exit 0. `gpu.o` was rebuilt at 14:03. The linked accelerant contains `PSP TMR`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 14:09 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### Renoir MEC is a body and a jump table

The 13:23 stick reached the desktop. PSP accepted RLC, PFP, CE, and ME, and refused the MEC command, type 4, with status `0x80000203`. `renoir_mec.bin` is header 1.0. Its ucode is 267968 bytes. `jt_offset` is 66768 dwords and `jt_size` is 224 dwords, a suffix of 896 bytes. Linux `amdgpu_ucode_init_single_fw` on a PSP load sends `CP_MEC` as `ucode_size_bytes` minus that table, then `CP_MEC_ME1` as the table at `jt_offset`. That is 267072 bytes as type 4 and 896 bytes as type 5. PFP, ME, and CE also store jump-table fields. Those tables are not suffixes, and those files stay one image. `gfx_v9_0_load_mec2_fw_bin_support` is false for GC 9.3.0, so Renoir, Barcelo, and Cezanne do not send `CP_MEC_ME2`. A MEC table that is not that suffix is refused. A non-zero PSP status still restores the registers and does not program the ring.

`jam -q @r5-anyboot` for AMD64 finished at 13:45 Europe/Amsterdam on 4 Nov 2026 with exit 0 (8401 targets). `gpu.o` was rebuilt at 13:42. The linked accelerant contains `PSP MEC2 not loaded on GC 9.3` and `body %u jt %u`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 13:48 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### PSP refused the Renoir MEC image

The stick written at 13:23 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. The aperture low page was again `0x3d0000` against a framebuffer page of `0x20400`. The reader loaded `renoir_mec.bin`: header 1.0, version `0x1e3`, 267968 ucode bytes. PSP accepted RLC, PFP, CE, and ME with status `0x0` and fences `0x1` through `0x4`. The MEC command, type 4, finished with fence `0x5` and status `0x80000203`. There is no `renoir_mec2.bin`; the fallback open of `renoir_mec.bin` is expected. The start restored the registers and did not submit the ring. Scratch was not tested.

The MEC header carries a jump table at dword `0x104d0`, 224 dwords, 896 bytes at the end of that ucode. Linux `amdgpu_ucode_init_single_fw` sends the PSP the MEC body without that table as `GFX_FW_TYPE_CP_MEC`, then the table itself as `GFX_FW_TYPE_CP_MEC_ME1` and, for the second engine, `GFX_FW_TYPE_CP_MEC_ME2`. This driver sent the body and the table as one type-4 command. ME, PFP, CE, and RLC have no jump table and were accepted.

### The GFX9 firmware reader accepts the MEC blob

`renoir_mec.bin` is 268224 bytes. The reader allocated 262144 bytes and, once that buffer was full, reported the file missing. The ring was never submitted. The reader now takes the file size from `fstat` and accepts a file up to 1 MiB. A missing file, a bad size, and a short read are logged separately.

`jam -q @r5-anyboot` for AMD64 finished at 13:19 Europe/Amsterdam on 4 Nov 2026 with exit 0. The linked accelerant contains `GFX9 firmware %s refused, size`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 13:23 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### Renoir aperture boot stopped before the ring

The stick written at 13:02 Europe/Amsterdam on 4 Nov 2026 reached the desktop on the Ryzen 5 5625U. There is no panic. The scanout stayed 1920x1080. `MC_VM_FB_OFFSET` was `0x810` and the ring GPU address was `0x8107e9000`. The firmware system aperture was low page `0x3d0000` and high page `0x3d0800`. The framebuffer page is `0x20400`, so that low page sits above the visible BAR. The high page `0x3d0800` already covers the needed `0x20800`, so only the low page is moved. PSP then accepted `renoir_rlc.bin`, `renoir_pfp.bin`, `renoir_ce.bin`, and `renoir_me.bin`. `renoir_mec.bin` is on the image at `/system/non-packaged/data/firmware/amdgpu` and is 268224 bytes. The reader stops at 262144 bytes and reports the file missing. The start restored the registers and did not submit the ring. Scratch was not tested. `packet` is `no`.

### Renoir uses the GFX9 framebuffer aperture

The page table at virtual address 0 was the Linux path for an APU whose memory hub is `connected_to_cpu` (XGMI 6.1.0). Renoir, Barcelo and Cezanne are not that part. `gfxhub_v1_0_init_system_aperture_regs` keeps the framebuffer aperture and, for `AMD_APU_IS_RENOIR` and `AMD_APU_IS_GREEN_SARDINE`, sets the system-aperture high page to one past the end of VRAM. A VRAM address above that register faults and hangs. The accelerant now only widens `MC_VM_FB_LOCATION` and `MC_VM_SYSTEM_APERTURE` so the visible BAR is inside them. The CP ring base stays `(MC_VM_FB_OFFSET << 24) + offset`. `VM_L2_CNTL4` does not force physical page-table requests.

`RLC_CSIB_ADDR` and `RLC_CSIB_LENGTH` now point at the clear-state buffer from `gfx_v9_0_get_csb_buffer` (the preamble through `CLEAR_STATE`, 904 dwords for this table). F32 is stopped before that write, including on the direct loader. That loader then writes `renoir_rlc.bin` or `green_sardine_rlc.bin` at `RLCG_UCODE_LOADING_START_ADDRESS` (`0x2000`) and only then sets `RLC_ENABLE_F32`. The CE partition base, `VGT_INDEX_TYPE` and the scratch packet stay on the ring. The save/restore machine is enabled. `GRBM_CNTL` read timeout is `0xff`. The MGCG override bits are set and the RLC, CP and GFX power-gating enables are cleared, which is the Linux path for gating off. `RLC_CGCG_CGLS_CTRL_3D` clears only `CGCG_EN` and `CGLS_EN`. There is no SMU `DisableGfxOff` message. `GCEA_PROBE_MAP` from the Renoir golden list is not a `gc_9_0` register here and is not written. The other GC 9.3 golden values are. A miss restores those registers with the ring.

`MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR` is a zeroed framebuffer page. On Renoir `amdgpu_gmc_vram_mc2pa` leaves that MC address unchanged, which is the value Linux takes from `mem_scratch`. The fence page is not that redirect. `VM_L2_PROTECTION_FAULT_DEFAULT_ADDR` stays as the firmware left it: Linux fills it from a system DMA page, and this driver does not invent one. `CP_MEC_CNTL` keeps ME1 and ME2 halted. No compute ring is submitted.

Cezanne `1002:1636` and `1002:1638` load `green_sardine_*.bin`, including MEC and MEC2. Barcelo `1002:15e7` keeps the `renoir` blobs and now also loads `renoir_mec.bin` as MEC and, because Linux ships no separate Renoir MEC2 file, as MEC_ME2. The direct register loader no longer treats a read of `0xffffffff` as success. The device names for `1636` and `1638` are Cezanne.

`jam -q radeon_hd.accelerant` for AMD64 finished at 12:04 Europe/Amsterdam on 4 Nov 2026 with exit 0. `jam -q @r5-anyboot` finished at 12:55 Europe/Amsterdam on 4 Nov 2026 with exit 0 (8401 targets). The image is `r5-os-anyboot.iso`, 2156724224 bytes. The linked accelerant contains `GFX9 aperture`. The image contains `renoir_mec.bin` and `green_sardine_mec2.bin`. It was written raw to the General UDisk at 13:02 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. It has not been booted.

### Renoir maps the GFX ring in the APU system VM

The 11:03 boot submitted 914 dwords at physical `0x8107e9000` and left scratch at `0xcafedead`. `CP_RB0_RPTR` read `0x98`. Linux `gmc_v9_0` for an APU (`connected_to_cpu`, Renoir is GC 9.3) puts the GFXHUB system VM at virtual address 0. `amdgpu_gmc_init_pdb0` points that translation at `MC_VM_FB_OFFSET << 24`. The CP ring base is the virtual address. The PSP DMA keeps the physical address. This driver was giving the CP the physical address, which is outside that VM.

Before the ring is enabled, the accelerant now writes one GFXHUB page directory and one page table. Virtual page 0 of the directory covers the ring page and the fence page. The page-table base is the physical address of the directory, with the valid bit set, matching `amdgpu_gmc_pd_addr`. `VM_CONTEXT0_CNTL` enables context 0 at depth 1 and block size 0. `VM_L2_CNTL4` sets `VMC_TAP_PDE_REQUEST_PHYSICAL` and `VMC_TAP_PTE_REQUEST_PHYSICAL`. Engine 0 invalidates VMID 0. `CP_RB0_BASE` is the framebuffer offset shifted by 8. `CP_RB_RPTR_WR` is set to 0 while `RB_RPTR_WR_ENA` is set, so a leftover read pointer cannot skip the preamble. `CP_ME_CNTL` now changes only the CE, PFP, and ME halt bits. The pipe-reset and icache-invalidate bits stay as they were. A miss halts the command processor and restores the ring registers and the VM context. The scanout is not moved.

`gfxinfo` prints `rptr_before` as well as `rptr`. `jam -q @r5-anyboot` for AMD64 finished at 11:26 Europe/Amsterdam on 4 Nov 2026 with exit 0. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It has not been booted.

### Renoir submits the GFX9 clear-state preamble

The 10:23 boot left `RLC_CNTL` at `0x1` and scratch at `0xcafedead`. Linux `gfx_v9_0_cp_gfx_start` runs after the ring is programmed: preamble begin, context control `0x80000000`/`0x80000000`, each `SECT_CONTEXT` extent from `gfx9_cs_data` as `PACKET3_SET_CONTEXT_REG`, preamble end, `PACKET3_CLEAR_STATE`, `PACKET3_SET_BASE` for the CE partition (`3`, `0x8000`, `0x8000`), and `PACKET3_SET_UCONFIG_REG` of `VGT_INDEX_TYPE` with the index-type flag. The scratch packet from `gfx_v9_0_ring_test_ring` follows that stream. The table is the unmodified AMD MIT header from Linux v6.12. A walk that is not exactly 914 dwords, an extent outside `0xA000`–`0xA400`, or a ring that cannot hold the stream does not program `CP_RB0_*`. The ring is the next power of two from 4 KiB, and at most 64 KiB. `CP_RB0_CNTL` uses `order_base_2(bytes/8)` and block size two less. The write pointer is that dword count. An `sfence` runs before the write pointer moves. Scratch is still polled for 100 ms. A miss reads `CP_RB0_RPTR`, halts the command processor, and restores the ring registers, including the delay, VMID, and pointer addresses, plus `RLC_CNTL` and the clock-gating register when this boot changed them. `GRBM_SOFT_RESET` stays unreachable. MEC and golden registers are not written. The scanout is not moved.

`gfxinfo` also prints `dwords`, `rptr`, and `wptr` when the ring was programmed. `jam -q @r5-anyboot` for AMD64 finished at 10:53 Europe/Amsterdam on 4 Nov 2026 with exit 0. The image is `r5-os-anyboot.iso`, 2156724224 bytes. The accelerant contains the clear-state log lines, including `fence landed` and `ring restored` with `rptr`. It was written raw to the General UDisk at 10:57. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. That stick reached the desktop on the Ryzen 5 5625U. The PSP accepted all four files again. `RLC_CNTL` stayed `0x1`. Scratch stayed `0xcafedead`. `CP_RB0_RPTR` was `0x98` after 914 dwords were submitted, and the log says `ring restored`. `keep the boot scanout` is 1920x1080. There is no panic. The command processor did not finish the packet.

### Renoir starts the RLC after the PSP load

The 09:56 boot accepted RLC, PFP, CE, and ME through the PSP and left scratch at `0xcafedead`. `gfx_v9_0_rlc_resume` is the step Linux takes before `gfx_v9_0_cp_gfx_resume` when the firmware load type is PSP. For an APU it does not enable the GUI idle interrupt. This driver now writes `RLC_CGCG_CGLS_CTRL` to 0, clears `RLC_CNTL` bit 0, then sets that bit. `RLC_GPM_GENERAL_6` is logged. If the enable bit does not read back, or the scratch word is not `0xDEADBEEF` after 100 ms, `RLC_CNTL`, the clock-gating register, `CP_ME_CNTL`, and the ring registers are restored. The clear-state preamble from `gfx_v9_0_cp_gfx_start` is still not submitted. `GRBM_SOFT_RESET` stays unreachable.

The same boot writes `/boot/system/cache/radeon-gfx.txt`. `gfxinfo` prints that file and says whether the packet ran. `jam -q @r5-anyboot` for AMD64 finished at 10:19 Europe/Amsterdam on 4 Nov 2026 with exit 0. The image is `r5-os-anyboot.iso`, 2156724224 bytes. The accelerant contains the `RLC_CNTL` log line and the image contains `gfxinfo`. It was written raw to the General UDisk at 10:23. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. That stick booted on the Ryzen 5 5625U and reached the desktop. The PSP accepted all four files again, status `0x0`, fences `0x1` through `0x4`. `RLC_CNTL` was already `0x1` and stayed `0x1`. `RLC_GPM_GENERAL_6` read `0x59`. Scratch stayed `0xcafedead` and the log says `ring restored`. `keep the boot scanout` is 1920x1080. Tracker started. There is no panic. The command processor did not run the packet. The RLC was already enabled, so that start was not the missing step.

### Renoir GFX firmware goes through the PSP when the secure OS is up

The 09:19 stick wrote PFP, CE, and ME through the command-processor registers. Those address registers read `0xffffffff`, scratch stayed `0xcafedead`, and the ring was restored. Linux `amdgpu_ucode_get_load_type` uses the PSP for Renoir. PSP v12 is `psp_v12_0`. On an APU that already shows a picture, `MP0` `C2PMSG_81` is the secure-OS sign of life, and Linux does not load the bootloader again.

When that register is neither 0 nor `0xffffffff`, and `C2PMSG_35` is only logged, the accelerant creates the GPCOM ring: the ring GPU address goes to `C2PMSG_69` and `C2PMSG_70`, the size to `C2PMSG_71`, and `0x00020000` to `C2PMSG_64`. It does not reroute IH and it does not send mode-1 reset. Each firmware file is then `GFX_CMD_ID_LOAD_IP_FW` on that ring. The order is RLC type 8, PFP type 2, CE type 3, ME type 1. The bytes are the ucode payload from the common header, placed in the visible pool. The command buffer follows `psp_gfx_if.h`, with `buf_size` 1024 and version 1. The ring frame leaves `cmd_buf_size` at 0, as `psp_ring_cmd_submit` does. A fence in the pool must match before the next file. A timeout or a non-zero response status restores `CP_ME_CNTL` and does not program `CP_RB0_*`.

`renoir_rlc.bin` is the unmodified linux-firmware blob from 11 Oct 2025. Header 2.1, IP 9.3, ucode `0x3c`, 16896 bytes, sha256 `66f4397c2e9d19af245f9da290c8678eb1a51cc815fa7864a9383c2cd0db319d`. PFP, ME, and CE are unchanged. A sign of life that is missing still uses the direct register writes. `RLC_CNTL` is not written. `jam -q @r5-anyboot` for AMD64 finished at 09:51 Europe/Amsterdam on 4 Nov 2026 with exit 0. The accelerant links the PSP log lines, including `PSP GPCOM ring ready`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 09:56 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. That stick booted on the Ryzen 5 5625U and reached the desktop. `C2PMSG_81` was `0x1aadc7`. The GPCOM ring came ready as `0x80020000`. RLC, PFP, CE, and ME each returned status `0x0`, with fences `0x1` through `0x4`. Scratch stayed `0xcafedead` and the log says `ring restored`. `keep the boot scanout` is 1920x1080. Tracker started. There is no panic. The command processor did not run the packet. `RLC_CNTL` was not written.

### A write-only CP address register does not reject the firmware

On the 09:00 stick, `renoir_pfp.bin` was written and `CP_PFP_UCODE_ADDR` read back `0xffffffff`. Linux `gfx_v9_0_cp_gfx_load_microcode` writes the version and does not read that register. The load is now accepted when the read is the version or `0xffffffff`. Any other value is still rejected. CE and ME use the same rule. The scratch write of `0xCAFEDEAD` is still required before `CP_RB0_BASE` is programmed. A scratch value other than `0xDEADBEEF` after 100 ms still halts the command processor and restores the ring registers.

`jam -q @r5-anyboot` for AMD64 finished at 09:15 Europe/Amsterdam on 4 Nov 2026 with exit 0. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 09:19. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick booted on the Ryzen 5 5625U and reached the desktop. The syslog accepts all three files with address `0xffffffff`: PFP version `0xc3`, 21376 bytes; CE version `0x50`, 9088 bytes; ME version `0xa7`, 17280 bytes. `CP_ME_CNTL` was `0x15000000` before the load. `MC_VM_FB_OFFSET` was `0x810`, so the ring GPU address was `0x8107e9000`. Scratch stayed `0xcafedead`. The log says `ring restored`. `keep the boot scanout` is 1920x1080. Tracker started. There is no panic. The command processor did not run the packet. The address registers still do not read back.

### Renoir PFP, ME, and CE load through the CP registers

Linux `amdgpu_ucode_get_load_type` returns PSP for Renoir unless `fw_load_type` is direct. `gfx_v9_0_cp_gfx_load_microcode` is that direct path: halt `CP_ME_CNTL`, write PFP, CE, and ME, then store the header version in the address register. The files are the unmodified linux-firmware blobs `renoir_pfp.bin`, `renoir_me.bin`, and `renoir_ce.bin` from 11 Oct 2025. Each header is version 1.0 and IP 9.3. PFP is ucode `0xc3`, 21376 bytes, sha256 `0c815572cc466d6c1e608e1d36ab16f35660179e19f0feeabc448b85e8158af4`. ME is ucode `0xa7`, 17280 bytes, sha256 `38574ad3c583f3965cf1c4be84f9d0c3818511781c03733b28f07683061b1189`. CE is ucode `0x50`, 9088 bytes, sha256 `b4ea0bf5d589d82ccef52e54536221f094b66b648514b14f03cf4ad50f9a4f72`. `LICENSE.amdgpu` ships beside them. They are installed at `system/non-packaged/data/firmware/amdgpu/`.

This runs for PCI `1002:15e7`, `1636`, and `1638`. Lucienne `164c` is the green-sardine firmware and is not loaded. Raven still only allocates the ring. Navi is not programmed with GFX9 registers. RLC is not loaded. There is no PSP ring.

When the three address registers read back the header version, and scratch 0 accepts `0xCAFEDEAD`, the driver follows `gfx_v9_0_cp_gfx_resume`: `CP_RB_WPTR_DELAY`, `CP_RB_VMID`, `CP_RB0_CNTL` (size order 9, block 7), write pointer 0, read-pointer and poll addresses in the fence page, `CP_RB0_BASE` from `MC_VM_FB_OFFSET << 24` plus the ring offset, doorbell disabled, `CP_MAX_CONTEXT` 7, `CP_DEVICE_ID` 1, then the halt bits clear and the write pointer moves to 3. The clear-state preamble is not submitted. Scratch is polled for 100 ms. `0xDEADBEEF` means the packet ran. Any other value halts the command processor and restores the ring registers and `CP_ME_CNTL`. `GRBM_SOFT_RESET` stays unreachable. The scanout is not moved.

`jam -q @r5-anyboot` for AMD64 finished at 08:52 Europe/Amsterdam on 4 Nov 2026 with exit 0. The image is `r5-os-anyboot.iso`, 2156724224 bytes. The image rules copy `system/non-packaged/data/firmware/amdgpu`. It was written raw to the General UDisk at 09:00 Europe/Amsterdam on 4 Nov 2026. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick booted on the Ryzen 5 5625U and reached the desktop. The syslog says `GFX9 ring 0x7e9000+0x1000 packet 0xc0017900 fence 0x7ea000 read 0xcafedead`, then `CP_ME_CNTL 0x15000000 before load`. That value is the CE, PFP, and ME halt bits. `renoir_pfp.bin` was found: IP 9.3, version `0xc3`, 21376 bytes. The address register read back `0xffffffff`, so the load was rejected. CE and ME were not written. The log then says `firmware not running, ring left`. `keep the boot scanout` is 1920x1080. Tracker started. There is no panic. Linux does not read that address register back; `0xffffffff` can mean the register is write-only. The ring was not programmed. Scratch was not polled.

### Raven and Renoir keep a GFX9 ring in the visible pool

Linux `gfx_v9_0_cp_gfx_resume` points `CP_RB0_BASE` at a ring only after the ME, PFP, and CE microcode is loaded. `gfx_v9_0_ring_test_ring` then writes scratch register 0 from `0xCAFEDEAD` to `0xDEADBEEF` with `PACKET3_SET_UCONFIG_REG`. The register numbers come from `gc_9_0_offset.h` plus the Vega10 GC bases that `soc15_reg_base_init` uses for Raven and Renoir: segment 0 is `0x2000`, segment 1 is `0xA000`, and scratch 0 is segment 1. Navi is GFX10 and is not programmed with those registers.

On Raven and Renoir, `radeon_gpu_ring_boot` now takes 4 KiB from the visible pool for that ring and another 4 KiB for a fence word. The ring is filled with type-2 NOPs (`0x80000000`). The first three dwords are the scratch packet, index `0x40`, value `0xDEADBEEF`. The fence word is `0xCAFEDEAD`. The CPU reads both back. No `CP_RB0_*` or `CP_ME_CNTL` register is read or written, and `GRBM_SOFT_RESET` stays unreachable. Hainan, Topaz, and Navi log `GFX9 ring stays off`. DCE still returns at the old TODO before the R600 reset. The scanout is not moved. Memory beyond the BAR and GART are not programmed.

`jam -q @r5-anyboot` for AMD64 finished at 00:23 Europe/Amsterdam on 4 Nov 2026 with exit 0. The linked accelerant contains `GFX9 CP not touched, fence not submitted`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It does not contain `refind.conf`.

That stick was booted on the Ryzen 5 5625U and reached the desktop. The syslog, read afterwards on 4 Nov 2026, says `GFX9 ring 0x7e9000+0x1000 packet 0xc0017900 fence 0x7ea000 read 0xcafedead` and `GFX9 CP not touched, fence not submitted`. The pool is still `0x7e9000+0xf817000`. `keep the boot scanout` is 1920x1080. Tracker started. There is no panic. The packet header is the `SET_UCONFIG_REG` encoding. The fence word is the value the CPU stored. The command processor did not write it.

### The visible VRAM window keeps the firmware picture

Linux `gmc_v9_0_mc_init` takes the framebuffer BAR as the CPU-visible aperture. `amdgpu_gmc_get_vbios_allocations` then keeps the firmware picture out of the heap. Raven and Renoir also keep 9 MiB at the start. The Haiku driver maps that same BAR with one write-combining area, for every chip, not for one PCI ID. The boot picture is reserved when it sits inside the BAR, aligned to 4 KiB. The pool is the larger side that does not overlap it. `radeon_vram_alloc` bumps the pool and does not free it. Memory beyond the BAR is not part of the pool. GART is not programmed. DCN still does not write memory-controller registers. DCE still programs its controller and still uses the start of the BAR for the scanout.

`jam -q @r5-anyboot` for AMD64 finished at 23:46 Europe/Amsterdam on 3 Nov 2026 with exit 0. The linked `radeon_hd` contains `visible VRAM bar`. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It was written raw to the General UDisk at 23:51. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick booted on the Ryzen 5 5625U and reached the desktop. The syslog says `visible VRAM bar 0xfce0000000 size 0x10000000`, window the same size, `boot framebuffer 1920x1080 pitch 7680 at window +0x0`, `reserved 0x0+0x7e9000`, and `pool 0x7e9000+0xf817000`. The accelerant repeats the pool and `gpu base 0x0`. The kernel noted the 256 MiB area overlaps the firmware area of `0x7e9000` and kept the existing memory type. `keep the boot scanout` is 1920x1080. The command ring stays off. There is no panic. Tracker started. The pool was not allocated.

### The radeon_hd device table names the display engine

The block Haiku had inside `#if 0` for R1/beta5 is compiled again. Each row now carries the display class and version from Linux `amdgpu_drv.c` (v6.12) and from `vi.c` plus `dc_resource.c` `resource_parse_asic_id` (v6.12, with the v5.15 IP blocks where the parser is less specific). DCE rows keep that version in `dceMajor` and `dceMinor`. DCN rows and Topaz store the version beside that, and leave `dceMajor` at 0, so the DCE register paths do not see DCN as DCE 12 or 13. `1002:15e7` is Renoir, DCN 2.1, the same class Linux gives `1636`, `1638` and `164c`. Vega M is DCE 11.2, not DCE 12. Vega 20 is DCE 12.1. Stoney uses the DCE 11.0 block. `0x73cf` is not in those Linux tables and is gone. `0x163f` is not in the v6.12 table. Hainan and Topaz do not run the DCE setup.

The same table now has every explicit PCI ID from that Linux v6.12 list. That adds the missing Tahiti, Oland, Verde, Hainan, Polaris 10, Arcturus, Navi 12, Sienna Cichlid, Yellow Carp, Navy Flounder, Dimgrey Cavefish, Aldebaran, Cyan Skillfish, and Beige Goby IDs. Arcturus and Aldebaran have no display. Their chipset id is Navi so the DCE path stays off. Linux v6.12 has no per-card list after Beige Goby. A later AMD VGA, display-other, or processing-accelerator device takes one fallback row, the Linux `CHIP_IP_DISCOVERY` match. The real PCI ID is stored. The display version stays 0, and the log says it is unknown. The GPU IP-discovery table is not read. A match still only claims the device.

`jam -q @r5-anyboot` for AMD64 finished at 22:26 Europe/Amsterdam on 3 Nov 2026 with exit 0. The linked `radeon_hd` contains `Radeon Renoir (Barcelo)`, `Radeon IP discovery`, `Radeon Yellow Carp`, and `Radeon Beige Goby`. The image is `r5-os-anyboot.iso`, 2185232384 bytes. It was written raw to the General UDisk at 22:43. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened. That stick was booted on the Ryzen 5 5625U and stopped in the kernel debugger. `/system/var/log/syslog` was never created, so the panic text is not on the volume. The image also started from the rEFInd menu, because the build branch included those commits. Those commits are not on this branch, and `main` never had them.

`jam -q @r5-anyboot` for AMD64 finished at 23:14 Europe/Amsterdam on 3 Nov 2026 with exit 0. The Haiku EFI video and HPET sources in the build tree were put back, and the rEFInd image rules and installer copy were taken out. The image is `r5-os-anyboot.iso`, 2156724224 bytes. It does not contain `refind.conf`, `refind_x64`, or “Boot R5 from HAIKU ESP”. Mesa 25.3.6 is on the image. It was written raw to the General UDisk at 23:18. Read-back matched at the start, at 6291456, and at 2153775104. Disk 0, the NVMe, was not opened.

That stick booted on the Ryzen 5 5625U and reached the desktop. The syslog names `GPU(0) Radeon Renoir (Barcelo)`, revision `c2`, `Radeon Renoir 1002:15E7`, and `DCN, boot framebuffer 1920x1080 pitch 7680`. The accelerant logs `leave the firmware display setup`, `skip AUX`, `command ring stays off`, `keep the boot scanout` for `1920x1080`, and `skip encoder DPMS`. eDP is attached. The old `radeon` driver found no supported device. There is no panic. Tracker started. This is the firmware scanout with the new table. It is not a DCN modeset. No other card in the table was booted.

### The remaining Haiku sources are copied from the R5 tree

`scripts/apply-rest-native.py` copies 55 files from `distro/rest` before branding runs. That covers the network stack and net server, package `Volume.cpp`, input, DriveSetup, accounts and the locked preferences, LaunchDaemon, Installer, the boot prompt, About, Deskbar `icons.rdef`, the home desktop resource, `ServerApp.cpp`, the public interface and locale headers that the desktop copy left behind, and `runtime_loader.cpp`. Each file keeps its original copyright and license. Haiku files stay MIT, with NewOS where that notice was already there. `getaddrinfo.c` and `ping6.c` stay under the WIDE and Berkeley BSD terms. The R5OS edits are GPL-3.0-or-later `SPDX-Snippet` blocks. A program that ships both follows GPL-3.0-or-later, and the original notices stay.

`runtime_loader` drops a trailing CR from a script's interpreter path. The net server logs `R5 net_server: NetServer from distro`. The older apply scripts still run after the copy. When a file already contains the R5OS license sentence they leave that file alone, so a second pass does not insert the same lines again. Jamfile lines, package lists, and launch jobs stay in those scripts. The AirPlay tray, NetworkStatus, and VirtualBox clipboard jobs are recognized by name, because the login script removes `on initial_volumes_mounted` from the desktop target and the old anchor text is then gone. Branding still rewrites the Deskbar Jamfile link line. QEMU's `virtio_block` package line, the settings font hooks, the VirtualBox guest hooks, and the `libr5icons` / `libr5ui` SubIncludes stay as they are.

`jam -q` for AMD64, from `generated.x86_64`, finished with exit 0 on 3 Nov 2026. It linked `libroot.so`, `runtime_loader`, `libnetwork.so`, `libbnetapi.so`, `libbe.so`, `net_server`, `package_daemon`, `input_server`, `launch_daemon`, `app_server`, DriveSetup, AboutSystem, Installer, NetworkStatus, FirstBootPrompt, Time, Screen, Network, Printers, Repositories, VirtualMemory, `ping`, `ipv6`, `icmp6`, `udp`, `stack`, and IPP. Compiler warnings in `getaddrinfo.c`, Installer, `ping6.c`, and the IPv6 multicast header were already in those files. No new anyboot image was built. QEMU was not run. The stick was not written.

### Interface, Tracker, and Deskbar sources are copied from the R5 tree

The string patches in `apply-ui-bugs.py`, `apply-tracker-desktop.py`, `apply-deskbar-bugs.py`, `apply-darkmode.py`, `apply-ui-shell.py`, and `apply-desktop-enh.py` are retired. `scripts/apply-desktop-native.py` copies those files from `distro/desktop` before branding runs. `distro/ui` stays the `libr5ui` sources. Each file keeps its Haiku copyright and MIT notice. The R5OS edits are GPL-3.0-or-later `SPDX-Snippet` blocks. Deskbar logs `R5 deskbar: BarApp from distro`. Tracker logs `R5 tracker: Tracker from distro`. The control look logs `R5 interface: HaikuControlLook from distro`. Branding, login, and About still adjust the product name and the session menu on top of that copy, and those hooks stay idempotent. `MenuField.cpp` stays with the media copy. The Deskbar Jamfile link line stays in the branding script. `icons.rdef` and `ServerApp.cpp` were copied later from `distro/rest`. `scripts/tracker-deskbar-more.diff` is no longer applied.

### Kernel TSC guards and the microcode match are copied from the R5 tree

`arch_timer.cpp` and `arch_cpu.cpp` now live under `distro/kernel`. `scripts/apply-kernel-bugs.py` copies them. It no longer string-patches those files. The Haiku MIT header and the NewOS notice stay. The TSC zero checks, the `r5_pstate` hook, and the `installed_cpu` comparison are GPL-3.0-or-later snippets. Boot logs `R5 kernel: arch_timer TSC guards active` and `R5 kernel: arch_cpu from distro, microcode match uses installed_cpu`. Jamfile lines and the CPU-log launch job are still idempotent inserts. Graphics, media, USB audio, and MIDI were already copies. Interface, Tracker, and Deskbar were copied in the same change. The later `distro/rest` copy covers IPv6, About, branding text, accounts, and the other remaining Haiku sources.

### Barcelo keeps the picture, and family 19h microcode is on the boot volume

`CONFIG_MEMSIZE` on this APU is not a size. The `0MB` line is gone. The log now says the register is unused and the PCI aperture stays the upper bound. The mapped picture is still the boot framebuffer. DPMS on DCN returns before `encoder_dpms_set`, so the DIG table and link training do not run. The command ring returns before the R600 reset and stays off.

The bootloader already looks for `system/non-packaged/data/firmware/amd-ucode/microcode_amd_fam19h.bin`. That file is the unmodified linux-firmware container, sha256 `c614d6db8056c5c67a9189b225124127d56990a190305bcb3927d50e132de7dd`, with AMD's license beside it. Family `0x19` model `0x50` stepping `0` is CPUID `0x00a50f00`, equivalence id `0xa500`, patch `0x0a500012`. The BIOS on this machine is patch `0x0a50000f`, which is older. The equivalence match used to compare the CPUID with the 16-bit equivalence id, which never hits. It now compares `installed_cpu`. P-state MSRs are not written.

`jam -q @r5-anyboot` for AMD64 finished at 18:07 Europe/Amsterdam on 3 Nov 2026 with exit 0 (1415 targets). The accelerant contains `skip encoder DPMS` and `command ring stays off`. The kernel driver contains `CONFIG_MEMSIZE unused`. The anyboot image contains `microcode_amd_fam19h.bin` and `LICENSE.amd-ucode`. The image was written raw to the General UDisk at 18:11. Read-back matched at the start, the BeFS start, and the EFI start. Disk 0, the NVMe, was not opened.

That stick booted on the Ryzen 5 5625U and reached the desktop. The BeFS syslog contains `CONFIG_MEMSIZE unused, PCI aperture 262144 KiB`, `leave the firmware display setup`, `command ring stays off`, `keep the boot scanout`, and `skip encoder DPMS`. There is no `found 0MB`, no `encoder_dpms_set`, and no `dp_link_train`. First login ran. The bootloader logged `microcode file read in memory`. The kernel logged `found equiv cpu: a500`, then `incorrect alignment` on the Barcelo section, then `no fw update found for this cpu`. `patch_level` stayed `0xa50000f` on every CPU. The patch `0x0a500012` was not applied. No P-state MSR was written. The logs are on `/boot/home` (`no FAT mount`, pass 2).

### DCN init leaves the firmware picture in place

The image that left `1002:15e7` unclaimed reached the desktop. Claiming Barcelo and then running the DCE encoder init and `SetDCEClock` stops that picture. `radeon_hd` claims the id again. On `RADEON_RAVEN` and later the accelerant does not run encoder init, the display-clock command, DisplayPort link setup, AUX, DPMS, or a modeset. The kernel maps the bootloader framebuffer and keeps its pitch. The parser and the other DCN skips stay. This is not a DCN modeset. The panel is the firmware scanout with `radeon_hd` drawing into that buffer.

`jam -q @r5-anyboot` for AMD64 finished at 17:34 Europe/Amsterdam on 3 Nov 2026 with exit 0 (1185 targets). The linked driver contains `Radeon Vega Barcelo` and `boot framebuffer`. The accelerant contains `leave the firmware display setup`, `keep the boot scanout`, `leave the display clock alone`, and `skip AUX`. The image was written raw to the General UDisk at 17:38. Read-back matched at the start, the BeFS start, and the EFI start. Disk 0, the NVMe, was not opened.

That stick booted on the Ryzen 5 5625U and reached the desktop. The BeFS syslog names `Radeon Vega Barcelo` revision `c2`, ignores `CONFIG_MEMSIZE` `4294967295`, and maps the boot framebuffer at `1920x1080`, pitch `7680`, physical `0xfce0000000`. The display-object table is 1.4 with 6 paths. eDP is attached from the boot EDID. The log contains `leave the firmware display setup`, `skip AUX`, `keep the boot scanout` for `1920x1080`, and `bytes_per_row` `7680`. First login ran. There is no panic. After the DPMS skip, `encoder_dpms_set` still called the DIG transmitter; the picture stayed. No MSR was written. The logs are on `/boot/home` (`no FAT mount`, pass 2). This is the firmware scanout. It is not a DCN modeset and not acceleration.

### Barcelo stays off the claimed display list

The boots at 16:14 and 16:47 Europe/Amsterdam stayed on the rocket. The flushed log ends at first-boot package 131. The next lines on a QEMU serial log of the same image are the graphics probes, then `ps2: keyboard found`, and then silence. QEMU has no `1002:15e7`. `ati-vga` is a Rage 128 or an RV100, so that device id cannot be substituted.

`1002:15e7` is compiled out of the live `radeon_hd` table. The parser and the DCN skip stay in the tree and do not run. `framebuffer` keeps the boot picture. Each post-install script logs its package name before `system()` and the result after. The file is `distro/package/.../CommitTransactionHandler.cpp`: Haiku keeps the MIT header, and those two lines are GPL-3.0-or-later snippets.

`jam -q @r5-anyboot` for AMD64 finished at 17:14 Europe/Amsterdam on 3 Nov 2026 with exit 0 (1170 targets). The linked `radeon_hd` does not contain `Radeon Vega Barcelo`. `package_daemon` contains `script starting` and `script finished`. The image was written raw to the General UDisk at 17:18. Read-back matched at the start, the BeFS start, and the EFI start. Disk 0, the NVMe, was not opened. This image has not been booted.

### Boot logs stay on the BeFS volume

The stick written at 15:59 Europe/Amsterdam reached the kernel and then stayed on the rocket. The first start listed all 132 first-boot packages and never wrote `activated-packages`. The second start, at 16:23, skipped that round and the flushed syslog ends when both package volumes are called consistent, about 23 seconds in. There is no panic, no `app_server`, and no `radeon_hd`. The logger had mounted the stick's own EFI partition, on the same USB disk as the boot volume, and did not write again.

The boot logs are now only `/boot/home/r5-cpu.txt`, `/boot/home/r5-hardware.txt`, and `/boot/home/r5-syslog.txt`. Nothing is mounted from `/dev/disk`. `tools/r5-befs/r5_befs.py` reads them from the BeFS partition. No MSR is written.

`jam -q @r5-anyboot` for AMD64 finished at 16:38 Europe/Amsterdam on 3 Nov 2026 with exit 0 (1168 targets). The linked `r5_pstate_log` contains `no FAT mount`. This image has not been booted.

### Radeon sources live in the R5 tree

`connector.cpp`, `display.cpp`, `gpu.cpp`, `pll.cpp`, `driver.cpp` and `radeon_hd.cpp` are copied from `distro/graphics` by `scripts/apply-radeon.py`. They replace the same paths in the Haiku tree. The string patches that used to edit those files are gone. Each file keeps the Haiku copyright and the MIT license on the original work. R5OS changes sit in `SPDX-Snippet` blocks and are GPL-3.0-or-later. A build that ships both follows GPL-3.0-or-later, and the MIT notices stay. `r5_pstate_log.cpp` was already an R5OS file under GPL-3.0-or-later in `distro/cpu`.

Content revision 4 is the 16-byte path: the encoder-record offset is at byte 8. Content revision 5 reserves bytes 6 through 11, so that offset stays 0. The device tag is at byte 12 in both. The syslog line is `R5 radeon_hd display object table` plus `encoder record`.

DCN, from `RADEON_RAVEN` onward, does not program the DCE 5 CRTC registers. `display_crtc_fb_set` and `display_dce45_crtc_load_lut` return after `R5 radeon_hd: DCN, skip DCE 5 CRTC registers`. The memory-controller skip logs `R5 radeon_hd: DCN, leave the boot memory aperture alone`. The stick written at 15:35 Europe/Amsterdam does not have this CRTC skip.

`jam -q @r5-anyboot` for AMD64 finished at 15:59 Europe/Amsterdam on 3 Nov 2026 with exit 0 (1176 targets). The linked accelerant contains `R5 radeon_hd`, `display object table`, `SetDCEClock 2.2`, `skip DCE 5 CRTC`, `boot memory aperture`, and `boot VESA EDID`. The kernel driver contains `R5 radeon_hd: CONFIG_MEMSIZE` and `Radeon Vega Barcelo`. That image has not been written to the stick and has not been booted. AtomBIOS `SetCRTC_Timing` is still called; a bad modeset can still black the panel.

### Barcelo reads the display-object table

The 14:48 boot opened `radeon_hd` and then closed it. That AtomBIOS is master format 2. The driver read the GPIO pin table at the old index 11, which on this format is `vram_usagebyfirmware`, and it looked for `GPIO_I2C_Info` at index 10, which is empty. `connector_probe` treated the flat display-object list as the old object header, counted 235 paths, and stopped on `usSize`. `SetDCEClock` reported table 2.2 and returned before running. `CONFIG_MEMSIZE` read `0x3fffff` and was printed as 4194303 MB. The mapped window was still the 256 MB PCI BAR. `detect_displays` then logged 0 monitors. `framebuffer` kept the picture.

The object table at revision 1.4 or newer is now a flat path list: connector object, encoder object, record offsets, and the device tag. On master format 2 the GPIO pin LUT is index 12, and a missing `GPIO_I2C_Info` table is not fatal because I2C is on the path records. `SetDCEClock` 2.2 uses the 2.1 argument block and runs. A memory-size register of 0 or above 65536 MB is ignored, and both the map and the reported size follow the PCI BAR. An eDP connector whose AUX EDID fails uses the VESA EDID the boot loader already stored. Raven does not program the Evergreen memory-controller registers; those writes halt a controller this driver does not own. This is still not a claim that the panel is driven. A bad modeset can leave the picture black until the next boot. `framebuffer` is the fallback when the accelerant closes.

The logger that skips `/dev/disk/nvme` is in this image. `jam -q @r5-anyboot` for AMD64 finished at 15:35 Europe/Amsterdam on 3 Nov 2026 with exit 0 (1191 targets, `r5-os-anyboot.iso`, 2156724224 bytes, BeFS type `0xeb` plus EFI type `0xef`). The linked accelerant contains `display object table`, `boot VESA EDID`, and `leaving the boot memory`. The kernel driver still contains `Radeon Vega Barcelo`. `r5_pstate_log` contains `/nvme/`. The image was written raw to the General UDisk. The first pass stopped at the EFI partition because that volume was still mounted; the body matched the image, and a second pass locked that volume and wrote the tail. A read-back of the first EFI sector matched. Disk 0, the NVMe, was not opened. The four-byte Windows disk signature in the MBR is not the image's. This image has not been booted.

### The 14:48 stick booted, and the log landed on the Windows EFI partition

That image reached the desktop and then shut down. `radeon_hd` claimed `1002:15e7` as `Radeon Vega Barcelo`, mapped 256 MB after reading the video memory size as 4194303 MB, probed 235 connector paths, rejected path 0 for `usSize`, and logged `0 attached monitors`. It closed. `framebuffer` then kept the firmware buffer. The desktop the user saw was that framebuffer. `package_daemon` ran first-boot processing for 132 packages; the last of those lines is at about 8.9 seconds into the boot. The syslog ends in the first-login script, then shutdown. No panic. No MSR write. The idle sample was P-state 1 (1800 MHz) and the busy sample was P-state 0 (2300 MHz).

`r5_pstate_log` wrote `r5-cpu.txt`, `r5-hardware.txt`, and `r5-syslog.txt` on `/dev/disk/nvme/0/0`. That is the Windows EFI partition, because the chooser preferred FAT32 over the FAT12 USB partition. `E:` stayed empty. The three files were removed from that EFI root afterwards. The chooser now skips `/dev/disk/nvme` and keeps the smallest other FAT volume. The stick still has the old chooser.

### Barcelo in radeon_hd, and the FAT log looks again

`1002:15e7` from the 5625U boot is now in the compiled `radeon_hd` table: DCE 12.0, `RADEON_RAVEN`, `CHIP_APU`. Picasso `1002:15d8` is the same Raven path, but that entry (and the rest of Vega and Navi) sits inside `#if 0 /* disabled for R1/beta5 */`, so a line next to it is not compiled. `15e7` is placed after that `#endif`. It has not been modeset on the machine. If `init_driver` claims the device and the registers do not match, the panel can go black; `framebuffer` remains the fallback when the id is absent.

`r5_pstate_log` stopped after about ten seconds when `/dev/disk` had no FAT boot sector, and the later copies then did nothing. The anyboot EFI partition is FAT12, which the check already accepted. The scan now waits longer, runs again after 15 seconds, 45 seconds and two minutes, counts the nodes it opened, and also accepts a BIOS parameter block whose type string is blank. Still no MSR write.

`tools/r5-befs/r5_befs.py` reads a BeFS image or an anyboot MBR from Windows. On the partition copied from this stick it listed `home` and exported `system/var/log/syslog` (155889 bytes, including `device 15e7`). There is no separate toolchain repository; the reader lives in this tree. No C++ or C# compiler is installed here, so the tool is Python 3. Attributes and a Dokan or WinFsp drive letter are not implemented.

`jam -q @r5-anyboot` for AMD64 finished at 14:44 Europe/Amsterdam on 3 Nov 2026 with exit 0 (`r5-os-anyboot.iso`, 2156724224 bytes, BeFS type `0xeb` plus EFI type `0xef`). The linked `radeon_hd` contains the string `Radeon Vega Barcelo`. `r5_pstate_log` contains the node/opened/signature note. That image was written raw to the General UDisk. Disk 0, the NVMe, was not opened. This image has not been booted.

### USB boot of the Ryzen 5 5625U

The image written at 13:53 Europe/Amsterdam booted. The kernel enumerated 12 CPUs (0–11), family `0x19` model `0x50`, `AMD Ryzen 5 5625U with Radeon Graphics`. The VGA PCI id is `1002:15e7` (Barcelo, revision `c2`, subsystem `103c:8a31`). `radeon_hd` does not list that id. The old `radeon` driver found no supported device. `framebuffer` loaded and kept the firmware buffer, 1920×1080 at 32-bit. HDMI audio is `1002:1637`. The CPU log shows P-states at 2300, 1800 and 1600 MHz and `no msr write`. `r5_pstate_log` did not see a FAT boot sector, so `r5-cpu.txt` and the syslog are on the BeFS volume (`home/r5-cpu.txt`, `system/var/log/syslog`, 155889 bytes). The EFI partition stayed without those files. The syslog ends in the first-login script, then the machine shut down. No panic in this boot.

### CPU log read panics on hardware

Reading `/dev/misc/r5_pstate` copied the report into the caller’s buffer with `memcpy`. That buffer is a user address. On the Ryzen 5 5625U SMAP is on, so the kernel panicked: `SMAP violation user-mapped address … touched from kernel`, thread `r5_pstate_log`, in `r5_pstate_read` (the debugger named the callee `memcmp`). The same read on QEMU did not fault. The copy now goes through `user_memcpy`. No MSR is written. The photo of that panic is from the USB boot on 3 Nov 2026. `jam -q @r5-anyboot` afterwards exited 0, and that image was written raw to the same General UDisk at 13:53 Europe/Amsterdam. That image is the one that booted, described above.

### USB boot log on the EFI partition

The anyboot image has a small FAT partition named HAIKU ESP. Windows can open that partition from the stick. `r5_pstate_log` writes three files there: `r5-cpu.txt`, `r5-hardware.txt` (`listdev`, the kernel's loaded images, and syslog lines that mention PCI, VGA, radeon, VESA, framebuffer, or a panic), and `r5-syslog.txt` (the newest 1.2 MB of `/var/log/syslog`). The first copy runs when the boot volume is mounted. Later copies replace those files after 15 seconds, then 45 seconds, then two minutes, so a hang during display setup still leaves the previous copy. The rest of the log stays on the BeFS system partition. The EFI partition is about 2.8 MB, which is why the full syslog is not copied. No MSR is written. `jam -q @r5-anyboot` for AMD64 finished on 3 Nov 2026 with exit 0 (`r5-os-anyboot.iso`, 2.01 GiB, BeFS plus a 2.8 MB EFI partition). That image was written raw to the General UDisk at 13:33 Europe/Amsterdam. Disk 0, the NVMe, was not opened. Booting that image on the Ryzen 5 5625U reached the kernel and then panicked in `r5_pstate_log`, as the section above describes.

### Address bar while a background tab loads

Middle-clicking a link opens a tab without selecting it. `LoadNegotiating` still wrote that URL into the address bar, then a later update put the current page back (Haiku #20319). The bar looked as if the open page was being replaced. A background tab now keeps its URL on that tab and leaves the address bar alone. The selected tab still shows the URL it is requesting.

`jam -q WebPositive` for AMD64 finished at 12:51 Europe/Amsterdam on 3 Nov 2026 with exit 0 (`BrowserWindow.o` compiled, `WebPositive` linked). Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. A background tab was not opened.

### TextSearch long grep line

`Grepper` read a grep line into a buffer twice as long as the file-name array, then copied the name with an unbounded scanset. A long line, or the rest of a line that `fgets` had split, wrote past that array and TextSearch crashed (Haiku #14485). The scanset is now limited to the array, leaving room for the terminating NUL. A line that does not fit is skipped. A normal `file:line:text` result is unchanged.

`jam -q TextSearch` for AMD64 finished at 12:52 Europe/Amsterdam on 3 Nov 2026 with exit 0 (`Grepper.o` compiled, `TextSearch` linked). Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. No search was run.

### Terminal wheel while the mouse is reported

On the alternate screen the wheel always became arrow or page keys. A program that had turned mouse reporting on, such as a full-screen editor, then saw keys instead of a scroll (Haiku #20409). When any mouse-report mode is set, the wheel is written as X10 button 64 or 65, or as the same button in the extended `CSI <` form. One event is sent per wheel step. Arrow and page keys stay in use on the alternate screen when mouse reporting is off. The ordinary scrollback, outside the alternate screen, is unchanged.

`jam -q Terminal` for AMD64 finished at 12:44 Europe/Amsterdam on 3 Nov 2026 with exit 0 (`TermView.o` compiled, `Terminal` linked). Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. The wheel was not turned in a running Terminal.

### POP3 flush on a closed connection

`POP3Protocol::SendCommand` drained leftover bytes before the next command. A closed socket stays readable, and `Read` then returns 0. The loop printed `Had to flush 0 bytes` and never left, so `mail_daemon` used a core (Haiku #14441). A zero-length read now ends the flush. A real leftover is still discarded, and a read error is still returned. The command is sent after that, and a closed connection fails on the write.

`jam -q POP3` for AMD64 finished at 12:40 Europe/Amsterdam on 3 Nov 2026 with exit 0 (4 targets updated, `POP3` linked). Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. No POP3 server was contacted.

### Mail dates marked UT

A date ending in `UT` (Universal Time, from RFC 822) was left for `parsedate()`, which accepts `GMT` and `UTC` and rejects `UT`. The parse failed and the mail's When time became the moment it was fetched (Haiku #20440). A trailing `UT` is rewritten to `GMT` before that parse. Numeric zones and `UTC` are unchanged.

### HTTP request timeout

`SetTimeout()` on a `BHttpRequest` did nothing when it was called before the socket existed. The value is kept and applied when the socket is created, before connect (Haiku #20436). A timeout set after the socket exists still reaches that socket. Gopher already had a socket at construction, so that path is unchanged.

### Mixer with no sound device

A late-buffer notice and the buffer-group request both read the mixer output without checking it. With no sound device that pointer is empty, and `media_addon_server` crashes (Haiku #20425). Both calls return when there is no output. A connected mixer is unchanged.

### Virtual surround

Multi-channel sound folded into headphones with one gain per ear. The MIT KEMAR recordings are research-only, so they are not shipped. The mixer uses a synthetic delay instead: Woodworth's formula, head radius 8.75 cm, 12 dB quieter at the far ear when the speaker is at 90 degrees. Center and the low-frequency channel stay in both ears. The added wait is the far-ear delay, under a millisecond at 48 kHz. The history is 128 samples, which holds that delay at 96 kHz.

Audio settings has Virtual surround: Off or On. It is written as `virtual_surround` in the user settings file `kernel/drivers/virtual_surround`. Restart media services to apply it. The mix runs only for a stereo output. A 5.1 or 7.1 device still gets its own channels. USB and HDA playback are unchanged. With the switch off, the mixer sums channels the way it did before. `virtual_surround_test` is a host program and is not in the image. On this machine it printed `virtual_surround_test: all checks passed`. `jam -q mixer.media_addon Media` for AMD64 finished at 12:09 Europe/Amsterdam on 3 Nov 2026 and linked `mixer.media_addon` and Media. A following `jam -q` of those targets exited 0 (13707 targets). Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. The image was not rebuilt. The menu was not opened, and no headphone mix was heard.

### HDMI output name

Enabling an HDMI converter cleared the analog format mask, and a pin with no device type was shown as `HDMI <NULL>` (Haiku #20171). The missing name is now `HDMI`. Analog converters keep their format. An HDMI converter is added only when it supports that format. The digital converter is enabled with DigEn. A GPU modesetting driver still has to carry the samples onto the link. The VirtualBox codec has no HDMI pin, so this was not heard.

`jam -q hda` for AMD64 finished at 11:57 Europe/Amsterdam on 3 Nov 2026 with exit 0 (9333 targets). Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. The image was not rebuilt.

### USB audio host stability

Playing USB audio on EHCI took the isochronous lock in the finish thread and again in the callback that queues the next packet. That is the double-lock KDL on startup (Haiku #19472). The same wait is the hang that looks like the system is looping (Haiku #18842). The callback now runs without that lock. An error completion waits 1 ms before the next packet is queued, so a stream that fails immediately does not spin.

A babble, stall, or transaction error that halts an xHCI endpoint left the endpoint halted. Later transfers on that controller then stopped (Haiku #20149). The finish thread resets a halted endpoint, clears its ring, and cancels the packets that were still queued before the audio callback queues another one. A soft error does not clear the ring.

`SetAltInterface` indexed the alternate array with `bAlternateSetting` and the interface array with `bInterfaceNumber`. A webcam audio interface walks off that array (Haiku #20323). The call now uses the alternate pointer it was given, after checking that the pointer belongs to the current configuration.

Haiku #18994 is the KDL once that callback or that alternate switch runs. The unlock and the pointer check are the changes for it. The earlier unplug stop flag is unchanged.

Jam `usb`, `usb_audio`, `<usb>ehci`, and `<usb>xhci` for AMD64 finished at 11:39 Europe/Amsterdam on 3 Nov 2026 with exit 0. `ehci.cpp` still prints the existing packed-member warning in `CreateDescriptor`. Jam `@r5-raw` started at 11:42 the same morning and finished with exit 0 (1828 targets). The header date is "3 Nov 2026, 11:42". The Mesa 25 check passed. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox was started from that image. The USB audio test on that guest was reported good at 11:49 Europe/Amsterdam. PR #4072 was merged at 11:48 Europe/Amsterdam (`5440628b7e`).

### USB audio channels, UAC2, and layout

USB audio stayed on class 1.0 and ignored an alternate with more than two channels. A class 2.0 device was not recognised. 24-bit in three bytes already worked. 24-bit in four bytes was the ordinary 32-bit store, which is left-justified if the device reads the top 24 bits.

The control header version `0x0200` is accepted. The streaming descriptor is read with the class 2.0 layout: channel count from the general descriptor, subslot size and bit depth from the format descriptor, and 44.1, 48, 88.2, and 96 kHz on the clock source. Class 1.0 still sets the rate with the three-byte endpoint request. A continuous class 1.0 range now includes the usual rates inside that range, not only the two ends. One to sixteen channels are allowed. The UAC1 input-terminal channel bitmap is a 16-bit field, so side channels are not dropped.

Media, Audio settings, has USB layout: Automatic, Stereo (2.0), 4 channels, 5.1, or 7.1. It is written as `channel_layout` in the user `usb_audio` settings, next to the latency keys. Restart media services to apply it. Automatic keeps the widest alternate. Another choice picks that count, or the nearest one the device has. `multi_audio` sizes each bus from its own channel count, so a stereo pair and a centre channel do not share one buffer size. The system mixer stays stereo.

Unplug sets a stop flag, cancels isochronous transfers, and waits until the callback has left. An isochronous packet is not larger than the endpoint's maximum. The EHCI locks in Haiku #18842, #18994, #18995, and #19472 are unchanged. `usb_audiotest` checks the descriptor bytes and both 24-bit layouts. No USB audio device was attached.

The first jam of `usb_audio` failed: the channel-count comparison mixed `size_t` and `int32`, and `layout_channels` was called before it was defined. Both are fixed. A host compile of `usb_audiotest` printed `all checks passed`. Jam `usb_audio` and `usb_audiotest` for AMD64 finished at 11:15 Europe/Amsterdam on 3 Nov 2026 with exit 0. Jam `@r5-raw` started at 11:17 the same morning and finished with exit 0 (1802 targets). The header date is "3 Nov 2026, 11:17". The Mesa 25 check passed. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. `haiku.hpkg` contains `usb_audiotest` and `usb_audio`. VirtualBox was started from this image. On that guest, `usb_audiotest` printed `ok` for every check and `usb_audiotest: all checks passed` (reported at 11:26 Europe/Amsterdam). Media, Audio settings, showed USB layout: Automatic, Stereo (2.0), 4 channels, 5.1, and 7.1, with Automatic selected. The output device on that screen was HD Audio, the VirtualBox codec. No USB audio device was attached.

### MIDI 2.0 per-note controllers, profiles, and discovery

Property Exchange could ask for a resource. A per-note controller, a profile, and endpoint discovery still had no calls.

`midi2_encode_per_note` writes one type 0x4 packet. Status 0 is registered and status 1 is assignable. The value stays in `Data`. `NoteOn` and `ControlChange` are not called. Profile inquiry, Set On, Set Off, the enabled and disabled reports, and Profile Specific Data are MIDI-CI System Exclusive on the existing SysEx7 path. One catalog profile starts disabled and can be enabled. UMP Stream messages answer Endpoint Discovery and Function Block Discovery for one local endpoint. The USB MIDI 2.0 interface descriptor (including the alternate setting), the class-specific header, and a Group Terminal Block can be written and parsed without a device. `usb_midi` treats protocol `0x11` in that block as a MIDI 2.0 endpoint. MIDI 1.0 and the earlier MIDI 2.0 calls are unchanged.

`midi2test` covers each of those paths, including a sprayed per-note controller, a sprayed discovery packet, and a sprayed Profile Enabled report. No USB MIDI device was attached. Jam `usb_midi` for AMD64 finished at 10:43 Europe/Amsterdam on 3 Nov 2026 with exit 0. Jam `midi2test` (and `libmidi2.so`) finished at 10:46 the same morning with exit 0. Jam `@r5-raw` started at 10:49 and finished at 10:53 with exit 0 (1792 targets). The Mesa 25 check passed. On the VirtualBox guest from that image, `midi2test` printed `all checks passed` (reported at 10:55 Europe/Amsterdam). That run includes the new helper lines, the sprayed per-note controller, the sprayed discovery packet, the sprayed Profile Enabled report, and the earlier MIDI 1.0, UMP, SysEx, and Property Exchange checks. There is no `midi2info` command. That run does not exercise a USB MIDI device.

### MIDI-CI Property Exchange

An application could send notes and SysEx, and could not ask a device for DeviceInfo or subscribe to a resource.

Property Exchange is MIDI-CI System Exclusive in `libmidi2`, not a second kit. `midi2_pe_encode` and `midi2_pe_decode` read the request, reply, subscription, notify, and capabilities layouts. A body that does not fit one SysEx is split into chunks and joined again with `midi2_pe_assembly_add`. The bytes between `F0` and `F7` are the existing SysEx7 packets. `midi2_pe_catalog` answers `ResourceList`, `DeviceInfo`, `ChannelList`, `ProgramList`, and `CtrlMapList` (also accepted as `ControllerMap`). `ChannelList` and `CtrlMapList` can be subscribed and then reported with a `full` command. Notify status 144 has no reply. MIDI 1.0 and the earlier UMP calls are unchanged.

`midi2test` covers encode, decode, two chunks, the catalog replies, subscribe, unsubscribe, and one sprayed Get inquiry. No USB MIDI device was attached. Jam `@r5-raw` for AMD64 started at 10:19 Europe/Amsterdam on 3 Nov 2026 and finished with exit 0 (1178 targets). The Mesa 25 check passed. On the VirtualBox guest from that image, `midi2test` printed `all checks passed` (reported at 10:26 Europe/Amsterdam), including the Property Exchange lines and the earlier MIDI 1.0 and UMP checks. That run does not exercise a USB MIDI device.

### MIDI 2.0 receive and SysEx

Incoming UMP was reduced to 7-bit MIDI 1.0 before an application could read it, and `usb_midi` returned `B_UNSUPPORTED` for SysEx on a version 2.0 endpoint.

`Data` still receives the Universal MIDI Packet. `midi2_note_velocity` and `midi2_control_value` read the 16-bit velocity and the 32-bit controller from that packet. The existing `NoteOn`, `NoteOff`, and `ControlChange` hooks still get the top 7 bits. A type 0x3 packet is SysEx7: `midi2_encode_sysex` builds it, and the consumer calls `SystemExclusive` once the message is complete. `SpraySystemExclusive` is unchanged.

`midi_server` reads `B_MIDI_READ_UMP` and sprays that packet, so a USB producer delivers the same `Data` buffer. A driver that does not implement the ioctl is still read as MIDI 1.0 bytes. `read()` on `usb_midi` expands the same queue to those bytes. A SysEx `write()` on a version 2.0 endpoint becomes type 0x3. On a version 1.0 endpoint a type 0x3 packet becomes CIN SysEx.

`midi2test` checks the helpers, the values left in `Data`, MIDI 1.0 SysEx, and an eight-byte SysEx7. No USB MIDI device was attached. Jam `@r5-raw` for AMD64 started at 09:54 Europe/Amsterdam on 3 Nov 2026 and finished with exit 0 (8287 targets). The Mesa 25 check passed. On the VirtualBox guest from that image, `midi2test` printed `all checks passed` (reported at 10:03 Europe/Amsterdam). That run does not exercise a USB MIDI device.

### Latency choice was still permission denied

Build 10043 showed `Could not save the latency choice` and `Permission denied`, with no path on the alert. The image already contains `/boot/home/config/settings/kernel/drivers` (the `kernel` driver settings file). That directory is root, mode 0755. `r5-desktop` gave the session user `config`, `settings`, and `settings/system`, and left `kernel/drivers` owned by root. Creating `hda.settings` there returns `EACCES`. `chmod` from Media cannot change a directory this user does not own.

`r5-desktop` now gives `config/settings/kernel` and `config/settings/kernel/drivers` to the session user, mode 0700, before it drops uid. That is the same moment as `settings/system`. The kernel still reads `hda.settings` from that user directory. A root-owned file in a directory the user can write is removed and created again.

Build 10044. What's new is "Latency save".

`jam -q r5-desktop Media` exited 0 before the image. `jam -q @r5-raw` on 3 Nov 2026 at 09:22 Europe/Amsterdam exited 0 and updated 1794 targets. The header date is "3 Nov 2026, 09:19". Jam also printed `package mesa_swpipe not available`, `package mawk not available`, and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started from this image at 09:23 Europe/Amsterdam. The guest syslog sent at 09:28 Europe/Amsterdam shows the save is used. The first HDA open prints `buffer length 2048`. After media services restart the same line is `256`, then `128`, then `2048` again. Those are Low latency, Pro audio, and Normal. This is the VirtualBox ICH6 HDA (codec 8384:7680, 16-bit, 16/22/44 kHz), not the Ryzen machine. The log shows the programmed buffer, not a measured delay.

### What's new for finer MIDI

Build 10043. What's new is "Finer MIDI". MIDI notes can use a finer velocity, and controllers a finer value. Programs that already send MIDI keep working. The Terminal command `midi2test` checks the new notes.

`jam -q @r5-raw` on 3 Nov 2026 at 09:09 Europe/Amsterdam exited 0 and updated 1140 targets. The header date is "3 Nov 2026, 09:07". Jam also printed `package mesa_swpipe not available`, `package mawk not available`, and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started from this image on 3 Nov 2026 at 09:10 Europe/Amsterdam. The guest was on its first boot. About was not opened from here. The earlier guest run of `midi2test` was build 10042.

### MIDI 2.0 test and developer notes

`midi2test` checks the UMP helpers and sprays a local producer into a local consumer. A `uchar` note stays three MIDI 1.0 bytes. A `uint16` note and a `uint32` controller arrive as a type 0x4 packet, and the old hooks see the top 7 bits. The command is `/system/bin/midi2test` on the regular image. `docs/MIDI2.md` is the developer contract: the casts, the roster payload, what `Data` does, and what `usb_midi` sends. The program does not open a USB device. A bare integer is ambiguous between the MIDI 1.0 and MIDI 2.0 overloads; the header now says to cast it.

`jam -q midi2test` exited 0 on 3 Nov 2026 at 08:49 Europe/Amsterdam. `jam -q @r5-raw` on 3 Nov 2026 at 08:54 Europe/Amsterdam exited 0 and updated 1795 targets. `haiku.hpkg` contains `midi2test`. The raw image stores that package; `package_daemon` publishes `/system/bin/midi2test` at boot. Jam also printed `package mesa_swpipe not available`, `package mawk not available`, and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started from this image on 3 Nov 2026 at 08:57 Europe/Amsterdam. In the guest Terminal, `midi2test` printed `ok` for every check and `midi2test: all checks passed` on 3 Nov 2026 at 08:59 Europe/Amsterdam. That covers the helpers and a local producer sprayed into a local consumer through `midi_server`. No USB MIDI device was attached.

### MIDI 2.0 UMP and 16-bit velocity

`kits/midi2` can build and parse a Universal MIDI Packet without a new kit. `SprayNoteOn(channel, note, uchar)` and `SprayControlChange(channel, controller, uchar)` still send a MIDI 1.0 byte message. The new overloads take `uint16` velocity and `uint32` controller value and send one MIDI 2.0 channel-voice packet (message type 0x4, two 32-bit words). `SprayUMP` sends one packet of 1, 2, or 4 words when the count matches the message type. The methods are not virtual: the existing spray calls are not virtual, and a new virtual would move the vtable of a subclass that was compiled earlier.

The bytes after the roster header are `B_MIDI2_UMP_MAGIC`, the word count, and the words. `BMidiLocalConsumer::Data` turns a MIDI 2.0 note or controller into the existing 7-bit `NoteOn` / `ControlChange` (top 7 bits). An application that wants the full value overrides `Data`, calls `midi2_ump_from_payload`, and does not call the base for that buffer. `midi_server` sends that payload with `B_MIDI_WRITE_UMP` instead of `write()`.

`usb_midi` reads the class-specific MIDI header. Below version 2.0 the endpoint stays on USB MIDI 1.0 CIN packets. A type 0x4 packet is then the 7-bit note or controller in one CIN packet, so a current interface still plays. Version 2.0 or newer carries UMP words on the bulk endpoint. A MIDI 1.0 `write()` on that endpoint becomes one type 0x1 or type 0x2 word. SysEx on a version 2.0 endpoint returns `B_UNSUPPORTED`. The group nibble is the cable of the open port. Incoming UMP is parsed back into MIDI 1.0 bytes for the existing reader, so the 16-bit value is not kept on the way in.

No USB MIDI device was attached. Property Exchange and Profiles are not implemented.

`jam -q libmidi2.so libmidi.so usb_midi midi_server` exited 0 on 3 Nov 2026 at 01:13 Europe/Amsterdam. `libmidi.so` still compiled against the new overloads. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. The running VirtualBox image was not rebuilt.

### Latency file and mixer gain

Build 10041 created `~/config/settings/kernel/drivers` and still did not create `hda.settings`. The prefs built that path with `BPath` / `create_directory`, which walk a `BEntry`. The directory was left behind and the file was not. The save now uses `find_directory` into a string, `mkdir`, `chmod 0755`, and `open`. A failure alert includes that path and `strerror`. The same text is written to `usb_audio`.

The mixer scaled a stereo gain by the average of the two channels. With both channels at 0 that divides by zero and stores NaN. A slider that starts from silence now sets both channels to the new level. This is the code path behind the mixer-volume crash (Haiku #18981, GitHub #3347). It is not proven to be the whole crash.

`multi_audio` called `BufferExchange` in a loop and ignored an error. A driver that returns at once then uses a whole CPU (Haiku #16057, GitHub #2250). An error now waits 20 ms before the next try. A successful exchange is unchanged.

Hardware-specific "no sound on this laptop" tickets were not changed. Guest syslog was not read.

`jam -q Media mixer.media_addon hmulti_audio.media_addon` exited 0 before the image. `MediaViews.o`, `AudioMixer.o`, `MultiAudioNode.o`, and `libbe.so` linked. `jam -q @r5-raw` on 3 Nov 2026 at 00:48 Europe/Amsterdam exited 0 and updated 1743 targets. `distro/BUILD` is 10042. What's new is "Latency file". The header date is "3 Nov 2026, 00:45". Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started from this image on 3 Nov 2026 at 00:50 Europe/Amsterdam. The Latency menu was not opened from here.

### Media preferences quit double-free

Closing Media after changing a popup (audio output, or the HD Audio sample rate) stopped the program. The debugger showed `bogus pointer (double free)` in `BLooper::SetCommonFilterList`, called from `MediaWindow::~MediaWindow`. `BMenuField::MouseDown` puts a filter on the window. `MouseUp` removes it. Quitting while that menu is still tracking skips `MouseUp`. `~BMenuField` deleted the filter, then `~BLooper` deleted it again. The filter is now removed in `DetachedFromWindow` before the field is deleted.

The destructor also called `StartWatching` for `B_MEDIA_SERVER_QUIT` where it should stop. Both watches are now stopped, and a missing roster is ignored.

The guest syslog is inside the virtual machine. This host cannot read it: there are no Guest Additions, and COM1 is the clipboard pipe. `hda.settings` was not created. A failed save only printed to stderr, which Deskbar does not show. The directory is now created as `kernel` then `drivers`, the file is written with `open`, and a failed save opens an alert. After a save the line under the menu says to restart media services. `buffer length` is printed only when HD Audio sets the stream up again, which is that restart, or the next time sound starts.

`jam -q Media` exited 0 on 3 Nov 2026 at 00:21 Europe/Amsterdam. `distro/BUILD` is 10041. What's new is "Media closes". `jam -q @r5-raw` on 3 Nov 2026 at 00:24 Europe/Amsterdam exited 0 and updated 1731 targets. `libbe.so` and Media were linked at 00:21 and went into that image. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started from this image on 3 Nov 2026 at 00:26 Europe/Amsterdam. The guest syslog was not read. The Latency menu was not opened from here.

### Audio latency choice

Media preferences, Audio settings, has a Latency menu. Normal leaves the sound-card buffer alone. Low latency writes 256 frames. Pro audio writes 128 frames. The choice is stored in the user driver settings `kernel/drivers/hda.settings` and `kernel/drivers/usb_audio` (`play_buffer_frames` and `record_buffer_frames`). Restart media services closes the device so the next open picks it up.

HDA already read those keys on open and used them instead of `default_buffer_length_for_rate` (2048 frames at 48 kHz, about 42.7 ms). 256 frames is about 5.3 ms. 128 frames is about 2.7 ms. The buffer count stays 2. syslog prints `hda: sample size … buffer length …` when the stream is set up.

USB audio ignored that file. It now reads the same keys when it lays out isochronous buffers. A 1 ms USB packet does not divide 256 or 128 at 48 kHz, so the driver rounds up to a whole packet (288 or 144 frames). The normal path is unchanged: 2048 samples times two buffers, then rounded to a page. syslog prints `usb_audio: play buffer N frames (setting M)`. No USB device was attached here.

Packed 24-bit was already in the previous build. `usb_audio` accepts a 3-byte subframe, and `hmulti_audio` writes three bytes when the channel stride is not a multiple of 4. That path was not changed.

`jam -q Media usb_audio` exited 0 on 2 Nov 2026 at 23:55 Europe/Amsterdam. `distro/BUILD` is 10040. What's new is "Shorter sound buffers". `jam -q @r5-raw` on 2 Nov 2026 at 23:58 Europe/Amsterdam exited 0 and updated 1237 targets. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started a new VDI from this image on 2 Nov 2026 at 23:59 Europe/Amsterdam. The Latency menu was not opened from here. No loopback was measured.

### Mixer resampling uses cubic Hermite

The mixer’s high-quality resampler joined two samples with a straight line. That rolls the highs off well below the Nyquist frequency (Haiku #9438). It now uses four neighbours (Catmull-Rom). The same-rate path is still a copy. Drop/repeat samples is unchanged. The preference line says cubic interpolation.

The first image crashed on that copy. After the samples were written, the tail was read from the pointer that had already walked off the end of the buffer. A 48 kHz MP3 into the 48 kHz mixer takes this path, so MediaPlayer hit a segment violation in `AudioMixer control` (`kernel<float, float, …>`, `movss (%rsi,%r8,1)`). The tail is now indexed from the start of the buffer.

`jam -q mixer.media_addon` exited 0 on 2 Nov 2026 at 23:22 Europe/Amsterdam. `distro/BUILD` is 10039. What's new is "Clearer audio". `jam -q @r5-raw` on 2 Nov 2026 at 23:31 Europe/Amsterdam exited 0 and updated 1234 targets. AboutSystem was relinked. The mixer add-on was already linked at 23:22 and went into the rebuilt `haiku.hpkg`. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started a new VDI from this image on 2 Nov 2026 at 23:33 Europe/Amsterdam. Playing a 48 kHz MP3 opened the debugger, as described above.

`jam -q @r5-raw` on 2 Nov 2026 at 23:40 Europe/Amsterdam exited 0 and updated 1232 targets, including `Interpolate.o`. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox started a new VDI from this image on 2 Nov 2026 at 23:41 Europe/Amsterdam. The MP3 was not played again from here.

### Audio tickets: time source, hot-plug, packed 24-bit, mixer latency

`BTimeSource` no longer enters the debugger when a performance or real time span is too wide for a float. The scale uses double (Haiku #20307). A finished notification sound deletes its player, which disconnects and releases the mixer (Haiku #16739). Changing a sound-card rate no longer divides by zero in `hmulti_audio` (Haiku #19978). Packed 24-bit samples are written and read as 3 bytes when the channel stride is not a multiple of 4, and `usb_audio` accepts that subframe size again (Haiku #20267).

`multi_audio` watches `/dev/audio/hmulti` and publishes a new device. The add-on server instantiates that physical node and asks the media server to rescan. A dead default output is dropped, and a failed disconnect does not block the next connection (Haiku #12776, #12777).

The system mixer no longer adds every late buffer on top of the last one, up to 150 ms. Its published latency is one buffer plus the current lateness, and a smaller notice gives half of the extra back. The wildcard format is 48 kHz instead of 96 kHz (Haiku #10142, #9438). That image still used the linear resampler. Cubic Hermite is the section above.

Not changed: USB audio KDL and EHCI isochronous locks (#18320, #18842, #18994, #18995, #19472), UAC2 headers, and more than two USB channels. No loopback was measured.

`jam -q` of `libmedia.so`, `media_addon_server`, `usb_audio`, `hmulti_audio.media_addon`, `mixer.media_addon`, and `media_server` exited 0 on 2 Nov 2026 at 23:05 Europe/Amsterdam. `jam -q @r5-raw` on 2 Nov 2026 at 23:09 Europe/Amsterdam exited 0 and updated 1212 targets. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. No loopback was measured. VirtualBox is started from this image afterwards.

### Fonts and Look and Feel taken from the Appearance preference

The first Settings pages for fonts and look drew only the lower box. The font rows, the decorator, the control look, and the scroll-bar arrows were not on the page. Those modules, and `AppearanceModule.cpp`, are removed. The two pages are built again from the Appearance preference (`FontView`, `FontSelectionView`, `AntialiasingSettingsView`, `LookAndFeelSettingsView`, `FakeScrollBar`, `ColorsView`). Those files keep the Haiku MIT copyright. Antialiasing is a box on Fonts. Colors is a box on Look and Feel. A change still posts the preference's update message to the page, which applies it at once.

Sign in says Welcome to R5. The first-start screen says Welcome to R5 Operating System, Try R5, Install R5, and Quit R5. What's new headings start with `R5`, not `R5OS`. The Wi-Fi hint says R5 remembers the network. The AirPlay credit line is R5 AirPlay.

`distro/BUILD` stays 10038. `jam -q settings_fonts settings_look` exited 0. About, Login, and FirstBootPrompt linked. `jam -q @r5-raw` on 2 Nov 2026 at 22:40 Europe/Amsterdam exited 0 and updated 1155 targets. Jam also printed `package mesa_swpipe not available` and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox is started from this image afterwards. The pages were not opened from here.

### About R5, disk name, and separate font and look pages

The Deskbar leaf item and the About window title are About R5. The logo page shows R5 Operating System, then Version plus the version and build, then the ABI line. Credits uses that same name as its heading. The sentence under it says R5 is based on Haiku. The boot volume label is R5.

Settings no longer has an Appearance page or tabs. Personalization has Fonts and Look and Feel. Fonts is the four font rows from the separate Appearance program, with Antialiasing in a box under them. Look and Feel is the decorator, control look, and arrow style, with Colors in a box under them. A change is applied at once. Revert and Defaults stay the window buttons. The separate Appearance program stays installed.

`distro/BUILD` stays 10038. `jam -q settings_fonts settings_look` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 22:18 Europe/Amsterdam exited 0 and updated 1137 targets. Jam also printed `package mesa_swpipe not available`, `package mawk not available`, and the usual `bios_ia32` and `pxe_ia32` lines. VirtualBox is started from this image afterwards. The new pages and About were not opened from here.

### Desktop picture

The artwork folder shipped two Haiku logo pictures, and the Desktop folder had no picture of its own. The desktop now uses `r5-wallpaper.jpg`, an original picture contributed to R5OS under GPL-3.0-or-later. The placement is scale to fit (mode 2): the picture fills the screen and keeps its proportions. Those two Haiku logo files are no longer installed. The picture files stay in the Haiku tree. They are just not copied into this image.

The attribute is on the administrator Desktop folder in the image. A home created later does not get it until that folder is given the same attribute.

`distro/BUILD` stays 10038. `jam -q @r5-raw` on 2 Nov 2026 at 21:43 Europe/Amsterdam exited 0 and updated 1109 targets. The staged artwork directory contains only `r5-wallpaper.jpg`. VirtualBox is started from this image afterwards. The desktop was not seen from here.

### Appearance body and workspace fields

The previous layout pass still left Appearance as four tabs over an empty page. `BTabView` kept its page body at the height it had before the scroller gave the page a real size. The tab view now sets that body to the area under the tabs after its own layout. The font size field keeps a single-line maximum, and the preview takes its colors from the page, the same way the separate Appearance program does.

Workspace columns and rows grew into two large boxes. The row under Workspaces was taking the spare height of that box, and a full-height alignment then stretched each field. The fields stay on one line. Spare height sits under them.

A box title lost its bold face when the plain-font walk compared `Face()`. That flag does not survive `SetFont`. The walk now matches family, style, and size, and it leaves a view named `box title` alone.

`distro/BUILD` stays 10038. `jam -q Settings` exited 0 and updated 18 targets. `jam -q settings_appearance` exited 0 and updated 2 targets. `jam -q @r5-raw` on 2 Nov 2026 exited 0 and updated 1127 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mesa_swpipe not available`, plus the usual `bios_ia32` and `pxe_ia32` lines. `scripts/jam-r5-raw.sh` was not used: from this shell it stops on carriage returns, so the About stamp was not rewritten and is still the 20:22 stamp. The image file is `generated.x86_64/r5-os.image`. VirtualBox was not started. The tabs, the workspace fields, and a box title were not opened.

### Settings audit after the empty Appearance tabs

Opening Appearance showed the four tabs and an empty page. The page scroller sizes its page by hand. A resize during that layout does not lay the page out again, so a tab body stayed at zero height. The scroller now lays the page out after it places it. A hidden tab lays itself out when it is shown.

Manrope was applied to every child, including a box title that had already been set bold. Enable notifications and Enable virtual memory lost that face the same way. The walk now leaves the bold face and the title face as they were set.

Display Apply called `BScreen::SetMode` on the window thread and again from a second thread after 12 seconds, and the first call stored the mode as the boot mode. Choosing 1024x768 froze the desktop, Settings stopped in the kernel debugger (`bogus pointer` while freeing the window), and a later start could come up in that mode. The trial mode is no longer the boot mode. Keep writes it. Undo and the 12 second timeout put the previous picture back on the window thread. There is no second thread.

`distro/BUILD` stays 10038. `jam -q Settings` exited 0 and updated 18 targets. `jam -q settings_appearance` exited 0 and updated 2 targets. `jam -q @r5-raw` on 2 Nov 2026 at 20:22 Europe/Amsterdam exited 0 and updated 1139 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available` and `package mesa_swpipe not available`, plus the usual `bios_ia32` and `pxe_ia32` lines. The package script failed on a `bash\r` shebang. Running it after stripping the carriage return printed `mesa_packages_ok`. About shows build 10038. The image stamp is `2 Nov 2026, 20:22`. This image replaces the 20:01 image. VirtualBox is started from this image afterwards. The desktop was not seen from here. The tabs and a resolution change were not tried from here.

### Appearance page

The Appearance item opened a small sample checkbox and a button for the separate program. Colors and fonts was a second list item that launched that same program. Appearance is now the page. The subtitle is "Personalize how the system looks". The tabs are Fonts, Colors, Look and feel, and Antialiasing. A change is applied at once, through the existing font, color, decorator, control look, scrollbar, and antialiasing calls. Revert and Defaults are the window buttons. The page does not write its own settings file. The separate Appearance program stays installed. Colors and fonts is no longer a list item, because this page was asked for in its place. Information stays first under System, the other pages stay alphabetical, and the five existing descriptions are unchanged.

A chosen font was written, then the menu was filled again from `be_plain_font` before that copy had changed, so Plain snapped back and the other rows kept a menu mark that did not match the preview. The row now keeps the choice it applied. The preview uses the panel color and the width of the old preview, so it is no longer a white field. Size keeps its number. The font menu stays a menu. Look and feel selects a decorator by its list index. The earlier call looked the decorator up by a name that search does not use, so the choice did not stick.

`distro/BUILD` stays 10038. `jam -q settings_appearance` for this fix exited 0 and updated 2 targets. `jam -q @r5-raw` on 2 Nov 2026 at 20:01 Europe/Amsterdam exited 0 and updated 1155 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available` and `package mesa_swpipe not available`, plus the usual `bios_ia32` and `pxe_ia32` lines. The package script in the image script failed again on a `bash\r` shebang. Running it after stripping the carriage return printed `mesa_packages_ok`. About shows build 10038. The image stamp is `2 Nov 2026, 20:01`. This image replaces the 19:29 image. VirtualBox is started from this image afterwards. The desktop was not seen from here. The four tabs were not opened from here.

The first image for this page was `2 Nov 2026, 19:29`. `jam -q Settings` then exited 0 and updated 18 targets. The first `settings_appearance` compile failed: the font call wanted a writable family and style, `get_font_style` could not choose a flags pointer, and `AppearanceModule` was used before it was named. Those were fixed before that image. That image is the one where the font rows, the white preview, the missing size number, and Look and feel were seen.

### Information stays first

The Settings list followed registration order, so Information sat at the bottom of System. Information is now first under System. Every other page, in every category, is alphabetical by name. The same order is used for the cards on a category page. The card line and the subtitle are the page description.

Those descriptions are shorter. Information is "View system details". Virtual memory is "Extra memory stored on disk". Wallpaper is "Set the background of your desktop". Desktop and Deskbar is "Adjust desktop and Deskbar settings". Notifications is "Manage alerts and notifications".

This was written earlier on `cursor/settings-list` and never merged. Display had already taken build 10036, so this lands as 10037 on top of that page. The Display page, the bold box titles, and `libr5ui.so` stay. The order and those five lines stay as they are until the user explicitly asks to change them. Another Settings task does not include reordering the list or rewriting the lines.

`distro/BUILD` is 10037. `jam -q Settings` exited 0 and updated 22 targets. `jam -q @r5-raw` on 2 Nov 2026 at 18:52 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available` and `package mesa_swpipe not available`, plus the usual `bios_ia32` and `pxe_ia32` lines. About shows build 10037. The image stamp is `2 Nov 2026, 18:52`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The list was not opened from here.

### Display page and libr5ui.so

The sidebar item Display opened the separate Screen program. Display is now a page under System. It reads the mode list from `BScreen` and sets a mode with `BScreen::SetMode`, the same call Screen uses. The separate Screen program stays installed. The page requires an administrator. Resolution, colors, and refresh stay on the menus until Apply. Apply asks to keep the picture. A thread restores the previous mode after 12 seconds if neither button was used. Workspace columns and rows call `BPrivate::set_workspaces_layout` immediately. Revert puts back the mode and the workspace layout from when the page was opened. Brightness, combined displays, and the VESA boot file are not on this page.

The screen frame that Notifications and Desktop and Deskbar each drew is now `r5_screen_draw()` and `r5_screen_caption()` in `libr5ui.so`. Those two pages call it. Display uses it for the picture and the dpi line. `libbe` is unchanged. A program that does not link `libr5ui.so` is unchanged. The contract is `docs/LIBR5.md`.

The closed Resolution and Colors menus kept the names `resolution` and `colors`. The marked item is copied onto the menu label after the menu field is attached, and again when the choice changes. If the current mode is missing from the list, the label is still the size or the bits per pixel. Refresh uses the same `%g` text as Screen, then ` Hz`. The number still comes from the mode timing (`pixel_clock` over the totals). On this VirtualBox mode that timing is 0.1 Hz in Screen as well. Workspace columns and rows sit in a group layout, and the text field does not grow, so the plus and minus buttons stay the spinner's own size.

Box titles in ordinary text are bold: the screen size (or Display), Workspaces, Applications, Menu, Window, Preview, Device, and R5 Operating System. Enable notifications and Enable virtual memory are bold because they are the box title. A menu on the box border stays plain.

`distro/BUILD` stays 10036. `jam -q Settings` for these fixes exited 0 and updated 18 targets. `jam -q @r5-raw` on 2 Nov 2026 at 18:45 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available` and `package mesa_swpipe not available`, plus the usual `bios_ia32` and `pxe_ia32` lines. About shows build 10036. The image stamp is `2 Nov 2026, 18:45`. This image replaces the 18:13 image. VirtualBox is started from this image afterwards. The desktop was not seen from here. The pages were not opened from here. The first Settings compile failed because `BStringView` was only a name in the files that pass it to `BBox::SetLabel`. Including `StringView.h` fixed that, and the next compile exited 0. The earlier Display compile failed because `register_display_page` used `SettingsManager` without including its header. A second apply duplicated the `r5icons` and `r5ui` Jam includes. The scripts now look for their own `SubInclude` line, and the extra lines were removed.

### Settings opens narrower

The list and Search were 248 wide, and a home card was at least 300. On a 1024-wide screen the window opened at 1100, so Defaults sat past the right edge. The list and Search are now 200. A home card is at least 280. The page area's minimum is 740. The window opens at 1030 by 610, and the shortest size is 910 by 560. On a 1024-wide screen the frame is still 6 pixels wider than the display. A first cut used 198 and 290. Those widths were changed to 200 and 280 before this image was looked at.

VirtualBox guest resize for a standard account is [#4055](https://github.com/nickrusman-bot/r5os/issues/4055). It is not changed here.

`distro/BUILD` is 10035. `jam -q Settings` for the 200 and 280 widths exited 0 and updated 17 targets. `jam -q @r5-raw` on 2 Nov 2026 at 17:30 Europe/Amsterdam exited 0 and updated 1138 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available` and `package mesa_swpipe not available`, plus the usual `bios_ia32` and `pxe_ia32` lines. About shows build 10035. The image stamp is `2 Nov 2026, 17:30`. An earlier image at 17:22 used 198 and 290. This image replaces it. VirtualBox is started from this image afterwards. The desktop was not seen from here. The window width was not measured from here.

### Settings lock and Virtual memory

`libr5icons.so` gains `R5_ICON_UNLOCKED` (5) and `R5_ICON_LOCKED` (6), plus `r5_button_set_lock()`. The pictures are unmodified HVIF of Haiku `Action_Login` and `Action_Logout`. The bytes stay MIT in `distro/icons/lock_icons.inc`. The call is additive. Old programs that already link the library keep working.

Settings puts that button between Home and Revert. Closed means a `RequiresAdmin()` page is gray and `SaveSettings()` is not called. Open asks Login for an administrator name and password, the same grant the old preferences use, and one open lock covers every such page. An administrator account starts open. Clicking the open lock closes the grant again. The password is not logged. Lists still scroll. Back, Home, Search, and the sidebar stay usable.

Virtual memory is a System page. It reads and writes `/boot/system/settings/kernel/drivers/virtual_memory` in the kernel's existing text (`vm`, `swap_auto`, `swap_size`, and the four volume fields). A missing file uses swap on, automatic size from the installed memory, and the boot volume. Turning swap off asks first. The separate VirtualMemory program stays installed. A written change takes effect on the next start.

`distro/BUILD` is 10034. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 16:51 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10034. The image stamp is `2 Nov 2026, 16:51`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### The Notifications boxes match in height

Enable notifications was taller than Applications, because the boxes stayed top-aligned and only as tall as their contents. Both now use the full height of that row. The sliders, the screen picture, the list, and Mute stay at the top of their box. The extra space is below them, so the two frames end on the same line.

`distro/BUILD` is 10033. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 16:12 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10033. The image stamp is `2 Nov 2026, 16:12`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### Notifications and Wallpaper sit side by side

Enable notifications and Applications were stacked, so Notifications was taller than the area under the subtitle. They now share one row, top aligned, the same way Desktop and Deskbar puts Applications and Menu next to each other. The screen picture stays 200 by 148. The application list keeps a minimum width of 220. Both fit in the 1100-wide window, so that page no longer needs a scrollbar.

Wallpaper already used one row. The preview column was on the left. It is now on the right, and the workspace menu, picture, placement, outline, and color are on the left. The strut that lines the two box borders up still follows the workspace-menu label.

`distro/BUILD` is 10032. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 15:45 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10032. The image stamp is `2 Nov 2026, 15:45`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### Notifications default to the lower right, and Deskbar fits

A missing notification position, and Defaults, use the lower right (`B_FOLLOW_BOTTOM | B_FOLLOW_RIGHT`). Follow Deskbar stays a choice on the screen picture. The server constant `kDefaultNotificationPosition` matches that, so a new file and the old preferences program use the same place.

Desktop and Deskbar lays Applications and Menu in one row. Window is under that row. The page was taller than the area under the subtitle, so the scrollbar was showing.

`distro/BUILD` is 10031. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 15:34 Europe/Amsterdam exited 0 and updated 1742 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10031. The image stamp is `2 Nov 2026, 15:34`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### The notification example can be saved

Build 10029 still reported `Permission denied` and showed no example. The image creates `/boot/home/config/settings` as root, mode 0755, the same way it creates `config/cache`. The signed-in user cannot create `settings/system` there. The example is sent only after the write succeeds, so the alert was the only result.

`r5-desktop` now gives `config`, `config/settings`, and `config/settings/system` to the session user and mode 0700 before it drops uid. That is the same moment it already fixes `config/cache`. The notification server re-reads the file when a notification arrives, so the example uses the place just written.

`distro/BUILD` is 10030. `jam -q @r5-raw` on 2 Nov 2026 at 15:26 Europe/Amsterdam exited 0 and updated 1165 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10030. The image stamp is `2 Nov 2026, 15:26`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### Notification position is a screen, and the save is writable

Changing position in Settings reported `Permission denied`. The notification server is `launch_daemon` service `x-vnd.Haiku-notification_server`, so it runs as uid 0. `get_user_home_path` for uid 0 is `/boot/home`. The server created `settings/system` and the notifications file there. The signed-in account is uid ≥ 1000 and could not replace that file. The old preferences program then showed its save error and still posted the sample, because the sample was sent after a failed write.

The server now only reads the file. It watches the settings directory until the file exists, then watches the file. It does not create either. Settings and the old program create the directory as the signed-in user. The old program posts the sample only after the write succeeds.

Position is no longer a menu. The page draws a screen, the same kind of picture as Desktop and Deskbar. The four corners place the message there. The bar on the screen is the running Deskbar, and choosing it stores Follow Deskbar. Width, timeout, and that picture still send one sample after a successful write.

`distro/BUILD` is 10029. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 15:14 Europe/Amsterdam exited 0 and updated 1150 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10029. The image stamp is `2 Nov 2026, 15:14`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### Notifications is a Settings page

Settings adds Notifications under System. The home card for System now says display, sound, notifications, and this computer. The card on the System category is the page description. The icon signature is `application/x-vnd.Haiku-Notifications`.

The page writes `~/config/settings/system/notifications` as soon as a value changes. That flattened `BMessage` is the file the notification server already watches. There is no second copy under `r5-settings`. `settings/system` is created first. `B_CREATE_FILE` does not create that parent, and the old program reported the failure as "It's possible you are running out of disk space." The alert now includes `strerror`. An applications list that is missing from the file is a valid empty list. Build 10028 also had the notification server create that directory. That was wrong: the server is uid 0, so the signed-in user could not write the file. Build 10029 removes that create.

Width, timeout, and position send one sample (`NotificationsSample`) after a successful write. Enable and mute do not. Defaults restores notifications on, a timeout of 10 seconds, a width of 300, and Follow Deskbar. It does not clear the application list, and it keeps a stored icon size. Revert restores the snapshot from when the page was opened, including that list. The separate Notifications program stays installed. The field names match the server: `auto-start`, `timeout`, `width`, `icon size`, `notification position`, and `app_usage` blobs of type `'ipau'`.

`distro/BUILD` is 10028. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 14:47 Europe/Amsterdam exited 0 and updated 1152 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10028. The image stamp is `2 Nov 2026, 14:47`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### Settings applies a change immediately

The bottom row is Back, Home, Revert, and Defaults. Save is gone. `NotifyChanged()` makes the window call `SaveSettings()`. Revert restores the snapshot from when the page was opened and writes that again. Defaults calls `ApplyDefaults()`. Only Desktop and Deskbar reports `CanApplyDefaults()`. The Settings module API is version 2 because those two methods were added at the end of `SettingsModule`. An add-on built for version 1 is not loaded.

A page taller than the area under the subtitle sits in a scroller. The scrollbar is shown only when the page is taller than that area. The list, the title, and the bottom row stay put.

Desktop and Deskbar no longer has its own Defaults button. The icon-size slider and the application checkboxes message Deskbar while you edit them, as do the other controls on that page. "Expand new applications" is indented 20 pixels. The old indent used `B_USE_DEFAULT_SPACING * 2`, and that constant is -1002, so the strut pushed the checkbox across the box. Where the bar sits is a screen picture: edges for top and bottom, corners for the four sides, and the middle of a top side for the expanded list. Information's Copy buttons target the page. They had been targeted at the window, so the view never saw the click. The clipboard text includes the trailing NUL.

`distro/BUILD` is 10027. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 14:15 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10027. The image stamp is `2 Nov 2026, 14:15`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Desktop and Deskbar is a Settings page

Settings replaces the legacy Deskbar row with a page under Personalization. The name stays Desktop and Deskbar. Save messages the running Deskbar (`application/x-vnd.Be-TSKB`). The page does not write `~/config/settings/deskbar/settings`. Deskbar still writes that file when it quits. Auto-hide is written immediately, which is the behavior Deskbar already had. The clock is not on this page. The separate Deskbar preferences program stays installed.

Sort, Tracker first, expanders, expand new, and hide names are toggles. Save sends one of those messages only when the choice differs from the settings file read when the page loaded. There is no public getter. If the old preferences window changed Deskbar's memory in this session and Deskbar has not quit, that toggle can land on the wrong side. Icon size is absolute (`be:value` times 8, from 16 to 96). Recent counts are absolute. A count of 0 is stored by Deskbar as off. Place and the three window flags come from `BDeskbar` when it is running. Top and bottom stay expanded. Left bottom and right bottom stay collapsed. Show applications is only enabled for left top and right top. Always on top is sent before auto-raise, and auto-raise is not sent while always on top is on. Defaults fills Deskbar's own defaults and waits for Save. Edit in Tracker sends `B_REFS_RECEIVED` for `B_USER_DESKBAR_DIRECTORY`. The count fields use private `BSpinner`, with `UsePrivateHeaders interface` on the Settings Jamfile.

`distro/BUILD` is 10026. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 12:11 Europe/Amsterdam exited 0 and updated 1141 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About shows build 10026. The image stamp is `2 Nov 2026, 12:11`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. The page was not clicked from here.

### libr5icons.so is a system library

The icon helpers are no longer a static archive inside Settings. They are `libr5icons.so`, soname `libr5icons.so`, linked against `libbe.so`. Settings lists that library as `NEEDED`. The regular image and the minimum image both name it in `HaikuImageGetSystemLibs`, so it is installed under `/system/lib` with `libbe.so`. The devel package links it from `develop/lib` when that package is built. The header for programs is `<R5Icons.h>`. `docs/ICONS.md` tells a developer how to include it, how to link it, and which changes break the soname.

The C++ is GPL-3.0-or-later. The four built-in pictures stay Open Tracker HVIF. They are not relicensed, and the calls are not added to `libbe`.

`jam -q Settings` exited 0 and linked `libr5icons.so`. `readelf` shows soname `libr5icons.so` and shows Settings needs `libr5icons.so`. `jam -q @r5-raw` on 2 Nov 2026 at 10:16 Europe/Amsterdam exited 0 and updated 1141 targets. The package copy script for `haiku.hpkg` includes `libr5icons.so`. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays build 10025. The image stamp is now `2 Nov 2026, 10:16`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. A minimum image was not built.

### The minimum image includes Settings

`build/jam/images/definitions/minimum` lists `Settings` with the other preferences, and it installs `settings_appearance` with it. The regular image already did. The name of the icon library stays `libr5icons`. This definition was not built into an image from here. The running regular image is unchanged.

### Settings asks a public function for the Back and Home icons

Back and Home no longer read resource ids out of `libtracker.so`. Those ids are private. The pictures are now unmodified Tracker HVIF inside `libr5icons.a`, and Settings calls `r5_button_set_icon()` with `R5_ICON_BACK` and `R5_ICON_HOME`. The buttons stay the same height as Revert and Save, in that order, and they still do not get the wrench.

The same library can draw any other icon. `r5_icon_bitmap()` takes a predefined name: back, forward, up, or home. The numbers 1 to 4 stay fixed. `r5_vector_icon_bitmap()` takes HVIF bytes, `r5_file_icon_bitmap()` takes a path, and `r5_mime_icon_bitmap()` takes a MIME type. Each has a `r5_button_set_*()` call that sizes the button. `docs/ICONS.md` is the contract. `AGENTS.md` says not to open Tracker’s private resources for this.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 09:54 Europe/Amsterdam exited 0 and updated 1140 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays build 10025. The image stamp is now `2 Nov 2026, 09:54`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Back and Home are Tracker icons, and the wrench stays on Settings

The preferences wrench is only for this Settings application and the modules inside it: the page icon and the home cards. About R5 is not a settings module and does not get the wrench.

Back and Home on the bottom row are now buttons that draw Tracker's back arrow (`R_ResBackNav`) and home-directory icon (`R_HomeDirIcon`) from `libtracker`. They are the same height as Revert and Save. The order is Back, Home, Revert, Save. Those two icons do not get the wrench. `AGENTS.md` says so.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 09:40 Europe/Amsterdam exited 0 and updated 1140 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays build 10025. The image stamp is now `2 Nov 2026, 09:40`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Settings icons wear the preferences wrench

Every icon in Settings, on the page and on the home cards, gets the Haiku preferences wrench drawn on top. The wrench is the unmodified Haiku file `data/artwork/icons/Overlay_prefs`, MIT, copied to `distro/settings/overlay-prefs.hvif` and stored in the Settings resources as `prefs-overlay`. `AGENTS.md` requires that overlay. A page icon is not drawn without it.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 09:29 Europe/Amsterdam exited 0 and updated 1140 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays build 10025. The image stamp is now `2 Nov 2026, 09:29`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### The Settings frame stays put, and each page has an icon

The list, the title block, the icon at the right of that block, and the bottom row (Search, Home, Back, Revert, Save) are fixed. `AGENTS.md` says not to move them. A page only changes the area under the subtitle. Each page loads the icon of its program. Home uses the Preferences folder icon. Information uses the About icon. A page without a signature in `module_icon()` shows an empty slot of the same size.

Information labels are left-aligned. Copy sits under the rows of its box.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 08:36 Europe/Amsterdam exited 0 and updated 1140 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays build 10025. The image stamp is now `2 Nov 2026, 08:36`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Information is a Settings page, and the list stays beside the title

The list now starts at the top of the window, under the tab, and runs down to Search. The title and the subtitle sit beside the list. The page is under the subtitle. The window opens at 1100 by 610. That layout is the one to keep.

Information is under System. Device shows the host name, the processor brand from `cpuid` plus the current frequency when the kernel reports it, installed memory, the boot volume (`used of capacity`), the graphics device from `BScreen::GetDeviceInfo`, and the system type (AMD64 on this image). There is no device id in the public system info, so that row is not shown. R5 Operating System shows `2026.1, Development`, build 10025, the image time `2 Nov 2026, 08:24` (Europe/Amsterdam, written into `r5_os_version.h` by the jam script), and the kernel name with the kernel's own build date. Copy puts that box on the clipboard as plain text. The page does not save anything.

`distro/BUILD` is 10025. `jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 08:24 Europe/Amsterdam exited 0 and updated 1140 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays in step with this build. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Settings opens shorter, and the home cards respond to the pointer

The window opened at 680 pixels tall, so Home had a large empty band under the cards and the same empty area in the list. It now opens at 1100 by 510, which is 170 pixels shorter. The shortest height is 480, so a margin stays above the buttons.

The list scrollbar was always drawn. It is hidden, and that column is given back to the list, when every item fits. It comes back when the items are taller than the list.

A category card uses the button look. It lights up under the pointer and looks pressed while the mouse button is down. Releasing the button inside the card opens that page. The card does not stay marked.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 08:13 Europe/Amsterdam exited 0 and updated 1138 targets. The package check printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. About stays build 10024. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Choosing Image after Workspace still locked Settings

The refresh posted from a menu click runs inside the menu's own loop, while the popup is still tracking. Refresh rebuilt the image menu and freed the item that had just been chosen. The window then ignored clicks, and quitting it was a double free. A menu click now updates the page directly and does not touch the menu that is open. Image items are not removed. A new picture is inserted.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 07:53 Europe/Amsterdam exited 0 and updated 1138 targets. The package check printed `mesa_packages_ok`. About stays build 10024. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### Wallpaper menus overlapped, and choosing one froze Settings

The workspace menu was the label of the right box, and that box also had its own layout, so the Image menu was drawn underneath it. Choosing Current workspace rebuilt the menus while that menu was still open. The window then ignored clicks. Quitting it stopped in the debugger with `double free` in `SettingsWindow::~SettingsWindow`. The menu update is posted after the menu closes. The box keeps a single child below the label, so the label is no longer laid out as content.

The preview grows with the box and keeps the screen's shape. The page title sits above the list and the page. The list and the page share one row, and Search sits with the buttons.

`jam -q Settings` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 07:42 Europe/Amsterdam exited 0 and updated 1138 targets. The package check printed `mesa_packages_ok`. About stays build 10024. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`.

### About names this computer AMD64, and Wallpaper is the Backgrounds page

About still reads the short ABI name from `libbe.so` (Walter on this disk) and puts the target in parentheses. `x86_64` is shown as AMD64. The package ABI is still `x86_64`. The source maps `aarch64` to ARM64 and `riscv64` to RISC-V 64. This image is the AMD64 build, so those two names are not in the About binary.

`distro/BUILD` is 10024. What's new is the section that was missing for Settings, Manrope, and this Wallpaper page. The disk the user had open was still build 10023.

Wallpaper in Settings is that page rewritten to the Backgrounds layout: a preview, the workspace menu (current, all, the default folder, another folder, and recent folders), the image menu, Manual / Center / Scale to fit / Tile, the icon-label outline, and the color cells. Save writes `be:bgndimginfo`. On the desktop it also sets the desktop color. Tracker is told to restore the picture. The current workspace keeps the other workspace records. All workspaces replaces that list with one record. Center and Scale to fit apply to the desktop. The Backgrounds program is still installed and is not a button on this page. Revert and Apply are the Settings buttons. Dragging the preview in Manual updates X and Y.

On the 23:41 image the user again saw no desktop icons, and only on the first boot. This session did not see that desktop. A first boot can sit in `package_daemon`, and the icons can stay missing until a guest restart.

`jam -q Settings` exited 0. `jam -q AboutSystem` exited 0. `jam -q @r5-raw` on 2 Nov 2026 at 00:23 Europe/Amsterdam exited 0 and updated 1137 targets. `install-mesa-packages.sh` did not start from that jam script because its shebang is CRLF; the same check run afterwards printed `mesa_packages_ok`. Jam also printed `package mawk not available`, plus the usual `mesa_swpipe`, `bios_ia32`, and `pxe_ia32` lines. `haiku.hpkg` contains this `Settings` and the existing `Backgrounds`. The About binary contains `AMD64` and `Build 10024`. VirtualBox is started from this image afterwards. The desktop was not seen from here.

### About licenses use one block per component

The Licenses tab text is `distro/about/licenses.txt`. Each component is a heading (`# Name`), one sentence, a `License:` line, and a `Source:` URL. About draws the heading in the bold face and makes the URL after `Source: ` a link. The window title stays About R5OS. Not rebuilt into an image in this change, and not checked on a booted screen.

### Manrope is the UI font, and Wallpaper is a Settings page

The plain and bold system fonts are Manrope Regular and Bold when no font settings have been saved. The fixed font stays Noto Sans Mono, so Terminal is unchanged. Noto Sans is the fallback if Manrope is missing. The files are the unmodified static fonts, also installed in `data/fonts/ttfonts`. `distro/app_server/ServerConfig.h` is copied over the Haiku header. It keeps the Haiku MIT notice.

Wallpaper is a page in Settings instead of a button that only opens Backgrounds. Save writes `be:bgndimginfo` on the Desktop folder, the same attribute Backgrounds writes, and sends `B_RESTORE_BACKGROUND_IMAGE` to Tracker. That one picture is used on every workspace. A saved per-workspace list is replaced. Desktop color, a different picture per workspace, and folder backgrounds stay in the existing Backgrounds program, opened from "More wallpaper options".

`jam -q Settings` exited 0. `jam -q @r5-raw` on 1 Nov 2026 at 23:41 Europe/Amsterdam exited 0 and updated 1170 targets. The package check printed `mesa_packages_ok`. `app_server` contains the string Manrope. `haiku.hpkg` has `data/fonts/ttfonts/manrope-regular.ttf` and `manrope-bold.ttf`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. About stays build 10023. A home folder that already saved a font still uses that saved font.

### Settings categories are cards, and Settings uses Manrope

A category page repeated its summary under the title and stacked a button for each page. The summary stays in the title. Personalization, System, Devices, Network, and Accounts use the same cards as Home, with the existing preference icon on the left. Home is the first button on the bottom row, before Back, Revert, and Save. Search stays under the list.

The page title is Manrope Bold, eight points above the bold system size. Every other label in Settings uses Manrope Regular or Bold. The files are the unmodified static fonts, loaded with `BFont::LoadFont` from `/boot/system/data/settings/fonts`. If a file does not load, that face stays the system font. The desktop font is not changed. The license is OFL-1.1. Copyright 2018 The Manrope Project Authors. Designed by Mikhail Sharanda. No Reserved Font Name is declared, and the files are not modified.

`jam -q Settings settings_appearance` exited 0. `jam -q @r5-raw` on 1 Nov 2026 at 23:24 Europe/Amsterdam exited 0 and updated 1134 targets. The package check printed `mesa_packages_ok`. `haiku.hpkg` contains `Settings`, `settings_appearance`, and `data/settings/fonts` (`manrope-regular.ttf`, `manrope-bold.ttf`, `OFL.txt`, `FONTLOG.txt`). VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. About stays build 10023.

### Back in Settings freed the previous page twice

Clicking Back stopped Settings in the debugger: `double free` in `BWindow::_task_looper`. The previous page was taken off the stack with `LastItem()`, freed by `RemoveItem()`, and then freed again. It is freed once now. The home page from the 22:53 image is the one the user called good. `jam -q @r5-raw` on 1 Nov 2026 at 22:59 Europe/Amsterdam exited 0 and updated 1145 targets. The package check printed `mesa_packages_ok`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. About stays build 10023.

### Settings window layout from the first boot

The first Settings window was too narrow, and the sidebar grew when a page had a short label, so the list changed width between Home and a page. The list now sits directly under the title tab, at a fixed width, with search underneath. The loose “Settings” label and the breadcrumb are gone. Back sits with Revert and Save. Home cards draw the category’s existing preference icon on the left. The window opens at 1100×680.

Appearance’s checkbox invoked the window, so the mark changed and Save stayed disabled. The checkbox and the legacy Open button now invoke their own page. Save enables after the sample-size mark changes.

`jam -q Settings settings_appearance` then `jam -q @r5-raw` on 1 Nov 2026 at 22:53 Europe/Amsterdam exited 0. The image update was 1133 targets. The package check printed `mesa_packages_ok`. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot can sit in `package_daemon`. About stays build 10023.

### Settings host and one Appearance module

Settings is a new preference window: a sidebar, a search field, home cards, and one content area. Pages come from modules. The first native module is Appearance. It stores `large_sample` in `~/config/settings/r5-settings/appearance` and does not rewrite the Haiku Appearance file. Colors and fonts, Deskbar, wallpaper, display, sound, keyboard, mouse, Bluetooth, printers, network, and Users & Accounts stay their existing programs; Settings only launches them.

The module API is version 1 (`distro/settings/SettingsModule.h`, `distro/settings/ModuleAPI.md`). Add-ons export `settings_api_version` and `instantiate_settings_module`. A different version is not loaded. An administrator page uses `r5_admin_change_allowed` and cannot be saved while that is false. `jam -q Settings settings_appearance` on 1 Nov 2026 at 22:32 Europe/Amsterdam exited 0 and linked both targets. `jam -q @r5-raw` at 22:36 Europe/Amsterdam exited 0 and updated 1145 targets. The package check printed `mesa_packages_ok`. `haiku.hpkg` contains `Settings` and `settings_appearance`. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`, and the desktop icons can stay missing until a guest restart.
### Shader-cache error is gone on the 21:48 disk

On the image packed at 21:48 Europe/Amsterdam the user reported that the Mesa shader-cache permission line no longer appears. That disk is the one where `r5-desktop` creates `/boot/home/config/cache` for the session uid before it drops privileges. This session did not run `vulkaninfo` again and did not see the desktop. The device is still llvmpipe on the CPU. About stays build 10023.

### Session user can create the Mesa shader cache

`vulkaninfo` printed `Failed to create /boot/home/config/cache/mesa_shader_cache for shader cache (Permission denied)`. The image creates `/boot/home/config/cache` as root, mode 0755. `r5-desktop` then drops to the session uid, which is not 0, so Mesa cannot create a directory there. Before that drop, `r5-desktop` now creates the cache directory from the account’s home, gives it to that uid and gid, and sets mode 0700. `jam -q r5-desktop` on 1 Nov 2026 at 21:46 Europe/Amsterdam exited 0 and linked `r5-desktop`. `jam -q @r5-raw` at 21:48 Europe/Amsterdam exited 0 and updated 1130 targets. The package check printed `mesa_packages_ok`. `haiku.hpkg` contains that `r5-desktop` (21:46). About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. `vulkaninfo` has not been run again. The first boot of this disk can sit in `package_daemon`, and the desktop icons can stay missing until a guest restart.

### vulkaninfo named llvmpipe

On the 21:36 Europe/Amsterdam image the user ran `vulkaninfo --summary` in Terminal. Vulkan instance version 1.4.367. GPU0 was `PHYSICAL_DEVICE_TYPE_CPU`, device name `llvmpipe (LLVM 21.1.8, 256 bits)`, driver `llvmpipe`, driver info `Mesa 25.3.6 (LLVM 21.1.8)`, driver ID `DRIVER_ID_MESA_LLVMPIPE`, apiVersion 1.4.328, driverVersion 25.3.6. That is Lavapipe reporting the software renderer. The word Lavapipe is not the device name. Mesa also printed `Failed to create /boot/home/config/cache/mesa_shader_cache for shader cache (Permission denied)--disabling.` The desktop icons were missing on the first boot of this disk. A guest restart made them visible. This session did not see that desktop. This is not the 5625U GPU.

vkcube and the Vulkan-Tools mock ICD are not part of this result. vkcube draws into a window system this image does not have. The mock ICD is a fake driver for loader tests. The loader already opened Lavapipe.

### Image with vulkaninfo

`jam -q @r5-raw` on 1 Nov 2026 at 21:36 Europe/Amsterdam exited 0 and updated 1130 targets. The package check printed `mesa_packages_ok`, including `vulkan_tools` and the Lavapipe package whose manifest is under `add-ons`. The first jam of this package exited 0 but did not install `vulkan_tools`: the name was missing from the HaikuPorts pin, so jam said the package was not available. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. `vulkaninfo` has not been run. The first boot of this disk can sit in `package_daemon`.

### Lavapipe manifest moved to add-ons

The Haiku Vulkan loader searches `vulkan/icd.d` under the add-ons directories (`find_paths` with `B_FIND_PATH_ADD_ONS_DIRECTORY`). `libvulkan.so.1.4.367` contains the relative path `vulkan/icd.d`. The Lavapipe manifest was in `data/vulkan/icd.d`, which that search does not read. `mesa_lavapipe` now ships `add-ons/vulkan/icd.d/lvp_icd.json`. The library path inside the file is still `/boot/system/lib/libvulkan_lvp.so`. Not on a disk yet. `vulkaninfo` has still not been run.

### vulkaninfo is packed, not run

The next Mesa step is software Vulkan, not the 5625U GPU. Lavapipe is already on the image. `vulkaninfo` from Vulkan-Tools 1.4.367 (Apache-2.0; Khronos, Valve, LunarG, RasterGrid) was cross-compiled with the Haiku g++ on 1 Nov 2026 at 21:19 Europe/Amsterdam and packed as `vulkan_tools`. It links `libstdc++.so.6` and `libroot.so` and loads `libvulkan` at run time. `scripts/build-vulkaninfo.sh` writes `distro/packages/x86_64/vulkan_tools-1.4.367-1-x86_64.hpkg`. The image package list names `vulkan_tools`. That package is not on a disk yet. VirtualBox was left on the locale image. `vulkaninfo` has not printed Lavapipe. This is not GPU acceleration.

### User confirmed the Deskbar clock and Prullenbak

On the 20:58 Europe/Amsterdam image the user selected Dutch (Netherlands) and 24-hour. The Locale preview read 20:01 and the Deskbar clock read 20:01. VBox Share sat beside Trash, not on it. With Nederlands above English, the Trash icon read Prullenbak. Home stayed “home”. QuickTour and VBox Share stayed those names. The Locale window that was already open kept the title “Locale”.

That is the path that worked. Keep it. The preview and the Deskbar clock both call `BTimeFormat` on the formatting country. The country change is broadcast even when `Locale settings` is not written, and `ApplySettings()` puts that message on top of `Refresh()`. The clock pulse does not reload the file. A language change calls `ForceUnloadCatalog()` and rebuilds menus and icon titles. A mounted volume does not restore a saved icon position.

Two earlier disks did not do this. The 19:05 image coerced the pattern generator and the clock stayed “6:07 PM”. The 19:34 image reloaded `Locale settings` on each pulse and the clock stayed “6:54 PM”, because a missing file makes `Refresh()` put en_US back. Do not draw the clock from `BDateTimeFormat` again, and do not `Refresh()` from the pulse.

After Nederlands was moved above English the same boot showed “8:01 PM”, and a minute later “8:02 PM”. The language message does not carry the formatting country. `Refresh()` runs first and can replace the live Dutch conventions with a file that does not have them yet. That flip is not changed on this disk. VirtualBox was left running. It is [#4045](https://github.com/nickrusman-bot/r5os/issues/4045).

The same menu was only partly Dutch. Strings in `nl.catkeys` no longer match the Deskbar source (`Show replicants handle`, `About R5OS`, `Shut down…`, `%appname% preferences…`, Lock screen, Log out, Show desktop). Mount is still “Betrek”. That catalog update is [#4044](https://github.com/nickrusman-bot/r5os/issues/4044). Neither is on this disk.

### Locale broadcast on the build 10023 image

`jam -q @r5-raw` on 1 Nov 2026 at 20:58 Europe/Amsterdam exited 0 and updated 7865 targets. `haiku.hpkg` was created at 20:57 and the raw image at 20:58. Deskbar (20:42), `libtracker.so` (20:42), and `libbe.so` (20:38) are the binaries from the locale compile and are older than that package, so this image contains them. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`.

### Locale changes still did not reach Deskbar

The image packed at 19:34 Europe/Amsterdam contained the country-locale pattern and the settings-file pulse. The user booted that disk and, with Dutch (Netherlands) and the 24-hour choice selected, the Deskbar clock still read “6:54 PM” while the Locale preview read 18:51. Moving Nederlands above English did not change menus. Restarting Tracker for translated folder names did not change icon titles, and VBox Share landed on Trash again.

The preview formats from the preflet’s own roster. Deskbar and Tracker only notice a `B_LOCALE_CHANGED` message, and that message was sent only after the settings file was saved. A failed save left the preview updated and every other program on en_US. `Refresh()` then reads the file and, when the file is missing or old, puts en_US back. The clock pulse did that too.

A locale change is now broadcast even when the save fails, and `~/config/settings` is created when the file is written. `BLocaleRoster::ApplySettings()` applies the message after `Refresh()`, so the message wins over a stale file. The Deskbar clock uses `BTimeFormat`, the same call as the preview, instead of the date-time pattern generator. It no longer reloads the settings file on each pulse. Deskbar and Tracker call `ForceUnloadCatalog()` on the locale message. A language change rebuilds Tracker menus and the Deskbar menu, and icon titles are read again. Trash can become Prullenbak from the Dutch catalog. The Dutch catalog still translates the home folder as “home”. QuickTour and VBox Share have no catalog names. The open Locale window keeps the labels it was opened with. A saved icon position on a mounted volume is ignored, so VBox Share is placed in a free cell instead of on Trash.

`jam -q libbe.so` then `jam -q libtracker.so Deskbar` on 1 Nov 2026 exited 0. `libbe.so`, `libtracker.so`, and Deskbar were relinked. The image section above packs those binaries. About stays build 10023.

### Deskbar still showed AM/PM

The image from 19:05 Europe/Amsterdam still drew “6:07 PM” while the Locale preview showed 18:07. The user had Dutch formatting, the 24-hour choice, and Nederlands as the preferred language. The preview is redrawn from the formatting country. The Deskbar pattern generator used the preferred language’s locale, and it can put AM/PM back into an English pattern. `k` was also rewritten to `K`, which is a 0–11 hour, so 18:00 can draw as 6. The clock rebuilt that pattern only when the locale message reached the view.

The generator now uses the formatting country, unless month and day names are taken from the preferred language. A 24-hour choice rewrites `h`, `K`, and `k` to `H` and drops the day-period marker. Each clock pulse reloads `Locale settings` when that file’s time changes, then rebuilds the pattern. `jam -q libbe.so Deskbar` exited 0 and updated 11 targets, including `FormattingConventions.o`, `DateTimeFormat.o`, `libbe.so`, `TimeView.o`, and Deskbar. `jam -q @r5-raw` on 1 Nov 2026 at 19:34 Europe/Amsterdam exited 0 and updated 1720 targets. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`.

Menus and folder names still come from the catalog loaded at program start. The Locale checkbox for folder names says Deskbar and Tracker need a restart.

### Build 10023 image with the 24-hour clock

`jam -q @r5-raw` on 1 Nov 2026 at 19:05 Europe/Amsterdam exited 0 and updated 2294 targets. `libbe.so` was linked again and packed into the system package. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`.

### Deskbar clock kept AM/PM

The Locale examples followed the 24-hour choice. Deskbar did not. Those examples call `GetTimeFormat()`, which runs `CoerceFormatForClock()`. Deskbar stores the ICU pattern from `BDateTimeFormat::SetDateTimeFormat()` as an explicit format, and an explicit format is not coerced later. For an English locale the pattern generator can keep AM/PM after the skeleton asked for 24-hour (`k`). The clock therefore stayed “5:27 PM” while the preflet preview showed 17:27.

`Use24HourClock()` also returned the unset explicit flag once the cache was already filled, so a remembered 24-hour choice could read as off. It now returns that cached or explicit choice. `CoerceFormatForClock()` is public, and `SetDateTimeFormat()` coerces the generated pattern before storing it. Applied by `scripts/apply-ui-bugs.py`.

`jam -q libbe.so` on 1 Nov 2026 at 18:48 Europe/Amsterdam updated 32 targets, including `FormattingConventions.o`, `DateTimeFormat.o`, and the `libbe.so` link, and exited 0. That library was not on the build 10023 disk that was running then. VirtualBox was not restarted for that compile. The image section above packs this library.

Menus, the Locale window’s own labels, and folders already open stay in the language that program loaded at start. The translation catalog is loaded once. A program started after the language change loads the new catalog. Folder names also stay English while “Translate application and folder names” is off. Tracker does not rebuild those names on the locale message.

### Build 10023 daily

What's new for the four Interface Kit controls, and for the well-known folder names and icon labels, is build 10023. `jam -q @r5-raw` on 1 Nov 2026 at 18:14 Europe/Amsterdam exited 0 and updated 1727 targets. `libbe.so` and `libtracker.so` were relinked. About was relinked and contains Build 10023. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. Not booted from here during the jam. The first VirtualBox start reused the build 10022 disk: the guest had been writing that disk, so its timestamp was newer than the image. `scripts/run-vbox-r5.ps1` now stores the raw image time in `generated/r5-os.built` and does not trust the timestamp of a disk that was running. A second start converted this image and powered the VM on at 18:18 Europe/Amsterdam. The user showed About on that desktop at 18:23 Europe/Amsterdam: Build 10023, session up 5 minutes 43 seconds. The user confirmed the text box, the slider, the scrollbar arrows, the option menu, the translated folder names, and the full icon labels on that session.

### Text control, slider, scrollbar, and option popup

Four Interface Kit bugs, applied by `scripts/apply-ui-bugs.py`. They are on the build 10023 image. The object compile below was not a boot. VirtualBox was still on build 10022 then.

`BTextControl::SetText()` always went through the private text view, and that view sent the modification message for every insert and delete. Programmatic `SetText()` now suppresses that message. Typing still sends it (#11735).

`BSlider` sent its message only when the thumb moved. A click on the bar or the thumb now sends the message even when the value stays the same. A click in the padding still does nothing. The modification message is still only sent when the value changes (#13680).

Scrollbar arrows stayed enabled when the thumb already covered the whole range. They stay off when the minimum is below the maximum and the proportion is at least 0 and below 1. Proportion 0 is still the non-proportional mode, so those arrows keep working (#6873).

`BOptionPopUp::SetValue()` stored a value that was not one of the options, so the control value and the marked item disagreed. An unknown value is now ignored. A menu that is not created yet still stores the value (#18648).

`jam -q '<libbe!x86_64>interface_kit.o'` on 1 Nov 2026 at 18:06 Europe/Amsterdam updated `OptionPopUp.o`, `ListView.o`, and `interface_kit.o` and exited 0. The text-input objects were linked into `interface_kit.o` in an earlier run the same evening, after a duplicated declaration in the header was removed. That run updated `TextControl.o`, `TextInput.o`, `ListView.o`, and `interface_kit.o` and exited 0. `ScrollBar.o` and `Slider.o` were already compiled. `libbe.so` was not relinked. Not booted.

### Well-known folder names and icon labels

Two Tracker bugs, applied by `scripts/apply-tracker-desktop.py`. They are on the build 10023 image. The `libtracker.so` compile below was not a boot.

`system`, `home`, `apps`, `preferences`, `develop`, and `config` have catalog strings, but Tracker only showed them when the folder carried a `SYS:NAME` attribute. If that attribute is missing, the name now comes from the same catalog context (#15527). Other folders are unchanged (#10200 stays open).

A name fitted to the list column stayed truncated after switching to icon view. Icon view refits when the stored width is still the column width (#15444).

`jam -q libtracker.so` on 1 Nov 2026 at 17:37 Europe/Amsterdam updated 3 targets (`Model.o`, `WidgetAttributeText.o`, and the library link) and exited 0. Not booted. VirtualBox was left on build 10022.

### Leaf menu scroll arrows

The leaf menu on build 10021 drew an up arrow and a down arrow while the items still fit, with empty desktop below the menu. `BMenu::_CalcFrame` insets the screen by 4px so a menu border stays on screen (#13431). That same rectangle decided scrolling. The leaf menu starts at the top of the screen, which is above the inset, so scrolling turned on and `AttachScrollers()` added the arrows.

Scrolling now uses the real screen. The 4px margin is still used when placing a menu that would hang off an edge. `jam -q '<libbe!x86_64>interface_kit.o'` on 1 Nov 2026 at 17:22 Europe/Amsterdam updated 2 targets (`Menu.o` and `interface_kit.o`) and exited 0. `jam -q @r5-raw` at 17:25 Europe/Amsterdam exited 0 and updated 1725 targets. `libbe.so` was relinked. About is build 10022. The package check printed `mesa_packages_ok` and no Mesa 22. The 10021 session was still running when this image was built. The first restart compared the disk time after power-off, so VirtualBox booted that old disk again. `scripts/run-vbox-r5.ps1` now keeps the time from before power-off. A forced convert wrote the 10022 image and the VM started at 17:27 Europe/Amsterdam. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`. Do not Reset.

### Build 10021 daily

What's new for the list, Find, and clock fixes is build 10021. `jam -q @r5-raw` on 1 Nov 2026 at 17:11 Europe/Amsterdam exited 0 and updated 1256 targets. About was relinked after What's new was copied. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. The pin script did not run for this jam because the launch copy was not executable. The image still used the Mesa 25 repository files already on disk. Not booted in this record. VirtualBox is started from this image afterwards.

### Tracker keys, Find title, and Deskbar replicant label

Three more desktop fixes, applied by `scripts/apply-tracker-desktop.py`. Not on the daily image. Not booted.

Ctrl+Up and Ctrl+Down in a Tracker window follow Home and End. In list view that selects the first or last item. In icon view it scrolls to the top or bottom. Command+Up is still Open parent (#9974).

Find used to keep the first query file name, so a second search left the result window on the old title. The file is renamed from the current name, formula, or attribute line before the result opens again (#4154). A name typed by the user is left as it is. `UserSpecifiedName()` still returns nothing, so that path is unused.

The Deskbar menu item is “Show replicants handle” (#7644).

`jam -q libtracker.so` on 1 Nov 2026 at 15:58 Europe/Amsterdam updated 3 targets (`FindPanel.o`, `PoseView.o`, and the library link). `jam -q Deskbar` updated 2 targets (`DeskbarMenu.o` and the Deskbar link). Both printed `updated` and the jam process exited 0. Not booted.

### Tracker sort, location field, and Deskbar clock

Three more Tracker and Deskbar bugs, still applied by `scripts/apply-tracker-desktop.py` (`replace_once` on the existing tree). No new overlay script. Not on the daily image. Not booted.

Shift-click on a list column header now cycles the secondary sort: ascending, descending, then off. A plain click clears the secondary sort. The header triangle for that column follows its own direction. The reversed secondary order is kept for the open window and is not written into the folder view-state attribute, which has only one reverse flag (#14900).

Command+A in the single-window location field selects the path. A window filter runs before the Window menu shortcut, and only when that text view has focus, so Select all in the menu still selects the files (#16968).

A vertical Deskbar clock that is wider than the bar no longer slides the string off the left edge. Draw shortens it with an ellipsis: the weekday stays when it is shown, otherwise the clock digits stay (#14846). The earlier preferred-width margin did not stop a cut first letter.

Open With drops Tracker add-ons and no longer compares the running Tracker against an uninitialized `app_info` (#12859). That was already in `libtracker.so` from the earlier compile. This jam rebuilt that library, so the same binary has #12859 as well.

`jam -q libtracker.so` on 1 Nov 2026 at 15:22 Europe/Amsterdam updated 36 targets and exited 0. `jam -q Deskbar` updated 2 targets (`TimeView.o` and the Deskbar link) and exited 0. Not booted. The VirtualBox session that is already running was not restarted.

### Desktop icon no longer freezes Tracker

VBox Share is a directory on the Desktop with another filesystem mounted on it. Placement gave up by putting a late icon on the first grid cell, which was already taken, so the share sat on top of another icon. Dragging it then saved the icon position with `WriteAttr` on that mount. That call blocks, the Desktop window thread stops, and folders no longer open.

`scripts/apply-tracker-desktop.py` now keeps scanning for a free cell instead of dropping the icon on the first one, and stops after 400 cells. Saving positions skips a mount point (`stat` device differs from the parent) and clears the save flag, so Tracker does not open that filesystem to write the attribute. The icon still moves until the next boot. `jam -q libtracker.so` updated 35 targets and exited 0. `jam -q @r5-raw` on 1 Nov 2026 at 15:14 Europe/Amsterdam exited 0 and updated 1256 targets. The package check printed `mesa_packages_ok` and no Mesa 22. Not booted. The VirtualBox session that is already running is still the previous disk.

### Tracker and Deskbar batch

Thirty open Tracker and Deskbar bugs, applied by `scripts/tracker-deskbar-more.diff` from `scripts/apply-tracker-desktop.py`. No new overlay script. The patch is against the r5 Haiku tree after the existing overlays. It does not replace those overlays.

Tracker: the copy status buffer terminated a long name with the character `0` (#5338) and the window width ignored the font (#16185). A non-finite finish estimate became 13 Dec 1901 (#11176). Escape clears the selection when it is not clearing a cut or type-ahead (#9216). List rows use the font's ascent, descent, and leading (#16853). A stat change with no field mask still re-sorts (#20261). Scrolling a just-shown file updates the scroll range first (#16643). Single-window Open parent selects the folder you left (#11029). Find starts in the open folder (#19546). The Add-ons menu is rebuilt when the menu bar opens (#11495). The item count no longer uses the scrollbar's minimum width (#16726). Find's most-used type still fills the attribute row (#11402). Open With no longer writes into the MIME type string (#8551) and restores its frame after layout, in 12-point units (#19520, #8772). A narrow rating column and a date `parsedate` cannot read no longer crash or store year 1901 (#19661, #19663). The replace question uses `BDateTimeFormat` (#12335). The volume space bar invalidates the whole pose (#19573). Get Info refreshes the icon after identify (#8544), keeps the path tip on the current workspace (#8543), can be resized (#19433), and says when a name cannot be changed (#9383).

Deskbar: the leaf menu says Shut down and Restart, matching the question (#18667). An empty Recent menu gets a disabled item instead of a huge frame (#7328). Auto-raise is applied again when the workspace changes (#16359). A vertical Deskbar's minimum height includes one icon row (#15975). The lower scroll arrow is moved back to the visible edge (#15910). Menus stay 4px inside the screen (#13431). Escape always quits menu tracking (#16144).

`jam -q libtracker.so` and `jam -q '<libbe!x86_64>interface_kit.o'` compiled the Tracker objects and `Menu.o`, then linked `libbe.so` (the unknown target name `deskbar` made that command exit 1 after those links). `jam -q Deskbar` then updated 18 targets and exited 0. `jam -q @r5-raw` on 1 Nov 2026 at 12:02 Europe/Amsterdam exited 0 and updated 1724 targets. The image is `generated.x86_64/r5-os.image`, 2 GiB. About was relinked after `r5_whatsnew.txt` was copied, and `r5_os_version.h` says build 10019. The package check printed `mesa_packages_ok` (libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, LLVM 21 runtime, Vulkan loader) and no Mesa 22 package. Not booted. Not tested on a LAN. The VirtualBox session started earlier is still the previous disk.

### Deskbar closes the other menu bar before opening one

No commit on `upstream/master` (fetched 1 Nov 2026) names #20099. The leaf menu and the running-apps list are two `BMenuBar`s in one window. Each one installed the window’s menu semaphore and left the other menu tracking.

`scripts/apply-deskbar-bugs.py` now stops the other menu bar before `StartMenuBar` tracks, and waits outside the window lock so that tracking thread can exit. When a menu finishes, it clears the window semaphore only if that semaphore is still its own. A popup does the same, so an older popup thread does not wipe a menu bar that started later.

`jam -q '<libbe!x86_64>interface_kit.o'` compiled `MenuBar.o`, `PopUpMenu.o`, and `Window.o` (47 targets updated, exit 0). Not booted. The other open Deskbar tickets (crashes, layout, enhancements) are not in this change.
### Large fonts no longer clip box and separator labels

No commit on `upstream/master` (fetched 1 Nov 2026) names #11625, #18518, or #18813. `scripts/apply-ui-bugs.py` changes the label metrics.

A `BBox` label used to shift its baseline up by one sixth of the line height, which drew the top of the glyphs above y=0. The label box is now the full line, including leading, and the baseline sits on the ascent (#11625, #18518). A `BSeparatorView` section title uses that same line height and is centered in the label band when the view is taller (#18813).

The same script used to insert another `#include <Region.h>` into `ListView.cpp` on every apply, because the new text still contained the old `#include <Window.h>` line. A second apply now leaves a single include.

`jam -q '<libbe!x86_64>interface_kit.o'` compiled `Box.o`, `SeparatorView.o`, and `ListView.o` (47 targets updated, exit 0). Not booted.
### Control colors for flat buttons, scrollbar arrows, and plain text

Haiku’s June 2025 control-color rework (`91bed92`, “use B_CONTROL_BACKGROUND_COLOR”) is already in this tree. Nothing newer on `upstream/master` (fetched 1 Nov 2026) names #19629, #19630, #20418, or #20372. Gerrit 11871 for #20372 was abandoned. The leftovers are applied from `scripts/apply-darkmode.py`.

Scrollbar arrow buttons use `B_CONTROL_BACKGROUND_COLOR` and `B_CONTROL_TEXT_COLOR`. The trough stays the panel color (#19630). A flat `BButton` takes its parent’s low color, so hover does not jump to the control gray (#19629). `SetViewColor()` still clears the UI color; that face is now the button color, with black or white text when the control text would have no contrast (#20418). High and low color changes inside an overridden `Draw` stay ignored, which is what LibLayout in Wonderbrush relies on. A non-stylable `BTextView` updates its single run when the document text color changes (#20372). Styled runs are unchanged: a run does not record which UI color it came from.

`jam -q '<libbe!x86_64>interface_kit.o'` from `generated.x86_64` updated 47 targets and exited 0, including `Button.o`, `ScrollBar.o`, and `TextView.o`. Not booted. R5OS sign-in, lock, and Users buttons do not call `SetViewColor()` on the button, so they keep the system control color.

### GLTeapot draws on the VirtualBox daily

2026-11-01 about 9:21 Europe/Amsterdam. The guest clock read 8:21 because the VM uses UTC. The user opened GLTeapot on the daily VirtualBox session. The window showed a red teapot and 387 FPS. That disk is the Mesa 25 / libglvnd image. No renderer string was captured on that boot. The 5625U was not the machine.

The same session reached ACPI S5 (`ACPI: Entering S5 power state` in `VBox.log`, about 88 seconds after power on) and VirtualBox stayed in `POWERING_OFF` with no `POWERED_OFF`. The window froze on “Shutting down...”. The NIC is bridged to the MediaTek MT7921. The process was ended from the host. That hang is not fixed here.

`docs/GRAPHICS.md` is the GPU plan for this image. `docs/LOOK.md` is the window-chrome plan. `docs/KERNEL.md` is the CPU audit. Those files had only existed on the local `cursor/graphics-plan` checkout, whose `distro/BUILD` was still 10013. The source edits in that checkout already match `main`.

### OpenGL apps compile against libglvnd, not Mesa 22

`scripts/pin-mesa-packages.sh` runs before jam. It replaces the HaikuPorts pin `mesa-22.0.5-3`, `mesa_devel-22.0.5-3`, and `mesa_swpipe-22.0.5-3` with libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel` 25.3.6, Lavapipe, the LLVM 21 runtime, and the Vulkan loader. `mesa_devel` carries the Mesa 25 headers and libglvnd's `libGL.so` (the real file, not a symlink into a Mesa 22 base package). Haiku's `GLView.h` and `GLRenderer.h` from the old devel package are copied unchanged.

The image solver no longer sees Mesa 22. `scripts/pin-mesa-packages.sh` builds `download/r5-haikuports.repo` from the HaikuPorts catalog with those four Mesa 22 packages removed, and `download/r5-opengl.repo` for the R5OS packages (a different vendor, so they cannot share one catalog). `distro/UserBuildConfig` points the image at those files and installs the packages. A solver check with `glu` and `haiku.hpkg` asked for `libglvnd-1.7.0-1`, not Mesa 22. `jam -q @r5-raw` then exited 0 and updated 1230 targets. It extracted `mesa-25.3.6-1` and relinked GLTeapot. `develop/lib/libGL.so.1.0.0` in that tree is 954288 bytes, the libglvnd file. The image check printed `mesa_packages_ok`: libglvnd, Mesa 25, `mesa_devel`, Lavapipe, the LLVM 21 runtime, and the Vulkan loader, and no Mesa 22 package. That jam itself was not booted. The VirtualBox sitting is the section above. No new build number.

### About on the Mesa image still showed Build 10017

The version line in About was already Build 10018. What's new was still the Build 10017 license text. `scripts/jam-r5-raw.sh` wrote `r5_os_version.h` and did not copy `distro/WHATSNEW`. The What's new resource is rebuilt when `AboutSystem.rdef` is newer than the last resource compile, not when only the text file changes. The script now copies What's new and touches that rdef before jam. No new build number: this is the Build 10018 section that was already in `distro/WHATSNEW`.

The daily `haiku.hpkg` was rebuilt (`jam -q haiku.hpkg`, exit 0) and replaced on `r5-os.image`. The AboutSystem inside it contains `R5OS 2026.1-dev Build 10018 - OpenGL`. Mesa 25 and libglvnd stayed on that image. VirtualBox was pointed at the updated disk. The About window was not opened from here.

### Read AMD P-states and Intel HWP without writing them

2026-10-31 about 16:00 Europe/Amsterdam. The kernel prints a read-only CPU snapshot and publishes it at `/dev/misc/r5_pstate`. Nothing in this change writes an MSR. On AMD and Hygon, if the hardware P-state bit is set, it reads the current P-state and P-state table. Family 17h and 19h use the same 200 MHz × fid / did formula and the same divisor checks as the existing TSC path. If the CPPC bit is set, it also reads the CPPC capability, enable, request, and status registers. On Intel, if HWP is enumerated, it reads the capabilities and the enable register, and the request register only when HWP is already enabled. If the hypervisor bit is set, it prints that line and does not read those MSRs, so a missing register cannot panic QEMU.

`/system/bin/r5_pstate_log` starts when the boot volume is mounted. It reads the device, burns the CPU for one second, reads again, and writes `/boot/home/r5-cpu.txt`. It also looks for a FAT partition under `/dev/disk`, mounts it, writes `r5-cpu.txt` there, and unmounts. A raw image has no FAT partition. An anyboot image does, on the EFI system partition, which Windows can open from the USB stick.

The first image build left `r5_pstate_log` out of the package: the bin list in `build/jam/images/definitions/minimum` is explicit. A second apply also duplicated the Jamfile and launch hooks, because the inserted text still contained the search text. The script now stops when a marker is already present. The boot below used the image after both fixes.

QEMU was TCG with `-cpu qemu64`. This shell could not open `/dev/kvm` (permission denied). Serial showed `publish misc/r5_pstate status 0`, vendor amd, family `0xf`, model `0x6b`, name `QEMU Virtual CPU version 2.5+`, TSC about 2297152544 Hz, and `hypervisor bit set, hardware MSRs not read`. There was no panic. After shutdown, `bfs_shell` read `/boot/home/r5-cpu.txt` with both samples and `FAT: no FAT partition found under /dev/disk`. The Ryzen 5 5625U was not booted. No P-state was written.

`jam -q @r5-anyboot` then exited 0 and wrote `generated.x86_64/r5-os-anyboot.iso` (2.1G, 16:03 Europe/Amsterdam). `fdisk` shows a 2G BeFS partition and a 2.8M EFI partition (type `ef`). That ISO was not booted.

### Mesa 25 replaces Mesa 22 on the daily image

Build 10018. The jam pin still downloads Mesa 22.0.5 so OpenGL apps can compile. `scripts/install-mesa-packages.sh`, run at the end of `scripts/jam-r5-raw.sh`, then removes `mesa-22.0.5-3`, `mesa_devel-22.0.5-3`, and `mesa_swpipe-22.0.5-3` from `r5-os.image` and adds the hpkgs in `distro/packages/x86_64`: libglvnd 1.7.0 (`libGL.so.1`), Mesa 25.3.6 (`libEGL_mesa.so.0` plus `50_mesa.json` with `./libEGL_mesa.so.0`), Lavapipe, a private LLVM 21.1.8 runtime that provides only `lib:libllvm_21` (so it does not clash with `llvm12_libs` on `liblto` / `libremarks`), and the HaikuPorts Vulkan loader 1.4.367.

QEMU test image before this jam, TCG, one CPU, GTK, no KVM (this user is not in group `kvm`). `/boot/home/mesa-test.txt`: `renderer=llvmpipe (LLVM 21.1.8, 128 bits)`, `vendor=Mesa`, `version=4.5 (Compatibility Profile) Mesa 25.3.6`. The probe locked, swapped, unlocked, and exited. An earlier boot of that same image showed GLTeapot with a drawn teapot, and Get Info on `libGL.so.1` showed package 1.7.0-1. The regular image was still Mesa 22 at that point. Lavapipe was not the library that probe loaded. The 5625U was not used.

### Licenses tab uses What's new headings and Haiku's component list

Build 10017. Section titles on the Licenses tab are green and bold, like What's new. Lines that are only a web address are links. After that summary, the tab lists the same components Credits already lists (libprint and the rest), with the same license-file and site links. Credits itself is unchanged. The first compile failed because this Haiku `BString` has no `Contains`; the heading test uses `FindFirst(".")`. AMD64 jam then finished with exit 0 (1105 targets). The tab was not opened from here.

### About has a Licenses tab

Build 10016. About's Credits tab no longer carries the VirtualBox GPL paragraph. That text, plus the R5OS, Haiku, and AirPlay license notes, is a Licenses tab. Credits still names people and projects, including a short VirtualBox line. The tab words are `distro/about/licenses.txt`. AMD64 jam of the SPDX headers (pull request 92) finished with `jam_exit=0` and updated 1310 targets. The Licenses tab itself has not been opened on a booted screen.

### SPDX headers on R5OS-owned source

Files that R5OS wrote as a whole now carry `SPDX-FileCopyrightText: 2026 R5OS Contributors` and `SPDX-License-Identifier: GPL-3.0-or-later`. That covers the new Login pieces (lock screen, session helper, tray), Users & Accounts, the IPv6 and Wi-Fi code, AirPrint, the COM1 clipboard, mDNS, and the AirPlay Deskbar program. Four IPv6 headers had said MIT under a 2026 R5OS copyright. That grant is now GPL-3.0-or-later.

Haiku files copied under `distro/` keep their original copyright and license. Edits inside those files were not retagged. The AirPlay RAOP server stays MIT because the flow follows Shairport. ALAC stays MIT (David Hammerton). tiny-AES-c is tagged Unlicense, and the text is `LICENSES/Unlicense.txt`. `scripts/` still has no SPDX headers. No image was rebuilt for this.

### The Desktop share is named VBox Share

2026-10-31 about 14:25 Europe/Amsterdam. Build **10015**, GUI VM, UART `generated/vbox-daily.log`. VirtualBox rejects a space in a share name, so the host mapping stays `r5share`. Sign-in mounts it at `/boot/home/Desktop/VBox Share`. Serial shows `mounted r5share /boot/home/Desktop/VBox Share`, `vboxsf: mount(r5share)`, and `publish_vnode(): 0`. The guest opened the existing `from-host.txt`. There is no `VbglR0SfCreate: -104` and no panic line. The Desktop icon was not watched from here.

`-104` is `VERR_INVALID_NAME`. The driver was sending `.` and `..` to the host. Those names now stay in the driver: `.` is the directory itself, and `..` is its parent. At the share root the VFS walks out to the Desktop.

A relink of `vboxsf` first produced a read-only `PT_LOAD` and a GNU hash. The kernel did not load that file (`General system error`, and `Bad data` when the segment base was `0x400000`). The module that loads uses `-z noseparate-code -z norelro --hash-style=sysv`: SysV hash, `.rodata` in the executable segment, same shape as the module from the 13:14 boot. `generated/r5-os.vdi` was not replaced. QEMU was not used. Unicode names, a large new file, unmount, and a host disconnect were not tried.

### Daily image follows the VirtualBox window and mounts host shares

2026-10-31 13:14 Europe/Amsterdam. Build **10015**. `HAIKU_IMAGE_SIZE` is 2048. `jam -q @r5-raw` exited 0 and wrote `generated.x86_64/r5-os.image` at 2147483648 bytes. `bfs_shell` reports 1.2G free of 2.0G (622564 blocks of 2048), volume name R5OS. `generated/r5-os.vdi` was not replaced.

Sign in starts `VBoxService`, `/system/non-packaged/bin/r5-vbox-shares`, and `VBoxTray` only when `/dev/misc/vboxguest` exists. The script asks `VBoxControl` for every host share and runs `mount -t vboxsf -p <name>` on `<home>/Desktop/<name>`. QEMU and real hardware do not have that device, so the same sign-in returns without mounting. This image was not booted in QEMU. The user launch file no longer starts a second `VBoxService` or `VBoxTray`.

The GUI VM was powered off and its disk swapped again (`generated/r5-os-vboxid.vdi`, UUID `3eab20a2-9599-4ac2-8353-0f1a436ce727`). The previous 2 GB disk is `generated/r5-os-vboxid-before-pkgfs.vdi`. Host log `VBox.log` opened 2026-10-31T12:10:18Z (13:10 Europe/Amsterdam). At about four minutes it reports graphics capability `0x4` and `Display::i_handleDisplayResize` from 1024×768 to 1160×775. Serial `generated/vbox-daily.log` prints `r5-vbox-shares: mounted r5share /boot/home/Desktop/r5share`, `vboxsf: mount(r5share)`, and `publish_vnode(): 0`. The guest then opened `from-host.txt` and `from-guest.txt` (the files already in `generated/vbox-share`). Lookup of `..` inside the share returns -104. No panic line. The picture was not watched from here, and pointer clicks after the mode change were not tried. Unicode names, a new large file, unmount, and a host disconnect were not tried. Files inside a share stay uid 0; the mount point is the signed-in Desktop, which is mode 0700 before the mount. Another account was not used.

Two earlier 2 GB boots missed the share. The first script was saved with CR LF, so Haiku `sh` did not run it. The second script was installed with `AddFilesToHaikuImage system bin`. packagefs covers `/boot/system`, so `/system/bin/r5-vbox-shares` was not there. The session log says `r5-vbox-shares: No Error (127)` about 14 ms after launch. `VBoxTray` still started on that boot, which is why the window size followed while the share stayed unmounted. The script is now `system non-packaged bin`, same place as `VBoxTray`. Writes to `/dev/dprintf` from the script did not appear on the serial log. The `vboxsf` lines did.

### Daily image installs the VirtualBox guest additions

2026-10-31 11:49 Europe/Amsterdam. The daily raw image `generated.x86_64/r5-os.image` was jammed again (`jam -q @r5-raw`, exit 0) after build **10014**. `bfs_shell` on that image lists the guest files and the source. Free space afterwards is 368.0 MB of 1.2 GB. `generated/r5-os.vdi` was not replaced.

At 12:00 the throwaway VM was powered off and its disk was swapped for a fixed VDI of that image (`generated/r5-os-vboxid.vdi`, same UUID as before). The previous throwaway disk is `generated/r5-os-vboxid-before-daily.vdi`. Around 12:04 the user said this boot works. The window resize did not follow. The host share `r5share` was still defined on that VM and was not mounted; this image has no automount job. No panic line was in `generated/vbox-daily.log` through the USB driver. First-boot processing of 129 packages is in that log. Tracker on a share was not tried.

Installed under `/boot/system/non-packaged`:

- `add-ons/kernel/generic/vboxguest` (274024 bytes) and `add-ons/kernel/drivers/dev/misc/vboxguest` (20976 bytes, the device stub built as `vboxdev`)
- `add-ons/kernel/file_systems/vboxsf` (55256), `add-ons/kernel/drivers/dev/graphics/vboxvideo` (34424), `add-ons/accelerants/vboxvideo.accelerant` (43632)
- `bin/VBoxService`, `bin/VBoxTray`, `bin/VBoxControl`

`data/licenses/virtualbox/` has the unmodified GNU GPL version 3 (`COPYING`, 35147 bytes), `VirtualBox-7.2.22.tar.bz2` (257289481 bytes, sha256 `440a2b3eeb46e7e992961b70ef24f43e95b3c99fa2ab30aac20b963ef9e91ad7`), `virtualbox-7.2.22-r5os.patch`, the build scripts, and `SOURCE.txt`. The same GPL text is also the packaged `GNU GPL v3` file. About R5OS states that this build includes GNU GPL version 3 code and names that directory. The What's new text for build 10014 is in the About binary. The user launch file in `haiku.hpkg` starts `VBoxService` and `VBoxTray`. It does not mount a share.

The mouse filter is not installed. VirtualBox clipboard mode and drag-and-drop stay off in the product script. The clipboard R5OS starts is still the serial one. The binaries and the tarball are not in git. `scripts/apply-vbox-guest.py` stops if the local build or the tarball is missing.

A first jam in this same run deleted the previous `r5-os.image`. Jam was started without `PWD`, so the image script names stayed relative and dash could not source them. The second jam was a login shell, `PWD` was set, and it wrote the image above. The one shell copy remains the throwaway result from earlier today. Tracker was not opened on a share. QEMU was not used for this image.

### Throwaway shared folder reads and writes one small file

2026-10-31 10:54 Europe/Amsterdam, headless, throwaway `generated/r5-os-vboxid.vdi`, UART `generated/vbox-sf6.log`. The machine mapping `r5share` is `generated/vbox-share`. A launch job ran `mount -t vboxsf -p r5share /boot/home/vbox-share` after boot. Haiku's `mount` passes the share name with `-p`; a bare extra argument is the device, and this file system rejects a device. Serial shows `vboxsf: connected`, `vboxsf: inited successfully`, and `publish_vnode(): 0`, then an open of `/from-host.txt`. Guest property `R5Sf` is `host=R5OS-SF-HOST` at 09:54:23Z. That matches the 12-byte host file `from-host.txt`. The host directory then also had `from-guest.txt`, 11 bytes: `from-guest` plus the newline from `echo`. No panic line in that log. Tracker was not opened on the share. Unicode names, a large file, unmount, and a host disconnect were not tried. Not the daily image. The binaries are not in git.

Two earlier headless boots never reached `mount`. The kernel loads every file system module while it is still scanning drivers. `init_module` called the host then, before `drivers/dev/misc/vboxguest` existed, and `generated/vbox-sf3.log` stops at `file_systems/vboxsf: B_MODULE_INIT`. The connect now waits for the first mount. That mount then stopped after `vboxsf: guest module loaded` (`generated/vbox-sf5.log`). `VbglR0SfInit` takes `RTSemMutexRequest`, and Haiku `RTSemMutexCreate` created the semaphore with count 0, so the first acquire waited forever. The count now starts at 1. A release compared `OwnerId` with `-1` and did not clear it; it now assigns `-1`. A recursive acquire incremented the thread id; it now increments the recursion count. The mutex code is linked into the `vboxguest` module. `vboxsf` calls it through that module. Drag and drop was not started. It is GitHub issue 89. The build fixes against the Oracle 7.2.22 tarball are `distro/vbox-guest/virtualbox-7.2.22-r5os.patch`. The tarball and the binaries are not in git. Build steps: `distro/vbox-guest/README.md`.

### Throwaway guest mode follows a host size hint

2026-10-31 10:01 Europe/Amsterdam, GUI, throwaway `generated/r5-os-vboxid.vdi`, UART `generated/vbox-resize.log`. This boot stopped at Sign in. Enter with an empty password opened the desktop. Deskbar listed Tracker only. `VBoxTray` was not in that list. The app resource `BEOS:APP_FLAGS` is now in the `VBoxTray` binary (`B_BACKGROUND_APP`); the earlier binary had the resource file but it was not attached.

`VBoxDisplayService` runs in that same process, next to the clipboard. It is not a Deskbar replicant. After the desktop was up, `setvideomodehint 1280 800 32` changed the guest framebuffer from 1024×768 to 1280×800. The picture stayed the normal desktop (icons, Deskbar, the Haiku logo). No panic line in the serial log. At 10:06 Europe/Amsterdam the user showed the VirtualBox window itself: the desktop filled that window, Deskbar still listed only Tracker, and the guest clock read 9:06 AM. They said the resize works. Pointer clicks after the change were not tried. Not the daily image. The binary is not in git.

### Throwaway Screen prefs names the VirtualBox display

2026-10-30 22:59 Europe/Amsterdam, GUI, throwaway `generated/r5-os-vboxid.vdi`, UART `generated/vbox-video3.log`. Screen says `Virtual display (VirtualBox Graphics Adapter)`, 1024×768, 24 bits/pixel. The desktop is the normal Tracker picture, not a black screen. Serial shows `VBoxVideo: found device graphics/vd_80EE_BEEF_000200` and `loaded driver .../drivers/dev/graphics/vboxvideo`. The packaged `vesa` driver still loads; Screen is not using it. No panic line in that log. `VBoxDisplayService` is not running, so a window resize was not tried. The 60 Hz line in Screen is the placeholder timing in the driver, not a measured rate. Only the current mode is published. Not the daily image. The binaries are not in git.

The first copy of this driver was installed under `drivers/graphics/` (no `dev/`). The 20:42 GUI boot (`generated/vbox-video.log`) loaded only `.../dev/graphics/vesa` and had no `VBoxVideo:` line. After the move into `drivers/dev/graphics`, the 20:46 boot loaded the driver and mapped the framebuffer (`assuming memory type` for `0xe0000000`), and Screen still said `VESA driver (Generic VESA)`. The accelerant had been linked as a guest executable with hidden visibility, so `get_accelerant_hook` was not in the dynamic symbol table and `app_server` fell back to VESA. The hook is now default-visibility, and the link keeps it (`-Wl,--export-dynamic -Wl,-u,get_accelerant_hook`). That binary is 43632 bytes at `non-packaged/add-ons/accelerants/vboxvideo.accelerant`. Deskbar still lists `VBoxTray` on this boot.

### Throwaway additions boot in QEMU with no guest device

2026-10-30 20:21 Europe/Amsterdam. QEMU `pc`, TCG, `qemu64`, 2048 MB, 2 CPUs, std VGA, serial `generated/qemu-nodevice.log`. The disk was `generated/r5-os-vboxid-emptyout.raw`, a copy of the throwaway that already has `vboxguest`, `VBoxService`, and `VBoxTray`. KVM in this shell returned permission denied, so this was TCG. PCI in the log is `8086:1237`, `8086:7000`, `1234:1111` (display), `1af4:1000` (virtio-net), and `8086:2668` (HDA). No `80ee:cafe` and no `80ee:beef`. `vesa_init()` completed. `package_daemon` registered `/boot`. No panic line and no `vboxguest` load line. The guest tray log, mtime 20:22, ends with `tray start` and `init -102` (`VERR_FILE_NOT_FOUND`). That is `VbglR3Init` not finding `/dev/misc/vboxguest`, after the tray job had already waited for `app_server`. `vbox-service.log` stayed at 20:05: the service opens that file only after init succeeds. The screen was not watched, so Tracker and Deskbar were not seen. QEMU was killed from the host after the log had stopped growing; the guest did not shut itself down. The VirtualBox VM was not the disk under test.

### StyledEdit text reached Windows Notepad

2026-10-30 20:13 Europe/Amsterdam, GUI, throwaway `generated/r5-os-vboxid.vdi`. The user copied text in StyledEdit and pasted it into Windows Notepad. Deskbar listed `VBoxTray` next to Tracker. That entry is the running clipboard process, not a replicant. `VBoxTray.rdef` now sets `B_BACKGROUND_APP` so Deskbar skips it. The binary on this disk does not have that flag yet. Not the daily image.

### Throwaway VBoxControl no longer panics, and a 32 KB host paste is on disk

2026-10-30 19:55 Europe/Amsterdam, headless, `generated/r5-os-vboxid.vdi`, UART `generated/vbox-panicfix.log`. The 19:12 panic was `_mutex_lock()` with interrupts disabled inside `RTSemEventMultiCreateEx`, called from `vgdrvWaitAlloc` during an HGCM disconnect. Two Haiku IPRT mistakes in the VirtualBox tree outside git caused it. `RTSpinlockAcquire` stored the saved interrupt state before `acquire_spinlock`. The other CPU, including the ISR where `disable_interrupts()` returns 0, could overwrite that field, and release then left interrupts disabled for the next `malloc`. The state is now saved in a local, stored only after the lock is owned, and copied out before release. `RTSemEventMultiCreateEx` and `RTSemMutexCreate` also treated `create_sem` failure (`sem_id < B_OK`) as success, so a real semaphore was freed and the call returned `VERR_TOO_MANY_SEMAPHORES`. An interruptible HGCM wait then returned `VERR_INTERRUPTED` immediately. The driver sent `HGCMCancel2`, and the tray’s `MSG_CANCEL` completed a host message that was not in process. That is the `HGCMThread::MsgComplete` assert (`0x80000003`) about 50 ms after `New Clipboard API enabled`. The success test is now `sem_id >= B_OK`, matching the event and fast-mutex files.

This boot ran the `VBoxControl` probe. The guest set `R5Clip=ready` at 19:55:58. There is no panic line in the UART log and the kernel debugger did not open. The host cleared the clipboard with `Clipboard.Clear()` (`Set-Clipboard -Value ""` throws here). The guest file `/boot/home/vbox-clip-empty-in.txt` is 0 bytes, and the guest property recorded `empty-in bytes=0`. The host then placed 32768 bytes (`R5OS-LARGE-0000` through `R5OS-LARGE-2047`). After ACPI S5 (`VBox.log` `ACPI: Entering S5 power state` at 00:03:58, `showvminfo` `poweroff`) the image still had `/boot/home/vbox-clip-large-in.txt` at 32769 bytes: those 2048 lines, plus one trailing newline from `clipboard -p`. The same `VBox.log` has `New Clipboard API enabled` and no `Assertion Failed`. Headless, so no Windows dialog. The tray log still shows eight `wait -39` lines and then `stop -2909` (`VERR_HGCM_TOO_MANY_CLIENT_CALLS`). A second `tray start` in that log received `msg 3 formats 1`, which is the large paste. Not the daily image. The modules are not in git.

A later headless start the same evening (20:02 Europe/Amsterdam) put `R5OS-MARKER` on the Windows clipboard first. The guest recorded `empty-in bytes=12` (the marker plus `clipboard -p`’s newline). `clipboard -r` then left the Windows clipboard as `R5OS-MARKER` (length 11) at 20:03:50. The host service ignores an empty format report on purpose, so a guest clear does not wipe Windows. A rebuild that reports format 0 on a local clear was booted at 20:06; at 20:07:11 the host clipboard was still `R5OS-MARKER`. That direction is not shown. No `Assertion Failed` in that boot’s log either. `showvminfo` afterwards was `poweroff`.

### Throwaway desktop, mouse, resize, and guest shutdown

2026-10-30 19:16 Europe/Amsterdam, GUI, `generated/r5-os-vboxid.vdi`, `VBoxControl` probe replaced with `exit 0`. The user had already seen boot icons, sign-in, Tracker, and Deskbar on the previous start of this disk. On this start, movement, clicks, and scrolling worked. The pointer reached the edge of the guest screen and then moved on in Windows. The guest desktop did not follow a VirtualBox window resize, and shutdown from the guest produced no Windows error. `showvminfo` then said `powered off` since 18:16:33Z. `VBoxDisplayService` was not running, so the picture stays the VESA framebuffer. No second mouse was added. Not the daily image. The `VBoxControl` panic from 19:12 is not fixed.

### VBoxControl panics the throwaway after the desktop is up

2026-10-30 19:12 Europe/Amsterdam, GUI boot of `generated/r5-os-vboxid.vdi`. The user saw the boot icons, the sign-in screen, a successful sign-in, Tracker, and Deskbar. Then the kernel debugger opened. Thread `VBoxControl` on CPU 1. Panic: `_mutex_lock()` called with interrupts disabled. The stack runs from the syscall through `vgdrvHaikuIOCtl`, `VbglR3CommonIOCtl`, `vbglR3HGCMInternalDisconnect`, `vgdrvWaitAlloc`, and `RTSemEventCreateEx` into `RTMemAllocEx`. The probe that calls `VBoxControl` was replaced with `exit 0` on the next image so the desktop can be looked at. `VBoxTray` is still started. The panic is not fixed. Not the daily image.

### Throwaway guest can place 32 KB of text on the Windows clipboard

Headless boot of `generated/r5-os-vboxid.vdi`, 2026-10-30 18:49 Europe/Amsterdam, UART `generated/vbox-clip5.log`, this VM’s clipboard mode still bidirectional. The guest built 2048 lines `R5OS-LARGE-0000` through `R5OS-LARGE-2047` (32768 bytes) with `clipboard -i`. `Get-Clipboard -Raw` on the host returned that length, that first line, and that last line. `VBox.log` for that start (opened 17:49:34Z) still has the `HGCMThread::MsgComplete` assert at 00:00:27 from `VBoxSharedClipboard.DLL`. Headless did not show the Windows `0x80000003` dialog, and the transfer above happened after the assert. The log then records power off to `OFF` at 00:03:36. `showvminfo` afterwards said `aborted`. The probe’s paste file was not on the disk; it never called `sync`. `Set-Clipboard -Value ""` throws in this PowerShell, and the host clipboard was already empty, so an empty transfer is not shown. The host’s 32768-byte paste was only acknowledged (`R5Clip=large-in`). Product clipboard mode is still off. COM1 is still the shipped path. Not the daily image.

### Throwaway clipboard text works in both directions

`VBoxService` 7.2.22 has no Haiku clipboard service. `VBoxTray` normally installs a Deskbar replicant and exits; that replicant also starts the display service. For this test `ReadyToRun` keeps the process up, starts only `VBoxShClService`, and does not load the replicant. The first bidirectional boot on `generated/r5-os-vboxid.vdi` (serial `generated/vbox-clip2.log`) logged `connect 0` and then no text. The host log shows `VERR_CANCELLED` while reporting formats to the guest and `VERR_TIMEOUT` while reading from the guest. `VbglR3ClipboardGetHostMsgOld` returned `VERR_INTERRUPTED` (-39) and the service thread treated that as fatal. A host read was also posted to the looper and the thread waited again immediately. The thread now cancels and retries an interrupted wait, and it handles the host message before the next wait. That change is in the VirtualBox tree outside git.

The next boot (host clipboard log has no timeout line) wrote `/boot/home/vbox-clip-paste.txt` with `R5OS-CLIP-IN` between `PASTE-BEGIN` and `PASTE-END`. `clipboard -c R5OS-CLIP-OUT` then left that string on the Windows clipboard. The tray log shows `msg 2 formats 1` and `write 0`. UART was `generated/vbox-clip3.log`. Empty clipboard and a large paste were not tried. Clipboard mode on `scripts/run-vbox-r5.ps1` is still disabled. COM1 is still the shipped path. The binaries are not in git and not on the daily image. `VBox-GPL-3.0.txt` on this throwaway is `LICENSES/GPL-3.0-or-later.txt`.

Shutdown of that boot opened the debugger. Team `VBoxTray`, thread segment violation in `__dynamic_cast`, called from `BApplication::_WindowQuitLoop` during `Quit`. `VBoxTray` is built `-fno-rtti`. libbe `dynamic_cast`s every looper to `BWindow` on quit, and this `BApplication` had no typeinfo. The original `ReadyToRun` called `exit(0)`, so it never reached that walk. `VBoxTray_CXXFLAGS` now adds `-frtti` (the template still passes `-fno-rtti`; `-frtti` is last). The following boot entered ACPI S5 (`VBox.log`: `ACPI: Entering S5 power state`) about 45 seconds after start. The serial log has no segment violation. Earlier in that same boot, at about 30 seconds, `VirtualBoxVM.exe` raised `0x80000003` (breakpoint) on the `ShClipboard` thread. `VBox.log` shows `HGCMThread::MsgComplete` asserting `(pMsg->m_fu32Flags & HGCM_MSG_F_IN_PROCESS) != 0` at `HGCMThread.cpp:578`, from `VBoxSharedClipboard.DLL` after `Unknown guest function`, 20 ms after `New Clipboard API enabled`. The Windows dialog says the exception is at `0x00007FFF19CEB61E`, which is that log's `ExceptionAddress`. VirtualBox then reported the machine state as `aborted`.

### Guest version properties show up on the throwaway

`vminfo` writes were `VERR_ACCESS_DENIED` because Haiku user-memory locks always asked for a writable mapping (`B_READ_DEVICE`) and, when the caller passed no team, looked the address up in the kernel team. Read-only guest strings then fault with `B_PERMISSION_DENIED`, which IPRT prints as `VERR_ACCESS_DENIED`. `memobj-r0drv-haiku.c` in the VirtualBox tree outside git now locks `B_CURRENT_TEAM` for that case, asks for a writable mapping only when the access includes write, and repeats that team and those flags on unlock. That file is not in this git tree.

The first boots of that module panicked. Serial `generated/vbox-vminfo.log` (two starts) and `generated/vbox-vminfo2.log` (cold start) stop in `interrupts.cpp`: interrupts were enabled after the handlers for vector 20. The routing table maps PCI `0:4` (`80ee:cafe`) to GSI 20. On the cold start the driver line was printed and the panicked thread was `VBoxService`, already waiting. No `/VirtualBox/GuestAdd/Version` was written. `vgdrvHaikuISR` now calls `disable_interrupts()` before it returns.

The next start (about 16:35 Europe/Amsterdam, serial `generated/vbox-vminfo3.log`) loaded `drivers/dev/misc/vboxguest`, did not panic, and reached the first-login script. `VBoxManage guestproperty enumerate` shows `/VirtualBox/GuestAdd/Version` `7.2.22`, `Revision` `175245`, `VersionExt` `7.2.22`, and `/VirtualBox/GuestInfo/OS/Product` `Haiku` with release `R1~beta6+development` and version `r5-2026.1-dev Oct 26 2026 16:02`. `LoggedInUsers` was `0` at that sample. At 16:38 Europe/Amsterdam the screen showed ProcessController on the desktop: Tracker, Deskbar, Login, `vbox-probe.sh`, and `VBoxService` (`-f -v -v`). Clipboard mode stayed disabled. The modules and `VBoxService` are not in git and not on the daily image.

### VBoxService stays up on the throwaway and sets the guest clock

`VBoxService` 7.2.22 was already on `generated/r5-os-vboxid.vdi`. The first launch that wrote a log (about 15:40 Europe/Amsterdam) opened `/dev/misc/vboxguest` and exited immediately. Every ioctl came back as `VERR_INVALID_POINTER`. Haiku's `ioctl()` reads a `size_t` after the buffer pointer; the VirtualBox userspace call did not pass one. `vbglR3DoIOCtlRaw` in the VirtualBox tree outside git now passes the request size on Haiku. That change is not in this git tree.

The next boot (about 15:47 Europe/Amsterdam, serial `generated/vbox-svc3.log`) reached the first-login script with no panic. `/boot/home/vbox-service.log` says `All services started`. `timesync` set the guest clock from the host. `vminfo` connected and then every guest-property write returned `VERR_ACCESS_DENIED`, including `/VirtualBox/GuestAdd/Version`. `VBoxManage guestproperty enumerate` showed only host-written properties. The driver open used `VMMDEV_REQUESTOR_USERMODE` alone. It now also sets `VMMDEV_REQUESTOR_USR_ROOT` when `geteuid()` is 0. The boot after that rebuild (about 15:52 Europe/Amsterdam, serial `generated/vbox-svc4.log`) again reached the first-login script. The service log shows `timesync time set` at 14:52:07, 14:52:17 and 14:52:27 UTC and the same `VERR_ACCESS_DENIED` on property writes. The screen was not watched. Memory ballooning was disabled. Clipboard mode stayed disabled. The modules and `VBoxService` are not in git and not on the daily image.

### VirtualBox guest driver claims 80ee:cafe on a throwaway disk

The first throwaway that reached userspace (`generated/vbox-vboxsvc.log`, 2026-10-30) loaded `VBoxService` and then refused `drivers/dev/misc/vboxguest`: `could not resolve symbol 'VBoxGuest_RTLogLoggerEx'`. Haiku resolves driver imports before `init_hardware`, and that symbol existed only in the `generic/vboxguest` module, which is not loaded yet. A no-op definition was added in the VirtualBox 7.2.22 tree outside git (`VBoxGuest-haiku-stubs.c`) and `vboxdev` was relinked. The same link also rebuilt `vboxsf`, `vboxvideo`, the mouse filter, and the accelerant. Those were not installed. The stub does not print, so driver `LogRel` lines stay silent.

The replacement disk `generated/r5-os-vboxid.vdi` (UUID `3eab20a2-9599-4ac2-8353-0f1a436ce727`) was started about 15:27 Europe/Amsterdam with clipboard mode disabled and UART1 writing `generated/vbox-identify.log`. Serial shows PCI `80ee:beef` and `80ee:cafe`, then `loaded driver .../misc/vboxguest`. That message is printed only when `init_hardware` succeeds, and that function returns the `vboxguest` module init, which returns success only after the PCI attach. No `Symbol not found`, no `RTR0Init failed`, no panic. The log reaches the first-login script and then stays quiet while the VM keeps running. The screen was not watched. The version word was not printed. `VBoxService` has no line in this log. The modules are not in git and not on `r5-os.vdi` or `r5-os-loginfix.vdi`. The VM was left on this throwaway.

### Sign in lists root, and Tracker crashes on the desktop shelf

The GUI boot of `generated/r5-os-rocketfix.vdi` reached Sign in. The list showed `root` and `user`. That disk's `/etc/passwd` gives `root` home `/boot/home`, which is the same test Sign in uses for a person. The system account `root` is no longer listed. On sign-in its home is moved to `/boot/system/var/root` and the shadow password is locked (`!`). An administrator that is still uid 0 and named `user` stays on the list.

Signing in as `user` opened the debugger: Tracker thread, segment violation in `BShelf::_InitData`, called from `BDeskWindow::Init`. The desktop pose view was never created, and the shelf still called `AddFilter` on that null view. This disk already had `user` at uid 1001, so migration did not `chown` `/boot/home`. Login then sets that tree to mode `0700` while it is still owned by uid 0, and Tracker (uid 1001) cannot open `/boot/home/Desktop`. Migration now gives that tree to the account when the home directory is still owned by uid 0. If the pose view is still missing, Tracker logs the status to `/dev/dprintf` and returns instead of entering the debugger.

Jam AMD64 2026-10-30 14:55 Europe/Amsterdam: image built, no jam error line. The fresh image was then given the same test accounts as the rocket disk (`root` home `/boot/home`, `user` uid 1001, empty password, session file `user`). GUI start of `generated/r5-os-loginfix.vdi` the same afternoon: serial shows `package_daemon` registering `/boot` and `vesa: vesa_init() completed successfully!`, then `Running first login script ...`. No `Segment violation`, no `Tracker: desktop` line, no `vesa_uninit`. At 15:01 Europe/Amsterdam the screen showed Tracker on `/boot/home` (Desktop, Documents, and the other home folders), Deskbar, and About R5OS 2026.1-dev Build 10013. About reported Walter, two processors (AMD Ryzen 5 5625U), and 2048 MiB. The guest clock was 2:01 PM UTC. COM1 was the log file. The daily `r5-os.vdi` was not attached.

### Second boot stuck on the rocket icon

After the first sign-in, `/boot/system/settings/r5-desktop-session` contains the account name. On the next boot the registrar (still uid 0, still inside `ReadyToRun()`) built the MIME database and called `getpwnam()` to find that account's home. `getpwnam()` sends the question back to the registrar, which is not reading messages yet. The registrar never finishes startup, `package_daemon` never logs, and the splash stays on the rocket (`BOOT_SPLASH_STAGE_7_RUN_BOOT_SCRIPT`). CPU goes idle. Serial on that boot stops at the same slab line where the first boot printed `REG: Failed to open shadow passwd`.

`mime_user_settings_path()` now reads `/etc/passwd` itself. The first-login script `default_deskbar_items.sh` was CRLF, so its shebang was `/bin/sh\r` and the runtime loader could not open it (`needed by <NULL>`). `apply-login.py` writes that script with LF line endings. That script failure is what the first boot printed just before `vesa_uninit()` and a reset; it is not what keeps the next boot on the rocket.

Jam AMD64 2026-10-30 14:11 Europe/Amsterdam: `jam_exit=0`. The new image was given a `r5-desktop-session` of `user` and a passwd line `user` uid 1001, which is the state that deadlocked the previous disk. Cold start of `generated/r5-os-rocketfix.vdi` the same afternoon: serial shows `smp: booting 1 other cpu`, `package_daemon` registering `/boot`, and `vesa: vesa_init() completed successfully!`. The log then goes quiet at the audio driver and the guest CPU is idle. No `vesa_uninit`, no second boot loader, no `/bin/sh` error. The screen was not watched from here. COM1 is still the log file. The daily `r5-os.vdi` was not attached.

### Licensing policy

`docs/LICENSING.md` is the multi-license policy. New R5OS-owned files default to GPL-3.0-or-later when R5OS can license the whole file. Haiku headers stay as they are. GPL-2.0-only is not relicensed to GPL-3.0 and is not combined with GPL-3.0 in one work. `THIRD_PARTY_NOTICES.md` lists what has actually been identified. `LICENSES/` has the unmodified GPL-3.0 and MIT texts. The Haiku tree has not been audited file by file. About credits, REUSE lint, and a release SBOM are not built. VirtualBox 7.2.22 Haiku additions are GPL-3.0-only and are not in git or the image. Userspace may ship later as separate GPL-3.0 programs. Kernel modules stay out pending review of how far GPL-3.0 reaches into the kernel. `README.md` points at the policy, the notices, and `LICENSES/`.

### Boot loader no longer waits forever for a missing CPU

The BIOS boot loader’s `smp_boot_other_cpus()` waited, with no timeout, until the next CPU’s trampoline cleared a stack word. On this VirtualBox VM that wait often never finished, so the kernel never started and the splash icons stayed gray. Delivery-status waits had the same problem. Each CPU now gets three attempts. A delivery bit that stays set is reported and the startup sequence continues. If a CPU still does not answer within about a second, the loader sets `num_cpus` to the CPUs that did start and enters the kernel. Later CPUs are not started, so the kernel does not wait on a hole in the CPU list. The waits use a bounded `pause` loop. They do not call `spin()`, because that BIOS wait can fail to return. The source copy is `distro/boot/bios_ia32/smp.cpp`, installed by `scripts/apply-kernel-bugs.py`.

Jam AMD64 2026-10-30 13:16 Europe/Amsterdam: `jam_exit=0`. Cold start of that image (`generated/r5-os-smpfix.vdi`, not the daily VDI) at 13:18 Europe/Amsterdam: serial shows `smp: booting 1 other cpu`, then the kernel, `CPU 1`, and `scheduler_init: found 2 logical cpus`. The same boot reached the first-login script and then the boot loader started again (`vesa_uninit`). A second pass in that same run again entered the kernel with CPU 1. No `did not start` line, so the give-up path was not hit. The screen was not watched. The VM was put back on `generated/r5-os.vdi`, which still has the old loader. COM1 is the pipe again.

### Lock screen (build 10013)

Deskbar → Lock screen covers the session. Tracker and Deskbar stay running. Login does not call `StartSession` and does not clear the session file. Unlock checks the password of the name in the session file. Log out on that screen is the existing log-out path (apps quit, Sign in returns). An open preference grant is cleared, so those windows lock again while the screen is locked.

A keyboard shortcut was tried and then dropped. Ctrl+Shift+L is taken by Windows before VirtualBox sees it. Ctrl+Alt+L did not arrive either, including from the VirtualBox soft keyboard. The `r5_lock` input filter is no longer built. The shortcut is [#72](https://github.com/nickrusman-bot/r5os/issues/72). A menu shortcut was not set: Deskbar shortcuts always add the command key.

Jam AMD64 2026-10-30 09:33 Europe/Amsterdam: `jam_exit=0` (Login, Deskbar, `r5_lock`, image).

VBox 2026-10-30 09:37 Europe/Amsterdam (guest 08:34, UTC): the cover, password, Unlock, Log out, Restart, and Shut down were on screen. The panel had no title tab. Ctrl+Shift+L did nothing. Control turns the `bytes` field into a control character, so the filter never saw the letter L. It now uses `raw_char`. The panel is a titled window, same look as Sign in, on the blue cover.

Jam AMD64 2026-10-30 09:43 Europe/Amsterdam: `jam_exit=0` (Login, `r5_lock`, image).

VBox 2026-10-30 09:45 Europe/Amsterdam (guest 08:44, UTC): the title tab is right. Ctrl+Shift+L still did nothing. On Windows that chord switches the keyboard layout before VirtualBox sees it. The filter now also accepts Ctrl+Alt+L (Alt is the Haiku command key). Ctrl+Shift+L stays for a machine that is not behind the Windows shortcut.

Jam AMD64 2026-10-30 09:54 Europe/Amsterdam: `jam_exit=0` (the `r5_lock` filter was rebuilt on its own, then the image).

VBox 2026-10-30 09:58 Europe/Amsterdam: Ctrl+Alt+L still did nothing, including from the VirtualBox soft keyboard. The chord is out of the source. Lock screen stays the leaf menu item. `apply-login.py` no longer builds `r5_lock`. Not jammed or booted after that drop. The image from 09:54 Europe/Amsterdam still contains the filter; it does not lock the screen because the chord never arrives.

### Machine settings and account checks (build 10012)

Printers, Repositories, and VirtualMemory use the same bottom-right lock as Network. Printer queues resolve to `/boot/system/settings/printers` for every account (`B_USER_PRINTERS_DIRECTORY`). An existing queue folder under `/boot/home/config/settings/printers` is copied once when the system folder is empty. Swap settings are written to `/boot/system/settings/kernel/drivers/virtual_memory`. The kernel reads that file and, if it is missing, the old per-account file. The repository list file moves to system settings. `package-repositories` is group `admin`, and world-writable only while a grant is open, same as network settings. Window positions for Printers and VirtualMemory stay in the signed-in account's settings.

A new account requires a password. Login refuses to delete the name in the session file (Login itself is uid 0, so `getuid()` did not mean the person at the screen). If `rm` of `/boot/home/users/<name>` fails, the reply says the account is gone and the files are not. Add, Disable, and Delete already send `error` 0 on success; that fix is in this image together with the checks above.

Jam AMD64 2026-10-29 23:48 Europe/Amsterdam: `jam_exit=0` (Login, Users, Printers, Repositories, VirtualMemory, kernel, libroot, image).

VBox 2026-10-30 Europe/Amsterdam: build 10012 confirmed on the running image.

Bijhouden in deze overlay-repo. Geen Haiku-hrev. Nieuwste eerst.

## 2026-10-29 (Europe/Amsterdam)

### Your account (build 10011)

Users & Accounts is a card: round picture (initials until you choose one), name, username, email, and Change password. A standard account sees only that card. An administrator, or Unlock, also sees the people list and Add / Disable / Delete. Sign in still lists everyone who can sign in.

Name and password go through Login (uid 0). The signed-in person must type the current password. An administrator setting someone else's password does not. Email and the picture live in that account's `config/settings/r5-account` (mode 0600). Username stays fixed.

Jam AMD64 2026-10-29 22:42 Europe/Amsterdam: `jam_exit=0` (Users, Login, image).

VBox 22:44 Europe/Amsterdam (guest 9:44 PM, UTC): as `user`, the card and Change password opened. The card background was white (`B_DOCUMENT_BACKGROUND_COLOR`) and the labels did not share one column.

The card now uses the normal panel color. Name, username, email, and account sit on one grid. Choose picture and Remove picture share one width under the avatar. Change password and Save sit on the same row. The people row shows the name and the account on two lines. Same build number (10011).

Jam AMD64 2026-10-29 22:51 Europe/Amsterdam: `jam_exit=0` (Users, image).

VBox 22:56 Europe/Amsterdam: the panel color is right, and the people list (name, then account) looks right. As `nicky` the window title was "Your account" but the people list was still there, and the lock blocked editing that account's own name, email, picture, and password. As `user`, Name and Email grew into tall boxes; the label sat in the middle of the row and the text sat at the top. `IsHidden()` before the window is shown follows the window, so the list was never actually hidden. A grid row beside the avatar took the spare height.

The people column is now a layout item, hidden after the window is shown, unless this account may manage people. The signed-in person's card stays editable while Locked. Name and Email stay one line, labels share the right edge, and the first row starts level with the top of the list. Rules for the next window are in `docs/STYLE.md`.

Jam AMD64 2026-10-29 23:09 Europe/Amsterdam: `jam_exit=0` (Users, image). Not yet seen in VBox.

VBox 23:12 Europe/Amsterdam: as `nicky`, Your account no longer showed the people list. Change password closed as if it had worked, and the password stayed the old one. Login's message switch never included profile get/set (`r5ah`, `r5af`), so the request was dropped. The reply had no `error` field and Users treated that as success. The same hole covered Save of name, email, and picture. Login now handles those messages, and a reply without `error` is a failure.

Jam AMD64 2026-10-29 23:19 Europe/Amsterdam: `jam_exit=0` (Login, Users, image). Not yet seen in VBox.

VBox 23:25 Europe/Amsterdam (guest 9:58 PM, UTC): as `user`, Users & Accounts showed the people list, a one-line Name and Email, and `nicky` as a standard account. As `nicky`, the own card worked, including a password change. Add account created the person and then reported "Sign in did not answer." Add, Disable, and Delete returned no `error` field on success, and the stricter reply check treated that as failure.

Success on those three requests now includes `error` 0. Login relinked 2026-10-29 23:28 Europe/Amsterdam. The image was not rebuilt, so the running VBox still shows the false alert.

### Log out name and Sign in after Restart (build 10010)

The Deskbar leaf item is `Log out <name>` for the signed-in account.

After the first Sign in, uid 0 is locked `root` with home `/boot/system`. `autologin` still `StartSession`s that uid-0 name (Login must stay uid 0). `/boot/system` is packagefs, so the user `launch_daemon` could not take the screen and Restart/Reset stayed on the boot splash. The session daemon now uses `/boot/system/var/root` when uid 0 has no writable home. Still not `StartSession` of the signed-in person.

Jam AMD64 2026-10-29 22:20 Europe/Amsterdam: `jam_exit=0` (launch_daemon, Deskbar, Login, image).

VBox 22:26 Europe/Amsterdam: after the first Sign in, Restart reached Sign in, not the splash. Log out, then Sign in as someone else. The preference lock was closed again after Log out.

### Locked machine settings (build 10009)

Network, Screen, Time, and Users & Accounts stay visible. An administrator (group `admin`, or the `/boot/home` account) changes them immediately. A standard account sees the same window with a lock. Unlock sends that administrator's name and password to Login, which is still uid 0. The preference app itself stays the signed-in user. The grant lasts 5 minutes and is dropped on Log out. It applies to that preference app only.

Time's clock and timezone go through Login, because the kernel allows those calls only for uid 0. Screen mode is refused in app_server unless the client is an administrator or holds a grant (`/boot/system/settings/r5-machine-unlock`). Network settings files are group `admin` mode `0664`. During a standard account's grant the directory is world-writable, because that account is not in `admin` and only one person is at the screen. Personal preflets are not locked.

Jam AMD64 2026-10-29 21:44 Europe/Amsterdam: `jam_exit=0` (Login, Users, Time, Screen, Network, app_server, image).

VBox `-ForceConvert` 21:45 Europe/Amsterdam: the VM powered on. Fresh disk, so the first desktop waits on `package_daemon`.

VBox 21:49 Europe/Amsterdam (guest clock 8:49 PM, UTC): Network showed Locked and Unlock at the top left. Add, Apply, and the other controls stayed active. The lock now sits on the bottom right of Network, Screen, Time, and Users. While it is locked, buttons, checkboxes, menus, and text fields in that window are disabled. Scrolling still works.

Jam AMD64 2026-10-29 22:02 Europe/Amsterdam: `jam_exit=0` (Users, Time, Screen, Network, image).

VBox 22:04 Europe/Amsterdam (guest clock 9:04 PM, UTC): signed in as standard account `nick` (uid 1002, `/boot/home/users/nick`). Network: lock at the bottom right, Revert / Disable / Renegotiate gray. Users: after Unlock, “Unlocked for a few minutes” and Lock at the bottom right, Add on. Network stayed Locked while Users was unlocked — the grant is per preference window.

VBox 22:06 Europe/Amsterdam (guest clock 9:06 PM, UTC): Screen and Time, still as `nick`, Locked at the bottom right. Screen: Revert and Apply gray, workspace +/− gray. Time: Revert gray. The date and analog clock still update.

### Sign in as that person (build 10008)

The session file is the account whose password just worked, not always the first administrator. `r5-desktop` already drops to that name before Tracker and Deskbar. A standard account’s home is `/boot/home/users/<name>` (`create_home` on sign-in). `user` still gets `/boot/home`. Log out deletes the session file.

This does **not** call `BLaunchRoster::StartSession`. That spawns another `launch_daemon <name>`, which reads `data/launch/user` and starts a second `app_server`. Removing autologin instead left the boot splash. Autologin stays. Desktop jobs stay off `initial_volumes_mounted`. One shared screen.

Trash is `$h/trash` for the signed-in account. Preferences are not hidden by role. Not a second graphical session.

Jam AMD64 2026-10-29 19:53 Europe/Amsterdam: `jam_exit=0` (Login, `r5-desktop`, What’s new, image). VBox `-ForceConvert` started.

VBox 19:55 Europe/Amsterdam (guest clock 6:55 PM, UTC): Sign in as `nicky` reached a blue desktop. Terminal `id` was `uid=1002(nicky) gid=100(users) groups=100(users)`. `ls /boot/home/Desktop` was permission denied. `ls` in `nicky`’s folder showed Desktop, Documents, and the rest. `echo $HOME` was still `/boot/home`, and the shell reported `sh: /boot/home/config/settings/profile: Permission denied`. The desktop showed the volume icon only: no Trash, Home, Quick Tour, or User Guide. Sign in as `user` afterwards: `uid=1001(user)`, `$HOME` `/boot/home`, `ls /boot/home/users/nicky` permission denied. A Deskbar crash dialog sat over Sign in (`/system/Deskbar`).

`/bin/sh` sources `SetupEnvironment`, which forced `HOME=/boot/home` for every account. Trash is `find_directory(B_TRASH_DIRECTORY)`, which was `/boot/trash` mode `0700` for the administrator, so Tracker drew no Trash pose. Home, Quick Tour, and User Guide are symlinks that the image only puts in `/boot/home/Desktop`. Those three are now created in each account’s Desktop. Trash is `$h/trash` (the old `/boot/trash` moves to `/boot/home/trash` once). Deskbar no longer calls through a missing settings file on quit.

Jam AMD64 2026-10-29 20:14 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 20:16 Europe/Amsterdam (guest clock 7:16 PM, UTC): Sign in as `nicky`. Terminal had no profile error. `echo $HOME` was `/boot/home/users/nicky`. `ls /boot/home/Desktop` was permission denied. Tracker titled `nicky` listed Desktop, Documents, Downloads, Music, Pictures, Videos, and config. The desktop showed R5OS, Home, Quick Tour, User Guide, and Trash. Deskbar listed Tracker and Terminal.

VBox 20:17 Europe/Amsterdam (guest clock 7:17 PM, UTC): Sign in as `user`. `id` was `uid=1001(user) gid=100(users) groups=100(users),103(admin)`. `ls /boot/home/users/nicky` was permission denied. That desktop also showed Home, Quick Tour, User Guide, and Trash. No Deskbar crash dialog on this pass.

Deskbar leaf-menu folders were yellow for `nicky` and blue-grey for `user`. Tracker reads `~/config/settings/mime_db` first. `user` had that database because registrar (uid 0) writes into `/boot/home`. `nicky` had none and used the system yellow folder icon. Sign in now copies `/boot/system/data/mime_db` into that account’s settings if missing, and puts the system directory icon on `application/x-vnd.be-directory` so every account gets the yellow folders. Registrar follows the session file instead of always `/boot/home`.

Jam AMD64 2026-10-29 20:45 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 20:47 Europe/Amsterdam (guest clock 7:47 PM, UTC): `nicky`’s leaf menu (Applications, Demos, Desktop applets, Preferences) was the yellow folder. `user`’s same four items were the blue-grey folder. `nicky`’s `config/settings/mime_db` was present (7:48 PM). Copying the directory type was not enough. Those menu rows are merged folders, type `application/x-vnd.haiku-virtual-directory`, whose icon is blue. A standard account often cannot create `/tmp/tracker/virtual-directories` (mode 0755, owned by whoever opened the menu first), so Tracker keeps the real system folder and its yellow icon. The administrator can, and then sees the blue virtual-directory icon. That type now uses the same yellow icon as `application/x-vnd.be-directory`. Sign-in copies both types into the account database again.

Jam AMD64 2026-10-29 21:13 Europe/Amsterdam: `jam_exit=0` (8116 targets). Optional translator headers were missing again; the image still built. VBox `-ForceConvert` started 21:16 Europe/Amsterdam.

VBox 21:16 Europe/Amsterdam (guest clock 8:16 PM, UTC): the leaf menu showed Applications, Demos, Desktop applets, and Preferences with the yellow folder. This was the fresh disk, so that desktop is the first account (`user`). `nicky` is not on a fresh image until Users & Accounts creates the account again.

### First administrator off uid 0 (build 10007)

Login (still uid 0) rewrites `/etc/passwd` so the `/boot/home` account is the first free uid ≥ 1000 (sshd keeps 1000), primary group `users`, member of `admin`. It `chown`s `/boot/home` except `/boot/home/users/<other>`. Desktop jobs start through `/system/apps/r5-desktop`, which drops to that uid before exec. Not `StartSession`. Sign in as a standard user still opens this administrator desktop.

Users & Accounts talks to Login for add/disable/delete (preflet euid is no longer 0). The `/boot/home` account cannot be deleted or disabled. Last-admin check stays.

Jam AMD64 2026-10-29 15:58 Europe/Amsterdam: `jam_exit=0` (Login, Users, `r5-desktop`, image).

VBox first boot: Sign in showed **General system error**. Login had rewritten `/etc/passwd` on disk; `getpwnam` still talks to the registrar, which had not changed uid, so migrate returned `B_ERROR`. That rewrite also left **no uid 0 account**. Restart then stuck on the black bootsplash. Registrar cannot change an existing uid (`B_BAD_VALUE`); useradd uses `B_REG_UPDATE_USER` + `add user`. Migrate now deletes `user` and adds it back at a free uid ≥ 1000, and adds a locked `root` uid 0 so boot still has a root account. Session file is `/tmp/r5-desktop-session`. Migrate runs only after a valid password, not when Sign in appears.

Jam AMD64 2026-10-29 16:20 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started a fresh VDI.

VBox after that Sign in: password accepted, then a **empty blue screen** (app_server, no Tracker/Deskbar). Target("desktop") returned OK but the wrapped jobs did not come up. Login now launches Tracker and Deskbar itself through `r5-desktop` (no libbe; session file under `/boot/system/settings`) and waits for Tracker before hiding Sign in. launch_daemon does not start those two as uid 0.

Jam AMD64 2026-10-29 16:29 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox Sign in still empty blue, a bit longer wait. Tracker registered (`IsRunning`) but its windows were on a **second app_server desktop** for uid 1001; the screen stays on uid 0 (Login). `AS_GET_DESKTOP` now reuses the existing screen until `StartSession`. Login starts Tracker/Deskbar with `load_image` (not `BRoster::Launch`, which would reuse one r5-desktop team) and quits the Sign in wallpaper window.

Jam AMD64 2026-10-29 16:36 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Shared app_server desktop after Sign in not confirmed yet.

VBox after that Sign in: desktop icons appeared (R5OS, home, Quick Tour, User Guide, Trash) at 15:38 Europe/Amsterdam. Deskbar listed **four `r5-desktop` teams**. The mouse cursor sat as a hand and did not move. Haiku `exec` keeps the team name of the helper, so Tracker, Deskbar, NetworkStatus and the clip/AirPlay tray all showed as `r5-desktop`. `r5-desktop` now `load_image`s the real binary and exits. launch_daemon jobs are not wrapped with it (a service whose team exits is restarted forever). Login still starts Tracker, Deskbar, NetworkStatus, `airplay_deskbar`, and `r5_vbox_clip` through the helper after Target("desktop"), and stops those launch jobs so Target does not also spawn uid-0 copies.

Jam AMD64 2026-10-29 16:49 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 16:51 Europe/Amsterdam (guest clock 3:51 PM, UTC): desktop icons, mouse, Deskbar names **Tracker / AboutSystem / Terminal** (not four `r5-desktop`). Terminal `id` was **`uid=0(root)`** `gid=0(root)` `groups=0(root),101(party)`. Migrate did add `root` at uid 0, but Tracker came from launch_daemon as uid 0. `GetJobs("desktop")` returns `B_NAME_NOT_FOUND` without a session daemon (`StartSession` is not used), so Login never disabled Be-TRAK/Be-TSKB. Target then starts Tracker as root; the helper’s second Tracker is a single-instance no-op. Login now stops those jobs by hardcoded name and waits until uid-0 Tracker is gone before `r5-desktop` starts the session copy.

Jam AMD64 2026-10-29 16:59 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:01 Europe/Amsterdam: Terminal `id` still `uid=0(root)`. `/system/var/log/syslog` had no `r5-desktop` / Login lines: the helper only syslog()s on error, exits in milliseconds, and does not call `openlog`. Target("desktop") was still starting Tracker as uid 0 *before* the helper, so the root copy won. Login now starts Tracker/Deskbar through `r5-desktop` first, refuses a uid-0 Tracker team, then Target. Session trace goes to `/tmp/r5-session.log` and `/system/var/log/r5-session.log`.

Jam AMD64 2026-10-29 17:07 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:14 Europe/Amsterdam (guest clock 4:09 PM, UTC): Terminal `id` is **`uid=1001(user)`** `gid=100(users)` `groups=100(users),103(admin)`. Deskbar shows Tracker and Terminal (not `r5-desktop`). Mouse works. Session log is `/tmp/r5-session.log` (lowercase), not next to syslog; `/system/var/log/` is packagefs and that copy often cannot be created. Isolation of another account’s home was not tested on this first-boot VDI (`nicky` is not an image account).

VBox 17:16: `/tmp/r5-session.log` shows Tracker team 256 and Deskbar team 258 at uid 1001, then NetworkStatus/AirPlay/clip, `sign-in done: No error`. Deskbar was missing ProcessController, PowerStatus, and the volume replicant: those are installed by `first-login` as uid 0 (`default_deskbar_items.sh`), and `BDeskbar::AddItem` does not land in a uid-1001 Deskbar. Login now starts ProcessController `-deskbar`, PowerStatus `--deskbar`, and `desklink --add-volume` through `r5-desktop` after Deskbar is up.

Jam AMD64 2026-10-29 17:21 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:24 Europe/Amsterdam: tray replicants looked right. `/boot/home` listed `config`, `Desktop`, `mail`, `users` (correct first-home layout; Documents/Pictures are not created for that account). Trash opened as `/boot/trash` (Haiku’s trash path). Log out did not return to Sign in: `StopTarget("desktop")` ran while Tracker/Deskbar were not launch_daemon teams, then **launch_daemon user** crashed. ProcessController showed **Your setting file could not be saved (Initialization failed)** on that teardown. Debugger attached to that uid-0 daemon and had no windows on the uid-1001 Deskbar. Logout now quits Tracker/Deskbar first, shows Sign in, then StopTarget. `/boot/trash` is chown’d to the session user so Tracker can treat it as trash.

Jam AMD64 2026-10-29 17:30 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:34 Europe/Amsterdam: Trash icon is correct. Log out reached Sign in slowly, showed **Damned! / Initialization failed**, then the next Sign in left the controls disabled (gray). ProcessController `-deskbar` still runs `ReadyToRun`, which opens a window; quitting that window saves `ProcessController Prefs` and the alert is `B_NO_INIT` when `config/settings` cannot be created. Logout then called `StopTarget("desktop")`, which took down the user launch_daemon, so the next `Target("desktop")` never replied and Sign in stayed disabled. Logout now only disables those jobs and quits Tracker/Deskbar (no `StopTarget`). Replicants are installed by `r5-tray` (`AddItem` only). `/boot/home` gets Documents, Downloads, Music, Pictures, Videos, and `config/settings` for the administrator too (the image only shipped Desktop/config/mail; extra accounts already had the full set).

Jam AMD64 2026-10-29 17:41 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Log out then Sign in again not confirmed yet.

VBox 17:43 Europe/Amsterdam (guest clock 4:43 PM, UTC): Documents, Downloads, Music, Pictures, and Videos are in `/boot/home`. Trash icon is correct. **launch_daemon user** still crashed (`autologin` → `StartSession("user")` while that account was uid 0; the daemon stays uid 0). Choosing Debug put **Debugger** in the Deskbar with **No windows**. Log out after confirm did nothing: `Stop`/`SetEnabled` wait for a reply from that daemon, and a team stopped in the debugger never replies, so Sign in was never shown.

Log out no longer talks to the launch daemon. It quits Tracker, Deskbar, and the session tray apps, then shows Sign in (`kill_team` if quit is ignored). Sign in does not call `Target("desktop")`. Jobs inside `target desktop` no longer use `on initial_volumes_mounted`, so that uid-0 daemon does not start Tracker beside `r5-desktop`. The Debugger team window is shown as soon as it exists (`Hide()` before the first `Show()` left the show level at 1, which Deskbar skips). `user-bootscript`, `dstcheck`, and `first-login` in that target do not run. Not a second account. Not StartSession.

Jam AMD64 2026-10-29 18:10 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 18:14 Europe/Amsterdam (guest clock 5:14 PM, UTC): **Log out** returned to Sign in. No ProcessController **Damned** / Initialization failed. No **launch_daemon user** crash dialog. **Restart** came back to the desktop. Debugger opened a **Teams** window (kernel_team, launch_daemon, Login, Tracker, Deskbar, Debugger; Deskbar listed Debugger with that window, not “No windows”). Team list also has **launch_daemon root** (autologin `StartSession` after migrate names uid 0 `root`). Attaching from a crash dialog was not in this shot (there was no crash). Extra-home isolation still untested.

VBox after that Sign in: `ls /boot/home/users/nick` as the administrator is **Permission denied**. Isolation of that extra home holds on this VDI (uid 1001 cannot list another person’s folder).

After Deskbar is up, Login starts `dstcheck`, `~/config/settings/boot/UserBootscript` if that file exists, and first-login `PostInstallScript` if `~/config/settings/first_login` exists. `default_deskbar_items.sh` does not launch ProcessController `-deskbar` (that was Damned). Volume and tray replicants stay `r5-tray` / `desklink`.

Jam AMD64 2026-10-29 18:26 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Boot stayed on the **R5OS splash**: removing `x-vnd.Haiku-autologin` meant no user `launch_daemon`, so `app_server` and Login (in `data/launch/user`) never started. Autologin is back. It still `StartSession`s `getpwuid(0)` (`user` on first boot, `root` after migrate). Desktop jobs stay off `initial_volumes_mounted` so that daemon does not start Tracker. Not StartSession as the product path.

Jam AMD64 2026-10-29 18:33 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 18:34 Europe/Amsterdam (guest clock 5:34 PM, UTC): boot left the splash, Sign in reached the desktop. Debugger **Teams** lists `launch_daemon`, **`launch_daemon user`**, `app_server`, Login, Tracker, Deskbar, Debugger (no crash dialog, not “No windows”). `launch_daemon user` is autologin `StartSession` of uid 0 while that account was still named `user`. Not `launch_daemon root` on this first-boot VDI. Isolation and second Sign in were not rechecked on this disk.

### First administrator off uid 0 (plan)

Documented in `docs/MULTIUSER.md` and `AGENTS.md`. Product people: administrator `user` at `/boot/home`, optional standard users under `/boot/home/users/<name>`. Test names such as `nicky` are VBox fixtures, not image accounts. Next code: unused uid ≥ 1000 (not sshd 1000), `chown` of the first home, desktop jobs as that uid, Users & Accounts via an admin helper. No new overlays. Not `StartSession`. Not implemented yet.

### Joysticks on the image

Deskbar → Preferences includes Joysticks (`SYSTEM_PREFERENCES`). Settings dir is `find_directory(B_USER_SETTINGS_DIRECTORY)/joysticks`. Without a game port the preflet should still open.

VBox 2026-10-29 15:26 Europe/Amsterdam: Joysticks was in Preferences. As `user`, `id` was uid 0 and `$HOME` was `/boot/home`. `su nicky` showed uid 1001, `HOME=/boot/home/users/nicky`, and that home listed `config Desktop Documents Downloads Music Pictures Videos`. `su nicky -c 'ls /boot/home/Desktop'` — permission denied. Sign in is still one session.

### Per-user settings audit

Appearance, Backgrounds, Deskbar, Tracker, and app_server desktop settings already call `find_directory(B_USER_* )`. For uid 0 that still maps to `/boot/home` (`get_user_home_path`). Extra accounts would get their own `config/settings` only after a real session switch. The AirPlay deskbar log no longer writes `/boot/home/airplay_deskbar.log`. Not a second desktop.

Bundled apps (second pass): Mail, Terminal, Screenshot, ShowImage, Clock, DeskCalc, HaikuDepot, People, ProcessController, Workspaces and most other image apps already use `find_directory`. MidiPlayer, Joysticks, and PoorMan now live as native copies under `distro/user-paths` (apply copies files; not a string patch). WebPositive stays the image `webpositive.hpkg`. Preflet split accepted: personal vs machine (Network, Screen, Time). No second desktop.

Jam AMD64 2026-10-29 15:01 Europe/Amsterdam: `jam_exit=0` with those MidiPlayer/Joysticks/PoorMan sources in the tree (`haiku.hpkg` rebuilt). VBox 15:12: MidiPlayer wrote `/boot/home/config/settings/MidiPlayerSettings` (0-byte file with attributes; Tracker cannot open it as a document). PoorMan `public_html` existed. Not a second desktop.

### Private home folders (build 10006)

`/boot/home` is `0711` so other accounts can reach `/boot/home/users/<name>` but not list the first account’s files. Names directly under `/boot/home` except `users` are `0700` (directories) or `0600` (files). Each extra home is `0700`. Login applies this before Sign in; creating an account does too.

The first account `user` is still uid 0. Mode bits do not stop that account from reading other homes. Sign in is still one shared session.

VBox 2026-10-29 14:30 Europe/Amsterdam: `ls -ld` showed `/boot/home` `drwx--x--x`, Desktop `drwx------`, `users` `drwxr-xr-x`. `su nicky -c 'ls /boot/home/Desktop'` — permission denied. `su nicky -c 'ls /boot/home/users/nicky'` listed that home. As uid 0 `user`, listing nicky’s home still worked.

### Log out (build 10005)

Deskbar leaf menu **Log out** (confirm). Login stays running after Sign in (hidden). Logout quits session apps, `StopTarget("desktop")` so Tracker/Deskbar are disabled instead of relaunched, then Sign in is shown again. Next Sign in `SetEnabled` on desktop jobs then `Target("desktop")`. Same uid/home as before. Not registrar `ShutDown`.

VBox 2026-10-29 13:46 Europe/Amsterdam: Login appeared in the Deskbar running-apps list (`B_BACKGROUND_APP` was missing). Log out crashed Login: `PrepareForSignIn` called `BTextControl::SetText` from the app looper (`Looper must be locked`). net_server also crashed; logout now skips `B_BACKGROUND_APP` and `/servers/` teams and locks the Sign in windows before Show/SetText.

VBox 2026-10-29 13:54 Europe/Amsterdam: Log out no longer crashed, but the screen was only the Login desktop wallpaper (blue, no Sign in card). Showing the hidden modal again left it behind/unlaid-out; `Activate()` on the desktop-feel window made that worse. Log out now rebuilds the Sign in window the same way as first boot.

VBox 2026-10-29 14:01 Europe/Amsterdam: Log out returned to Sign in. Login is not a Deskbar team. Same session after the next Sign in. Isolation is still not done.

VBox 2026-10-29 14:09 Europe/Amsterdam: after opening Installer from Applications, Log out showed FirstBootPrompt (Try R5OS). Installer `Quit()` relaunches that prompt when Deskbar is not running (live cancel-install path). Log out stops Deskbar first. Installer now skips that launch if Sign in is running.

### Sign-in screen (build 10004)

Haiku Login is the boot path instead of auto-starting Tracker/Deskbar. Native widgets: person in the middle (list hidden when there is only one), password under the name, Sign in, clock plus Restart/Shut down at the bottom. After a valid password, `BLaunchRoster().Target("desktop")` starts the existing user session. `StartSession` for another account is not used yet (second app_server). Empty password is allowed when shadow is empty. Disabled accounts (`!`) are omitted.

VBox 2026-10-29 12:54 Europe/Amsterdam: live image after Restart was a white empty screen (PrintScreen still opened Screenshot). Login used `kDesktopWindowLook` plus layout, so the card did not draw. It is a titled modal window again, with the Haiku desktop window behind it. "Try R5OS" now starts `login`, not `desktop`.

VBox 2026-10-29 13:01 Europe/Amsterdam: Sign in appeared as a yellow tab over a white empty client. The window had no layout, so `LoginView()` (nameless `BView`, frame 0) never got a size. The window now owns the layout and the card has no expanding glue.

VBox 2026-10-29 13:07 Europe/Amsterdam: the form showed `R5OS` / `user`. Sign in printed `Name not found`: password was accepted, then `BLaunchRoster().Start("desktop")` looked up a job named desktop (`B_NAME_NOT_FOUND`). FirstBootPrompt uses `Target("desktop")`; Login now does too. Welcome copy: “Welcome to R5OS” / “Please login with your credentials”.

VBox 2026-10-29 13:16 Europe/Amsterdam: Sign in reached the desktop. Restart powered the VM off: `reboot` stayed false on `kRebootAction`. Restart now calls `ShutDown(true, …)`. The gecos line `R5OS` above `user` is removed; Welcome is enough.

VBox 2026-10-29 13:22 Europe/Amsterdam: Restart works.

The login name `user` had disappeared with the gecos line. It is shown again as `pw_name` (not display name R5OS). Jam 13:26 Europe/Amsterdam; VBox `-ForceConvert` 13:27.

VBox 2026-10-29 13:27 Europe/Amsterdam: live Try, one account — Welcome, hint, **user**, Password, Sign in, Restart, Shut down. 13:28: with `nicky` added, the list shows `user` and `nicky`; the selected login stays above Password. Restart was already confirmed. Logout and isolation are still not done.

### Multi-user foundations (build 10002)

R5OS is still a single shared desktop (`user`, `/boot/home`, often UID 0). Haiku already has `passwd`/`shadow`, `useradd`, and `src/apps/login` (`StartSession`); none of that is the boot path. First step: document the model (`docs/MULTIUSER.md`), put **Users & Accounts** in Preferences as a **read-only** `getpwent` list, bump **build 10002**, and stop hardcoding `10001` in `wsl-install-branding.sh`. Extra homes will be `/boot/home/users/<name>`; the first account keeps `/boot/home`. Login, isolation, and account create/delete are not in this build.

VBox 2026-10-29: the preflet listed `user` and **sshd** (home under `/packages`). Only accounts whose home is `/boot/home` or `/boot/home/…` are shown. About showed Build 10002 but What’s new started at 10001: Jam did not rebuild `AboutSystem.rdef` when `r5_whatsnew.txt` changed. Copy now touches the rdef and the Jamfile depends on the notes file.

### Users & Accounts add/disable/delete (build 10003)

Add / Disable / Enable / Delete in the preflet. Haiku `useradd` starts UIDs at 1000 and skips taken ids (sshd is 1000, so the first extra person is usually 1001). New homes are `/boot/home/users/<name>` with Desktop/Documents/…. Administrators go in group `admin`. UID 0 `user` cannot be deleted or disabled; the last enabled administrator cannot be removed. Empty password uses `passwd -d` (not on argv). Non-empty uses `crypt` + registrar. Disable prefixes `!` on the shadow hash so Enable can restore it. Still one shared desktop. Login as the new person is out of scope.

VBox 2026-10-29, Europe/Amsterdam (five tests from `docs/MULTIUSER.md`):

1. Open Users & Accounts — `user` uid 0 `/boot/home` administrator (R5OS); `sshd` hidden.
2. Add a standard user — `nicky` uid 1001 `/boot/home/users/nicky`. Also `nicky2`/`nicky3` uid 1002/1003 administrators; Tracker showed the three folders.
3. Disable a non-admin — not captured in the screenshots.
4. Delete a non-admin — list left as `user` and `nicky3`.
5. UID 0 refuse delete/disable — Disable/Delete stay inactive unless a removable account is selected; `user` was not removed.

The intro line first made the window wider than the screen (`BStringView` + `B_AUTO_UPDATE_SIZE_LIMITS`). It now wraps. Add-account labels are still uneven. The preflet icon is the Haiku Login/Tracker people vector.

### Shutdown hung after IPv6 SLAAC

Guest shutdown stuck again, same class of hang as before DAD used `select()`. SLAAC now listens for extra RAs for the whole session, so `~SLAACClient` did `wait_for_thread` forever if `shutdown()` did not wake the raw ICMPv6 `select()`, or if an RA with RDNSS did `SendMessage(..., reply)` while net_server was already quitting. Teardown now shuts the socket, waits at most 2s, then `kill_thread`. Resolver updates are fire-and-forget. DHCPv6 uses the same stop path. Not VBox-tested yet.

VBox still hung after that: `r5_vbox_clip` blocked in `WaitForInput` on COM1 with `B_INFINITE_TIMEOUT`, then `~ClipApp` did `kill_thread` + unbounded `wait_for_thread` (a kernel wait often does not die). Close the port, poll 250ms, never wait forever. AirPlay tray was a **service** that kept pulsing `BDeskbar::AddItem`; that is a user **job** that `Quit()`s after install (PowerStatus). net_server workers no longer `wait_for_thread` after `kill_thread`.

### IPv6 default route and dual-stack DNS

SLAAC put a ULA on the NIC but the default route used the router’s `fe80::` **without** `sin6_scope_id`, so apps could not send off-link IPv6. The route now copies the RA source sockaddr and fills the interface index. Syslog: `default route via fe80:…%N` and `dad … unique`.

VBox syslog 2026-10-29: DAD unique and ULA assigned, but no `default route` line. An RA with prefix A=1/L=1 can still have Router Lifetime 0 (not a default router per RFC 4861). If that RA also offered a SLAAC `/64`, R5OS still uses the advertiser as default router and logs `RA lifetime 0`.

Several LAN hosts re-advertise the same ULA `/64` with lifetime 0. Each one used to replace the IPv6 default route. Only the first such advertiser (or a later RA with lifetime > 0) keeps the gateway.

`ping -6 ipv6.google.com` resolved AAAA (`2a00:1450:…`) but sent from `fe80::` (the first IPv6 address on the NIC). Google will not answer a link-local source. Outgoing IPv6 now prefers the ULA/GUA when the destination is off-link (RFC 6724).

Ping to the machine’s own ULA also lost all packets: `B_SOCKET_ADD_ALIAS` never installed the IPv6 `RTF_LOCAL` host route (IPv4 DHCP uses `SIOCSIFADDR` and does). Packets to `fdae:…` followed `::/0` off-link. Alias add now calls `AddDefaultRoutes`.

IPv6 route lookup masked the destination into a `sockaddr` (16 bytes). `ipv6_mask_address` writes a `sockaddr_in6` (28 bytes), so a `/64` never matched and everything used `::/0`. Lookup now uses `sockaddr_storage`.

After that, `route` showed the ULA with `HL`, but `ping` still used `fe80` as source: an IPv6 default route’s all-zero mask had `first_mask_bit` 0, same as a host route, so `::/0` matched first. All-zero IPv6 masks now rank as 128 (least specific), like IPv4.

VBox 2026-10-29 after that jam: `::/0` was last, but `ping -6` to the guest ULA still printed `fe80` and lost every packet. UDP `connect` binds the source to `route->interface_address` (`::/0` is still the fe80 alias). Self-ping then followed the gateway instead of `RTF_LOCAL`. Connect now uses the dest if it is local, otherwise a ULA/GUA; sending a packet to a local IPv6 address loops back; `SIOCADDRT` binds `::/0` to the ULA when one exists.

Next VBox: source was the ULA, still 0 replies. ICMPv6 echo copied IPv4 but checksummed the request instead of the reply and fell through without `break`. `ping -6` also discarded replies that lacked `IPV6_PKTINFO`/`HOPLIMIT` cmsg. Echo reply matches IPv4; ping counts replies without those cmsgs. Two `::/0` lines were the fe80 default plus a ULA default; remove now matches any default on the same NIC, and SLAAC adds `::/0` after the ULA.

VBox bridged 2026-10-29 11:07 Europe/Amsterdam: `ping -6 -c 3` to the guest ULA (`fdae:aeb4:a752:9ae4:a00:27ff:fe44:61dd`) — 3/3 received, 0% loss, source and dest both ULA. Internet IPv6 (Google) was not retested; ULA is still not a GUA.

RDNSS from an RA no longer replaces `resolv.conf` (that wiped IPv4 DHCP nameservers). Existing nameservers are kept and new ones are appended. Extra RAs still refresh the route and DNS.

Host DAD packet helpers: `scripts/test_ipv6_dad.cpp`. Outbound `ping`/`wget` over IPv6 was not run in this change. DHCPv6 IA_NA still needs a LAN with a DHCPv6 server. Wi-Fi + IPv6 on the Envy was not tested.

## 2026-10-28 (Europe/Amsterdam)

### About R5OS: build number and What's new

About shows `R5OS 2026.1-dev Build 10001`. A What's new tab sits next to Credits. Text comes from `distro/WHATSNEW` (no dates; heading `R5OS <version> Build <n> - Title`). Bump `distro/BUILD` when adding a section, not on every jam. Credits still auto-scroll; What's new does not.

The R5OS wordmark is drawn in the same box as Walter (`170x110`). The PNG stays 420px wide for the installer; About was showing it 1:1 and looked oversized.

### IPv6 SLAAC in net_server

On link-up, net_server assigns IPv6 link-local, SLAAC (RS/RA `/64`, default route, RDNSS), DAD before using an address, and DHCPv6 (IA_NA + DNS, or Information-Request). VirtualBox bridged LAN got a ULA via SLAAC.

DAD Neighbor Solicit used to fail with “Network is unreachable” and then **block** the second IPv6 address, so Network only showed `fe80::`. DAD stays RFC 4862 (NS from `::`, no SLLA) and fail-closed: send/socket errors do not assign the address. Only a real duplicate NA/NS also refuses it.

Network → NIC → IPv6 already has Mode Automatic / Static / Disabled. Automatic is labeled SLAAC/DHCPv6; Disabled is honored per family. Test: `docs/ipv6.md` (`grep -E 'slaac|dad|dhcpv6' /var/log/syslog`). A RA without A-flag `/64` cannot invent a ULA; DHCPv6 may still run.

IPv6 aliases were listed twice: IPv4 DHCP reconfigure started a second AutoconfigLooper, and `FindAddress` missed existing IPv6 slots. One looper per NIC; add address only if the `in6_addr` is not already present.

DAD follows RFC 4862: NS from `::` with no Source Link-Layer option. Haiku `IPV6_MULTICAST_IF` takes an interface `in6_addr`, not an ifindex, and still cannot send multicast without a unicast route. Socket/bind/setsockopt/send failures block assigning the address. `select()` is used instead of a blocking `recvfrom` so shutdown can finish. Host tests: `scripts/test_ipv6_dad.cpp`.

SLAAC must not DAD the link-local before it exists (`IPV6_MULTICAST_IF` needs an address on the NIC). That aborted SLAAC, so no ULA, ~10s DHCPv6 wait, and shutdown could stick in `select`. Link-local is added without DAD (NetServer already does); DAD stays on the SLAAC/DHCPv6 address. Destructor `shutdown()`s the socket so the thread can exit.

Haiku `IPV6_MULTICAST_IF` looked up the interface with an uninitialized `sockaddr_in6` (`sa_family` 0 → `EADDRNOTAVAIL`). After setting `sin6_family`/`sin6_len`, RS/DAD/DHCPv6 still logged `Network is unreachable`: IPv6 `get_route` has no unicast route for multicast. Kernel IPv6 multicast uses the interface `direct_route` (`get_device_route`) from `IPV6_MULTICAST_IF`, `SO_BINDTODEVICE`, or `sin6_scope_id`, then `ipv6_send_routed_data` (IPv6 header + checksum). Calling datalink `send_routed_data` directly skipped that and broke RS/DAD again (`ENETUNREACH`). Userspace SLAAC/DAD/DHCPv6 still bind the socket to the NIC. DAD stays fail-closed. Shutdown no longer hangs (`select` + socket `shutdown`).

A new VirtualBox VDI is a virgin disk: `package_daemon` first-boot on IDE often takes several minutes (QEMU virtio of an already-booted image is ~seconds). Resetting that boot leaves IPv6/packages half-done. Wait for the desktop. Syslog `ata 0-0 error: invalid target lun 2` … `lun 7` and `target device not present` is VirtualBox probing the empty IDE slave/secondary; a **Reset is ~5s with the same ATA lines**, so that probe is not the multi-minute first boot.

mDNS announces `_raop._tcp` / `_airplay._tcp` on IPv6 `ff02::fb` per NIC (`sin6_scope_id`). That does not make iPhone Control Center show an AirPlay 2 speaker (pairing/PTP still absent).

VirtualBox copies the raw image onto NTFS and uses a fixed VDI. The VDI is reused unless the raw image is newer (`-ForceConvert` rebuilds). Converting on every start made a virgin disk each time, so `package_daemon` ran first-boot processing and the guest looked hung until Reset. A **new** VDI first boot can take several minutes; a guest Restart is fast because packages are already processed. Do not Reset during first-boot.

VBox bridged LAN (2026-10-28, Europe/Amsterdam): after `ipv6_send_routed_data`, syslog `fdae:… via fe80:…`, PIO `A=1`, extra RA `already assigned`, `dhcpv6: no server`, Network shows `fe80::` and `fdae:aeb4:a752:9ae4:a00:27ff:fe44:61dd`. Not a DHCPv6-server LAN. No `mdns: IPv6 announce` error in the snippet. QEMU usernet was not used. DAD NS/NA was not packet-captured.

### VirtualBox text clipboard (no Guest Additions)

Host/guest copy-paste of Unicode text over COM1 (`\\.\pipe\R5OS-clip`). Guest `r5_vbox_clip` opens `pc_serial1` and quits if the port never appears. Drag-and-drop is still off: that is a Guest Additions protocol. Needs a jammed image with the overlay.

### VirtualBox bridged LAN (no Guest Additions)

`scripts/run-vbox-r5.ps1` boots the raw image as IDE + Intel PRO/1000 on a bridged NIC. No vboxsf, no auto-resize. USB stick: Rufus DD of the same image (`docs/vbox.md`).

### AGENTS.md vs roadmap

`AGENTS.md` is the short rule file (git, overlays, Deskbar/launch, QEMU limits). Product backlog (Hub, Store, remaining network items, arches) lives in `docs/ROADMAP.md` so agents do not implement named products during a focused fix.

### Wi-Fi: one screen, remember, Deskbar tray

NetworkStatus opens a Wi-Fi window (scan, password, connect) and the same from the tray menu. Successful joins are stored as persistent networks. After link loss (sleep), autoconfig may auto-join again (`fJoiningNetwork` is reset). NetworkStatus `--deskbar` starts with the desktop session. Tooltip shows SSID, “Connecting…”, or “No network”.

### AirPlay replicant Instantiate was stripped

Log: `instantiate_deskbar_item` OK, `AddItem` OK (id 999), maar `HasItem=0` en geen `AttachedToWindow`. Deskbar archiveert de view en maakt hem opnieuw via `Class::Instantiate`. Die functie stond inline in de class en zat niet in de binary (`nm` toonde alleen `instantiate_deskbar_item`). Out-of-line `Instantiate` + juiste `class`/`add_on` in het archive.

### AirPlay tray-debug

`airplay_deskbar` als launch **job** zonder `on`-event startte waarschijnlijk nooit — vandaar geen proces. Terug als **service** (blijft draaien). `airplay_server` probeert zelf `AddItem` op `/system/servers/airplay_deskbar`. Elke stap naar `/boot/home/airplay_deskbar.log`.

### AirPlay Deskbar-replicant (PowerStatus-pad)

Echte tray-replicant: `airplay_deskbar` exporteert `instantiate_deskbar_item`, belt `BDeskbar::AddItem` op de eigen binary, en stopt daarna. launch_daemon start hem als **job** (niet als service), anders komt hij na `Quit()` meteen weer op.

Waarom het eerder misging (niet de API):

1. `AddItem` op `airplay_server` — Deskbar laadt die binary in z’n eigen team (`load_add_on`). OpenSSL + Media Kit + globale player/sender horen daar niet.
2. Helper als **service** die na install `Quit()` deed — services worden herstart; vandaar het flikkeren in ProcessController.
3. Helper die bleef draaien — PowerStatus sluit af; de replicant blijft in Deskbar achter.
4. Zwevend venster als noodoplossing — zichtbaar, maar niet in de tray-shelf.

### AirPlay 1 ontvangen / zenden

`airplay_server` (achtergrond, `/system/servers`): RAOP/AirPlay 1, mDNS `_raop._tcp` / `_airplay._tcp`. AirPlay 2 alleen advertentie + `/info`. Sender: mixer-tap naar RAOP-speakers (QEMU-usernet ziet geen LAN-multicast).
