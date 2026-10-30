# VirtualBox guest integration

Architecture and order of work. How to boot the current VM stays in `docs/vbox.md`. Native GPU and Vulkan are separate from this guest path. A virtual display is not a physical GPU driver.

Nothing in the product image or the git tree implements Guest Additions. The COM1 clipboard and the VM script settings below are the shipped path. Kernel modules stay out of git and out of the daily image until the GPL-3.0 reach into the MIT kernel is reviewed.

A throwaway boot on 2026-10-30 12:23 Europe/Amsterdam used a copy of the already-booted VDI with `vboxguest`, `vboxdev`, `VBoxService`, `VBoxTray`, and `VBoxControl` under `non-packaged` and `/boot/home/vbox`. The normal VDI was attached again afterwards. The kernel serial log showed PCI `80ee:cafe` (VirtualBox Guest Service) and `80ee:beef`. It continued through packagefs and the framebuffer console, then stopped at `slab memory manager: created area`. No `vboxguest` line, no `/dev/misc/vboxguest`, and `/boot/home/vbox/test.log` was never created. Syslog on that disk was still the 09:56–09:58 Europe/Amsterdam boot. The Windows clipboard never received the guest test string. This boot did not reach the driver or the clipboard test. The same stall happens on this VM without the additions. A cold start can leave the boot screen dark for hours (one start was left for 9 hours). A reset usually lights the icons. On 2026-10-30 12:42 Europe/Amsterdam a reset lit them through the rocket, the last icon, and the screen stayed there. A second reset was started to see if it would pass that icon; the VM was then powered off. That icon is the kernel stage set immediately before `launch_daemon`. The serial log never printed `vboxguest`, `launch_daemon`, or a panic. The additions are not shown to have caused this stop.

Later the same day, after the desktop path worked, a separate throwaway (`generated/r5-os-vboxid.vdi`, not the daily VDI and not `r5-os-loginfix.vdi`) carried only `generic/vboxguest`, `drivers/dev/misc/vboxguest`, `VBoxService`, and the 7.2.22 `COPYING`. The first start of that disk (serial `generated/vbox-vboxsvc.log`) reached `package_daemon` and then refused the driver: `could not resolve symbol 'VBoxGuest_RTLogLoggerEx'`. Haiku resolves those imports before `init_hardware`, and the symbol lived only in the not-yet-loaded `vboxguest` module. A no-op `VBoxGuest_RTLogLoggerEx` was added in the VirtualBox tree outside git (`VBoxGuest-haiku-stubs.c`) and `vboxdev` was relinked. `vboxsf`, `vboxvideo`, the mouse filter, and the accelerant were relinked by the same file and were not copied onto the image. Clipboard mode stayed disabled. UART1 was a file, not the COM1 pipe.

The next start, about 15:27 Europe/Amsterdam (serial `generated/vbox-identify.log`), listed PCI `80ee:beef` (display) and `80ee:cafe` (guest service) before any driver line. It then printed `loaded driver /boot/system/non-packaged/add-ons/kernel/drivers/dev/misc/vboxguest`. That line is printed only after `init_hardware` returns success, and `init_hardware` is `get_module("generic/vboxguest")`, whose init returns success only after the PCI attach (I/O port, MMIO, common device init, IRQ) returns success. There was no `Symbol not found`, no `RTR0Init failed`, and no panic. The log reached `Running first login script` and then went quiet while the VM stayed running. The version word was not printed: release logging goes through the stub, which does not call `dprintf`. The screen was not watched from here. Shared folders, resize, mouse filter, and clipboard were not enabled.

`VBoxService` on that first identify boot had no log of its own. A later start of the same throwaway, about 15:40 Europe/Amsterdam, launched it from a job on `initial_volumes_mounted` (`-f`, logfile `/boot/home/vbox-service.log`, `--disable-memballoon`, `--timesync-no-set-start`). `/dev/misc/vboxguest` existed (`crw-r--r-- root root`). The process opened, then stopped at once: every ioctl failed with `VERR_INVALID_POINTER`. Haiku's `ioctl()` takes the buffer size as a fourth argument; the three-argument call left that size as stack garbage. `VBoxGuestR3Lib.cpp` in the VirtualBox tree outside git now passes `cbReq`. The serial log still reached the first-login script. No panic.

The next start, about 15:47 Europe/Amsterdam (serial `generated/vbox-svc3.log`), kept the service up: `All services started`, `timesync` set the guest clock from the host, and `vminfo` ran. Guest-property writes returned `VERR_ACCESS_DENIED`, so `/VirtualBox/GuestAdd/Version` never appeared in `VBoxManage guestproperty enumerate`. The open path marked every session `VMMDEV_REQUESTOR_USERMODE` only. `VBoxDev-haiku.c` now adds `VMMDEV_REQUESTOR_USR_ROOT` when `geteuid()` is 0, matching the Linux guest driver. The following start, about 15:52 Europe/Amsterdam (serial `generated/vbox-svc4.log`), still reached the first-login script with no panic. The service log again says `All services started` and `timesync time set` at 14:52:07, 14:52:17 and 14:52:27 UTC (15:52 Europe/Amsterdam). The same property writes were still `VERR_ACCESS_DENIED`. The screen was not watched. Clipboard mode stayed disabled. `--timesync-no-set-start` did not stop the clock updates.

Those writes failed in the Haiku user-memory lock. With no team, the lock looked the address up in the kernel team, and it always asked for a writable mapping. A read-only guest string then returns `B_PERMISSION_DENIED`, which the guest prints as `VERR_ACCESS_DENIED`. The VirtualBox tree outside git now uses the current team for that case, asks for write access only when the buffer is written, and unlocks with the same team and flags. The first boots of that module panicked: interrupts were enabled after the handler for vector 20, which is the GSI for PCI `0:4` (`80ee:cafe`). The Haiku ISR now disables interrupts before it returns. The start about 16:35 Europe/Amsterdam (serial `generated/vbox-vminfo3.log`) loaded the driver, did not panic, and reached the first-login script. The host then listed `/VirtualBox/GuestAdd/Version` `7.2.22`, revision `175245`, and `/VirtualBox/GuestInfo/OS/Product` `Haiku`. `LoggedInUsers` was `0`. At 16:38 Europe/Amsterdam the screen showed ProcessController on the desktop: Tracker, Deskbar, Login, `vbox-probe.sh`, and `VBoxService`. Clipboard mode stayed disabled. This is still not in the product image.

## What the running guest already has

`scripts/run-vbox-r5.ps1` creates the VM with:

| Setting | Value | What the guest sees |
|---|---|---|
| Disk | PIIX4 IDE, fixed VDI | Haiku IDE stack |
| NIC | Intel 82540EM, bridged | `ipro1000` |
| Audio | Intel HDA | HDA driver |
| Mouse | USB tablet | absolute USB HID, not a Guest Additions pointer |
| Graphics | `vboxvga`, 16 MB VRAM | VESA / VBE framebuffer |
| Clipboard mode | disabled | not Devices → Shared Clipboard |
| Drag-and-drop | disabled | no host file drop |
| UART1 | `0x3F8` IRQ 4, server pipe `\\.\pipe\R5OS-clip` | COM1 |

There is no `vboxsf`, no auto-resize, and no VirtualBox drag-and-drop. The script says so on purpose.

Text clipboard is `distro/vboxclip` (`r5_vbox_clip`), started from the user launch job, plus `scripts/vbox-clipboard-host.ps1`. Frames are `R5C1` over `pc_serial1`. Unicode text, about 256 KB. `-NoClip` skips the host side. This is the fallback and stays until a native clipboard has been seen working both ways.

The Haiku tree at `/home/nickr/r5os/haiku` has no `VMMDev`, no `80EE:CAFE` driver, and no VMware SVGA driver. `virtio_gpu` exists for QEMU. Display in this VM is the VESA driver. `app_server` paints software bitmaps (`BitmapDrawingEngine`).

Mouse movement in this VM already works (VirtualBox sessions through build 10013). That is the USB tablet. A second absolute pointer from Guest Additions would risk duplicate events. Do not add one until the tablet path is shown to be insufficient.

## What already exists elsewhere

Do not write a second Guest Additions stack from memory.

- Oracle still ships Haiku additions under `src/VBox/Additions/haiku` (VBoxMouse, VBoxVideo, VBoxTray, SharedFolders). VirtualBox **7.2.22** is **GPL-3.0-only** (`COPYING` and the Haiku makefile). Some IPRT files are GPL-3.0-only OR CDDL-1.0. The HaikuPorts 6.1.26 recipe's GPL v2 and CDDL is not this release. The source and the cross-built ELF64 binaries sit outside this git tree. They are not in the image. Do not relicense them as MIT and do not paste them into MIT sources. Assessment: `docs/LICENSING.md`. Userspace programs can ship as separate GPL-3.0 programs with the license text and matching source. Kernel modules stay out of the image until the reach of that combination into the MIT kernel is reviewed.
- HaikuPorts has `virtualbox_guest_additions` recipes through **6.1.26** (`ARCHITECTURES` includes `x86_64`, not gcc2). The 6.1.26 recipe was added as work in progress in 2021. It has not been built or booted on this R5OS image. Current VirtualBox hosts are newer than 6.1.26. Protocol compatibility is unproven.
- Older HaikuDepot additions did clipboard and time sync for some people. `vboxsf` was omitted from the gcc2 package because it panicked. Tracker on a shared folder has panicked the kernel before. Treat shared folders as later, and hostile, until a current x86_64 build is tested.

The published PCI device is vendor `0x80EE`, device `0xCAFE`. BAR layout, version word, events, and HGCM requests must be taken from the VirtualBox headers for the host version in use. Linux driver behavior is not the Haiku driver model.

## Controllers are not interchangeable

| Controller | Script today | Guest driver in this tree | Resize | 3D |
|---|---|---|---|---|
| `vboxvga` | yes, 16 MB | VESA | no | no |
| `VBoxSVGA` | no | none | no | no |
| `vmsvga` | no | none (no SVGA driver) | no | no |

Keep `vboxvga` until a modesetting driver for one of the others has booted to a desktop. Switching the script first can leave the guest on a blank mode.

VirtualBox 3D is a separate question. Detecting VGA, or a larger mode, is not acceleration. There is no Haiku driver here that submits commands to the host GPU through VMSVGA or VBoxSVGA. Leave 3D out of the guest-integration path.

## Order

1. **See the device.** On a running guest, `listdev` (or the PCI list in the syslog) must show `80EE:CAFE` before any driver claims it. Also record the display controller PCI ID. Seen on the 15:27 Europe/Amsterdam throwaway: `80ee:cafe` at bus 0 device 4, and `80ee:beef` at bus 0 device 2, before the driver line. `listdev` itself was not run.
2. **Identify only.** A native driver may claim `80EE:CAFE`, map its BARs, and log the version field from the VirtualBox structure for this host. No HGCM, no clipboard replacement, no resize. If the device is missing, boot continues on VESA and COM1. The 15:27 throwaway claimed the device (driver load after a successful attach). The version field was not logged. That driver is not in the product image.
3. **One userspace service.** Time sync and guest version reporting share that device. The service exits when the device is absent. It must not block Login, Tracker, or Deskbar. On the 16:35 Europe/Amsterdam throwaway, `VBoxService` stayed up, `timesync` had already set the guest clock on the earlier boot, and the host listed `/VirtualBox/GuestAdd/Version` `7.2.22` plus `/VirtualBox/GuestInfo/OS/Product` `Haiku`. The serial log reached the first-login script with no panic. At 16:38 Europe/Amsterdam ProcessController showed Tracker, Deskbar, and `VBoxService` together. The service is not in the product image. A boot with the device missing was not run.
4. **Clipboard.** Host and guest text, both directions, empty clipboard, and a large paste. COM1 stays until that has been seen. Then the script can enable VirtualBox clipboard mode. File paste is later.
5. **Mouse.** Only if the USB tablet is wrong (capture, jumps, wrong coordinates after a mode change). One input path, through the input server.
6. **Display.** One controller. Mode change without restarting `app_server`. Pointer coordinates after the change. A safe mode remains if the notification fails.
7. **Shared folders.** Mount and unmount, read/write, Unicode names, large files, host disconnect. Permissions follow the signed-in account. A host directory must not become world-readable under another home. Not while the protocol is still panicking Tracker in older ports.
8. **Drag and drop.** After clipboard and folders work. Tracker’s existing drag path. No second transfer framework.
9. **3D.** Write down whether the host exposes a usable command stream to this guest. If it does not, the guest package is still useful.

## Tests that count

VMMDev present or absent, version negotiation, both clipboard directions, one resize, one shared-folder copy, reboot, and a boot with the device missing. QEMU does not stand in for this. The COM1 clipboard is already the tested path. Guest Additions features are not.

## Reporting

Say which VirtualBox version and which graphics controller. Do not call the guest accelerated.
