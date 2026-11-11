# Driver Brief: Realtek RTSX MMC Bus Driver (PR #4118)

This brief evaluates pull request [#4118](https://github.com/r5-operating-system/r5os/pull/4118) (*Drivers - Support for Realtek SD card readers*) and specifies the technical plan required to bring `busses/mmc/rtsx` from its current prototype state to a functional, production-ready storage driver in R5 Operating System.

---

## 1. Executive Summary & Current PR Status

PR #4118 introduces a Haiku `device_manager` bus driver under `busses/mmc/rtsx`, publishing the standard Haiku `mmc_bus_interface` (compatible with `mmc_bus` and `mmc_disk`). 

### Key Findings
1. **Source Heritage**: The core driver (`rtsx.cpp`, `rtsxreg.h`) is cleanly ported from OpenBSD `rtsx(4)` under the ISC license.
2. **Reference Hardware Discrepancy**: The R5 reference laptop (AMD Ryzen 5 5625U) contains a **Realtek RTS5228** card reader (`PCI 10ec:5228`, revision `01`, class `0xff:00:00`, subsystem `103c:8a31`).
3. **5228 Disabled**: OpenBSD does not support the RTS5228. PR #4118 adds `rtsx_5228.cpp` (derived from Linux `rts5228.c`), but explicitly refuses to claim the hardware:
   - `rtsx_5228_support()` returns `0.0f` (`rtsx: not claiming 10ec:5228`).
   - `RtsxBus::Init()` checks `ChipReady()`, which returns `false` for `RTSX_F_5228`, bypassing chip register programming entirely.
4. **Not Packaged**: The module compiles with `jam -q rtsx`, but is not included in image definitions (`minimum` / `regular`), so it does not ship on anyboot images or USB sticks.
5. **Untested on Hardware**: No Realtek card reader (neither legacy OpenBSD-supported models nor the 5228) has been booted and validated with physical media in R5.

---

## 2. Technical Gap Analysis

To convert PR #4118 into an operational driver, the following five technical gaps must be resolved:

### Gap 1: RTS5228 Register & Power Sequence Gaps
`rtsx_5228.cpp` contains partial hardware initialization, but several sequences are absent:
* **LDO Power Sequencing**: Powering the card slot requires programming the internal Low-Dropout (LDO) regulators (`RTS5228_LDO1_CFG1`, DV3318 regulator enablement, and overcurrent clear).
* **Spread Spectrum Clocking (SSC)**: `SSC_CTL2` requires `RTS5228_SSC_DEPTH_2M`. The generic depth in `rtsx_pci.h` differs and will cause clock instability if written.
* **Power Management / PME**: Missing PME force transitions and the 5228 card-detect autoload resume bit.
* **Licensing Compliance**: Linux `rts5228.h` is GPL-2.0-only. To comply with R5 licensing rules (`docs/LICENSING.md`), necessary register constants must be cleanly documented and declared in `rtsx_5228_regs.h` under GPL-2.0-or-later.

### Gap 2: Driver Architecture Decoupling
In PR #4118, `rtsx_5228.cpp` and `rtsx.cpp` are decoupled:
* `struct rtsx_5228` in `rtsx_5228.cpp` defines its own private register accessors, interrupt handler, and state struct.
* `RtsxBus` in `rtsx.cpp` manages the actual `mmc_bus_interface` (`SetClock`, `ExecuteCommand`, `DoIO`, `SetBusWidth`), but delegates none of its power or clock hooks to `rtsx_5228.cpp`.
* **Fix needed**: Unify the dispatch model so `RtsxBus` calls dedicated chipset operations (`rtsx_5228_init`, `rtsx_5228_power_on`, `rtsx_5228_power_off`, `rtsx_5228_set_clock`) when `fFlags & RTSX_F_5228` is set.

### Gap 3: PCI Class 0xFF Scanning & Device Publishing
* Realtek readers advertise PCI class `0xff:00:00` (vendor-specific), not `0x08:05:00` (standard SD host controller).
* PR #4118 modified `distro/kernel/src/system/kernel/device_manager/device_manager.cpp` to make `PCI_undefined` scan `busses/mmc`.
* Once `rtsx_5228_support()` returns `0.8f`, the device manager will match `busses/mmc/rtsx/driver_v1`, instantiate `RtsxBus`, and register the child node for `mmc_bus`.

### Gap 4: Integration with `mmc_bus` & Hotplug Limitations
* **Card Insertion**: When a card is inserted, `RTSX_BIPR` asserts `SD_EXIST` and triggers an interrupt. The interrupt spawns/notifies a worker that releases `fScanSemaphore` to prompt `mmc_bus` enumeration.
* **Card Ejection**: Haiku's `mmc_bus` module currently has a known limitation: it does not cleanly unpublish or tear down the mounted `mmc_disk` volume on card removal. The driver must ensure removal does not fault or cause a kernel panic.

### Gap 5: Image Packaging & Deployment
* `scripts/apply-kernel-bugs.py` copies the driver files into WSL, but the image jamfiles do not list `rtsx` under kernel bus managers/drivers.
* The addon must be explicitly installed into `/boot/system/add-ons/kernel/busses/mmc/rtsx`.

---

## 3. Implementation Roadmap

### Phase 1: Complete RTS5228 Register Definitions & Hooks
1. Populate `rtsx_5228_regs.h` with the missing LDO, DV3318, and SSC definitions under `GPL-2.0-or-later`.
2. Implement complete card slot power sequencing in `rtsx_5228.cpp`:
   - `rtsx_5228_power_on(rtsx_bus* bus)`: LDO output enable, 3.3V drive strength selection, pull-up/pull-down configuration.
   - `rtsx_5228_power_off(rtsx_bus* bus)`: LDO power down, clock gating.
   - `rtsx_5228_set_clock(rtsx_bus* bus, uint32 kilohertz)`: SSC tuning and divider programming.

### Phase 2: Integrate `rtsx_5228` into `RtsxBus`
1. Replace the disconnected `struct rtsx_5228` with direct operations on `RtsxBus`.
2. Connect `InitChip()`, `EnsurePower()`, `PowerOn()`, `PowerOff()`, and `SetClock()` to the 5228 implementations when `RTSX_F_5228` is active.
3. Update `rtsx_5228_support(vendor, device)`:
   ```cpp
   extern "C" float
   rtsx_5228_support(uint16 vendor, uint16 device)
   {
       if (vendor == 0x10ec && device == 0x5228)
           return 0.8f;
       return 0.0f;
   }
   ```
4. Set `ChipReady()` to evaluate to `true` for `RTSX_F_5228`.

### Phase 3: Build & Image Packaging
1. Update `build/jam/images/definitions/minimum` (or equivalent package manifest) to include `rtsx`.
2. Verify that WSL `apply-kernel-bugs.py` copies `device_manager.cpp` and `rtsx`.
3. Verify that `jam -q @r5-raw` packages `rtsx` into `/system/add-ons/kernel/busses/mmc/`.

### Phase 4: Hardware Verification on Reference 5625U
1. Boot anyboot image with `rtsx` on the HP 5625U laptop.
2. Verify syslog output on boot:
   ```text
   rtsx: loaded
   rtsx: match 10ec:5228
   rtsx: 5228 using MSI vector ...
   ```
3. Insert an SD card (FAT32 or BFS formatted) and verify:
   - Card presence interrupt fires (`SD_INT`, `SD_EXIST`).
   - `mmc_bus` completes CMD0, CMD8, ACMD41, CMD2, CMD3 negotiation.
   - `mmc_disk` publishes `/dev/disk/mmc/...`.
   - Tracker mounts the SD card volume on the desktop.
4. Validate read and write throughput with `dd` or DriveSetup.
