# GPU architecture

This is how R5 Operating System (R5) moves 2D and 3D rendering onto the GPU, without breaking Haiku compatibility, and while keeping the software path. It builds on [`GRAPHICS.md`](GRAPHICS.md). That document has the measured state of the hardware and the fixed GFX9 sequences.

## Decisions

These are settled. Change them only when the user asks.

1. **Haiku applications keep working, unchanged.** Applications use `BView`, `BBitmap`, `BGLView`/OpenGL and Vulkan. They never talk to the GPU directly. The GPU is an extra backend behind those APIs. `libbe` and its ABI do not change.
2. **Software always stays.** app_server's Painter (AGG), Mesa llvmpipe and Lavapipe stay installed and working. They are the path on every machine without a supported GPU: virtual machines, ARM64, RISC-V, and untested GPUs.
3. **When a supported GPU is present, rendering uses the GPU by default.** The software path is the fallback, not the default. The rules are under [Default and fallback](#default-and-fallback).
4. **The kernel side is a small R5 interface** in the GPU's Haiku driver (`radeon_hd` first), written by R5 itself. Linux `amdgpu` and `i915` are not ported. Most of their files carry an MIT-style notice. The reason is not the license: they are built on the Linux DRM core and kernel APIs, which Haiku does not have. Where a single file or table is MIT, it may be taken over with its notice (see [Licensing](#licensing)).
5. **Vulkan is RADV** from Mesa. RADV talks to the R5 interface through the R5 DRM library, which has one front end per vendor. The AMD front end provides the part of `libdrm_amdgpu` RADV uses.
6. **Mesa updates have to be easy to take.** That is why there is a version contract between the kernel interface, that library and Mesa. See [Release mechanism](#release-mechanism).
7. **The R5 GPU interface header and the R5 DRM library are MIT** (decided by the user on 5 Nov 2026). See [Licensing](#licensing).
8. **Mesa with RADV is built from a recipe of R5's own,** in this repository (decided by the user on 5 Nov 2026).
9. **VulkanTeapot is the proof of concept for M4.** It is a fork of GLTeapot that draws through Vulkan: RADV when the GPU is available, Lavapipe otherwise.
10. **The render node is `/dev/gpu/radeon_hd/<n>`** (decided by the user on 5 Nov 2026). Other GPU drivers later get their own directory under `/dev/gpu/`.
11. **OpenGL is Zink on RADV.** radeonsi is not built (decided by the user on 5 Nov 2026). This is revisited only when measurements with Zink fall short, and then only on the user's request.
12. **The R5 GPU interface is vendor-neutral.** Buffers, address spaces, submission, fences and sync objects are the same calls for every vendor. Only the device information and the command-stream details are per vendor. Intel is the second vendor (see [Intel](#intel-second-vendor)).

## Layers

```
Applications (unchanged)
  BView / BBitmap        BGLView / OpenGL           Vulkan
        |                      |                       |
   app_server             libglvnd                Vulkan loader
   |        |                  |                    |        |
 Painter   GPU backend     Mesa: Zink -> RADV     RADV    Lavapipe
 (CPU)     (GPU)           or llvmpipe (CPU)      (GPU)    (CPU)
               \               |                   /
                \              |                  /
          R5 DRM library: libdrm.so.2 + one front end per vendor
          (AMD: libdrm_amdgpu.so.1 subset; Intel later: i915 ioctls)
                               |
          R5 GPU interface: ioctls on /dev/gpu/<driver>/<n>
                               |
          kernel driver: radeon_hd (AMD), later intel_extreme (Intel):
          rings, interrupts, GART, VM, reset
                               |
                GPU (AMD GC 9.3 first, Intel Gen 9.5 second)
```

- **OpenGL goes through Zink** (OpenGL on Vulkan) on top of RADV, so there is only one GPU driver to maintain. radeonsi, Mesa's native OpenGL driver, is not built. It is only reconsidered if Zink falls short in measurements, and only when the user asks.
- **Display stays a separate track.** That is modes, more than one screen, vsync, and putting a frame on the panel. DCN is not programmed yet; the firmware scanout stays. See `GRAPHICS.md`, "Two problems".

## GPU Driver Kit, phase 1

A vendor backend is the struct `r5_gpu_backend_ops`. The core never calls a vendor function by name. Required ops are checked once, in `r5_gpu_ops_ready`, before `/dev/gpu/radeon_hd/0` is published. One missing required op and that node is not published; the display device still is. Optional ops may be NULL. The core checks those at the call and returns `B_NOT_SUPPORTED`. The contract for each op is the comment on the struct in `r5_gpu_backend.h`. Later phases add a second vendor. They do not change a call that already shipped.

## Default and fallback

### Which path is used

At boot, `radeon_hd` decides per GPU:

1. The PCI ID is in the table **and** the family has reached the level `release-supported` for GPU rendering (the levels from `AGENTS.md`).
2. The firmware blobs match their hashes.
3. A **boot self-test** passes: the fixed GFX9 sequences (ring, fence, IB, DMA, and later compute) within a time limit.
4. No opt-out is set.

If all four hold, the GPU is **available**:

- app_server uses its GPU backend;
- Vulkan lists RADV first;
- OpenGL uses Zink.

If one fails, that GPU is **not available**. Everything then goes through software, and the log says why, in one line.

### Opt-out and choice

- System-wide: `gpu false` in the `radeon_hd` driver settings turns off all GPU use. Login and the desktop then run in software.
- Per application: the usual variables, `VK_ICD_FILENAMES` or a device choice in Vulkan for Lavapipe, and `LIBGL_ALWAYS_SOFTWARE=1` for llvmpipe.
- The test switches from the bring-up (`gfx9_ring`, `gfx9_workload`, `gfx9_compute`) stay until the boot self-test replaces them. After that they are only for development.

### When things go wrong

- **GPU hang:** the driver detects a timeout, sends a mode-2 reset (already present for Renoir), and repeats the self-test.
- **Hang twice in one session:** the GPU stays off until the next boot. app_server falls back to Painter. Running Vulkan applications get `VK_ERROR_DEVICE_LOST`. The log says so.
- A failure in the GPU path must never take down Login or the desktop (already a rule in `GRAPHICS.md`, "Fallback").

### From opt-in to default

Today the GFX9 ring is **opt-in**. The GPU becomes the default per GPU family, as soon as that family reaches `release-supported` for GPU rendering. Until then the opt-in stays. The first family is GC 9.3 (Renoir, Barcelo, Cezanne) on the reference machine.

## R5 GPU interface (kernel)

### Device node

`/dev/gpu/<driver>/<n>`: `/dev/gpu/radeon_hd/<n>` for AMD (decided), and later `/dev/gpu/intel_extreme/<n>` for Intel. Deliberately **not** under `/dev/graphics/`. app_server opens every device in `/dev/graphics/` as a screen. The render node is for applications and libraries; the existing `/dev/graphics/radeon_hd_*` stays for app_server and the accelerant.

### Versioning

- `R5_GPU_API_MAJOR` and `R5_GPU_API_MINOR`, plus a capability bit field.
- `R5_GPU_GET_INFO` is call 0 and never changes.
- Additive only, as with LibR5 in `AGENTS.md`:
  - a new call or field is added at the end;
  - a call that has shipped does not change;
  - a new struct size goes along in a `size` field, so old callers keep working.
- A breaking change raises `MAJOR`. The library refuses a `MAJOR` it does not know. RADV then reports no device, and Vulkan falls back to Lavapipe.

### Calls in version 1

| Call | Purpose | Corresponds to (libdrm_amdgpu) |
|---|---|---|
| `GET_INFO` | Common part: version, capabilities, vendor, PCI ID, VRAM and GART sizes, the GPU address-space layout, clock frequency of the timestamps. Then a **vendor block** with its own size and version: for AMD the fields RADV needs (family, GC version, shader engines and CUs, `GB_ADDR_CONFIG`, firmware versions); later for Intel what ANV and iris need | `amdgpu_query_info`, `amdgpu_query_gpu_info`, `amdgpu_query_firmware_version` |
| `READ_REG` | Read a register from a fixed allow-list (for example `GB_ADDR_CONFIG`). Anything outside the list is refused | `amdgpu_read_mm_registers` |
| `CTX_CREATE` / `CTX_DESTROY` | Context with its own GPU address space (VMID) | `amdgpu_cs_ctx_create`, `amdgpu_device_initialize` |
| `BO_CREATE` / `BO_FREE` | Buffer in VRAM or GTT, flags for CPU access. Returns a handle and, for CPU access, an area | `amdgpu_bo_alloc`, `amdgpu_bo_free` |
| `BO_MAP` | Area for the CPU mapping | `amdgpu_bo_cpu_map` |
| `VA_MAP` / `VA_UNMAP` | Buffer at a GPU address in the context | `amdgpu_bo_va_op` |
| `CS_SUBMIT` | IB list, buffer list, dependencies in and out. Returns a sequence number | `amdgpu_cs_submit_raw2` |
| `FENCE_WAIT` | Wait for a sequence number, with a timeout | `amdgpu_cs_query_fence_status` |
| `SYNCOBJ_*` | Create, wait, signal, export and import of sync objects | `drmSyncobj*` |
| `BO_EXPORT` / `BO_IMPORT` | Share a buffer with app_server for presentation | `amdgpu_bo_export` / `import` (dma-buf) |

### What the kernel driver does

These tasks move from the accelerant into the kernel driver:

- **Owning the GPU.** With `gfx9_kernel true`, the kernel driver programs the rings and the memory controller. The accelerant logs `GFX9 owned by the kernel` and does not start the command processor. The fixed sequences from `GRAPHICS.md` stay unchanged and keep their readback check. Without that key, the accelerant still starts the GPU inside app_server.
- **Interrupts** through the IH ring: with `gfx9_irq true`, the end-of-pipe fence reports itself. The 5625U boot used MSI irq 61.
- **GART:** with `gfx9_gart true`, the GPU reads and writes a 1 MiB buffer in system memory. The 5625U boot matched all 262144 dwords and then ran an indirect buffer from that window.
- **Address space per context** (VMID 1–15, page tables). VMID 0 stays for the kernel.
- **Simple scheduling** per ring: FIFO with time limits, plus hang detection and reset.
- **Memory accounting** per team, released when a team ends.

### GPU address space in M4 and M5

- **VMID 0 stays the kernel's.** It holds the rings, the fence page, the system aperture and the kernel GART from M3. Applications never submit with VMID 0.
- **M4: one shared user address space.** VMID 1 gets real multi-level page tables, as `gmc_v9_0` builds them. Every client of the render node shares it. That is enough for `vulkaninfo` and VulkanTeapot, but it is **not isolation**. The layout follows what RADV expects from `amdgpu`: the low range (`virtual_address_offset` up to `virtual_address_max`) and the high range (`high_va_offset` up to `high_va_max`). The high range matters because RADV places 32-bit allocations at `address32_hi` (`0xffff8000` on GFX9). The VA manager lives in the DRM library, as in libdrm. A binary syncobj follows the latest submit. It is signalled when `submitted` is greater than 0 and `point` has caught up. A job signals it with that job's sequence. `submitted` rises at submit and `point` rises at retire. Reset sets both to 0. `DRM_SYNCOBJ_CREATE_SIGNALED` sets both to 1.
- **M5: one address space per open render node.** After that, two programs cannot see each other's buffers. The decisions:
  - **The unit is one open file of the render node**, as with an `amdgpu` file on Linux. Every context in that file shares that address space. RADV expects exactly that.
  - **VMIDs 1–15 are assigned per submission**, as `amdgpu_vmid_grab` does. A VMID is not a fixed seat, so there is no limit of 14 programs. Reusing a VMID for a different address space first invalidates that VMID's TLB. The fixed sequences in `GRAPHICS.md`, including the K2 invalidate word for VMID 0, do not change. Programming VMIDs 1–15 is a new, separate sequence.
  - **A GPU page fault** (an address outside its own space) stops only that address space. Its contexts report "lost", and RADV returns `VK_ERROR_DEVICE_LOST`. Other programs keep running. The faulting address goes to the syslog.
  - **A job that runs past its time limit** marks its address space "lost" in the same way. M5 does not reset the GPU. If the ring stays blocked, the GPU stays off until the next boot (see "When things go wrong"). The reset with a ring restart belongs to M10.
  - **Clean-up:** when a file closes, or its team ends or crashes, its jobs are retired or cancelled, and its buffers, page tables and VMID are released.
  - **Memory limits per address space**, as starting values in the radeon_hd settings: VRAM at most half of the carveout (256 MiB on the 5625U, whose carveout is 512 MiB; Windows reports 495 MB), and GTT at most a quarter of system memory. A limit that is hit returns `ENOMEM`, so RADV can fall back or report it. The largest single buffer goes up from the 64 MiB of M4 to the same limits.
  - **Pipeline sync.** Linux writes `COND_EXEC` and `WAIT_REG_MEM` when the VMID changes or the page tables are flushed. R5 has one VMID 1, so a user job does not write those packets. Two address spaces in M5 need them.
  - **Renoir page tables.** The 6 Nov 2026 capture `capture-20261106-194121` logs `vm size is 262144 GB, 4 levels, block size is 9-bit, fragment size is 9-bit`, and its indirect buffers sit at `0xffff8001…`. M4 programs that four-level, 48-bit table (`VM_CTX1_PT_END` `0x0000000fffffffff`). M5 keeps that table. `GET_INFO` reports `high_va_offset` `0xffff800000000000` and `high_va_max` `0xffffffffffbfe000`, the range `amdgpu_kms.c` reports for this VM. `VM_CONTEXT1_CNTL` bit 7 (retry) stays off: R5 does not handle a retry fault.
- **Security.** Until M5 is done, the render node only accepts submissions when `gpu_render true` is in the driver settings. That is a development switch. With it off, the node only answers `GET_INFO`, so `vulkaninfo` shows the GPU and RADV reports that it cannot create a device; Vulkan then uses Lavapipe. Whether user IBs can reach VMID 0 addresses through the system aperture is checked in M4 and recorded in `GRAPHICS.md`. **After M5**, `gpu_render` is no longer needed. Every account may use the render node, including standard accounts, as soon as the GPU steps in the radeon_hd settings are on. Isolation comes from M5, not from who is signed in. The GPU stays opt-in per machine until M10.

## R5 DRM library (userland)

- Provides **`libdrm.so.2`** (the common part: device enumeration, versions, sync objects, `drmIoctl`) and **one front end per vendor**, with the same names and function signatures as the **upstream libdrm headers**. That way Mesa builds against the ordinary headers and runs unchanged.
  - **AMD:** `libdrm_amdgpu.so.1`, the functions RADV and Zink call. Includes the VA manager.
  - **Intel (later):** ANV and iris do not use a libdrm library for Intel. They send the i915 ioctls directly through `drmIoctl`. That front end translates those ioctls (GEM create and mmap, execbuffer2, context and VM create, query) into R5 GPU interface calls.
- Both front ends sit on the same R5 GPU interface. A new vendor adds a front end and a vendor block in `GET_INFO`. It does not change the common calls.
- Only implements what Mesa (RADV, Zink and the winsys) actually calls. Everything else returns `-ENOSYS` and logs once.
- **Symbol check.** A script lists which libdrm symbols the Mesa build imports (`nm -D` / `readelf --dyn-syms` on the RADV ICD and libgallium). It checks that the library exports them all. That runs at every Mesa update.
- Talks only to the R5 GPU interface. It does not touch the hardware.

## Release mechanism

The goal: a new Mesa is a small change and not a project, and a mismatch shows up as a clear error, never as a broken GPU.

### Components and versions

| Component | Version kept in | Rule |
|---|---|---|
| R5 GPU interface | `R5_GPU_API_MAJOR.MINOR` in the kernel header | Additive. A breaking change raises `MAJOR` |
| R5 DRM library | Its own version, plus the lowest supported `R5_GPU_API` | Supports at least the current and the previous `MINOR` |
| Mesa (RADV, Zink, llvmpipe, Lavapipe) | Pin with version and sha256 of the source tarball | One pin per R5 release |
| Vulkan loader, libglvnd, LLVM | Pins in `distro/packages` (now Mesa 25.3.6, loader 1.4.367, libglvnd 1.7.0, LLVM 21.1.8) | Update together with Mesa when Mesa asks for it |

A **compatibility table** in this document (or a separate `distro/gpu/COMPAT` file) records per R5 release which Mesa pin, which library version and which `R5_GPU_API` belong together, and on which machine that was tested.

### Mesa source and patches

- The target is **Mesa without R5 patches**. The R5 support sits in the library, not in Mesa.
- Patches that turn out to be needed anyway go in a directory of their own. Each has a reason, the upstream status, and the Mesa version it applies to. Each patch is removed as soon as upstream no longer needs it.
- The Haiku WSI (presenting to a window) is checked first against what the HaikuPorts Mesa recipe carries for Lavapipe on Haiku. Reuse it before writing something new.
- Building Mesa with RADV (`-Dvulkan-drivers=amd,swrast` and Zink) uses **a recipe of R5's own, in this repository** (decided by the user, 5 Nov 2026), next to the current HaikuPorts packages. The recipe pins the Mesa version and sha256, lists the meson options, and builds the hpkg that `distro/packages` ships.

### Updating Mesa, step by step

1. Move the pin to the new version and sha256.
2. Build it. Any patches must still apply.
3. Run the symbol check against the R5 DRM library. A missing symbol means: add it to the library first.
4. Tests on the reference machine:
   - `vulkaninfo` names the GPU as RADV;
   - a fixed set of Vulkan tests: `vkcube` offscreen with a known image, and a small dEQP-VK subset;
   - the GPU self-test passes;
   - Zink: a GLTeapot frame matches;
   - fallback: with `gpu false`, everything runs on Lavapipe and llvmpipe.
5. Record the result in `CHANGELOG.md` and in the compatibility table.
6. Keep the previous Mesa package available. A rollback is one pin back.

## 2D in app_server

Haiku applications draw through app_server. The steps:

1. **Last copy through the GPU.** app_server's back buffer goes to the screen through a DMA copy (the `DMA_DATA` path from `GRAPHICS.md` has already been shown to work). Painter keeps drawing on the CPU. The risk is small and the result is easy to measure.
2. **Compositing on the GPU.** Windows as GPU buffers, combined on the GPU.
3. **A GPU drawing engine** behind the same `DrawingEngine` interface, for example through Vulkan. Only if measurements show it is faster. Ordinary buttons and text often stay faster on the CPU (`GRAPHICS.md`, step 7).

Painter stays in every step. With an unavailable GPU, app_server uses it exactly as it does today.

## Milestones

| # | Milestone | Proof | Status |
|---|---|---|---|
| M0 | GFX9 ring and fence | Scratch `0xDEADBEEF` on the 5625U | done (#4090) |
| M1 | IB and DMA fill | Pattern correct, time grows with the work | done (#4092) |
| M2 | Compute shader | Waves compute and write; time scales | done on the 5625U: every checked dword correct, 16 MiB took 3.9× as long as 4 MiB (#4094) |
| M3 | Kernel driver owns the GPU, plus interrupts and GART | The fixed sequences run from the kernel; a fence arrives by interrupt; the GPU writes into system memory | done on the 5625U: kernel runs the sequences, MSI irq 61 client 20 source 181, GART DMA 262144 dwords of `0xA5A5A5A5`, GART IB sequence 10, Shutdown without a panic (`docs/GRAPHICS.md`, `GFX9 kernel on GC 9.3 (fixed)`) |
| M4 | R5 GPU interface v1, the R5 DRM library, the Mesa build with RADV, and VulkanTeapot | `vulkaninfo` names the 5625U GPU as RADV, next to Lavapipe. **VulkanTeapot** (a fork of GLTeapot) draws the teapot through RADV, offscreen with a copy into a `BBitmap`, and shows the device name; on a machine without a supported GPU it draws through Lavapipe | not done. The 6 Nov 2026 stick answered the render node (`api 1.0 device 15e7 cu 7`). `vkCreateDevice` failed: context create returned `B_NO_INIT` because the VMID 1 TLB ack waited on bit 0. No RADV pixels |
| M5 | Address space per open render node | Two VulkanTeapots draw at the same time, each with the correct image. A test program that reads the other's GPU address gets a page fault and `VK_ERROR_DEVICE_LOST`, while the teapots keep running. A program killed halfway returns all its GPU memory. A standard account draws through RADV without `gpu_render` | |
| M6 | RADV workload | VulkanTeapot and `vkcube` offscreen with a correct image (checked pixels against Lavapipe), GPU time measured. The GFX ring is 1024 dwords. Linux pads a job to 256 dwords, so about three jobs fit and every frame waits for room. A larger ring needs the ring-contract procedure and a stick. | |
| M7 | Presentation | `vkcube` in a window; falls back to software if that fails | |
| M8 | app_server uses the GPU for the last copy | Measured gain; Painter fallback works | |
| M9 | Zink for OpenGL | GLTeapot on the GPU; llvmpipe as fallback | |
| M10 | GC 9.3 `release-supported` | GPU is the default on that family; opt-out works; hang policy tested | |

M4 in more detail, in the order of the work: the vendor-neutral R5 GPU interface v1 with the render node; VMID 1 with multi-level page tables; `CS_SUBMIT` with an interrupt-driven fence; the R5 DRM library with the AMD front end; the Mesa recipe with RADV and Zink; VulkanTeapot. The proof is a self-test after Sign in that writes one result file: the interface, `vulkaninfo`, and VulkanTeapot with checked pixels.

## Intel (second vendor)

The second reference machine is a **Lenovo X1 Carbon Gen 8** (10th-generation Core, Comet Lake). Its GPU is most likely Intel UHD Graphics, Gen 9.5; that is confirmed only by the PCI ID on the first boot. The Wi-Fi is most likely an Intel AX201.

Why it fits the plan:

- Intel publishes the hardware manuals (the PRMs). There is no PSP and no signed firmware load for rendering; Gen 9.5 submits through execlists, and GuC and HuC are optional.
- Haiku's `intel_extreme` can already set modes on many Intel chips, which `radeon_hd` cannot yet do on DCN.
- Mesa has ANV (Vulkan) and iris (OpenGL). OpenGL follows the AMD decision: Zink on ANV first.

The work, after M4 on AMD:

| # | Step | Proof |
|---|---|---|
| I0 | Boot R5 on the X1 Carbon | The PCI IDs of GPU, Wi-Fi and audio are recorded; what `intel_extreme` and the Wi-Fi driver do is recorded |
| I1 | Intel ring and fence in `intel_extreme` | A batch buffer writes a known value and a fence lands, opt-in like `gfx9_ring` |
| I2 | The R5 GPU interface on Intel | The common calls work unchanged; an Intel vendor block in `GET_INFO` |
| I3 | The i915 front end in the R5 DRM library, and ANV | `vulkaninfo` names the Intel GPU; VulkanTeapot draws through ANV |

Do not add Intel code before I0, and do not change shipped interface calls for Intel. A new need is a new call or a new field at the end.

## Licensing

- The R5 GPU interface (the kernel header) and the R5 DRM library are R5's own code, and they are **MIT** (decided by the user, 5 Nov 2026). Mesa, RADV and every Vulkan or OpenGL application load that library. With GPL-3.0, those applications would become part of a GPL-3.0 combination. Haiku's own driver interfaces are MIT for the same reason. This is a deliberate exception to the GPL-3.0 default in `AGENTS.md`.
- The GFX9 start code in the kernel add-on (`gfx9_start.cpp`) is GPL-3.0-or-later (decided by the user, 5 Nov 2026). Applications do not link it.
- The kernel driver `radeon_hd` stays as it is: the Haiku parts MIT, the R5 snippets GPL-3.0-or-later.
- Mesa and libdrm headers are MIT. Most files of Linux `amdgpu` carry an MIT-style notice from AMD, and most of `i915` is `SPDX-License-Identifier: MIT`. The Linux kernel as a whole is GPL-2.0, and some DRM core files are GPL-2.0-only. So: register offsets, values and their order are taken as facts. A single file or table may be taken over only when that file itself is MIT, with its notice kept, a row in `THIRD_PARTY_NOTICES.md`, and never from a GPL-2.0-only file.
- VulkanTeapot is forked from GLTeapot. The files it takes over keep their notices: the Be Sample Code License (Be Incorporated, 1991–1999) and MIT (Haiku, 2008). The new Vulkan code is GPL-3.0-or-later. The teapot geometry comes from a source with a known license, recorded in `THIRD_PARTY_NOTICES.md`; GLTeapot itself does not carry it (it calls `auxSolidTeapot`).
- New components get a row in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`.

## Open decisions

None at the moment. New ones are added here.
