# VirtualBox and USB (LAN tests)

Haiku/R5OS has **no VirtualBox Guest Additions**. Expect:

- no `vboxsf` shared folders
- no automatic guest resolution
- no shared clipboard from VirtualBox

The guest uses ordinary PC devices: IDE disk, Intel PRO/1000 (`ipro1000`), VESA/VBoxVGA, USB tablet. The VM has clipboard and drag-and-drop turned off.

## VirtualBox (Windows)

Needs a jammed `r5-os.image` and VirtualBox with **bridged** networking (not NAT).

```powershell
powershell -File scripts/run-vbox-r5.ps1
powershell -File scripts/run-vbox-r5.ps1 -BridgeAdapter "Wi-Fi"
```

`$env:R5_VBOX_BRIDGE` also selects the host NIC. The `.vdi` lands in `generated/r5-os.vdi` (gitignored).

Use this for IPv6 SLAAC, mDNS, AirPlay speakers, and SMB on the home LAN. QEMU usernet cannot.

## USB stick (Ryzen 5 5625U)

`r5-os.image` is a raw disk. In **Rufus**, choose the image, **DD mode**, pick the stick. The stick is wiped. Size must be ≥ the image (~1.2 GB). Boot the 5625U from USB.

That is the hardware check. VirtualBox bridged is the Windows LAN check.
