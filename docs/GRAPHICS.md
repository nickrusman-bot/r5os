# Native graphics and Vulkan

Long-term plan for a physical GPU. VirtualBox guest devices are `docs/VBOX-GUEST.md`. A framebuffer mode is not GPU acceleration. A Vulkan loader that only finds Lavapipe is not GPU acceleration.

Nothing below has been shown on the Ryzen 5 5625U. The desktop people use today is software painting in `app_server`.

## What the daily image has

Build 10018. OpenGL on that disk is libglvnd 1.7.0 plus Mesa 25.3.6. Jam compiles OpenGL apps against that devel package. Mesa 22 is not the pin and is not on the image.

| Piece | Role here |
|---|---|
| `vesa`, `framebuffer` | Linear framebuffer. This is the VirtualBox desktop when `vboxvideo` is absent. |
| `vboxvideo` | VirtualBox display on the daily image. Not a physical GPU. |
| `virtio` / `virtio_gpu` | QEMU display. Not the 5625U. |
| `radeon` | Old Radeon, not HD / GCN / Vega. |
| `radeon_hd` | Modesetting for many later AMD IDs, including Vega and some Navi. Not a Vulkan driver. |
| `intel_extreme` | Intel display, with a Tiger Lake PLL in tree. Not Arc, and not proof of 3D. |
| `nvidia` | Legacy kernel driver (`driver.c`). Not NVK, not Turing-and-newer. |
| Accelerants | Userspace mode and blit hooks beside those drivers. |
| `app_server` `drawing/BitmapDrawingEngine` | CPU bitmaps. Windows are not GPU surfaces. |
| libglvnd 1.7.0 + Mesa 25.3.6 | `libGL.so.1` and llvmpipe. Headers come from `mesa_devel` 25.3.6. |
| Lavapipe + Vulkan loader 1.4.367 | Installed. Not shown as the library an OpenGL app loaded. |

There is no libdrm and no amdgpu/i915/nouveau kernel driver. OpenGL, where it runs, is Mesa software. A QEMU probe of an earlier test image reported `llvmpipe (LLVM 21.1.8, 128 bits)` and `Mesa 25.3.6`. On 2026-11-01 the VirtualBox daily showed GLTeapot drawing a teapot. That boot did not print a renderer string. It is not a GPU result.

`radeon_hd` in this tree lists `1002:1636` and `1002:1638` as “Radeon Renoir” and enables them. `1002:164c` is compiled out (upstream note: not working, Haiku `#17516`). `1002:15e7` is not in the table. PCI databases call `1638` Cezanne and `15e7` Barcelo. The Ryzen 5 5625U is a Barcelo-refresh Cezanne with Radeon Vega 7. **This machine’s PCI ID has not been read.** Until it is, do not claim `radeon_hd` drives the panel. Even a match is modesetting, not command submission and not RADV.

## Other people’s work

Reuse before rewriting. Do not paste Linux `amdgpu` into the kernel.

- **HaikuPorts Mesa 25.3.6** is the llvmpipe already on the image, plus Lavapipe. Lavapipe is a software Vulkan implementation. A headless Lavapipe demo has been shown by Haiku developers; that is CPU Vulkan, and it was not what the OpenGL probe loaded.
- **Vulkan loader** 1.4.367 is on the image. Loader presence is not a GPU.
- **radeon_hd** (kallisti5 and others) is the display driver to extend for AMD, not a second modesetting stack.
- **RADV** needs a kernel that can allocate GPU memory, submit rings, and signal fences. Haiku does not have that for Vega. Porting RADV alone cannot light the GPU.
- **Intel `intel_extreme`** can mean a picture on older and some recent Intel iGPUs. ANV (Mesa Vulkan) is a separate kernel interface. A working panel is not 3D.
- **NVIDIA.** The in-tree driver is the old one. Experimental Haiku work that talks about NVK or Zink is not in this tree and is not a supported path. Do not treat a forum driver as production, and do not mix it with the Mesa package until the license and the kernel boundary are written down.
- **Zink** (OpenGL on Vulkan) is useful after a real Vulkan device exists. It does not create one.
- **app_server_neo** and similar display-server experiments are reference material. Replacing `app_server` is not required to put a bitmap on a GPU later. Existing apps stay on the current `BView` / `BBitmap` path.

Licenses follow `docs/LICENSING.md`. Mesa and libglvnd are MIT. The Vulkan loader is Apache 2.0. LLVM 21 is Apache-2.0 with the LLVM exception. VirtualBox 7.2.22 additions are GPL-3.0-only; how they ship is `docs/LICENSING.md` and `docs/VBOX-GUEST.md`. Linux kernel DRM drivers are GPL. Importing one into the kernel is an uncertain combination: report it and wait for legal review. Do not reject a better driver only because it is GPL, and do not relicense it.

## Two problems

**Acceleration.** An app or the display server submits GPU work and waits for it to finish.

**Display.** Modes, refresh, more than one monitor, cursor, planes, vsync, and putting a finished frame on the panel.

Either one can exist without the other. Test them separately, then connect them. A Vulkan swapchain that only copies back to a VESA framebuffer is still software presentation.

## Reference machine

AMD64 first. The reference GPU is the 5625U’s Vega 7, after its PCI ID is read on the metal (USB boot or a later native boot). ARM64 and RISC-V 64 stay behind that. Do not add abstraction for GPUs nobody can boot.

QEMU and VirtualBox may test the software path and `virtio_gpu` / VESA / `vboxvideo`. They do not count as the 5625U.

## Order

1. **Name the device.** On the 5625U, record the VGA PCI ID. Confirm whether this tree’s `radeon_hd` table contains it. No driver change before that.
2. **Picture, software stack unchanged.** If the ID matches, boot the panel with `radeon_hd` (or VESA if it does not). Desktop still painted by `app_server` on the CPU. This is the display milestone, and it is not acceleration.
3. **Software Vulkan.** Lavapipe is already packaged. `vulkaninfo` must say Lavapipe / llvmpipe. Keep it as the API test and the fallback. This step is not done until that string is captured.
4. **One kernel submission path** for the 5625U only: memory, a ring, a fence, and a fault that does not panic the machine. This is the long task. It is not a Linux DRM dump.
5. **One Vulkan ICD** (RADV or a thin Haiku ICD) whose `vulkaninfo` names that GPU, not Lavapipe. A triangle that stays correct for a while, with the GPU actually busy.
6. **Present.** The same triangle on the panel, vsync noted, fallback to the software bitmap if init fails.
7. **Desktop.** Only after (5) and (6). Measure a window move before talking about Skia or a new `app_server`. If the CPU painter is faster for ordinary controls, leave those on the CPU.

OpenGL apps keep the Mesa 25 / libglvnd path. Experimental ICDs are not required to boot.

## Fallback

No supported GPU: VESA, `framebuffer`, or `vboxvideo`, software `app_server`, optional Lavapipe. A GPU reset or a failed accelerant must not take down Login. The standard image boots with the experimental GPU driver absent.

## Tests that count

| Claim | Evidence |
|---|---|
| Desktop works | Boot, Tracker, a window. True in VirtualBox. |
| Software OpenGL | GLTeapot drew on the VirtualBox daily (2026-11-01). A QEMU probe named llvmpipe and Mesa 25.3.6. |
| Panel driver | 5625U syslog names the PCI ID and a mode. Not done. |
| Software Vulkan | `vulkaninfo` device string is Lavapipe. Not done. |
| GPU Vulkan | Device string is the Vega/Cezanne GPU, and a workload moves GPU counters or an equivalent busy signal. Not done. |
| Accelerated desktop | A measured `app_server` path submits that GPU. Not done. |

## Reporting

Say software or GPU. Name the PCI ID, the ICD, and the machine. VirtualBox is never the 5625U result.
