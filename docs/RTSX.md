# Realtek rtsx card reader

This is the developer note for `busses/mmc/rtsx`. The module is a Haiku `device_manager` driver. It publishes the same MMC bus as `sdhci`, so `mmc_bus` and `mmc_disk` stay as they are. A card that enumerates shows up as a normal volume.

The register file is OpenBSD `sys/dev/ic/rtsxreg.h` r1.5. The command, clock, and power sequences follow OpenBSD `rtsx.c` r1.22 and `rtsx_pci.c` r1.16. The license of the whole module is ISC. See `docs/LICENSING.md`.

The module is built with jam. It is not installed in the image.

`module_dependencies` stores the MMC bus module in `sMMCBusModule`. The kernel writes the loaded module through that pointer. NULL panics in `get_module`.

The first call of `supports_device` prints `rtsx: loaded`. That is how a serial boot shows the module ran. `10ec:5228` also prints `rtsx: not claiming 10ec:5228` and returns 0.

## Chips

| PCI id | Name | Flag | What the driver does |
|---|---|---|---|
| `10ec:5209` | RTS5209 | `RTSX_F_5209` | OpenBSD init, including the 5209 PHY value and GPIO |
| `10ec:5227` | RTS5227 | none | OpenBSD default init |
| `10ec:5229` | RTS5229 | `RTSX_F_5229` | OpenBSD init, including the type-C revision check |
| `10ec:522a` | RTS522A | none | OpenBSD default init |
| `10ec:5249` | RTS5249 | `RTSX_F_5229` | Same path as 5229, as in OpenBSD |
| `10ec:525a` | RTS525A | `RTSX_F_525A` | OpenBSD init. BAR 1, not BAR 0 |
| `10ec:5286` | RTL8402 | none | OpenBSD default init |
| `10ec:5287` | RTL8411B | none | OpenBSD default init |
| `10ec:5289` | RTL8411 | none | OpenBSD default init |
| `10ec:5228` | RTS5228 | `RTSX_F_5228` | Not claimed. `supports_device` returns 0 until a chip init exists |

Match requires vendor `10ec` and PCI class `0xff`. A Realtek chip that advertises the SDHCI class stays with `sdhci`. That is the OpenBSD rule.

## Adding a chip

1. Add a row to `kDevices` in `rtsx.cpp`: the PCI device id, the flag, the BAR index, and the name.
2. If OpenBSD `rtsx_pci.c` already has a flag for that id, use that flag. The init in `InitChip`, `PowerOn`, and `PowerOff` then follows that flag.
3. If OpenBSD has no sequence for the id, give it a new flag, as with `RTSX_F_5228`. Do not point it at another chip's flag. Leave `ChipReady()` false for that flag so no register write runs.
4. Do not copy Linux `drivers/misc/cardreader/rts5228.c` or `rts5228.h`. `rts5228.h` is GPL-2.0-only. A sequence that exists only there is not added here. Write the missing step down under Open points instead.

`rts5228.h` is not in this tree.

## RTS5228, open points

The 5625U reader is `10ec:5228` revision `01`, subsystem `103c:8a31`, class `ff:00:00`, BAR0 `0xd0700000` size `0x1000`. OpenBSD does not list this id. `supports_device` returns 0 for that id, so no MMC bus is published and no chip register is written.

Still open, because the only published init for this chip is the Linux `rts5228` code, which this port does not use:

- PHY, LDO, drive strength, and the 5228 clock switch
- Card-detect interrupt and hotplug
- MSI. The other chips use the legacy interrupt line, as `sdhci` does. The 5228 does not install that handler until it has an init sequence
- A boot on the 5625U with a card

The other ids are ported and have not been booted on hardware. QEMU has no rtsx device. A QEMU boot with `-device sdhci-pci` scans `busses/mmc`, so `supports_device` runs, matches nothing, and returns 0. Class `0xff` is not scanned under `busses/mmc`, so a 5228 on the 5625U is not probed until that path is added with the init.

## How a command runs

For a chip OpenBSD supports, the host command buffer is one contiguous page below 4 GiB. The chip reads it through `RTSX_HCBAR`. Data uses the physical address of the `IOOperation` vector, also below 4 GiB, through `RTSX_HDBAR`. A multi-block transfer uses `RTSX_TM_AUTO_READ4` or `RTSX_TM_AUTO_WRITE4` from the OpenBSD header, because those modes send CMD12 and `mmc_disk` does not.

`TRACE_RTSX` in `rtsx.cpp` turns on the trace lines. There is no `driver_settings` file yet.
