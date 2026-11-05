# GPU architecture

This is how R5 Operating System (R5) moves 2D and 3D rendering onto the GPU, without breaking Haiku compatibility, and while keeping the software path. It builds on [`GRAPHICS.md`](GRAPHICS.md). That document has the measured state of the hardware and the fixed GFX9 sequences.

## Decisions

These are settled. Change them only when the user asks.

1. **Haiku applications keep working, unchanged.** Applications use `BView`, `BBitmap`, `BGLView`/OpenGL and Vulkan. They never talk to the GPU directly. The GPU is an extra backend behind those APIs. `libbe` and its ABI do not change.
2. **Software always stays.** app_server's Painter (AGG), Mesa llvmpipe and Lavapipe stay installed and working. They are the path on every machine without a supported GPU: virtual machines, ARM64, RISC-V, and untested GPUs.
3. **When a supported GPU is present, rendering uses the GPU by default.** The software path is the fallback, not the default. The rules are under [Default and fallback](#default-and-fallback).
4. **The kernel side is a small R5 interface** in the `radeon_hd` driver, written by R5 itself. Linux `amdgpu` is not ported. That code is GPL-2.0-only, and `AGENTS.md` does not allow combining it with GPL-3.0.
5. **Vulkan is RADV** from Mesa. RADV talks to the R5 interface through a thin library that provides the part of `libdrm_amdgpu` RADV uses.
6. **Mesa updates have to be easy to take.** That is why there is a version contract between the kernel interface, that library and Mesa. See [Release mechanism](#release-mechanism).
7. **The R5 GPU interface header and the R5 DRM library are MIT** (decided by the user on 5 Nov 2026). See [Licensing](#licensing).
8. **Mesa with RADV is built from a recipe of R5's own,** in this repository (decided by the user on 5 Nov 2026).
9. **VulkanTeapot is the proof of concept for M4.** It is a fork of GLTeapot that draws through Vulkan: RADV when the GPU is available, Lavapipe otherwise.
10. **The render node is `/dev/gpu/radeon_hd/<n>`** (decided by the user on 5 Nov 2026). Other GPU drivers later get their own directory under `/dev/gpu/`.
11. **OpenGL is Zink on RADV.** radeonsi is not built (decided by the user on 5 Nov 2026). This is revisited only when measurements with Zink fall short, and then only on the user's request.

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
          R5 DRM library: libdrm.so.2 + libdrm_amdgpu.so.1 (subset)
                               |
          R5 GPU interface: ioctls on /dev/gpu/radeon_hd/<n>
                               |
          radeon_hd kernel driver: rings, interrupts, GART, VM, reset
                               |
                         GPU (GC 9.3 first)
```

- **OpenGL goes through Zink** (OpenGL on Vulkan) on top of RADV, so there is only one GPU driver to maintain. radeonsi, Mesa's native OpenGL driver, is not built. It is only reconsidered if Zink falls short in measurements, and only when the user asks.
- **Display stays a separate track.** That is modes, more than one screen, vsync, and putting a frame on the panel. DCN is not programmed yet; the firmware scanout stays. See `GRAPHICS.md`, "Two problems".

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

`/dev/gpu/radeon_hd/<n>` (decided), deliberately **not** under `/dev/graphics/`. app_server opens every device in `/dev/graphics/` as a screen. The render node is for applications and libraries; the existing `/dev/graphics/radeon_hd_*` stays for app_server and the accelerant.

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
| `GET_INFO` | Version, capabilities, PCI ID, chip family, GC version, VRAM and GART sizes, firmware versions, clock frequency of the timestamps | `amdgpu_query_info`, `amdgpu_query_gpu_info`, `amdgpu_query_firmware_version` |
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

- **Owning the GPU.** Today the accelerant starts the GPU inside app_server. After that, only the kernel driver programs the rings and the memory controller. The fixed sequences from `GRAPHICS.md` move along unchanged and keep their readback check.
- **Interrupts** through the IH ring: fences report themselves, without polling.
- **GART:** the GPU can read and write ordinary system memory.
- **Address space per context** (VMID 1–15, page tables). VMID 0 stays for the kernel.
- **Simple scheduling** per ring: FIFO with time limits, plus hang detection and reset.
- **Memory accounting** per team, released when a team ends.

Security: until per-context address spaces work, only app_server may submit work. Ordinary applications get access to RADV only after that.

## R5 DRM library (userland)

- Provides **`libdrm.so.2`** and **`libdrm_amdgpu.so.1`**, with the same names and the function signatures of the **upstream libdrm headers**. That way Mesa builds against the ordinary headers and runs unchanged.
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
| M3 | Kernel driver owns the GPU, plus interrupts and GART | The fixed sequences run from the kernel; a fence arrives by interrupt; the GPU writes into system memory | |
| M4 | R5 GPU interface v1, the R5 DRM library, the Mesa build with RADV, and VulkanTeapot | `vulkaninfo` names the 5625U GPU as RADV, next to Lavapipe. **VulkanTeapot** (a fork of GLTeapot) draws the teapot through RADV, offscreen with a copy into a `BBitmap`, and shows the device name; on a machine without a supported GPU it draws through Lavapipe | |
| M5 | Address space per context | Two programs submit at the same time, separated from each other | |
| M6 | RADV workload | VulkanTeapot and `vkcube` offscreen with a correct image (checked pixels against Lavapipe), GPU time measured | |
| M7 | Presentation | `vkcube` in a window; falls back to software if that fails | |
| M8 | app_server uses the GPU for the last copy | Measured gain; Painter fallback works | |
| M9 | Zink for OpenGL | GLTeapot on the GPU; llvmpipe as fallback | |
| M10 | GC 9.3 `release-supported` | GPU is the default on that family; opt-out works; hang policy tested | |

## Licensing

- The R5 GPU interface (the kernel header) and the R5 DRM library are R5's own code, and they are **MIT** (decided by the user, 5 Nov 2026). Mesa, RADV and every Vulkan or OpenGL application load that library. With GPL-3.0, those applications would become part of a GPL-3.0 combination. Haiku's own driver interfaces are MIT for the same reason. This is a deliberate exception to the GPL-3.0 default in `AGENTS.md`.
- The GFX9 start code in the kernel add-on (`gfx9_start.cpp`) is GPL-3.0-or-later (decided by the user, 5 Nov 2026). Applications do not link it.
- The kernel driver `radeon_hd` stays as it is: the Haiku parts MIT, the R5 snippets GPL-3.0-or-later.
- Mesa and libdrm headers are MIT. Linux `amdgpu` is not copied. Register offsets, values and their order are taken from it as facts.
- VulkanTeapot is forked from GLTeapot. The files it takes over keep their notices: the Be Sample Code License (Be Incorporated, 1991–1999) and MIT (Haiku, 2008). The new Vulkan code is GPL-3.0-or-later. The teapot geometry comes from a source with a known license, recorded in `THIRD_PARTY_NOTICES.md`; GLTeapot itself does not carry it (it calls `auxSolidTeapot`).
- New components get a row in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`.

## Open decisions

None at the moment. New ones are added here.
