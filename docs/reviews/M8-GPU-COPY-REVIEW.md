# Review: GPU Desktop Copy Fallback (M8 Stick Boot 41)

This review documents the root-cause investigation of why `app_server` fell back to CPU rendering on the Ryzen 5 5625U (Renoir, PCI `1002:15e7`) during M8 stick boot 41 (commit `cd7ee483`), even though `gpu_app_server true` and `gpu_debug true` were planted in `/boot/home/config/settings/kernel/drivers/radeon_hd`.

---

## 1. Observed Behavior on Hardware

During the 45-second window movement test (`m8-run`):
- `wsi.txt` confirmed RADV and Vulkan were functional: `wsi clear ok AMD Radeon Graphics (RADV RENOIR)`. Both window tests ran (`win-teapot.txt`, `win-vkcube.txt`) with `device lost no`.
- The self-test completed with `fence ok`, `gpu read deadbeef`, `selftest done`.
- The kept `syslog` contained 13 periodic reports from `app_server`:
  ```text
  USER: app_server: copy cpu median 0 us cpu 21 ms per 10 s
  USER: app_server: copy cpu median 0 us cpu 4 ms per 10 s
  USER: app_server: copy cpu median 1 us cpu 8 ms per 10 s
  ...
  ```
  It contained zero occurrences of `copy gpu median`, zero `gpu copy on`, and zero `gpu copy off`.
- Every frame copy during the surviving log was performed on the CPU.

---

## 2. Bug 1 (Primary Cause): Address Space Confusion in `amd_scanout()`

### Source Location
- **File**: `distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp`
- **Function**: `amd_scanout()`

### Code Path
In `amd_scanout()`:
```cpp
uint32 fb = gpu_mmio(device, kGcSegment, 0x096b) & 0x00ffffffu;
uint64 base = (uint64)fb << 24;
uint64 phys = (uint64)shared->frame_buffer_phys;
if (phys < base || (phys & 4095ull) != 0)
    return B_ERROR;
uint64 offset = phys - base;
uint64 vram = 0;
if (amd_vram_bytes(info, &vram) != B_OK || offset > vram
    || mapped > vram - offset)
    return B_ERROR;
```

### Physical Memory Discrepancy on Renoir (5625U)
On AMD APUs, the CPU-visible PCI BAR aperture and the memory-controller carveout reside in separate address spaces:
1. `gpu_mmio(..., 0x096b)` reads `MC_VM_FB_OFFSET` (`0x810`), representing the system physical RAM base of the carveout (`0x810000000`, ~32.25 GiB).
2. `shared->frame_buffer_phys` is set in `radeon_hd.cpp` from `mapAddr` (PCI BAR 0 aperture base `barPhys` plus `reservedOffset`). On this machine, PCI BAR 0 is located at `0xfce0000000` (~1011.5 GiB in the 64-bit PCI MMIO aperture).
3. `phys` and `base` belong to different physical address spaces. Subtracting them (`offset = phys - base`) produces:
   $$\text{offset} = \mathtt{0xfce0000000} - \mathtt{0x810000000} = \mathtt{0xf4d0000000} \approx 979\text{ GiB}$$
4. With total VRAM aperture reported as 256 MiB (`0x10000000`), the check `offset > vram` evaluates to `true`.
5. `amd_scanout()` returns `B_ERROR`, causing `ioctl(sFd, R5_GPU_SCANOUT_BO, &scan)` in `GPUCopy::Copy()` to fail.

### Resulting Action in `GPUCopy.cpp`
On the very first frame copy:
```cpp
if (ioctl(sFd, R5_GPU_SCANOUT_BO, &scan) != 0) {
    off("scanout refused");
    return false;
}
```
`off("scanout refused")` sets `sOff = true` and logs:
```text
app_server: gpu copy off: scanout refused
```
Because `sOff` latches permanently, every subsequent call to `GPUCopy::Copy()` returns `false` immediately without logging, leaving all rendering to the CPU fallback and accumulating CPU time in `GPUCopy::NoteCpu()`.

---

## 3. Bug 2 (Latent Stride Bug): Unaligned BackBuffer Pitch

### Source Location
- **File**: `distro/app_server/GPUCopy.cpp`, `GpuBackBuffer::BytesPerRow()` & `GPUCopy::Copy()`
- **File**: `distro/graphics/src/add-ons/accelerants/radeon_hd/display.cpp`, `display_crtc_fb_set()`

### Mechanism
- `GpuBackBuffer::BytesPerRow()` hardcoded:
  ```cpp
  uint32 GpuBackBuffer::BytesPerRow() const { return fWidth * 4; }
  ```
- On DCE 2 and later (or hardware requiring pitch alignment), `display.cpp` aligns `widthAligned = (width + 63) & ~63` and stores `shared_info->bytes_per_row = widthAligned * 4`.
- In `GPUCopy::Copy()`:
  ```cpp
  if (front->BytesPerRow() != back->BytesPerRow() || front->Width() != back->Width() || ...) {
      off("pitch");
      return false;
  }
  ```
- On resolutions where width is not a multiple of 64 (e.g. 1366x768 where `widthAligned = 1408`, `bytes_per_row = 5632`), `front->BytesPerRow()` (5632) does not match `back->BytesPerRow()` (5464). `GPUCopy::Copy()` would latch `off("pitch")`.
- On this specific test machine with a 1920x1080 panel, `1920 % 64 == 0` and `r5_display_is_dcn()` set `bytes_per_row = 1920 * 4 = 7680`. The pitch matched, so Bug 1 (`scanout refused`) triggered before this could take effect.

---

## 4. Why Early Log Lines Were Absent

- Haiku/R5 maintains a circular syslog buffer with a 512 KiB limit per file before rotating from `syslog` to `syslog.1`.
- When `gpu_debug true` is enabled, the driver emits debug lines for every indirect buffer and fence submission (`user job fence e flush 1 ...`).
- Running the window test suite (`win-teapot`, `win-vkcube`) and self-test generated thousands of fence log lines in a few seconds, quickly exceeding 512 KiB and forcing a log rotation.
- `syslog.1` only starts at timestamp 46.7 s (`USER: r5 window tests`). The early boot lines—including the boot loader banner, `GFX9 scratch 0xdeadbeef`, `k2 ok gart`, and the one-shot `app_server: gpu copy off: scanout refused` line—were discarded by log rotation.

---

## 5. Other Investigated Hypotheses (Non-Bugs)

1. **`gpu_caller_is_app_server()` team verification**:
   - `load_image` via `BRoster::Launch` and `launch_daemon` stores the resolved path (`/boot/system/servers/app_server`) in `team_info.args`.
   - `gpu_caller_is_app_server()` matches against both `"/system/servers/app_server"` and `"/boot/system/servers/app_server"`.
   - The team check succeeded during standard boot and did not return `B_NOT_ALLOWED`.

2. **Settings path divergence**:
   - Both `load_driver_settings("radeon_hd")` and `GPUCopy::load_settings()` resolve to `/boot/home/config/settings/kernel/drivers/radeon_hd`.
   - The 13 `app_server: copy cpu median` log lines proved `gpu_debug true` was read; `gpu_app_server true` was situated in the same file and parsed into `sWant = true`.

3. **VirtualBox behavior**:
   - VirtualBox emulates a standard VESA/VBoxSVGA card without `/dev/gpu/radeon_hd/0`.
   - `open_node()` in `GPUCopy.cpp` immediately exits with `off("no render node")` on VirtualBox, which is why VirtualBox could only verify the CPU median accounting and never reached `R5_GPU_SCANOUT_BO`.
