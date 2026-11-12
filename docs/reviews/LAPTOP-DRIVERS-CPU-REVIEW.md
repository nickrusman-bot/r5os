<!--
  SPDX-FileCopyrightText: 2026 R5OS Contributors
  SPDX-License-Identifier: GPL-3.0-or-later
-->
# Review: what the 5625U laptop still needs (12 Nov 2026)

For Cursor. Read-only. Sources: `generated/boot-5625u-41/r5-hardware.txt` (listdev), `CHANGELOG.md` on `origin/main` (sticks 48 and later, touchpad, camera), branch `cursor/mt7921-wifi` (`822f3a934c`), PR #4118, `docs/reviews/KERNEL-AMD64-REVIEW.md`, PR #4212 (merged into `main`), the WSL Haiku tree. Nothing was built or booted. The log folders of sticks 42 and later are not in the repository, so their syslog lines are taken from `CHANGELOG.md` only.

## Devices on the 5625U

| Device | Id | State | Needed |
|---|---|---|---|
| Wi-Fi, MediaTek MT7921 (AzureWave) | PCI `14c3:7961` | No driver in the image | Yes, first |
| Bluetooth on the same card | USB `13d3:3567` | `h2generic` loads no MediaTek firmware; air/OS `h2mediatek` does | With Wi-Fi |
| SD card reader, Realtek RTS5228 | PCI `10ec:5228` | PR #4118: built, not packaged, writes no chip registers | Low |
| Touchpad, ELAN2513 | I2C HID | Done: `input/touchpad/i2c/0` (#4197) | No |
| Touch screen (almost certainly) | I2C HID | Publishes `mouse`, two `tablet`, `keyboard` | Check first (see below) |
| Microphones (digital) | PCI `1022:15e2` (ACP) | No driver | Check first |
| Speakers, headphone | PCI `1022:15e3` (HDA) | Haiku HDA | Not checked here |
| Sensor Fusion Hub | PCI `1022:15e4` | No driver | Not now |
| Camera, HP True Vision 5MP | USB `04f2:b767` | Stream starts; no picture confirmed | Test only |
| AMD IOMMU | PCI `1022:1631` | No driver | Separate project (kernel review, finding 7) |
| Platform Security Processor | PCI `1022:15df` | Only the GPU path uses PSP | No |

The laptop is an **HP ENVY x360 2-in-1 15-ey0xxx** (`generated/boot-5625u-25/syslog`). A convertible with a touch screen and a tablet mode. That answers part of section 4.

### 1. Wi-Fi (MT7921)

There are two candidate sources. Read on GitHub, 12 Nov 2026; neither was built here.

| | air/OS `mt7922wifi` | RenkuOS `mediatekwifi` |
|---|---|---|
| Origin | Written for air/OS, MIT; comments follow Linux `mt7921` (the Linux mt76 driver is ISC) | OpenBSD `if_mwx.c` 1.46, ISC |
| Stack | FreeBSD `net80211` through Haiku's FreeBSD compat, like the other Haiku Wi-Fi drivers | OpenBSD `openbsd_wlan`, which Haiku also has |
| Ids | `0x7922`, `0x7961` (MT7921), `0x0616` | `0x7961`, `0x0608` |
| Firmware | Linux `linux-firmware` `mediatek/`, MediaTek licence (`LICENCE.mediatek`); air/OS script `docs/x399-workstation/tools/build-mediatek-wifi-firmware-package.sh` packages both MT7922 and MT7961 files | OpenBSD `mwx-firmware-20260309p0.tgz` |
| History | 29 commits in Sept 2026, scan, join, WPA keys, firmware rate control; tested on an MT7922, not on an MT7921 | Not read beyond the header |
| Known limit | Polls, no interrupt (`glue.c`) | HT20, firmware rate selection |

**Finding (air/OS):** `if_mtkvar.h:212-213` names only `WIFI_MT7922_patch_mcu_1_1_hdr.bin` and `WIFI_RAM_CODE_MT7922_1.bin`, while the part table lists `0x7961`. On this laptop it would load MT7922 firmware into an MT7921. The MT7921 files are `WIFI_MT7961_patch_mcu_1_2_hdr.bin` and `WIFI_RAM_CODE_MT7961_1.bin`. The fix is a firmware name per part, chosen in the part table. That is a small R5 change on top of the import, marked as an SPDX snippet. Whether the rest of the bring-up matches the MT7921 is not known until the 5625U boots it; Linux treats both as one family (`mt7921`), so it is likely.

**Advice: start from air/OS.** It uses the same stack as the Wi-Fi drivers Haiku already ships, it is MIT, it has been made to join and keep a key, and the same author's Bluetooth loader (section 2) covers this card. RenkuOS stays the fallback if the MT7921 does not come up. Do not take both.

Steps:

1. Licensing report before adding it, per `AGENTS.md`: air/OS MIT driver; the MediaTek firmware licence from `linux-firmware` (redistribution terms, copy into `LICENSES/` and `THIRD_PARTY_NOTICES.md`). Note that the driver comments name Linux functions: say whether code was copied from Linux or only behaviour followed. Linux mt76 is ISC, so either way it is not GPL, but it must be stated.
2. Import the driver into `distro/` with its MIT header. Add the per-part firmware names. Package only the MT7961 files R5 needs, plus MT7922 if the driver keeps that id.
3. Test only on the 5625U with a real access point. QEMU has no Wi-Fi device and usernet is not a LAN. The pass: the card attaches with the MT7961 firmware, a scan lists networks, a WPA2 join, DHCP, a download, the speed recorded. Watch the CPU load: the driver polls.

### 2. Bluetooth (13d3:3567)

The MediaTek Bluetooth part runs a bootloader after reset and does not answer HCI Reset until its firmware is loaded. Haiku's `h2generic` does not do that.

air/OS has the loader: `src/add-ons/kernel/drivers/bluetooth/h2/h2generic/h2mediatek.cpp` and `.h`, MIT. It only asks USB vendors from the Linux `btusb` MediaTek list, which includes `0x13d3` (AzureWave/IMC, this card). It reads the chip id and builds the name `BT_RAM_CODE_MT<chip>_1_<rev>_hdr.bin`, so an MT7961 gets `BT_RAM_CODE_MT7961_1_2_hdr.bin` from `linux-firmware`. Not built or booted here.

Advice: take it together with the Wi-Fi import, as a second commit, with the Bluetooth firmware file in the same firmware package. Bluetooth in R5 is otherwise basic (Haiku's stack); a pass is "the controller answers and the Bluetooth preferences find a phone", not audio or HID.

### 3. SD card reader (PR #4118)

The module builds. The 5228 attaches and writes no chip registers, because OpenBSD has no init for that chip and Linux `rts5228.c` was ruled out as a source. So a card is not read on this laptop. Advice: no merge into the image until a 5228 init sequence exists from a usable source. The PR can stay open, or land as a module that is not packaged (it already is not). It is not needed for daily use.

### 4. Touch

The touchpad is done. The open point is the **other** I2C HID device. After the combined stick it still published `input/mouse/i2c/0`, `input/tablet/i2c/0`, `input/tablet/i2c/1` and `input/keyboard/i2c/0`. The ENVY x360 has a touch screen with pen support, and two tablets (finger and pen) plus a mouse and keyboard collection is the usual shape of such a screen. So this node is almost certainly the touch screen. Its ACPI id is not in the logs in this repository.

Is this something we want now? Yes, but as a check, not a new driver. The device already has a driver (`i2c_hid`); what is open is what R5 does with it:

1. On the next stick: log the ACPI `_HID` of each I2C HID node, and touch the screen once with one finger.
2. If the pointer follows the finger, the screen works as an absolute pointer. Record it. That is enough for daily use.
3. If the pointer jumps or does not move, the likely cause is that the tablet collection is read but not mapped to the screen size. That is a small fix in the tablet path, not a new driver.
4. Multi-touch on the screen (two-finger scroll, pinch) and the pen with pressure are later work. They need the digitizer handling that the touchpad already got from air/OS, applied to a touch screen. Not for this round.

Tablet mode (the screen folded back) also needs the Sensor Fusion Hub (section 6) to rotate the picture and turn off the keyboard. Without it the laptop works as a laptop, and as a tablet only in landscape.

### 5. Microphones (ACP)

On Renoir/Cezanne laptops the built-in microphones are usually digital and run through the ACP (`1022:15e2`), not the HDA codec. The speakers run through HDA (`1022:15e3`). Check on the next stick whether the built-in microphone records anything. If not, an ACP DMIC driver is a separate project (Linux `snd-acp` is GPL-2.0 and not a source). `docs/MIXXX.md` already asks to record ACP vs HDA.

### 6. Sensor Fusion Hub

Accelerometer and lid angle, used for rotating the screen and for knowing that the laptop is folded into a tablet. On this 2-in-1 it matters more than on an ordinary laptop. Linux `amd_sfh` is GPL-2.0 and not a source; no fork was found with one. Also needs screen rotation in app_server, which R5 does not have. Not now; note it as a 2-in-1 item on the roadmap.

## CPU items (kernel)

All three are open in `docs/reviews/KERNEL-AMD64-REVIEW.md`. None needs a new driver.

### A. Leaf 1 ECX in `R5 CPU topology:` (why `pcid` is missing)

`distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp:902` prints leaf 1 EBX, `0x80000008` ECX and `0x8000001E` EBX. It does not print leaf 1 ECX. Stick 33 listed `invpcid` and not `pcid`. Add `leaf1 ecx` (raw, from `get_current_cpuid`) to the same line. Bit 17 is PCID. If the raw bit is set and the feature line still has no `pcid`, the kernel loses the bit between CPUID and its stored word, which is a bug in the feature table. If the raw bit is clear, the firmware or the CPU hides it and PCID work stops there.

Advice: PR #4212 is already merged, so this is its own small change. Put it in the same kernel branch as items B and C. It is one value in one line, and that branch's stick then also answers the PCID question.

### B. The `amd_pstates` race

`src/add-ons/kernel/power/cpufreq/amd_pstates/amd_pstates.cpp:66-82` (WSL tree, not in `distro/` yet). `set_normal_pstate()` runs on every CPU at once (`call_all_cpus_sync`) and writes CAP1 into the shared globals `sHWPLowest`, `sHWPEfficient`, `sHWPGuaranteed`, `sHWPHighest` before it reads them back for its own request. One CPU can take another CPU's values.

Fix: copy the file into `distro/kernel/...` with its MIT header, R5 change as an SPDX snippet. Use local variables in `set_normal_pstate()`. Keep the globals only if something else reads them, and then fill them once on CPU 0. Add one line per CPU: cpu, CAP1, and the request read back after the write. The pass on the 5625U is twelve lines, each with max equal to its own CAP1 (highest or guaranteed per `sAvoidBoost`). AMD only.

### C. The `memcpy` fix from air/OS (`6178b691d`)

Read in the WSL air/OS clone (`~/r5os/airos-pick`): `6178b691d2`, jmgasper, 27 Oct 2026. From 2048 bytes up, `memcpy` used `rep movsb`, which is about 14 times slower when the destination is less than 16 bytes from the source modulo the page size (bitmap rows copied a few pixels to the side; app_server hits it). In that window it now takes the SSE loop. Measured there on a Threadripper 1950X (Zen 1): distance 4, 2373 → 26276 MB/s; distances 0, 16, 64 and 4080 unchanged. Three files, 113 lines added: `memcpy.cpp` (+15), `src/tests/system/libroot/posix/string/memcpy_test.cpp` (94 lines, sizes 0–70000 at every distance modulo 64 and near page multiples, prints speed per distance) and the test `Jamfile` (+4).

The kernel links the same `memcpy.cpp`. Copy it into `distro/` with its MIT header and take the commit as it is. Run `memcpy_test` in QEMU as the correctness check. Run it once on the 5625U for the speed per distance, before and after: that is the timed copy the kernel review asks for, and it needs no extra tool. General AMD64, so also Intel.

The AVX2 body and the `setjmp` user copy (same finding) are later rounds, the second one with its own boot because it is a fault path.

## Advice: order

1. **Items A, B and C** in one small kernel branch from `main`, which already has the merged PR #4212. All three are small and have a clear pass line.
2. **One stick** for that branch. The same boot also proves what PR #4212 merged (the Zen SMT map and deep idle), which has not been booted on the 5625U yet.
3. **Wi-Fi and Bluetooth** as their own branch, from air/OS: licensing report first, then the driver with the MT7961 firmware names, then the Bluetooth loader, then the 5625U on a real network. This is the item that makes the laptop usable without a cable.
4. On the stick of step 2 or 3, also the **checks** for the touch screen and the microphone. No code beyond the logging.
5. The SD reader, the Sensor Fusion Hub and the IOMMU wait.

## Not proven

- The touch screen: the model has one, but the ACPI id of the node and whether a touch moves the pointer are not logged.
- Whether the built-in microphone uses the ACP.
- Whether air/OS `mt7922wifi` brings up an MT7921 beyond the firmware name. It was tested on an MT7922.
- The exact text of the MediaTek firmware licence. Not read for this review.
- The WSL air/OS clone (`~/r5os/airos-pick`) is older than GitHub: it has no `mt7922wifi`. Take the import from GitHub.
