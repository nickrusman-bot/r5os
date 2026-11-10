# Native graphics and Vulkan

Long-term plan for a physical GPU. The architecture (kernel interface, RADV, default and fallback, Mesa updates) is `docs/GPU-ARCHITECTURE.md`. VirtualBox guest devices are `docs/VBOX-GUEST.md`. A framebuffer mode is not GPU acceleration. A Vulkan loader that only finds Lavapipe is not GPU acceleration.

On the Ryzen 5 5625U the USB boots at 17:38 and 18:11 Europe/Amsterdam on 3 Nov 2026 reached the desktop through `radeon_hd`. That driver claimed `1002:15e7` and mapped the firmware framebuffer, `1920x1080`, pitch `7680`. It did not modeset DCN. The 18:11 syslog also has `skip encoder DPMS`, `command ring stays off`, and `CONFIG_MEMSIZE unused`. Acceleration below has not been shown. The desktop is software painting in `app_server`.

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

The device table that Haiku had inside `#if 0` is compiled. Linux `amdgpu_drv.c` from v6.12 is the PCI reference, and every explicit ID in that list is in the table. `1002:15e7`, `1636`, `1638` and `164c` are Renoir, DCN 2.1. `15e7` is the GPU on the Ryzen 5 5625U. DCE rows keep their DCE version. DCN rows store that version separately and leave the DCE generation at 0. After Beige Goby, Linux matches any AMD VGA, display-other, or processing-accelerator device as `CHIP_IP_DISCOVERY`. The table does the same and leaves the display version at 0. It does not read the GPU IP table. A DCN row, and the IP-discovery row, is a claim, not a modeset: the firmware scanout stays. A DCE row is a modeset. Tonga, Fiji, Carrizo, Stoney, Polaris, Vega M and Vega 10, 12 and 20 were inside `#if 0` in Haiku. They now run the DCE modeset and the Evergreen memory-controller setup. None of those families has been tested. The IP-discovery row also claims an AMD display-other device that does not drive the panel, such as the dGPU in a hybrid laptop. That case has not been tested either.

Visible memory is the PCI framebuffer BAR, the same window Linux `gmc_v9_0_mc_init` records as `aper_base` and `aper_size` before an APU replaces that base with the memory-controller offset. The CPU mapping stays the BAR. Haiku creates one write-combining area for it. `amdgpu_gmc_get_vbios_allocations` keeps the firmware picture out of the heap; Raven and Renoir also keep at least 9 MiB because a client writes the start of VRAM on resume. This driver reserves the live boot picture, aligned to 4 KiB, on every chip whose picture sits inside the BAR. The pool is the larger remaining side. `radeon_vram_alloc` bumps that pool and does not free it. On Raven and Renoir that pool holds a GFX9 ring and a 4 KiB fence word. The ring is the next power of two that holds `gfx_v9_0_cp_gfx_start` plus the scratch packet. For the v6.12 clear-state table that is 4 KiB and 914 dwords. The stream is the clear-state preamble, context control, the `SECT_CONTEXT` extents, clear state, the CE partition base, `VGT_INDEX_TYPE`, and then `PACKET3_SET_UCONFIG_REG` of scratch register 0 to `0xDEADBEEF`. The rest is type-2 NOPs. The fence word is `0xCAFEDEAD`. A boot of that allocation on the 5625U kept the desktop: ring `0x7e9000+0x1000`, packet `0xc0017900`, fence read `0xcafedead`. For PCI `1002:15e7`, `1636`, and `1638`, a non-zero `MP0` `C2PMSG_81` means the PSP secure OS is already running. The driver then halts RLC F32, creates the PSP v12 GPCOM ring, sends `GFX_CMD_ID_SETUP_TMR` for a 4 MiB region in the visible pool, and sends `GFX_CMD_ID_LOAD_IP_FW` for RLC, PFP, CE, ME, and the MEC body and jump table. `15e7` and `1638` load `green_sardine_*.bin`. `1636` loads `renoir_*.bin`. It does not load the PSP bootloader and does not reroute interrupts. It does not send a mode-1 reset. Linux `soc15_need_reset_on_init` is true only for `CHIP_RENOIR` in this series. Raven and Picasso return false, and `soc15_asic_reset` returns before a reset for those chips. `15e7`, `1636`, `1638` and `164c` are `CHIP_RENOIR` in v6.12. MP1 12.0.1 then uses mode 2: `smu_v12_0_mode2_reset` writes `PPSMC_MSG_GfxDeviceDriverReset` (`0x1E`) with argument `2` to the MP1 mailbox (`C2PMSG_66`, `C2PMSG_82`, `C2PMSG_90`). The ring start sends that message before any command-processor register write, and only when the response is `1`. An RLC header 2.1 also sends the CNTL, GPM and SRM lists (PSP types 22, 20 and 21) before `RLC_G`. `SRM_ENABLE` is set only after those three are accepted. `CP_PG_DISABLE` is set. The RLC jump table is not programmed. A mailbox that stays `0`, or any other response, leaves the ring unprogrammed. Lucienne `164c` is in the Linux set and is not reset here, because its firmware stays unloaded and DCN is not reprogrammed. The firmware scanout is not rewritten after the message. A missing sign of life still uses the direct writes in `gfx_v9_0_cp_gfx_load_microcode`. The address register must read back the header version on that direct path. `gfx_v9_0_cp_gfx_resume` then sets `CP_RB0_CNTL`, the pointers, and `CP_RB0_BASE` from `MC_VM_FB_LOCATION_BASE << 24` plus the byte offset. `MC_VM_FB_OFFSET << 24` is the physical carveout. The system-aperture default and `SETUP_TMR` `system_phy_addr` use that carveout. `FB_LOCATION` is not overwritten with the offset. That preamble is written into the ring before the scratch packet. `CP_RB0_CNTL` follows the ring size, and the write pointer is the dword count. Scratch is polled for `0xDEADBEEF` for 100 ms. A miss logs `CP_RB0_RPTR`, restores the ring registers, and halts the command processor. After the PSP accepts the firmware, `RLC_CGCG_CGLS_CTRL` is cleared and `RLC_ENABLE_F32` is set, then restored if the scratch word does not change. MEC is loaded through the PSP and started before the GFX ring. MEC2 is not loaded. The Renoir golden list is programmed, including `GCEA_PROBE_MAP` (`0x070c`, value `0x0000cccc`). GART is still not programmed. `gfxinfo` prints `/boot/system/cache/radeon-gfx.txt`. The 10:23 stick reached the desktop. `RLC_CNTL` was already `0x1` and stayed `0x1`. Scratch stayed `0xcafedead` and the ring was restored. The stick written at 09:56 Europe/Amsterdam on 4 Nov 2026 reached the desktop. The PSP accepted the four firmware files. Scratch stayed `0xcafedead` and the ring was restored. Lucienne `164c` does not get the Renoir blobs. Navi is GFX10 and does not take these registers. Hainan and Topaz do not either. The R600 reset below the old ring path stays unreachable. VRAM past the BAR is not allocatable. GART is not set up. DCN still does not program the memory controller. DCE still programs its own controller and still scans the start of the BAR.

USB boot of this Ryzen 5 5625U on 3 Nov 2026, read back from the stick’s BeFS syslog: the VGA device is `1002:15e7` revision `c2`, bus 4 device 0 function 0, subsystem `103c:8a31`. The kernel names it Barcelo. The old `radeon` driver logged `no supported devices found`. The loaded graphics driver on that boot was `framebuffer`, reusing the firmware buffer at `0xfce0000000`, size `0x7e9000` (1920×1080, 32-bit). HDMI audio on the same device is `1002:1637`.

`1002:15e7` is Renoir, DCN 2.1, `CHIP_APU`. Init does not run the DCE encoder, display clock, AUX, modeset, or encoder DPMS. It does place the Raven/Renoir ring and fence in the visible pool, and it does not submit that ring. The bootloader framebuffer stays the scanout. `CONFIG_MEMSIZE` is unused on this APU. The PCI aperture stays the upper bound. The 17:38 boot confirmed the firmware scanout: eDP attached, `1920x1080`, and the desktop appeared. The 18:11 boot confirmed `CONFIG_MEMSIZE unused`, `command ring stays off`, and `skip encoder DPMS`, and the desktop appeared again. Picasso `1002:15d8` is in the same Raven group, DCN 1.0, and is compiled. The stick written at 14:48 Europe/Amsterdam on 3 Nov 2026 did boot. `radeon_hd` opened the device, read the video memory size as 4194303 MB, mapped 256 MB, and then logged `0 attached monitors` and closed. `framebuffer` kept the firmware buffer.

That BIOS is AtomBIOS master format 2. The old object-header walk counted 235 paths and stopped on `usSize`. The driver now reads a display-object table of revision 1.4 or newer as a flat path list, takes the GPIO pin LUT from index 12, treats a missing `GPIO_I2C_Info` as normal, runs `SetDCEClock` 2.2 with the 2.1 argument block, and ignores a `CONFIG_MEMSIZE` of 0 or above 65536 MB in favour of the PCI BAR. eDP falls back to the boot loader's VESA EDID when AUX fails. Raven does not write Evergreen memory-controller registers. On DCN (`RADEON_RAVEN` and later) it also does not program the DCE 5 CRTC registers. The image built at 15:35 Europe/Amsterdam on 3 Nov 2026 contains the probe and was written to the USB stick. It does not contain the CRTC skip. A later image, jammed at 15:59 Europe/Amsterdam the same day (exit 0, 1176 targets), contains the CRTC skip. That image has not been written to the stick. Neither image has been booted. Do not claim `radeon_hd` drives the panel until a later syslog shows a mode. A table match is still modesetting, not command submission and not RADV. A bad modeset can leave the panel black until reboot.

Those six sources live in `distro/graphics` and are copied over the Haiku tree by `scripts/apply-radeon.py`. Haiku, Inc. keeps the MIT license on the original work; the copyright and MIT header stay at the top of each file. R5 changes are the `SPDX-Snippet` blocks and are GPL-3.0-or-later. Shipping the combined file follows GPL-3.0-or-later. The syslog lines that start with `R5 radeon_hd` come from that copy. The CPU log writer is a separate R5 program, `distro/cpu/r5_pstate_log.cpp`, already GPL-3.0-or-later.

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

1. **Name the device.** On the 5625U, record the VGA PCI ID from `r5-hardware.txt` on the anyboot EFI partition (FAT, Windows can open it; `listdev` is in that file). Confirm whether this tree’s `radeon_hd` table contains it. No driver change before that.
2. **Picture, software stack unchanged.** If the ID matches, boot the panel with `radeon_hd` (or VESA if it does not). Desktop still painted by `app_server` on the CPU. This is the display milestone, and it is not acceleration.
3. **Software Vulkan.** Done as a CPU test. Lavapipe’s manifest is `add-ons/vulkan/icd.d/lvp_icd.json`. On 1 Nov 2026 the user ran `vulkaninfo --summary` on the 21:36 VirtualBox image. The device was `PHYSICAL_DEVICE_TYPE_CPU`, name `llvmpipe (LLVM 21.1.8, 256 bits)`, driver `llvmpipe`, driver info `Mesa 25.3.6 (LLVM 21.1.8)`. Lavapipe is the ICD; the device string is llvmpipe, not the word Lavapipe. Keep this as the API test and the fallback. vkcube and the Vulkan-Tools mock ICD are not required for this step.
4. **One kernel submission path.** The visible pool is in place for every chip. Raven and Renoir get a ring and a fence word there. Renoir `15e7`, `1636`, and `1638` ask the PSP to load RLC, PFP, ME, and CE when `C2PMSG_81` shows the secure OS, then program the GFX9 ring. The 09:19 stick reached the desktop on the direct path. PFP, CE, and ME were written. Their address registers still read `0xffffffff`. Scratch stayed `0xcafedead`, and the ring registers were restored. The stick written at 09:56 Europe/Amsterdam on 4 Nov 2026 reached the desktop. The PSP accepted RLC, PFP, CE, and ME. Scratch stayed `0xcafedead` and the ring was restored. The ring now also carries the clear-state preamble before that scratch packet. The stick written at 10:57 Europe/Amsterdam on 4 Nov 2026 reached the desktop. Scratch stayed `0xcafedead` and `CP_RB0_RPTR` was `0x98`. The CP ring base remains the framebuffer MC address. The GFXHUB system aperture is widened to cover that window, the way `gfxhub_v1_0` does for Renoir and Cezanne. The aperture default is a zero page in that window. MEC is loaded and left halted. The 13:23 stick reached the desktop. PSP accepted RLC, PFP, CE, and ME, and refused the combined MEC image with status `0x80000203`. The MEC file is now split the way `amdgpu_ucode_init_single_fw` does: body as type 4, jump table as type 5. GC 9.3 does not load MEC2. `jam -q @r5-anyboot` wrote `r5-os-anyboot.iso` at 13:45 Europe/Amsterdam on 4 Nov 2026. That image was written to the General UDisk at 13:48 and reached the desktop. The MEC body was still refused with status 0x80000203. The next image, written at 14:05 Europe/Amsterdam on 4 Nov 2026 and put on the General UDisk at 14:09, sends SETUP_TMR before the firmware. That boot reached the desktop and the TMR command returned status 0xffff0006. The image built at 14:27 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 14:31 reached the desktop. Both SETUP_TMR forms returned status 0xffff0006, and the ring was not submitted. The image built at 14:50 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 14:53 skips SETUP_TMR, keeps going when MEC is refused, and waits for the RLC serdes before the clear-state buffer. That image reached the desktop. The command processor fetched 152 dwords from a read pointer of 0 and stopped. Scratch stayed 0xcafedead. The loaded PFP, ME, and CE were still the renoir blobs. The image built at 15:16 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 15:19 loads green_sardine for 15e7 and 1638, and renoir for 1636 and 164c. That boot reached the desktop. The green_sardine PFP, ME, and CE were accepted, and the read pointer still stopped at 0x98. Scratch stayed 0xcafedead. The image built at 15:52 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 15:56 maps PCI BAR 2 and publishes the write pointer the way gfx_v9_0_ring_set_wptr_gfx does: the 64-bit count in the poll slot, then doorbell qword 0x8b at byte 0x458. A missing BAR still writes the poll slot and uses CP_RB0_WPTR. That stick reached the desktop. BAR 2 is 0xfcf0000000, 2 MiB. The doorbell control read back 0x40000458 and the poll slot read back 914, but both the writeback and CP_RB0_RPTR stayed 0. Scratch stayed 0xcafedead. The 15:19 stick still fetched 152 dwords through CP_RB0_WPTR. The image built at 16:18 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 16:22 sets RCC_DOORBELL_APER_EN before the doorbell store and logs that bit plus the SDMA, IH and MMSCH ranges. That stick reached the desktop. The aperture read 0 and then 1. The SDMA, IH and MMSCH ranges read 0. The MMIO read pointer was 0x80 immediately after the doorbell and 0x98 after 100 ms. The writeback stayed 0. Scratch stayed 0xcafedead. The image built at 17:02 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 17:07 submits the scratch packet alone, then the preamble and context control, then one context register. A completed single register is bisected up to the 212 values of the first SET_CONTEXT. The 914-dword stream is still submitted last. That stick reached the desktop. The scratch probe stopped with both read-pointer samples at `0x3` and scratch `0xcafedead`. The preamble probe published write pointer 8, the one-register probe published 11, and both read back `0x98`, which is past those write pointers. The 914-dword probe also stayed at `0x98`. A doorbell of 0 between probes wraps the 1024-dword ring once the read pointer is 3, so the short rings were not isolated. The image built at 17:33 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 17:37 removes those probes. The ring holds the three-dword scratch packet from gfx_v9_0_ring_test_ring and one doorbell publishes write pointer 3. The clear-state stream is not written into that buffer. The calls are marked REMOVE-GFX9-SCRATCH-TEST. That stick reached the desktop. The read pointer was `0x3` immediately and after the wait, the write pointer was 3, and scratch stayed `0xcafedead`. The three dwords were fetched and `SCRATCH_REG0` did not change. The image built at 17:55 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 17:59 puts the preamble and context control in front of that scratch packet. The write pointer is 8. The 212 context registers are not in the buffer. The calls stay marked REMOVE-GFX9-SCRATCH-TEST. That stick reached the desktop. The read pointer was `0x8` immediately and after the wait, the write pointer was 8, and scratch stayed `0xcafedead`. All eight dwords were fetched. The preamble and context control did not make the scratch write land. The image built at 18:18 Europe/Amsterdam on 4 Nov 2026 runs three rings in one boot, still marked REMOVE-GFX9-SCRATCH-TEST. The package inside that ISO was checked: the accelerant contains GFX9 ring tests. It was written again to the General UDisk at 18:46. The first is SET_UCONFIG plus NOPs with write pointer 256, past one fetch block of 128 dwords. The second is WRITE_DATA to register 0xC040, also write pointer 256, in a fresh buffer. The third is the 914-dword clear-state stream, also in a fresh buffer. The command processor is halted and the read pointer is forced to 0 before the second and third doorbells. A doorbell of 0 is written only when that readback is 0. The log lines start with try. That stick reached the desktop. The uconfig ring, write pointer 256, stopped at read pointer 0x98 with scratch cafedead. The read pointer could not be forced back to 0, so the write-data ring and the clear-state ring were not submitted. The image built at 19:20 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 20:22 forces GRBM_GFX_CNTL to 0 and sets VMID 0 SH_MEM_CONFIG to 0x1018 and SH_MEM_BASES to 0 before the doorbell, then logs GRBM_STATUS, CP_STAT, CP_INT_STATUS and the VM fault registers at the stall. That stick reached the desktop. SH_MEM read back 0x1018 and scratch stayed cafedead. CP_STAT was 0x80808200: the PFP and the register interface were busy, the ME was not. There was no VM fault and no opcode error. GRBM_STATUS2 0x50000008 is ME0 pipe 1 fifo level 8, CPF_BUSY and CPG_BUSY. RLC_BUSY is bit 24 and ME1 pipe 2 is bit 8, and both were clear. The open RSMU request is the graphics register write. The 22:03 stick reached the desktop. Mode 2 answered 1. The PSP accepted the RLC 2.1 lists, RLC, PFP, CE and ME. Scratch stayed `cafedead` and the read pointer stopped at `0x98`. `RLC_GPM_GENERAL_6` stayed `0x59` while the file version is `0x3c`, and the system aperture was moved from `0x3d0000` to `0x20400`. The image built at 22:41 Europe/Amsterdam on 4 Nov 2026 and written to the General UDisk at 22:46 reached the desktop. `FB_LOCATION` read `0xf400-0xf41f`, the aperture was left at `0x3d0000-0x3d0800`, and `SETUP_TMR` and the MEC body returned status `0x0`. Scratch stayed `cafedead` and the read pointer stopped at `0x98`. `CP_STAT` is `0x80008200`. The image written to the General UDisk at 23:22 Europe/Amsterdam on 4 Nov 2026 turns on the GFXHUB L1 TLB and L2 cache before that ring and submits the 914-dword clear-state stream first. That stick reached the desktop. `L1_TLB` became `0x3d59` and `L2_CNTL` became `0x80603`. Scratch stayed `cafedead` and the read pointer stopped at `0x98`. `CP_STAT` is `0x84808200` and `CP_CPF_STALLED_STAT1` is `0x1`. Memory beyond the BAR, GART, and a fault that does not panic are still open. This is not a Linux DRM dump. Navi does not use the GFX9 ring registers. The first packet executed on the 5625U on 5 Nov 2026: the stick written at 08:11 Europe/Amsterdam, and the same image booted again, both left scratch `0xdeadbeef` with the read pointer at `0x392`. MEC is started before that ring. The ring stays off unless the driver settings say `gfx9_ring true`. Tonga through Vega 20 are not claimed. The IP-discovery row is compiled out. A DCN chip without the boot framebuffer is not claimed. `ECO_BITS` and the type-2 NOP padding are the known differences above.
5. **One Vulkan ICD** (RADV, through the R5 DRM library in `docs/GPU-ARCHITECTURE.md`) whose `vulkaninfo` names that GPU, not Lavapipe. A triangle that stays correct for a while, with the GPU actually busy.
6. **Present.** The same triangle on the panel, vsync noted, fallback to the software bitmap if init fails.
7. **Desktop.** Only after (5) and (6). Measure a window move before talking about Skia or a new `app_server`. If the CPU painter is faster for ordinary controls, leave those on the CPU.

OpenGL apps keep the Mesa 25 / libglvnd path. Experimental ICDs are not required to boot.

## GFX9 ring on GC 9.3 (fixed)

This is the sequence in the code after the test scaffolding came out. The 08:11 boot and the repeat boot are the evidence. A step that differs from an earlier outline is the step the code writes.

1. Mode 2. `smu_v12_0_mode2_reset`. Message `PPSMC_MSG_GfxDeviceDriverReset` (`0x1E`), argument 2, through MP1 `C2PMSG_90`. The mailbox has to answer 1.
2. PSP v12, `GFX_CMD_ID_LOAD_IP_FW`, after `GFX_CMD_ID_SETUP_TMR` (command 5, 4 MiB). Order: CE type 3, PFP type 2, ME type 1, MEC body type 4, MEC jump table type 5, RLC list 22, list 20, list 21, then RLC_G type 8. RLC_G is what starts the RLC. MEC2 is not sent on GC 9.3.
3. RLC stop, clear-state buffer, F32 start. `gfx_v9_0_rlc_stop`, `gfx_v9_0_rlc_resume`. `RLC_CNTL` bit 0 is left set.
4. MEC start. `CP_MEC_CNTL` (`0x008d`) is written 0 and the driver waits 50 µs, the same as `gfx_v9_0_cp_compute_enable(true)` inside `gfx_v9_0_kiq_resume` before `gfx_v9_0_cp_gfx_resume`. Both MEC PSP items have to return status 0. This step stays.
5. `FB_LOCATION` is read and not written. The system aperture is widened only when it does not already cover the framebuffer. `MC_VM_MX_L1_TLB_CNTL` becomes `(read & ~0x00003879) | 0x00003859`. `VM_L2_CNTL` sets bits 0 and 1, clears bit 8 and bit 18, sets bits 19–20 to 1, and clears bits 21–25. `VM_L2_CNTL3` is `0x80130009`. The twelve golden rows are written, and the last is `GCEA_PROBE_MAP` `0x070c` = `0x0000cccc`.
6. The ring is 914 dwords: the clear-state stream, then one `SET_UCONFIG` of scratch `0xDEADBEEF`. Doorbell qword byte `0x458`. Poll slot at the fence page plus 512. Writeback rptr at the fence page plus 256. `CP_RB0_CNTL` is the buffer-size encoding for that ring (4 KiB, value `0x709`). `GRBM_GFX_CNTL` is 0. `SH_MEM_CONFIG` is `0x1018`. `SH_MEM_BASES` is 0. `CP_RB0_VMID` is 0. Max context is 7. Device id is 1.
7. Before the ME is unhalted, the driver reads those registers back. Every mismatch is logged as `GFX9 ring contract:`. A mismatch restores the ring and does not ring the doorbell. `VM_L2_CNTL` is compared with mask `0x03fc0103` and expected `0x00080003`, which is the set of bits step 5 writes. `SH_MEM_BASES` is in that readback as well. The doorbell range upper is written `0x0ffffffc` and is not part of the readback mask.
8. One doorbell of write pointer 914. Success leaves the CP and MEC running.

The ring is off unless `/boot/home/config/settings/kernel/drivers/radeon_hd` contains `gfx9_ring true`. Userland `load_driver_settings("radeon_hd")` opens `B_USER_SETTINGS_DIRECTORY` plus `/kernel/drivers/` plus the driver name (`src/system/libroot/os/driver_settings.cpp`). The image does not ship that file. Tonga through Vega 20 are not claimed. The IP-discovery row is not compiled in. A DCN chip with no boot framebuffer returns `B_ERROR` from `radeon_hd_init`. `device_open` returns that error. `AccelerantHWInterface` then asks for the next graphics device, and once the index passes the published devices it opens VESA or the framebuffer.

Tested: the 08:11 stick, the repeat boot, boot 1 on 5 Nov 2026 with `gfx9_ring true` (`GFX9 ring contract verified`, `fence landed`), and boot 2 the same morning without that file (`GFX9 ring off`, no mode 2, no PSP load). Changing this sequence requires a new boot on the 5625U that lands the fence, recorded in CHANGELOG.md.

## GFX9 workload on GC 9.3 (fixed)

This runs only after the fence above is `0xDEADBEEF`. The first ring, its registers, the PSP load and the firmware blobs stay as they are. The next packets are written at the current write pointer, over the type-2 padding. A submission that passes the end of the 4 KiB ring wraps. The index and the space check are in `GFX9 kernel on GC 9.3 (fixed)`.

The same settings file has to contain `gfx9_workload true`. The image does not ship that file. With the ring on and this key absent, the log is `GFX9 workload off` and the first ring is unchanged. `gfx9_paint` is not a setting. Nothing is drawn into the firmware picture.

Fence page, 4 KiB. The first ring already uses `+0` for the scratch word, `+256` for the read-pointer writeback, and `+512` for the write-pointer poll. The workload adds four slots, 64 bytes apart, each below 4096:

- `+1024`, the indirect-buffer writeback, 32 bit
- `+1088`, the fence sequence, 64 bit
- `+1152`, the start timestamp, 64 bit
- `+1216`, the end timestamp, 64 bit

One submission is 12 dwords:

1. `PACKET3(INDIRECT_BUFFER, 2)`
2. indirect-buffer address, low, aligned down to 4
3. high half, masked to 16 bits
4. `INDIRECT_BUFFER_VALID` plus the indirect-buffer length, vmid 0
5. `PACKET3(RELEASE_MEM, 6)`
6. the four EOP actions, event `0x14`, event index 5
7. `DATA_SEL(1)`, no interrupt
8. sequence address, low
9. sequence address, high
10. sequence, low
11. sequence, high
12. 0

The sequence starts at 1 and increases by one. The CPU writes 0 into `+1088` before each submission. A miss writes `CP_ME_CNTL` with the halt mask and does not restore the stage-1 registers. The scanout stays the firmware picture.

The first indirect buffer is 5 dwords: `WRITE_DATA` to `+1024` with destination select 5, write confirm, and data `0xDEADBEEF`. The CPU writes `0xCAFEDEAD` there first. Success is writeback `deadbeef`, the seen sequence equal to the submitted sequence, and the read pointer equal to the new write pointer `0x39e` (926).

The fill comes after that check. The CPU zeros 8 MiB. The first fill submission writes the GPU counter to `+1152`, one `DMA_DATA` of 1 MiB, then the counter to `+1216`. The second does the same around eight 1 MiB packets, so the whole 8 MiB. `DATA_SEL(3)` is the 64-bit GPU counter from Linux v6.12 `soc15d.h`. One fill packet is 7 dwords: `PACKET3(DMA_DATA, 5)`, `CP_SYNC` with destination select 0, source select 2 and engine 0, the constant `0xA5A5A5A5`, a zero source high half, the destination address, and `CMD_RAW_WAIT` with byte count `0x100000`. The byte count is bits 20:0. `CMD_DAS` is not set. Linux `gfx_v9_0_ring_emit_ib_gfx` does not set `INDIRECT_BUFFER_VALID`. These submissions do.

Tested on the Ryzen 5 5625U, 5 Nov 2026. The stick sha256 `2a5cb8dbc5cfcd52af5d74a6e1149b0f563ba313876ce3ff6a2909786ff6295a`, written at 10:05 Europe/Amsterdam, reached the desktop with `ib_wb deadbeef`, `ib_seq 1 seen 1` and `ib_rptr 0000039e wptr 0000039e`. The stick sha256 `221a4837006941b4f23981db9ef35bb831bb05404a1b70ebb247fba1f252e9a4`, written at 10:59 Europe/Amsterdam, repeated that and then recorded `dma_1m_bad 0 ticks_1m 8470`, `dma_8m_bad 0 ticks_8m 67209` and `dma_ratio 7.9`. A boot with the ring on and `gfx9_workload` absent was not repeated after this fill.

## GFX9 compute on GC 9.3 (fixed)

This is milestone M2. It runs only after the fence and the indirect-buffer check above. The first ring, its registers, the PSP load, the firmware blobs, and the workload packets stay as they are. The new packets are written at the current write pointer. A submission that passes the end of the 4 KiB ring wraps, as in `GFX9 kernel on GC 9.3 (fixed)`.

The same settings file has to contain `gfx9_compute true`. The image does not ship that file. With the ring on and this key absent, the log is `GFX9 compute off`. `gfx9_workload` is independent. This stick does not set it, so the DMA fill does not run. If both are set, the fill runs only after the four dispatches and uses sequences 6 and 7. The workload-only path still uses sequences 2 and 3.

The shader source is `gfx9_compute.s`. `gfx9_compute_shader.h` is generated from that file and is not edited by hand. The recorded command is Ubuntu LLVM 18.1.8: `llvm-mc -triple=amdgcn-amd-amdhsa -mcpu=gfx90c -mattr=-xnack -filetype=obj gfx9_compute.s -o gfx9_compute.o`. `r5_empty` is `s_endpgm` (4 bytes, `0xbf810000`). `r5_fill` is 36 bytes. Both kernel descriptors have `COMPUTE_PGM_RSRC1` `0x000c0000` and `COMPUTE_PGM_RSRC2` `0x00000084` (`USER_SGPR` 2, `TGID_X_EN` 1, `TIDIG_COMP_CNT` 0). The shader address is 256-byte aligned. `COMPUTE_PGM_LO` is that address shifted right by 8.

One compute indirect buffer is 73 dwords. The order is `ACQUIRE_MEM`, `SET_UCONFIG_REG` of `GRBM_GFX_INDEX` to broadcast `0xe0000000` (offset `0x200`), the `SET_SH_REG` block, a GPU-counter `RELEASE_MEM`, `DISPATCH_DIRECT` with `COMPUTE_SHADER_EN`, `EVENT_WRITE` of `CS_PARTIAL_FLUSH` (7, event index 4), and a second GPU-counter `RELEASE_MEM`. `GRBM_GFX_INDEX` is not read over MMIO. The submission is the same 12 dwords as the workload, including `INDIRECT_BUFFER_VALID`.

Sequence 1 is the indirect-buffer check. Sequence 2 is one group of `r5_empty`. Sequence 3 is one group of `r5_fill` (64 threads) into 4 KiB that the CPU filled with `0xCAFEDEAD`. Sequence 4 is 16384 groups (1,048,576 threads, 4 MiB). Sequence 5 is 65536 groups (4,194,304 threads, 16 MiB). Thread `i` stores `i * 3 + 7`. The rest of the 4 KiB buffer stays `0xCAFEDEAD`. Timestamps reuse `+1152` and `+1216`. A miss logs the stage and halts `CP_ME_CNTL`. It does not run a later stage and does not restore the stage-1 registers. The ring use is 914 + 12 + four times 12 = 974 dwords.

Tested on the Ryzen 5 5625U, 5 Nov 2026. The stick sha256 `e978a0c67f23d3d3104c1ad641e55e906373f5d6a53cdf87a334fe0a4f5de410`, written at 12:03 Europe/Amsterdam, reached Tracker with `GFX9 ring contract verified`, scratch `0xdeadbeef`, read pointer `0x392`, `ib_wb deadbeef`, `ib_seq 1 seen 1`, `ib_rptr 0000039e wptr 0000039e`, `compute_c0 ok seq 2 seen 2`, `compute_c1 ok bad 0`, `compute_c2 ok bad 0 ticks 16664`, `compute_c3 ok bad 0 ticks 65426` and `compute_ratio 3.9`. The readback was `pgm f4010030 00000000`, `rsrc 000c0000 00000084`, `threads 64`, `user 01004000 000000f4`, `se0 ffffffff`. There is one boot-loader welcome and no panic. The scanout was not changed. A boot with `gfx9_compute` absent was not run. Other AMD GPUs, VirtualBox, ARM64 and RISC-V were not tested.

## GFX9 kernel on GC 9.3 (fixed)

This is milestone M3. The first ring, the workload packets and the compute packets stay as they are. The same settings file has to contain `gfx9_kernel true` together with `gfx9_ring true`. The image does not ship that file. The PCI id has to be `15e7`, `1636` or `1638`. The kernel maps only the visible pool, with kernel protection and write combining, and the accelerant then logs `GFX9 owned by the kernel` and does not start the command processor. A second open in the same boot logs `GFX9 kernel start already done this boot` and does not start it again. `gfx9_irq` and `gfx9_gart` default to false. With `gfx9_kernel` off, the driver maps nothing extra.

`gfx9_irq true` asks for one MSI vector and enables the Vega10 interrupt ring 0 and `CP_INT_CNTL_RING0`. The K1 packet is the same 12 dwords as the workload, with `INT_SEL(2)`. Success is an end-of-pipe interrupt, client `0x14`, source 181, ring 0.

`gfx9_gart true` places a 256 MiB window immediately under the framebuffer. `MC_VM_AGP_BOT` and `MC_VM_AGP_TOP` are read and not written. Bot 0 and top 0 are the window `0`-`0x1000000`. A GART block that overlaps that window is refused. The page-table base is `(MC_VM_FB_OFFSET << 24) + tableOffset`, then `AMDGPU_PTE_VALID`. The address is rejected when it fails the mask `0xFFFF00000000003F`. A system page is valid, system, snooped, executable, readable and writable, with `MTYPE_UC` (3) at bit 57. After the table is filled, the CPU issues an `sfence` and reads the last mapped PTE back before `VM_CONTEXT0_CNTL` and before the TLB flush. The invalidate request for VMID 0 and flush type 0 is `0x007c0001`.

The DMA packet and the check both use `kK2Pattern` (`0xA5A5A5A5`). The CPU first fills the 1 MiB buffer with `0xCAFEDEAD`. The log is the first four dwords, then the count equal to the pattern, the count still `0xCAFEDEAD`, and the count of anything else. The GART indirect buffer runs only when the submission landed and every dword matches. That buffer is five dwords of `WRITE_DATA` of `0xDEADBEEF`.

The ring is 1024 dwords. The write pointer keeps counting. Dword `i` of a submission is stored at `(wptr + i) & (dwords - 1)`. The doorbell and the poll slot receive that continuing write pointer. Space is `ringDwords - (wptr - rptr) - 1`. The read pointer is the 64-bit writeback at the fence page plus 256 when that word is not zero, otherwise `CP_RB0_RPTR`. The first fill of 914 dwords is not rewritten. The host test checks the twelve indexes from 1022: 1022, 1023, then 0 through 9.

`radeon_hd_uninit` calls `r5_gfx9_kernel_stop` before any `delete_area`. Stop halts the command processor (ME, PFP and CE) and the MEC, writes the saved `VM_CONTEXT0_CNTL` when K2 stored one, issues the same TLB invalidate, clears `IH_RB_CNTL` and `CP_INT_CNTL_RING0`, removes the interrupt handler, turns MSI off, and then deletes the system area and the pool mapping.

These keys stay opt-in. GC 9.3 is not `release-supported`, and the GPU is not the default.

Tested on the Ryzen 5 5625U, 5 Nov 2026. The stick sha256 `7f137a27d566fa2493648b1852e92620baf1c404cad43f8f71da98a1e4e85d47`, written at 15:29 Europe/Amsterdam, reached the desktop. The lines are `k0 ok fence`, `k1 ok msi irq 61 eop 1 client 20 src 181 ring 0`, `k1_wait_us 1132`, `k2 map count 8 pages 256`, `k2 ptb 0000000812c18001 got 12c18001 00000008`, `k2 inv 007c0001`, `k2 ok gart`, `k2 dma a5a5a5a5 a5a5a5a5 a5a5a5a5 a5a5a5a5`, `k2 dma pattern 262144 cafe 0 other 0`, and `k2_ib wb deadbeef seq 10 seen 10`. There is no `k2 fault` line. The user shut that session down and reported no panic. The syslog exported while the desktop was up does not contain the stop lines. A boot with `gfx9_kernel` absent was not run. Other AMD GPUs, VirtualBox, ARM64 and RISC-V were not tested.

## GFX9 user address space (draft)

This section is still a draft. Stick 19 did not draw the teapot without SAM (`pixels MISMATCH`). The register list is what the code programs. The stick lines below are what booted. The K sections above are not part of this draft.

## Render node (draft)

The node is `/dev/gpu/radeon_hd/<n>`, and it is published only when `gfx9_kernel`, `gfx9_irq` and `gfx9_gart` are on. Without those three it does not exist. The interface is API 1.0. The calls, from `R5_GPU_OP_BASE` 30479, are `GET_INFO`, `READ_REG`, `CTX_CREATE`, `CTX_DESTROY`, `BO_CREATE`, `BO_FREE`, `BO_MAP`, `VA_MAP`, `VA_UNMAP`, `CS_SUBMIT`, `FENCE_WAIT`, and the syncobj calls through `SYNCOBJ_TRANSFER`, then `VA_REPLACE`, `VA_CLEAR`, and `SELFTEST` (`R5_GPU_OP_BASE + 23`). `SELFTEST` runs when `gpu_selftest` is true and returns `B_NOT_ALLOWED` otherwise. It does not run on open. A shipped call does not change.

Stick 18 and stick 19 both reached the desktop with this node. On stick 19, `vulkaninfo --summary` exited 0 and named RADV Renoir next to llvmpipe. The SAM teapot submitted indirect buffers `ffff800100284000` (24 dwords) and `ffff800100001000` (752 dwords), `vkQueueWaitIdle` returned 0, and the six sample pixels matched (`pixels ok`, exit 0). The teapot without SAM used the same two buffers, `vkQueueWaitIdle` returned 0, and the samples were `00000000`, `b87214f0`, `00000000`, `00000000`, `00000000`, `00000000` (`pixels MISMATCH`, exit 2).

## VMID 1 (draft)

VMID 1 is one shared address space. The walk is four levels and 48 bits. `PAGE_TABLE_DEPTH` is 3 and `PAGE_TABLE_BLOCK_SIZE` is 0, because `translate_further` is off on GC 9.3. The 6 Nov 2026 Linux capture on this machine logs `vm size is 262144 GB, 4 levels, block size is 9-bit, fragment size is 9-bit`.

| Field | Value | Source |
|---|---|---|
| `virtual_address_offset` | `0x10000` | `AMDGPU_VA_RESERVED_BOTTOM` |
| `virtual_address_max` | `0x800000000000` | `AMDGPU_GMC_HOLE_START` |
| `high_va_offset` | `0xffff800000000000` | `AMDGPU_GMC_HOLE_END` |
| `high_va_max` | `0xffffffffffbfe000` | hole end or the byte size, minus `AMDGPU_VA_RESERVED_TOP` (`0x402000`). v6.18 `amdgpu_vm.h:175` is `AMDGPU_VA_RESERVED_TRAP_SIZE (2ULL << 12)`. The line becomes `(1ULL << 16)` in `31b8de5e55666f26ea7ece5f412b83eab3f56dbb` (`drm/amdgpu: Change AMDGPU_VA_RESERVED_TRAP_SIZE to 64KB`); stable carries it as `4487571ef17a30d274600b3bd6965f497a881299`, which is what v6.18.60 has. R5 keeps the v6.18 value. |

Linux 6.12 `amdgpu_gmc.h` does not reserve the first page of that high range. `gmc_v9_0_get_vm_pde` adds no flags when translate-further is off. The 6 Nov 2026 capture grep does not show whether AMD-Vi was present. A canonical high address and the same address masked with `AMDGPU_GMC_HOLE_MASK` (`0x0000ffffffffffff`) select the same slots. The host walk shows the software slots. It does not show what the hardware did. Adding the filled byte count to a GPU address inside the range does not wrap, and an address inside the hole is rejected.

The root is `VM_CONTEXT1_PAGE_TABLE_BASE_ADDR` (`0x08ed`/`0x08ee`). The end page is `2^36 - 1` (`0x092d`/`0x092e`). `VM_CONTEXT1_CNTL` (`0x0881`) is `0x007ffe07` on the third stick of 6 Nov 2026, and bit 7 (retry) stays off. The identity aperture (`0x0851`–`0x0854`) is empty (`LOW` `0xffffffff`/`0xf`, `HIGH` 0), so an unmapped VMID 1 address faults instead of becoming a system-aperture physical address. `SYSTEM_ACCESS_MODE` stays 3. The TLB flush uses engine 17, the MMIO engine. Engine 0 belongs to the GFX ring. A user job also writes that flush on the ring when the page table changed: pipeline sync, the page-table base as two `WRITE_DATA` packets with control `0x40100000` (`WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM`), one `WAIT_REG_MEM` with control `0x143` (write-wait-write on the PFP, request `0x28a3`, ack `0x28b5`), and `PFP_SYNC_ME`, 26 dwords, with no PASID. The register fields are dword offsets `0x2000 + index` (`0x28ed`, `0x28ee`, `0x28a3`, `0x28b5` for VMID 1, engine 0), not the MMIO byte address. VMID 1 through 15 get `SH_MEM_CONFIG` `0x1018` and `SH_MEM_BASES` `0x20001000`. VMID 0 stays bases 0. Stick 19 logged `vmid1 ptb` and no `vmid1 fail`.

`CP_IB1_BASE_HI` keeps bits 15:0. A user indirect buffer writes the full upper 32 bits, as Linux does. On stick 19 the hung U2d buffer at `0xffff800000000000` read back as `CP_IB1` base `0000800000000000`, size 0, offset 0.

A user GTT page is `MTYPE_NC` and `SNOOPED` unless the radeon_hd setting `gfx9_wc_gtt` is true. That setting is off. `CPU_GTT_USWC` is then accepted and does not change the RAM type or the snoop bit. User VRAM stays `MTYPE_UC` until its own boot. The K2 page stays `MTYPE_UC`. Stick 19 printed `gtt wc area`, `gtt wc clone` and `stage wc area` with `status 0x0`. U2c, a write-combined GTT indirect buffer at `00007fffffffe000`, printed `ok 1 ms`. U2d, the same packet at `0xffff800000000000`, printed `timeout 5000 ms`. The page-table entry was `000000002442f073` (write-combined, no `SNOOPED`) and `fault` stayed 0. Stick 21's U2c, on a fresh write-combined page, hung: the CPU read of that page was zeros before the submit. Stick 22 does not give a RAM area a write-combined type. A new leaf table is filled with `AMDGPU_PTE_EXECUTABLE` (`0x10`). A new directory page is filled with `AMDGPU_PDE_PTE` (bit 54). An unmap writes `0x10`, and a PRT entry is left as it is. Stage U prints the variable MTRRs, then U-self, U2a-n, U2a-f and U2b-n. Group B keeps the indirect buffer at a fresh low address: B1 writes VRAM at `0xffff800000041040`, B2 writes GTT there, B3 writes VRAM at `0xffff800000001040`, B4 copies 128 bytes from GTT at `0xffff800000041000` into a fresh low VRAM page, and B5 copies from VRAM at that address. Group A then fetches a VRAM indirect buffer, with the destination on the next page, in this order: A2 `0xffff800100000000`, A1 `0xffffffffffbfc000`, A3 `0xffff808000000000`, A8 `0x01000000`, A7 `0x00100000`, A4 `0xffff8000c0000000`, A5 `0xffff800040000000`, A6 `0xffff800000200000`. C1 is last: a VRAM indirect buffer at `0xffff800000001000` and the next page. Each of those steps prints the page-table words. A miss continues. A busy command processor at 5 seconds stops the series, which is why C1 is last. There is no write-combined step and no `-lx` step. The `clflush` before a GTT indirect buffer is submitted stays.

Stick 25 maps a second CPU window at `MC_VM_FB_OFFSET << 24` for VMID 1 and later. The K sequence keeps the BAR window. Stage U on that stick is the HDP probe first (eight BAR writes, read back through the carveout, then again after `sfence`), then B1, B2, B3, B5, A2, A1, A3, A8, A7, A4, A5, A6, and C1 last. Boot 1 read every probe word back. The carveout stays. The misses are a stale VMID 1 translation, which the ring flush above is for.

## User job (draft)

A user job is the non-MCBP frame Linux puts around the indirect buffers: two `SWITCH_BUFFER` packets, `CONTEXT_CONTROL`, both `FRAME_CONTROL` packets, the indirect buffers, both `RELEASE_MEM` packets, and a closing `SWITCH_BUFFER`. `CONTEXT_CONTROL` follows Linux v6.12 `gfx_v9_ring_emit_cntxcntl`. `load_enable` (`0x80000000`) is always set. `amdgpu_ib_schedule` sets `AMDGPU_HAVE_CTX_SWITCH` when `ring->current_ctx` differs from the job's fence context. R5 keeps that as the open file plus the `AMDGPU_CTX` id. A switch adds `0x8001`, `0x01000000` and `0x10002`, which is `0x81018003`. The same file and the same id again stay at `0x80000000`. Kernel jobs (`mark`, stage P, the copies) are file 0 and id 0, so the first of those loads and a later one of those does not. Each RADV process is its own file, so each gets the load bits on its first job. `AMDGPU_PREAMBLE_IB_PRESENT` on a switch, or `AMDGPU_PREAMBLE_IB_PRESENT_FIRST` without one, also sets `load_ce_ram` (`0x10000000`). A preamble IB is left out only when there is no switch and it is not that first presentation. The ioctl still rejects every IB flag, including `AMDGPU_IB_FLAG_PREAMBLE`, so RADV's jobs do not take that path. `COND_EXEC` and the pipeline-sync `WAIT_REG_MEM` are not written while every client shares VMID 1. The indirect-buffer packet is opcode `0x3f`. The VMID is bits 24–27.

`CP_IB1_BASE_HI` and `CP_IB2_BASE_HI` keep bits 15:0. The dump sign-extends VA bit 47, so `0x800100001000` is read back as `0xffff800100001000` and matches the length stored at submit. With `gpu_debug`, each user job logs `system_time()`, whether the VM flush prefix was written, the `cntx` dword, the file and context id, and the first eight dwords on the ring. The self-test stage lines carry `system_time()` as well.

The user fence is the kernel MC address of the fence page plus 2048, a 64-bit sequence, `INT_SEL` 0. The interrupt fence is 16 bytes later, 32-bit, `INT_SEL` 2. The fence page is not mapped in VMID 1. Stick 19's SAM teapot reached `vkQueueWaitIdle` 0, so that fence retired. The userspace IB stage after U2d printed `fence timeout` and the watchdog printed `gpu lost watchdog` with `seen` still 37.

`AMDGPU_CTX_OP_QUERY_STATE2` returns 0 while the device is usable. After the watchdog it returns `RESET` and `GUILTY`. The watchdog is 10 seconds. A client timeout does not mark the device lost.

## E2, not applied

Stick 27 run 2 stopped in RADV's `WAIT_REG_MEM` for its own `RELEASE_MEM` (`CACHE_FLUSH_AND_INV_TS_EVENT`). The page at `0xffff800000040000` was still 0. This names the Linux path that write takes, and the VMID-switch work Linux can emit that R5 does not. Nothing in this section is in the ring.

The data dword is written by the command processor. Linux v6.12 `gfx_v9_0_ring_emit_fence` (`gfx_v9_0.c`, from line 5279) builds `PACKET3(PACKET3_RELEASE_MEM, 6)`. The event dword is `CACHE_FLUSH_AND_INV_TS_EVENT` with `EVENT_INDEX(5)` and `EOP_TC_WB_ACTION_EN`. Unless the fence is `AMDGPU_FENCE_FLAG_TC_WB_ONLY`, it also sets `EOP_TCL1_ACTION_EN`, `EOP_TC_ACTION_EN` and `EOP_TC_MD_ACTION_EN`. `DATA_SEL` is 2 for a 64-bit write. That is an end-of-pipe timestamp: the CP writes it through the texture cache and GL2, and the write leaves through the memory client (EA) into the page the packet's VMID translates. The TLB is the GFXHUB TLB of that VMID. These jobs use VMID 1. It is not the VMID 0 TLB and it is not an HDP store. A stale GFXHUB L1 or L2 translation for VMID 1 sends the dword to the previous page.

`amdgpu_ib_schedule` (`amdgpu_ib.c`) calls `amdgpu_vm_flush` before `CONTEXT_CONTROL`. `amdgpu_vm_flush` (`amdgpu_vm.c`, from line 642) returns at once when there is no VM flush, no GDS switch and no pipeline sync. Otherwise, in order:

- `amdgpu_ring_emit_pipeline_sync` when `need_pipe_sync` is true. `amdgpu_vm_need_pipeline_sync` (from line 608) is false for VMID 0. For any other VMID it is true when `job->vm_needs_flush` is set, when the ring has the compute VM bug, when a GDS switch is needed, or when that VMID saw a GPU reset.
- `amdgpu_ring_emit_vm_flush` when `job->vm_needs_flush`, the ring has `emit_vm_flush`, and the page-table address is valid. R5's 26-dword prefix is this GFX9 packet (pipeline sync, the page-table base, the invalidate, `PFP_SYNC_ME`). It is not the rest of this function.
- `amdgpu_gmc_emit_pasid_mapping` when the VMID's PASID mapping is missing or not yet signaled. R5 does not emit this.
- `update_spm_vmid` when `job->spm_update_needed`. R5 does not emit this.
- `emit_gds_switch` (`gfx_v9_0_ring_emit_gds_switch`, from line 4022) when `job->gds_switch_needed`. That writes `GDS_VMID0_BASE`, `GDS_VMID0_SIZE`, `GDS_GWS_VMID0` and `GDS_OA_VMID0` for the VMID. RADV's GFX context can set `gds_switch_needed`. R5 never writes these.
- After a VM flush or a PASID update, `amdgpu_fence_emit` on that flush.
- Two `emit_switch_buffer` packets that `COND_EXEC` must not cover. The body above can sit under `COND_EXEC`. These two cannot. R5's user frame already opens with two `SWITCH_BUFFER` packets. It does not emit this extra pair after the flush.

A cleaner shader can be emitted before that early return, and only when `enable_cleaner_shader`, `emit_cleaner_shader` and `job->enforce_isolation` are all set. R5 does not emit one.

`amdgpu_ib_schedule` also calls `amdgpu_device_flush_hdp` before `emit_cntxcntl`, and `amdgpu_device_invalidate_hdp` after the indirect buffers (`amdgpu_device.c`). On GFX9 the flush function would be `gfx_v9_0_ring_emit_hdp_flush` (from line 5098): a `WAIT_REG_MEM` on the NBIO HDP flush request and done registers, engine 1 (PFP) for the GFX ring, reference `ref_and_mask_cp0`. On x86-64 that function returns before the packet when the device is an APU and not a passthrough. Renoir is that APU, including device `15e7`. `invalidate_hdp` returns on the same condition and does not write a ring packet. So Linux does not put an HDP flush on this machine's GFX ring. The CPU `clflush` of a GTT indirect buffer is a different step. `emit_mem_sync` runs only when the IB has `AMDGPU_IB_FLAG_EMIT_MEM_SYNC`. Stick 27's jobs had flags 0, so that call was not taken.

The GDS switch, the PASID mapping and the SPM VMID update are the VMID-switch packets R5 does not emit. They stay out of the ring until a later stick copies one Linux function.

## Device info (draft)

`GET_INFO` fills the vendor block. `num_rb_pipes` and `enabled_rb_pipes_mask` come from `CC_RB_BACKEND_DISABLE` and `GC_USER_RB_BACKEND_DISABLE`. Stick 19 logged `rb num 2 mask 3` on every read of that pair. One shader engine means `GRBM_GFX_INDEX` is not written. The GC 9.3 chip row (shader engines 1, arrays 1, 8 CUs per array, 2 render backends) is the cross-check. When the carveout is mapped, discovery at the end of that window replaces a field that disagrees and the log says `passport mismatch`. Clocks, `gpu_counter_freq`, and the memory type and width come from the ATOM tables. A missing table leaves those fields 0. Firmware versions still come from the ucode headers. `gb_addr_config` is the `READ_MMR_REG` of `0x063e`. This has not been booted on the 5625U.

## Submission (draft)

`CS_SUBMIT` takes one context, one ring, and at most eight indirect buffers. Stick 19's two teapots both submitted the 24-dword and 752-dword buffers named above. The `clflush` of a GTT indirect buffer at submit is still there. It does not cover a chained buffer. It stays until a stick shows the write-combined mapping is enough.

## Fence by interrupt (draft)

`FENCE_WAIT` waits on the sequence `CS_SUBMIT` returned. The K1 interrupt in `GFX9 kernel on GC 9.3 (fixed)` is a different fence and stays as it is. Stick 19 has no source 184. The SAM teapot's wait returned. The wait after the U2d hang did not: `seen` stayed 37 and the watchdog fired.

## Known GFX9 differences

`MC_VM_MX_L1_TLB_CNTL` keeps `ECO_BITS`. Linux writes 0 in those bits. The boot that landed the fence left the register at `0x3d59`, so those bits stay.

The ring is padded with `kType2Nop`. Linux pads with `PACKET3(NOP, 0x3FFF)`, which is `0xFFFF1000`, and aligns the ring to 256 dwords. Later packets overwrite that padding. A submission that crosses 1024 uses the wrap in `GFX9 kernel on GC 9.3 (fixed)`. The 15:29 boot ran that submission: `k2_ib` sequence 10 was seen.

## GFXHUB setup compared with Linux

`gfxhub_v1_0_gart_enable` in Linux v6.12. R5 already writes the L1 TLB, the L2 cache controls, the system aperture (high address plus one, which is the Renoir and Green Sardine workaround), and the identity aperture low and high. Those stay. `gfx9_hub_linux` defaults off and adds only the rows marked write.

| Register | Linux | R5 with the setting off | `gfx9_hub_linux` |
|---|---|---|---|
| `MC_VM_MX_L1_TLB_CNTL` | enable, access mode 3, advanced model, unmapped 0, MTYPE UC, ATC | same fields (`0x3859` after the clear of `0x3879`) | no extra write |
| `VM_L2_CNTL`, `CNTL2`, `CNTL3`, `CNTL4` | cache on, invalidate L1 and L2, bank 9, bigk 6, physical taps off | same fields in `radeon_gfx9_prepare_hw` | no extra write |
| `MC_VM_AGP_BASE` | 0 | not written | write 0 |
| `MC_VM_AGP_BOT`, `MC_VM_AGP_TOP` | `agp_start >> 24`, `agp_end >> 24` from `amdgpu_gmc_agp_location` | read only | not written. That function is not ported |
| system aperture low, high, default | from `fb_start`, `fb_end` and the scratch page. High is `fb_end >> 18` plus 1 on Renoir and Green Sardine | already done in `radeon_gfx9_prepare_hw` | no extra write |
| `VM_L2_PROTECTION_FAULT_DEFAULT_ADDR` | dummy page | K2 dummy page | no extra write |
| `VM_L2_PROTECTION_FAULT_CNTL2` retry bit | 1 | not written | set bit 0x12 |
| identity physical offset low and high | 0 | not written | write 0 |
| `VM_CONTEXT0_CNTL` | `enable_system_domain` | K2. Fixed contract | print only, behind `gpu_debug` |

`gpu_debug` prints the L1, L2, aperture, AGP, identity, fault and VMID 0 registers whether or not `gfx9_hub_linux` is on.

## COND_EXEC and the DE meta

`amdgpu_ib_schedule` calls `init_cond_exec` when the ring has that function (`amdgpu_ib.c` around the call before the context control). The GFX ring does (`gfx_v9_0_ring_funcs_gfx`). `gfx_v9_0_ring_emit_init_cond_exec` writes `PACKET3_COND_EXEC` (`0x22`) and a dword count that `amdgpu_ring_patch_cond_exec` fills in. The CP skips those dwords only when the dword at `cond_exe_gpu_addr` is 0. Linux keeps that dword at 1, so the packet does not skip the frame. `gfx9_cond_exec` does the same and defaults off. The dword is fence-page offset 32.

`gfx_v9_0_ring_emit_de_meta` runs only from `gfx_v9_0_ring_emit_ib_gfx` when the IB has `AMDGPU_IB_FLAG_PREEMPT` (`gfx_v9_0.c` at that `if`). R5 still rejects every IB flag, so the flag is 0 and the meta is not emitted. The packet writes `struct v9_de_ib_state` to the CSA (`amdgpu_csa_vaddr`). That struct and the CSA mapping are not copied while the flag cannot be set. This is the stop in night step N5.

## GDS switch

`gfx_v9_0_ring_emit_gds_switch` writes `GDS_VMIDn_BASE`, `SIZE`, `GWS` and `OA` with four `WRITE_DATA` packets. `amdgpu_vmid_gds_switch_needed` is true when those six values differ, and `amdgpu_vm_flush` also forces it after a GPU reset. `gfx9_gds_switch` defaults off. The first job after start is that forced write. Later jobs with the same values do not write again.

The R5 command-submit ioctl has no GDS chunk, so the six values are 0. That is the GFX9 RADV job that does not ask for GDS. A chunk in the ioctl is what it would take to pass other sizes through.

## PASID

`amdgpu_pasid_alloc` in `amdgpu_ids.c` hands out an id from the top of the requested width first. Each open of the render node takes one. The bitmap here is 16 bits wide. The function accepts up to 31; a wider bitmap is not built. `gmc_v9_0_emit_pasid_mapping` writes `mmIH_VMID_0_LUT` plus the VMID through `gfx_v9_0_ring_emit_wreg` (`WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM`) when that VMID's PASID changes. The 26-dword VM flush does not include it. A k2 fault line prints the PASID and the open-file pointer stored for that VMID.

## VMID per open file

`r5_vmid.h` is `amdgpu_vmid_grab` for VMIDs 1–15: an idle VMID, the same owner while its fence has not retired, otherwise the least recently used idle VMID, and a flush when the owner or the recorded TLB sequence changes. Fifteen busy VMIDs make the next grab return none. VMID 0 is not in the table.

`gfx9_vmid_per_file` defaults off. Off keeps the one root from `r5_gfx9_vmid1_setup` and submits on VMID 1. On, the first call for an open file builds a root with the same page allocator, and a submit grabs a VMID with `r5_vmid_reserve`. The existing VM flush writes that file's root into the grabbed VMID, then writes the shared root back so a later VMID 1 job is unchanged. The debug switch still refuses the bit: the root is created from the boot setting, not from a flip while files are open. Page-table pages are not freed until the next boot. A grabbed VMID above 1 still needs `gfx9_vmids` for its context bounds. The host test maps one address in two trees and reads two pages. It has not been booted.

## Deferred free

`gfx9_deferred_free` defaults off. When it is on, a freed VRAM offset stays out of the allocator until the next user-job VM flush has been submitted and that fence has been seen. The retire thread prints the offset when it comes back. An unmap still clears the page-table entry at once. The list holds 256 offsets, one per buffer object. A full list frees that offset immediately.

## REVIEW-39 section 2

| # | Linux | R5 | Setting | Commit |
|---|---|---|---|---|
| H1 | `gfxhub_v1_0_program_invalidation` | read of engines 0 and 17 always; write of all 18 engines after VMID setup | `gfx9_inv_range` on unless the line says false | `b5c0df3d66` |
| H2 | `init_tlb_regs` | the value already matches the field set | none | `2a619add46` names |
| H3 | `init_cache_regs` | already written | none | `2a619add46` names |
| H4 | `init_system_aperture_regs` | AGP base 0, identity offsets 0, migration retry. AGP bot and top are not written | `gfx9_hub_linux` off | `6226ff8f85` |
| H5 | `enable_system_domain` | VMID 0 stays the K2 contract | none | unchanged |
| H6 | `disable_identity_aperture` | the physical offsets are the hub setting | `gfx9_hub_linux` off | `6226ff8f85` |
| H7 | `setup_vmid_config` | VMIDs 1–15 | `gfx9_vmids` off | `06480f993f` |
| J1 | pipeline sync | unchanged | none | — |
| J2 | `emit_vm_flush` | 26 dwords, unchanged | none | — |
| J3 | fence after the VM flush | `RELEASE_MEM` then the job takes the next sequence | `gfx9_flush_fence` off | `98b0f8dd21` |
| J4 | two `SWITCH_BUFFER` after that fence | they move with J3 | `gfx9_flush_fence` off | `98b0f8dd21` |
| J5 | `COND_EXEC` and the DE meta | `COND_EXEC` only. The meta needs `AMDGPU_IB_FLAG_PREEMPT` and the CSA | `gfx9_cond_exec` off | `0dd5c153b6` |
| J6 | HDP flush | no packet on this APU, same as Linux | none | — |
| J7 | `dma_fence_context_alloc` | a counter that only goes up. 0 is the kernel | always | `c7304b3ae0` |
| J8 | frame, user fence, ring fence | unchanged when the new settings are off | none | — |
| J9 | `emit_gds_switch` | four `WRITE_DATA`s, once, sizes 0 | `gfx9_gds_switch` off | `cbee68e33d` |
| J10 | `emit_mem_sync` | IB flags stay rejected | none | — |
| J11 | `emit_pasid_mapping` | one PASID per open, LUT write when it changes | always | `040026c7c9` |
| P1 | `amdgpu_vmid_grab` | the allocator is tested and not called | not read | `52a1f44b64` |
| P2 | root and PASID per file | PASID is J11. The root is still one | not read | `040026c7c9`, `52a1f44b64` |
| P3 | `amdgpu_vm_clear_freed` | VRAM offset waits for the next flush fence | `gfx9_deferred_free` off | `64f47ab138` |

## GFXHUB invalidation range (fixed)

`gfxhub_v1_0_program_invalidation` writes 18 engines. The addresses are `mmVM_INVALIDATE_ENG0_ADDR_RANGE_LO32` (`0x8c7`) and `mmVM_INVALIDATE_ENG0_ADDR_RANGE_HI32` (`0x8c8`) in `r5_gc9_regs.h`, plus `engine * R5_GC9_ENG_ADDR_DISTANCE`. The stored values are `0xffffffff` and `0x1f`. The write is `r5_gfx9_inv_range_program`, called from `r5_gfx9_vmid1_setup` after the VMID setup. `gfx9_inv_range` defaults on. `gfx9_inv_range false` leaves the registers as they were.

Stick 29, `generated/boot-5625u-29/syslog`, is the evidence:

- At K2 the range covered no addresses: line 2321 `inv range k2 eng 0 lo 00000000 hi 00000000`, line 2322 the same for engine 17.
- With the setting off, R1a and R1b wrote the second value onto the old GTT page: line 3268 `r1a gtt 22222222 vram 00000000`, line 3275 `r1b gtt 22222222 vram 00000000`.
- The run-time switch then wrote the range: line 3277 `inv range wrote eng 0 lo ffffffff hi 0000001f`, line 3278 the same for engine 17.
- With that range on, the old page kept `11111111` and the new page received `22222222`: line 3286 for R1a, line 3293 for R1b. R1c's destination was `44444444` (line 3731).
- Each new process took the next fence context, 2 through 8, with the load bits `81018003`: lines 3396, 3465, 3527, 3590, 3660, 3722 and 3951.
- The log has no `vm fault` and no `gpu lost`.

**Limit.** Stick 29 wrote the range at run time, after stage P. The boot position, before the first user job, has not been booted. The next GPU stick confirms it. That stick also tries a USB 3 stick on an HP machine, plugged in and out, to see whether the mouse and keyboard keep working.

Built, and not needed for M4, still default off: `gfx9_hub_linux`, `gfx9_vmids`, `gfx9_flush_fence`, `gfx9_cond_exec`, `gfx9_pasid_map`, `gfx9_gds_switch`, `gfx9_ce_meta`, `gfx9_deferred_free`.

## Self-test switch

`R5_GPU_DEBUG_SWITCH` is accepted only while `gpu_debug` is on. It flushes VMID 1 and waits until the newest user fence has been seen, then sets the bits for `gfx9_inv_range`, `gfx9_hub_linux`, `gfx9_vmids`, `gfx9_flush_fence`, `gfx9_cond_exec`, `gfx9_gds_switch` and `gfx9_deferred_free`. `gfx9_vmid_per_file` is still refused by this switch. The boot setting of that name is separate and defaults off. The line carries the time and the bits.

R1a and R1b run once after stage P. R1c runs after the teapots. The result file prints pass when the old page is `11111111` and the new page is `22222222`.

## vulkaninfo

The driver watchdog is 10 seconds (`kWatchdogUs`), the same value as `amdgpu.lockup_timeout`. No other R5 wait of about 10 seconds was found on the vulkaninfo path. Whether the pause is that watchdog or the Vulkan loader is not known until a QEMU run shows the stack. QEMU has no Renoir, so it does not run the GFX ring.

## Fallback

No supported GPU: VESA, `framebuffer`, or `vboxvideo`, software `app_server`, optional Lavapipe. A GPU reset or a failed accelerant must not take down Login. The standard image boots with the experimental GPU driver absent.

## Tests that count

| Claim | Evidence |
|---|---|
| Desktop works | Boot, Tracker, a window. True in VirtualBox. |
| Software OpenGL | GLTeapot drew on the VirtualBox daily (2026-11-01). A QEMU probe named llvmpipe and Mesa 25.3.6. |
| Panel driver | 5625U syslog names the PCI ID and a mode. Not done. |
| Software Vulkan | User ran `vulkaninfo --summary` on the 21:36 VirtualBox image. Device type CPU, name `llvmpipe (LLVM 21.1.8, 256 bits)`, driver info `Mesa 25.3.6 (LLVM 21.1.8)`. |
| GPU Vulkan | Device string is the Vega/Cezanne GPU, and a workload moves GPU counters or an equivalent busy signal. Not done. |
| Accelerated desktop | A measured `app_server` path submits that GPU. Not done. |

## Reporting

Say software or GPU. Name the PCI ID, the ICD, and the machine. VirtualBox is never the 5625U result.
