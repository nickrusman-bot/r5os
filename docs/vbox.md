# VirtualBox and USB (LAN tests)

Plan for Guest Additions: `docs/VBOX-GUEST.md`. Native GPU and Vulkan: `docs/GRAPHICS.md`.

The daily image installs the 7.2.22 guest additions (`distro/vbox-guest/`, copied at apply time). On that image:

- `vboxsf` is present. A share is mounted only when the host offers one and someone mounts it. The product script does not add a share.
- the guest display can follow the VirtualBox window. VESA stays the fallback when the VirtualBox display device is absent.
- no VirtualBox drag-and-drop (that protocol is not built for Haiku in 7.2.22; GitHub issue 89)

**Text clipboard** is a COM1 named-pipe bridge (`r5_vbox_clip` in the guest, `scripts/vbox-clipboard-host.ps1` on Windows). It is not Devices → Shared Clipboard. Unicode text only, ~256 KB. `-NoClip` skips the host helper.

The guest uses ordinary PC devices: IDE disk, Intel PRO/1000 (`ipro1000`), VESA/VBoxVGA, USB tablet. VBox clipboard-mode and drag-and-drop stay off.

## VirtualBox (Windows)

Needs a jammed `r5-os.image` and VirtualBox with **bridged** networking (not NAT).

```powershell
powershell -File scripts/run-vbox-r5.ps1
powershell -File scripts/run-vbox-r5.ps1 -BridgeAdapter "Wi-Fi"
powershell -File scripts/run-vbox-r5.ps1 -ForceConvert
```

`$env:R5_VBOX_BRIDGE` also selects the host NIC. The script copies the raw image onto NTFS and makes a **fixed** `.vdi` in `generated/` (gitignored). The VDI is reused unless the jammed raw image is newer (Linux `stat` mtime, not `\\wsl.localhost`), or you pass `-ForceConvert`. Converting through `\\wsl.localhost` on first power-on often hung until Reset.

A **new** VDI is a virgin disk. `package_daemon` then does first-boot processing for every package (syslog: `Doing first boot processing #N for package …`, `activated-packages: No such file`). On VirtualBox IDE that often takes **several minutes** (QEMU virtio of an image that already completed first-boot is about five seconds). Wait for the desktop. Do not Reset; that restarts the same work. A guest **Restart** of the same VDI is fast.

IDE syslog `invalid target lun 2`–`7` / `target device not present` is the empty slave and secondary channel. Reset is still ~5s with those lines, so they are not the long first boot.

Use this for IPv6 SLAAC, mDNS, AirPlay speakers, and SMB on the home LAN. QEMU usernet cannot.

A cold start with two CPUs can sit on the boot splash with every icon still gray. 2026-10-30 12:51 Europe/Amsterdam, normal VDI, no reset, two minutes: serial stopped at `kernel entry at …`, the boot loader line just before `smp_boot_other_cpus()`. That wait used to have no timeout. The loader now retries each CPU three times and, if it still does not answer, enters the kernel with the CPUs that started. That change is in `distro/boot/bios_ia32/smp.cpp`. Jam and a cold start of the new image on 2026-10-30 13:18 Europe/Amsterdam entered the kernel with both CPUs. The daily VDI was not replaced, so it still has the old loader. The rocket icon is a later stop, after the kernel is already running.

## USB stick (Ryzen 5 5625U)

`r5-os.image` is a raw disk. In **Rufus**, choose the image, **DD mode**, pick the stick. The stick is wiped. Size must be ≥ the image (~1.2 GB). Boot the 5625U from USB.

That is the hardware check. VirtualBox bridged is the Windows LAN check.
