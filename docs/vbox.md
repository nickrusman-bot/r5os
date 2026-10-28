# VirtualBox and USB (LAN tests)

Haiku/R5OS has **no VirtualBox Guest Additions**. Expect:

- no `vboxsf` shared folders
- no automatic guest resolution
- no VirtualBox drag-and-drop (that protocol is Guest Additions)

**Text clipboard** is a COM1 named-pipe bridge (`r5_vbox_clip` in the guest, `scripts/vbox-clipboard-host.ps1` on Windows). It is not Devices → Shared Clipboard. Unicode text only, ~256 KB. `-NoClip` skips the host helper.

The guest uses ordinary PC devices: IDE disk, Intel PRO/1000 (`ipro1000`), VESA/VBoxVGA, USB tablet. VBox clipboard-mode and drag-and-drop stay off.

## VirtualBox (Windows)

Needs a jammed `r5-os.image` and VirtualBox with **bridged** networking (not NAT).

```powershell
powershell -File scripts/run-vbox-r5.ps1
powershell -File scripts/run-vbox-r5.ps1 -BridgeAdapter "Wi-Fi"
powershell -File scripts/run-vbox-r5.ps1 -ForceConvert
```

`$env:R5_VBOX_BRIDGE` also selects the host NIC. The script copies the raw image onto NTFS and makes a **fixed** `.vdi` in `generated/` (gitignored). The VDI is reused unless the jammed raw image is newer, or you pass `-ForceConvert`. Converting through `\\wsl.localhost` on first power-on often hung until Reset.

A **new** VDI is a virgin disk. `package_daemon` then does first-boot processing for every package (syslog: `Doing first boot processing #N for package …`). That often takes **several minutes** on VirtualBox IDE. Wait for the desktop. Do not Reset; that restarts the same work. A guest **Restart** (or Shutdown + start of the same VDI) is fast.

Use this for IPv6 SLAAC, mDNS, AirPlay speakers, and SMB on the home LAN. QEMU usernet cannot.

## USB stick (Ryzen 5 5625U)

`r5-os.image` is a raw disk. In **Rufus**, choose the image, **DD mode**, pick the stick. The stick is wiped. Size must be ≥ the image (~1.2 GB). Boot the 5625U from USB.

That is the hardware check. VirtualBox bridged is the Windows LAN check.
