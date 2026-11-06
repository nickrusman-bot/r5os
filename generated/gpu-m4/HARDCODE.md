# GPU hardcode inventory

Paper inventory from `generated/gpu-m4/HARDCODE-AUDIT.md` step 1. Line numbers are the tree at the high-address commit on `cursor/gpu-m4-teapot`. Nothing in the fixed K0–K2 sequence is changed by this file. A row marked "fixed contract, AGENTS.md" stays until the user asks.

| # | File:line | What | Value | Linux source | Risk | Plan |
|---|---|---|---|---|---|---|
| 1 | `r5_gpu.cpp:542` | `num_shader_engines` | 1 | Linux v6.12 `gfx_v9_0.c:gfx_v9_0_gpu_early_init` for GC 9.3 (one SE). Not read from `gpu_info` yet | high | M5 passport |
| 2 | `r5_gpu.cpp:543` | `num_shader_arrays_per_engine` | 1 | same function, `max_sh_per_se` for Renoir | high | M5 passport |
| 3 | `r5_gpu.cpp:63` and `:546` | `kCuPerSh` / `num_cu_per_sh` | 8 | Linux v6.12 `gfx_v9_0_gpu_early_init` Renoir `max_cu_per_sh` 8. The active bitmap is read from `CC_GC_SHADER_ARRAY_CONFIG`; the width of that mask is this constant | high | M5 passport, from `gpu_info` or the register |
| 4 | `r5_gpu.cpp:541` | `family` | `R5_AMDGPU_FAMILY_RV` | Linux v6.12 `amdgpu_device.c` family for Renoir/Cezanne is `AMDGPU_FAMILY_RV` | medium | M5 device table |
| 5 | `r5_gpu.cpp:581–582` | `gpu_counter_freq` only when PCI `0x15e7` | 100000 kHz | Linux v6.12 `amdgpu_kms.c` copies `adev->clock.xclk`. Mesa's capture for this PCI id is 100000. Other ids stay 0 | high | M5, read `xclk`; delete the PCI test |
| 6 | `r5_gpu.cpp:510–511` | `vram_bytes` and `gtt_bytes` | framebuffer size in KiB, and the same number again for GTT | Linux v6.12 `amdgpu_kms.c:amdgpu_info_ioctl` reports `real_vram_size` and `gart_size`. The 6 Nov 2026 capture is VRAM 512M and GTT 15701M. R5 reports the BAR twice | high | M5 passport. GTT is a quarter of RAM in the M5 limit, not the BAR |
| 7 | `r5_gpu.cpp:564–569` | engine and memory clocks, memory type, width | 1800000 / 1333000 kHz, type 11, 128 bits | Mesa drm-shim record for device `15e7`. Linux reads the SMU and `atom_get_clock_info` / the UMC table. min equals max, as the non-DPM branch of `amdgpu_kms.c` | high | M5, or stays until the SMU is read. Marked temporary |
| 8 | `r5_gpu.cpp:556` | `num_hw_gfx_contexts` | 8 | Linux v6.12 `gfx_v9_0.c:gfx_v9_0_gpu_early_init`, IP 9.3.0 | low | M5 passport |
| 9 | `r5_gpu.cpp:558` | `ce_ram_size` | `0x8000` | Linux v6.12 `gfx_v9_0.c:gfx_v9_0_sw_init` | low | M5 passport |
| 10 | `r5_gpu.cpp:560` | `pte_fragment_size` | 2097152 | default VM fragment 9: `(1 << 9) * 4096`. The capture logs fragment size 9-bit | low | M5, from the VM size setup |
| 11 | `r5_gpu.cpp:570` | `gc_double_offchip_lds_buf` | 1 | Renoir config Linux copies into `AMDGPU_INFO_DEV_INFO` | medium | M5 passport |
| 12 | `r5_gpu.cpp:573–577` | VGPR, TCC, GS depths | 256, 4, 32, 1792, 32 | Linux v6.12 GFX9 config copied into the info struct (`max_gprs`, `max_texture_channel_caches`, GS table) | medium | M5 passport |
| 13 | `r5_gpu.cpp:585–586` | VA alignment and GART page | 4096, 4096 | page size. Linux `AMDGPU_GPU_PAGE_SIZE` | low | stays, it is the page size |
| 14 | `r5_gpu.cpp:590–596` | GFX IP 9.3, IB align 32/4, one GFX ring, compute rings 0 | constants | Linux v6.12 `gfx_v9_0_sw_init` / IP discovery for GC 9.3.0. Compute is off until its own step | medium | M5 passport for the version; rings stay until compute ships |
| 15 | `r5_gpu.h:231–234` | `external_rev` by PCI id | `0x15e7`/`0x1638` add `0xa1`; `0x1636`/`0x164c` add `0x91` | Linux v6.12 `nbio_v7_0.c` strap plus the Renoir/Cezanne revision offset in `soc15.c` | medium | M5 device table. The K path may ask the table; bytes stay |
| 16 | `gfx9_start.cpp:713` | PCI `0x1636` or `0x164c` branch | those two ids | a chip property, not a register. Belongs in the device table | medium | M5 device table, behaviour unchanged |
| 17 | `gfx9_start.cpp:1854` | which PCI ids get the GFX9 ring | `0x15e7`, `0x1636`, `0x1638` | the device table in `driver.cpp:624–627` is the one place ids should be named | high | M5. Fixed K path asks the table and stays byte-for-byte |
| 18 | `gpu.cpp:1002` | PCI `0x15e7`/`0x1636`/`0x1638` | those ids | same | medium | M5 device table |
| 19 | `gfx9_k12.cpp:1186` | PCI test around the K path | `0x15e7`, `0x1636`, `0x1638` | same | high | M5. Fixed contract, AGENTS.md: behaviour unchanged |
| 20 | `r5_gpu.cpp:579–581` | PCI `0x15e7` gates the counter frequency | `0x15e7` | see row 5 | high | M5 |
| 21 | `driver.cpp:624–627` | the existing device table | `0x15e7` Renoir (Barcelo), `0x1636` Renoir, `0x1638` Cezanne | Linux v6.12 `amdgpu_drv.c` pciid list and `vi.c` / `nv.c` chip names. This is the allowed place for a PCI id | low | stays, and grows into `r5_amd_device` |
| 22 | `r5_gpu.cpp:64` | `kMaxBo` | 256 | no Linux counterpart. A process limit, not a hardware value | medium | M5, still a limit, counted |
| 23 | `r5_gpu.cpp:65` | `kMaxClientBo` | 256 | same | medium | M5 |
| 24 | `r5_gpu.cpp:70` | `kMaxBoBytes` | 64 MiB | Linux does not cap one buffer at 64 MiB. M5 raises it to the address-space limit | high | M5 |
| 25 | `r5_gpu.cpp:66` | `kMaxSync` | 128 | libdrm's syncobj table is larger. An R5 limit | low | stays until a program hits it |
| 26 | `r5_gpu.cpp:69` | `kMaxList` | 8 | Linux `AMDGPU_CS_MAX_IBS` is higher. Eight IBs is the R5 submit limit | medium | M6 if RADV needs more |
| 27 | `r5_gpu.cpp:67` | `kMaxRange` | 8 | VA ranges stored per buffer. Not a Linux constant | low | stays |
| 28 | `r5_gpu.cpp:74` | `kWatchdogUs` | 10000000 (10 s) | Linux `amdgpu.lockup_timeout` GFX default 10000 ms | low | stays. It is that default |
| 29 | `r5_gpu.cpp:72` | `kFreeWaitUs` | 5000000 (5 s) | no Linux name. How long close waits for a busy buffer | low | stays |
| 30 | `r5_gpu.h:91` | `R5_GPU_VA_OFFSET` | `0x10000` | Linux v6.12 `amdgpu_vm.h` `AMDGPU_VA_RESERVED_BOTTOM` (64 KiB). `amdgpu_kms.c` reports it as `virtual_address_offset` | low | stays. Computed from that macro, not from RAM size. The 6 Nov capture uses the same hole |
| 31 | `r5_gpu.h:92` | `R5_GPU_VA_MAX` | `0x800000000000` | Linux v6.12 `amdgpu_gmc.h` `AMDGPU_GMC_HOLE_START`. `amdgpu_kms.c` `virtual_address_max` | low | stays |
| 32 | `r5_gpu.h:93` | `R5_GPU_HIGH_VA_OFFSET` | `0xffff800000000000` | Linux v6.12 `amdgpu_gmc.h` `AMDGPU_GMC_HOLE_END`. The VM does not reserve the first page of that range (`gmc_v9_0_get_vm_pde` adds nothing when translate-further is off) | low | stays. X1 checked it |
| 33 | `r5_gpu.h:94` | `R5_GPU_HIGH_VA_MAX` | `0xffffffffffbfe000` | `AMDGPU_GMC_HOLE_END` or-ed with the byte size, minus `AMDGPU_VA_RESERVED_TOP` `0x402000` (8 KiB trap, 2 MiB seq64, 2 MiB CSA) in `amdgpu_kms.c` | low | stays. The formula is in the host test |
| 34 | `gfx9_k12.cpp:994` | `SH_MEM_BASES` | `0x20001000` | Linux v6.12 `gfx_v9_0.c` private base `0x1000`, shared base `0x2000`, packed into `SH_MEM_BASES` | medium | stays for the shared VMID. M5 writes it per VMID from the same packing |
| 35 | `r5_cs.h:19–20` | user fence and interrupt fence offsets | 2048 and 2064 | past the K0/K2 words on the kernel fence page. Not a Linux VA. The fence is a VMID 0 MC address | low | stays. Fixed layout of that page |
| 36 | `r5_gpu.cpp:1488` area and `:1512` | `clflush` of a GTT indirect buffer at `CS_SUBMIT` | the cache line loop | Linux write-combined GTT does not need this flush. W3 | high | remove after the stick 19 tests. The stick showed low WC works and the first high page hangs, so the flush stays tonight |
| 37 | `r5_gpu.cpp` stage U and `r5_gfx9_vmid1_map` | one shared VMID 1 | VMID 1 for every client | Linux `amdgpu_vmid_grab` uses VMIDs 1–15 | high | M5 |
| 38 | `gfx9_check.h` `kR5Gfx93Mtype` | user MTYPE | system `MTYPE_NC` 0, VRAM Linux `MTYPE_NC` but `vram_live` 0 so user VRAM stays `MTYPE_UC` | Linux v6.12 `gmc_v9_0.c:gmc_v9_0_get_coherence_flags` | medium | VRAM MTYPE after its own boot. The row is the source |
| 39 | `r5_translate.c` `r5_gem_create` | `GEM_CREATE` flags other than `CPU_GTT_USWC` | ignored (alignment, no-CPU-access, explicit, and the rest) | Linux honours the whole `domain_flags` set | medium | M6 as RADV starts using them. USWC is passed through |
| 40 | `r5_translate.c` `r5_va_flags` | VA flags other than read, write, execute, PRT | dropped. An empty set becomes read+write | Linux stores the page flags it was given | medium | M6 |
| 41 | `r5_translate.c` context priority | `CTX` priority | not read | Linux `amdgpu_ctx_priority` | low | stays ignored while there is one ring |
| 42 | `r5_translate.c` `AMDGPU_INFO_SENSOR` and video caps | sensor queries | refused or zeros | Linux `amdgpu_kms.c` reads the SMU | low | M6 |
| 43 | `r5_gpu.cpp:2193–2197` and `:2736–2744` | stage U addresses | low pages under `VA_MAX`, U4b/U2d at `HIGH_VA_OFFSET`, U2e at `0xffff800100000000`, U2f at `0xffff800000200000` | U2e is the region of the stick 19 teapot indirect buffers (`0xffff800100284000`). U2d is `AMDGPU_GMC_HOLE_END`. These are self-test addresses, not device properties | low | stays. They are the next stick's steps |
| 44 | `gfx9_start.cpp` K0–K2 registers, the 1024-dword ring, the golden list, the stop order, the page-table base, the PTE flags of the K2/GART contract, the invalidate word, the ring-index formula | the fixed sequence | as `docs/GRAPHICS.md` "GFX9 ring / kernel on GC 9.3 (fixed)" | the sequence that landed the fence. AGENTS.md | — | fixed contract, AGENTS.md. Not a shortcut to delete |
| 45 | `r5_gpu.cpp` `rb num` log | the line is printed on every `GET_INFO` | stick 18 logged it 116 times | the value itself comes from `CC_RB_BACKEND_DISABLE` (`gfx_v9_0_setup_rb`). The repeat is the logging policy | low | once per boot, task 1 point 8 |

`num_rb_pipes` is not in this table as a bare constant. `gpu_rb` reads `CC_RB_BACKEND_DISABLE` and `GC_USER_RB_BACKEND_DISABLE`. Stick 19 logged `rb num 2 mask 3`.
