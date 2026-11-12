# Device Manager

Device Manager shows every device the kernel's device manager knows, whether it has a driver, and which one. It works like Windows 11 Device Manager. Haiku's Devices app stays installed and unchanged.

- Source: `distro/apps/devicemanager/`, copied to `src/apps/devicemanager/` by `scripts/apply-devicemanager.py`.
- Binary: `/boot/system/apps/DeviceManager`, in Deskbar under Applications. Regular image only.
- Signature: `application/x-vnd.R5-DeviceManager`.
- License: MIT, the whole app (`distro/apps/devicemanager/LICENSE`), so other Haiku-based systems can use it. See "License" below.

## What it reuses

Haiku's Devices code is built from `src/apps/devices` without changes: `dm_wrapper.c` (the device-manager syscalls), `Device*.cpp` (PCI, USB, ACPI and SCSI names), and `DriverUtils.cpp` (the driver blocklist and `IsCriticalDriver()`). Device Manager's Jamfile copies Devices' rules that build the PCI, USB, ACPI and ISA name headers from `src/data/ids`.

`DeviceModel::_AddNode()` follows `DevicesView::AddDeviceAndChildren()`.

## Main window

- **View**: Devices by type (default), Devices by connection, Show hidden devices. The choice and the window frame are kept in `~/config/settings/DeviceManager`.
- **Action**: Properties, Turn off driver / Turn on driver, Scan for hardware changes, Turned-off drivers.
- **File**: Export > Full details (every device under its type, with all its properties) or Device tree (as connected, indented one step per level). Plain UTF-8 text.
- Groups by type: Cameras, Disk drives, Display adapters, Human interface devices, Network adapters, Other devices, Other system devices, Processors, Sound video and game controllers, Storage controllers, System devices, Universal Serial Bus controllers. Groups start folded, except Other devices.
- The list follows plug-in and unplug. The device manager sends no notifications and watching `/dev` did not report a USB keyboard in QEMU, so every 2 seconds Device Manager compares the node ids of the tree (`TreeSignature()`, no attributes read). After a change it waits 500 ms and reads the tree again. Selection and folded groups stay.

Hidden unless Show hidden devices: the nodes between a device and its driver, such as a bus driver's own node, an ATA channel's SCSI bus or a virtio device. They take the group of their device. A plain node directly under the computer is a system device.

Not shown: the nodes that only carry a driver (a driver node whose path is its parent's, and the PCI "legacy driver" reservations). Hidden unless Show hidden devices: unnamed ACPI nodes and nodes with no name.

## Which driver serves a device

The kernel's `DM_GET_DRIVER_PATH` returns the node's own module, or the module of a child under `drivers/`. Device Manager adds two sources:

1. **Legacy reservation.** A legacy PCI driver that calls `reserve_device()` (FreeBSD network drivers, HDA and other audio) adds a `legacy_driver` child with the driver's name. Device Manager takes that name and finds the file in `add-ons/kernel/drivers/bin` (system, then non-packaged, then the person's).
2. **Graphics.** Graphics drivers do not reserve. Most name their entry `/dev/graphics/<driver>_BBDDFF` (bus, device, function in hex, for example `radeon_hd_040000`). Device Manager matches that to the PCI device. `/dev/graphics/vesa` or `framebuffer` is given to the display controller when there is exactly one without a driver.

A bus, the root or an ACPI node served by a system module counts as working.

## Status

| Status | Rule | Shown |
|---|---|---|
| Working | A driver was found as above | no badge |
| Turned off | The driver is in a packages blocklist. It still runs until the next start | gray down arrow |
| No driver | A PCI device in Display, Network, Sound, Storage, USB or HID, with no driver found | yellow warning, moved to Other devices |
| Not reported | Anything else without a driver found (USB devices, whose drivers are not tied to a node) | no badge, a neutral sentence |

USB device drivers in Haiku do not attach to a device node, so Device Manager cannot name them. It says so instead of warning.

## Properties

One window per device, with tabs:

- **General**: type, manufacturer, location (PCI bus/device/function, the ACPI path, or the parent), status sentence.
- **Driver**: driver name, file, size, date, and from the driver's package the package name, version and license. Driver details lists the file and the `/dev` paths. Turn off driver / Turn on driver.
- **Details**: every attribute of the node, one per menu item, values one per row. Cmd+C copies.
- **Events**: the last 50 lines of `/var/log/syslog` (the last 2 MiB of the file) that name the driver or one of the device's `/dev` paths, newest first.

Not built: a Resources tab. User space has no call that reports a device's IRQ, I/O ports or memory ranges. That needs a kernel addition (for example PCI attributes on the node) and its own boot test.

## Turning a driver off and on

This is per driver, as in Haiku's Devices: every device of that driver changes. The blocklist is read at start, so the change takes effect after a restart. A turned-off driver no longer loads, and its device may no longer appear. Action > Turned-off drivers lists the blocked drivers and turns them on again.

A driver under `/boot/system` needs an administrator. Device Manager calls `r5_admin_set_driver_blocked()` (libr5ui). If the lock is closed it opens the unlock window first. Login handles `kMsgAdminSetDriverBlocked` (`'r5dv'`) in `distro/login/DriverAdmin.cpp`:

- fields `"driver"` (string) and `"blocked"` (bool); reply `"error"` (int32) and `"text"` (string);
- the sender must be an administrator or hold a grant;
- the path must start with `/boot/system/add-ons/kernel/` and have no `.`, `..` or `//` parts;
- to turn off: the file must exist at its normalized path, must not be critical (Devices' list: bus managers, busses, disk, power, timer, bus, common and input drivers, and `acpi`, `pci`, `root`), and must be in a package;
- to turn on: the package name comes from the blocked entry, because packagefs hides a blocked file;
- the entry is written with Haiku's `DriverUtils::UpdatePackageBlockedEntry()` into `/boot/system/settings/packages`.

After the call Device Manager reads the file again, so an older Login that does not know the message is reported as "The change was not saved."

A driver in the person's own packages is written by Device Manager itself, in `~/config/settings/global/packages`.

Not built: turning off one device instead of the whole driver (needs a kernel setting), a kernel rescan (no syscall exists, so Scan for hardware changes reads the tree again), update driver, roll back, uninstall device.

## License

- Device Manager's own files are MIT, `SPDX-FileCopyrightText: 2026 R5OS Contributors`.
- Haiku's Devices files keep their Haiku MIT headers.
- The icon is Haiku's Devices icon (MIT).
- The PCI ID list is GPL-2.0-or-later or BSD-3-Clause; Device Manager uses it under BSD-3-Clause. The USB ID list states no license in the file (open point, the same list Haiku's Devices uses).
- It links the administrator lock in `libr5ui.so`, which is MIT. Login, the side that checks the password and writes the file, is MIT too since 12 Nov 2026 and is reached only by messages.

## About

The About window follows About R5: a document card with the icon, "Device" bold and "Manager" plain at twice the body size and a muted version line, and a card with label and value cells. It uses only public `libr5ui` classes (`BCard`, `BIconView`).
