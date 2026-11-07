# Realtek rtsx card reader

This is the developer note for `busses/mmc/rtsx`. The module is a Haiku `device_manager` driver. It publishes the same MMC bus as `sdhci`, so `mmc_bus` and `mmc_disk` stay as they are. A card that enumerates shows up as a normal volume.

The register file is OpenBSD `sys/dev/ic/rtsxreg.h` r1.5. The command, clock, and power sequences follow OpenBSD `rtsx.c` r1.22 and `rtsx_pci.c` r1.16. Those files, and `rtsx.cpp`, are ISC. `rtsx_5228.cpp` and `rtsx_5228_regs.h` are GPL-2.0-or-later, from Linux v6.12 `rts5228.c`, `rtsx_pcr.c`, and the registers used from `rtsx_pci.h`. Linking them makes the addon binary GPL. Haiku can leave `rtsx_5228.cpp` out of the Jamfile and keep the ISC core. See `docs/LICENSING.md`.

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
| `10ec:5228` | RTS5228 | `RTSX_F_5228` | Not claimed. `rtsx_5228_support()` returns 0 |

Match requires vendor `10ec` and PCI class `0xff`. A Realtek chip that advertises the SDHCI class stays with `sdhci`. That is the OpenBSD rule.

## Adding a chip

1. Add a row to `kDevices` in `rtsx.cpp`: the PCI device id, the flag, the BAR index, and the name.
2. If OpenBSD `rtsx_pci.c` already has a flag for that id, use that flag. The init in `InitChip`, `PowerOn`, and `PowerOff` then follows that flag.
3. If OpenBSD has no sequence for the id, give it a new flag, as with `RTSX_F_5228`. Do not point it at another chip's flag. Leave `ChipReady()` false for that flag so no register write runs.
4. Do not copy Linux `drivers/misc/cardreader/rts5228.h`. It is GPL-2.0-only. A 5228 sequence from `rts5228.c` or `rtsx_pcr.c` belongs in `rtsx_5228.cpp`, and only with registers that are already in `rtsx_pci.h` or written as literals in those `.c` files.

`rts5228.h` is not in this tree.

## RTS5228, open points

The 5625U reader is `10ec:5228` revision `01`, subsystem `103c:8a31`, class `ff:00:00`, BAR0 `0xd0700000` size `0x1000`. OpenBSD does not list this id. `rtsx_5228_support()` returns 0, so no MMC bus is published and no chip register is written.

`rtsx_5228.cpp` has the Linux v6.12 PHY write `0x07 = 0x8F40`, the 3.3 V drive table, the pull table `0xAA` / `0xE9`, the `rtsx_pci_init_hw` register list that uses `rtsx_pci.h`, MSI when the PCI module can enable it, and the card interrupt. A card interrupt waits 200 ms, as Linux does, and then releases the MMC scan semaphore. Removal does not unpublish the volume: `mmc_bus` still has that as a FIXME. None of this runs, because the device is not claimed.

Still missing, because the values are only in `rts5228.h`:

- LDO power on and power off, DV3318, and the overcurrent clear
- `RTS5228_SSC_DEPTH_2M` for `SSC_CTL2`. The generic depth in `rtsx_pci.h` is a different number, so it is not written in its place
- PME force and the 5228 autoload card-detect resume bit
- A boot on the 5625U with a card

Class `0xff` now also scans `busses/mmc` (`device_manager.cpp`, `PCI_undefined`). That change is not in a booted kernel yet. The other ids are ported and have not been booted on hardware. QEMU has no rtsx device. A QEMU boot with `-device sdhci-pci` still only shows the module load.

## How a command runs

For a chip OpenBSD supports, the host command buffer is one contiguous page below 4 GiB. The chip reads it through `RTSX_HCBAR`. Data uses the physical address of the `IOOperation` vector, also below 4 GiB, through `RTSX_HDBAR`. A multi-block transfer uses `RTSX_TM_AUTO_READ4` or `RTSX_TM_AUTO_WRITE4` from the OpenBSD header, because those modes send CMD12 and `mmc_disk` does not.

`TRACE_RTSX` in `rtsx.cpp` turns on the trace lines. There is no `driver_settings` file yet.
