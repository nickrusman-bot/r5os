# R5 roadmap

Product direction for R5 Operating System (R5). This is not a license to implement everything in one patch. Agents: follow `AGENTS.md`, and pick work from the current user request.

R5 modernizes a Haiku-based desktop without waiting on upstream schedules. Compatibility with existing Haiku applications stays a design goal (`AGENTS.md`, "Compatibility").

## Where R5 stands (November 2026)

Tree `2026.1-dev`, build 10047. There is no public release yet. The details, build by build, are in `distro/WHATSNEW` (user language) and `CHANGELOG.md` (technical).

| Area | In the image now |
|---|---|
| Desktop | R5 branding, dark mode, the Settings window (`docs/SETTINGS.md`), notifications with a placement choice |
| Accounts | Sign in, Log out, Lock screen, Users & Accounts, private home folders, the administrator off uid 0 after the first Sign in, locked machine settings (`docs/MULTIUSER.md`) |
| Network | Wi-Fi join and remember, IPv6 (link-local, SLAAC, DAD, DHCPv6), AirPrint/IPP, AirPlay 1 to speakers, the network tray |
| Sound and MIDI | Better resampling, a latency choice, USB channel layouts, HDMI output names, virtual surround, MIDI 2.0 (`docs/MIDI2.md`) |
| Graphics | Software OpenGL (llvmpipe) and Vulkan (Lavapipe). On the Ryzen 5 5625U: the kernel owns the GPU and runs the command ring, indirect buffers, DMA, compute, an interrupt fence and a GART write, opt-in, with no rendering yet (`docs/GRAPHICS.md`, milestones M0–M3 in `docs/GPU-ARCHITECTURE.md`) |
| Virtual machines | VirtualBox 7.2.22 Guest Additions (`docs/VBOX-GUEST.md`) |
| Licensing | Multi-license policy, notices, and licenses in About (`docs/LICENSING.md`) |

Started is not done. A feature counts as done only after a test on the hardware or the network it is meant for (`AGENTS.md`, "Tracker and changelog" and "Networking claims").

## Near term (user-visible)

The order is impact, not ticket number.

1. **GPU on the reference machine.** Following `docs/GPU-ARCHITECTURE.md`:
   - **M3 is done:** the kernel driver owns the GPU, with interrupts and GART. The keys stay opt-in.
   - **Next, M4:** the R5 GPU interface, the R5 DRM library, and RADV visible in `vulkaninfo`.
   - **The goal:** when a supported GPU passes its checks, R5 renders on the GPU by default; otherwise in software. Haiku applications stay unchanged.
   - Display (DCN modesetting, more than one screen, vsync) is a separate track. Today the picture is still the firmware one.
2. **Finish multi-user** ([#63](https://github.com/nickrusman-bot/r5os/issues/63)).
   - The lock screen is done ([#64](https://github.com/nickrusman-bot/r5os/issues/64), build 10013).
   - Open: the keyboard shortcut for the lock screen ([#72](https://github.com/nickrusman-bot/r5os/issues/72)), and guest plus Public/Shared ([#65](https://github.com/nickrusman-bot/r5os/issues/65)).
   - Still one graphical session at a time. Do not skip isolation, and do not treat a list of names as "done".
3. **Other hardware on the Ryzen 5 5625U:** Wi-Fi on the laptop's own card, backlight, sleep and resume, and HiDPI without breaking the layout. None of these has been shown on the metal yet.
4. **Browser:** a current WebKit or Qt browser in the image. WebPositive is the weak daily application.
5. **Network, what is left:**
   1. IPv6: more LAN tests. DHCPv6 IA_NA still needs a LAN with a DHCPv6 server.
   2. DNS that uses the DHCP servers and still resolves `.local`.
   3. Captive portal.
   4. SMB in Tracker.
   5. A simple firewall, with incoming connections closed by default.
   6. WireGuard.
   7. LAN tests: VirtualBox bridged (`docs/vbox.md`), or QEMU bridged/tap. QEMU usernet is not a LAN.
6. **Dark mode in the remaining system applications** and Deskbar (Haiku #18970). The dark color scheme exists; some applications still keep their own look.
7. **Polish:** screenshots, emoji and fonts, the image clipboard. Rounder windows and Deskbar: `docs/LOOK.md`, not while multi-user is still open.

Zeroconf, AirPrint, AirPlay 1 and the network tray exist. They are not "done" until LAN tests exist.

## Named products (later)

Do not start these during an unrelated bugfix.

| Name | Intent | Status |
|---|---|---|
| **R5 Hub** | One place for settings: devices, display, audio, network, printers, updates | Started: the Settings window brings the main pages together (`docs/SETTINGS.md`) |
| **R5 Quicklaunch** | Keyboard launch, and search for applications, files and system items | Not started |
| **R5 Connect** | Device discovery (AirPrint, AirPlay, later SMB and printers) | The building blocks exist (zeroconf, AirPrint, AirPlay 1); no product yet |
| **R5 Store** | Installing software from Haiku packages | Not started |
| **R5 SafeUpdate** | Validated updates and recovery *before* disruptive changes | Not started |
| **R5 Doctor** | Logs, crashes, drivers, common failures | Not started |

## Graphics and media

- **GPU.** The plan is `docs/GPU-ARCHITECTURE.md`, and the measured state is `docs/GRAPHICS.md`. The software path always stays.
  - Do not destabilize `app_server`.
  - Do not call VESA, `vboxvideo`, Lavapipe, or GPU work without a rendering path "hardware acceleration".
  - The GFX9 sequences on the 5625U are fixed (`AGENTS.md`, "GFX9 ring (Renoir)").
- **Graphics Kit (after M7).** A C++ kit on top of Vulkan, in the style of the BeOS kits. Decided on 6 Nov 2026, not started:
  - **Names.** The kit is the **Graphics Kit**, named after the domain, like the Interface Kit and the Media Kit. The classes live in the namespace **`BGraphicsKit`**, the way Haiku does it for `BPackageKit`. Inside that namespace they carry ordinary B names: `BVulkanView` (in the line of `BGLView`), `BGPUDevice`, `BGPUBuffer`, `BGPUTask`. There is one header per class without the B (`<VulkanView.h>`), plus `<GraphicsKit.h>`. Constants are `B_GPU_…`, errors are `status_t`, and hooks follow the usual pattern (`Render()`, `FrameResized()`, `DeviceLost()`). Check every new name against the Haiku tree. The documentation says once that the Interface Kit draws through app_server, and that the Graphics Kit is the layer for drawing and computing on the GPU, with Lavapipe as the fallback.
  - **The BeOS way.**
    - A finished task sends a `BMessage` to the `BLooper`, instead of the program waiting on a fence.
    - A view draws in its own thread.
    - Without a GPU, the same code runs on Lavapipe.
    - A lost device gives `B_GPU_DEVICE_LOST`, and the kit rebuilds the device.
    - The kit does not lock Vulkan away: the `VkDevice` and `VkImage` underneath stay reachable.
  - **Every GPU.** The kit works the same on every GPU that has a Vulkan driver: AMD (RADV), Intel (ANV) and NVIDIA (NVK), and on Lavapipe without a GPU.
    - The kit contains no vendor code and no vendor checks.
    - A program never needs to know which vendor it runs on. `BGPUDevice` reports the name and the capabilities, and nothing more.
    - Vendor differences belong in the R5 GPU interface (the vendor block) and in the Mesa drivers, never in the kit.
  - **Where.** A separate library `libr5graphics.so` next to `libbe`, not inside `libbe` (`AGENTS.md`, LibR5). It is MIT, like the GPU header, because every program that uses it links it.
  - **Who it is for.** Three groups, in one kit:
    - games and 3D through `BVulkanView`;
    - simple GPU computing through `BGPUTask` and `BGPUBuffer`;
    - R5's own components, which use the same parts as they appear.
  - **How thin.**
    - `BVulkanView` is a thin shell, as `BGLView` was. The kit handles the instance, the device, the swapchain, resizing, the drawing thread and a lost device. The program writes plain Vulkan in `Render()`.
    - `BGPUTask` and `BGPUBuffer` are thicker. A program computes without writing Vulkan, and can still reach the `VkDevice` underneath.
    - No API of its own that hides Vulkan.
  - **OpenGL.** Not in the kit. `BGLView` already covers it, and moves to the GPU by itself with Zink (M9). A buffer bridge between the two can come later.
  - **R5 first, portable by design.**
    - The kit uses only Vulkan, the public Haiku API, and a small internal layer for what is R5-specific: device choice and the M7 presentation.
    - It depends on no other R5 library unless that is really needed.
    - It is not tested on stock Haiku for now.
    - Shipping it as a package for Haiku is a separate decision later. Haiku itself does not accept AI-assisted code (`AGENTS.md`).
  - **Order.**
    1. `BGPUTask` and `BGPUBuffer` may come after M6, because they need no window.
    2. `BVulkanView` comes after M7, with VulkanTeapot rewritten on top of it as the first example.
    3. Integration with the Media Kit comes after that.
  - **Before M7:** build nothing for `BVulkanView`, but design the M7 presentation (the R5 WSI layer) so that `BVulkanView` can sit on top of it.
- **Audio and MIDI.** Improve streaming and device interop, in small steps. The contracts are `docs/USB-AUDIO.md`, `docs/HDMI-AUDIO.md`, `docs/VIRTUAL-SURROUND.md` and `docs/MIDI2.md`.
- **Mixxx** is a side quest ([#55](https://github.com/nickrusman-bot/r5os/issues/55), `docs/MIXXX.md`). It is not an image application, and not while multi-user is open.

## Architectures

| Target | Status | Next |
|---|---|---|
| AMD64 | Boots to the desktop in QEMU, in VirtualBox, and from USB on the Ryzen 5 5625U. Not release-supported | Hardware items above |
| ARM64 | No work yet. Basis decided (7 Nov 2026): the haiku-pi port, see below | After AMD64 is usable on hardware: QEMU, then the Raspberry Pi 4 as the first board |
| RISC-V 64 | No work yet | After AMD64 and ARM64 |

Levels: buildable → bootable → functional → desktop-capable → release-supported. See `AGENTS.md`, "Architectures".

### ARM64 from haiku-pi

[haiku-pi](https://github.com/haiku-pi/haiku-rpi) ports Haiku to the Raspberry Pi 4 (BCM2711, Cortex-A72). Its Haiku sources are the fork [haiku-pi/haiku](https://github.com/haiku-pi/haiku), branch `rpi4`. That port reaches the Haiku desktop on the Pi, with drivers for the board's hardware. R5 takes that port as the basis of its ARM64 target, instead of starting from Haiku's own arm64 tree.

- **What R5 takes:** the arm64 boot path (the EFI loader under U-Boot, the EL2 to EL1 drop, secondary CPU start-up), the non-coherent DMA handling, the interrupt controller (GICv2), device-tree address translation, and the drivers for the Pi 4's on-board hardware.
- **Reference only:** the V3D GPU driver and how it draws Vulkan into ordinary Haiku windows. R5 keeps its own vendor-neutral GPU interface (`docs/GPU-ARCHITECTURE.md`). The V3D work is input for the M7 presentation and the Graphics Kit.
- **Not taken:** the packages (Firefox, Wine, games). They belong in a package repository, not in this source tree.
- **When:** after AMD64 is usable on hardware, the same rule as above. First QEMU, then the Pi 4.

### Import rules for haiku-pi

The same rules apply to both haiku-pi tracks (ARM64 above, the x86-64 fixes under "Upstream"):
- **Per commit,** never one large merge. haiku-pi is based on a different Haiku revision (hrev60314 on 5 Nov 2026), and R5 checks each change against its own overlays and APIs, as for `upstream`.
- **Native source in `distro/`,** as `AGENTS.md` describes. The haiku-pi file header stays exactly as it is, and R5 changes on top are marked with SPDX snippets.
- **Licence.** The new files in `haiku-pi/haiku` carry an MIT header. Third-party code inside them keeps its own licence (OpenBSD's bwfm driver, ISC; Mesa's DRM headers, MIT; Arm Optimized Routines, MIT OR Apache-2.0 WITH LLVM-exception). Each import gets a line in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md`. The coordination repository `haiku-pi/haiku-rpi` (scripts and documents) has no licence file, so nothing is copied from it unless its owner agrees.
- **Tested before it lands.** haiku-pi's code is also written by an AI agent. Each import is read against the source, built, and booted (QEMU, plus the 5625U for x86-64 or the Pi 4 for ARM64).

## Platform (CPU, power and firmware)

What the kernel knows and does about the processor and the firmware. The audit of the current state is `docs/KERNEL.md`. Each item starts only when there is hardware to test it on.

| Item | Where R5 stands | What it takes | Test hardware |
|---|---|---|---|
| **P-states (CPU frequency)** | Haiku has `amd_pstates` and `intel_pstates` (cpufreq) and two C-state drivers. Whether they load and change the frequency on the 5625U and the X1 Carbon has not been checked. R5's `r5_pstate` only reads the state; it does not write one | First measure what the existing drivers do on both machines. Then the modern paths: AMD CPPC (as amd-pstate) and Intel HWP, where the CPU chooses its own frequency within limits the OS sets. A power profile (balanced, performance, saver) on top | Ryzen 5 5625U, X1 Carbon Gen 8 |
| **Sleep and resume** | Open (see "Near term", item 3) | Many current laptops have no classic S3 any more, only S0ix ("Modern Standby"). Find out which the two machines offer, then make that path work, including the GPU, Wi-Fi and USB coming back | Ryzen 5 5625U, X1 Carbon Gen 8 |
| **Battery, thermal, backlight, embedded controller** | Partly through Haiku's ACPI drivers; not checked on the two machines | The ordinary laptop functions, read from ACPI: charge level, temperatures and fan, brightness keys | Ryzen 5 5625U, X1 Carbon Gen 8 |
| **CPU topology (ACPI PPTT)** | The kernel reads SMT and core levels from CPUID (`docs/KERNEL.md`) | Read which cores share a cache or a cluster, and let the scheduler use that | Any current CPU |
| **Hybrid cores (P and E)** | `get_hybrid_cpu_type()` reads the core type; the scheduler does not use it (`docs/KERNEL.md`, item 7) | Heavy threads on performance cores, background work on efficient cores. Later Intel Thread Director (HFI). AMD has "preferred cores" through CPPC and dense Zen 5c cores | A 12th-generation Intel or newer. The X1 Carbon Gen 8 has no E-cores |
| **More than 64 logical CPUs** | `SMP_MAX_CPUS` is 64 on both AMD64 boot platforms (`docs/KERNEL.md`, finding 2). The BIOS loader cannot start x2APIC processors (finding 3) | Larger CPU sets and per-CPU tables in the kernel, x2APIC on every boot path, and a check of the public API for structs with a fixed CPU count | QEMU with many virtual CPUs first, then a large Ryzen, Threadripper or EPYC |
| **NUMA (ACPI SRAT and SLIT)** | Not present | Keep a program and its memory on the same node, in the allocator and the scheduler. Only useful on machines with more than one memory node | Multi-socket or Threadripper/EPYC hardware |

The order follows what R5 is today, a laptop and desktop system: first P-states, sleep and the laptop functions, then topology, then hybrid cores, and only for workstation or server use more than 64 CPUs and NUMA.

## Upstream

Fetch `upstream` when a Haiku fix is useful. Inspect the `distro/` changes and R5 behavior before a merge. Record imports, local patches, and intentional differences in `CHANGELOG.md`. Do not send changes back to Haiku (`AGENTS.md`).

### x86-64 fixes from haiku-pi

About 120 haiku-pi commits change code that x86-64 also builds and runs: the kernel, the VM, the runtime loader, libroot, BFS, the block cache, the kits and the servers. Their docs (`docs/HAIKU-RPI4-CHANGES.md`, the rows marked ★) list them as fixes that are not Pi-specific. haiku-pi built and tested that tree for x86_64 under KVM on 4 Nov 2026, and reports that 14 tests that fail on upstream Haiku pass with those fixes. Examples are a kernel panic on `read()` into a guard page, `MAP_SHARED` after `mprotect` plus `fork`, an `rw_lock` race, the kill race, TLS alignment, and BFS metadata writes.

- **Decided on 7 Nov 2026:** R5 takes these fixes. They help AMD64 directly, on the 5625U and in QEMU.
- **When:** after GPU milestone M5. It is a separate track, not an M milestone.
- **How:** the import rules above. Start with the ★ fixes that have a test, and use that test as the proof on R5. A fix that conflicts with an R5 change in `distro/` is reviewed by hand, not overwritten.
