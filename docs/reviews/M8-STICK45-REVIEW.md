# Review of M8 stick 45, boot 1 (11 Nov 2026)

For Cursor. Branch `cursor/m8-stick` (`fb80628b4a`). Checked against the stick 45 capture (`check.txt`, `m8-run.txt`, `syslog.txt`, `r5-syslog.txt`, `gpu.txt`), the code on that branch, and Haiku `src/servers/app/drawing/HWInterface.cpp`.

**Verdict: the GPU copy now reaches the screen. Boot 1 still failed, but on the check and the log, not on the copy.** Fix points 1–4 in one PR from `origin/main`, one commit per point. Then one stick, boot 1 only. Boot 2 waits until boot 1 passes.

## Earlier findings (`M8-STICK43-REVIEW.md`)

All five are done on `cursor/m8-stick`, and this boot confirms the first one:
- Control word `0x80000000`: `b5 ok … word deadbeef`, and `copy compare ok` on the first frame.
- Full CPU repaint when the GPU copy turns off: in the code (`GPUCopy::TookOff`). Not seen on screen, because there is no photo of this boot.
- Compare on frame 1: in the code, and it passed.
- `b5 ok` in `check.sh`: passes.
- Stick 43 recorded in `CHANGELOG.md`.

## What the log shows

| Line | Meaning |
|---|---|
| `b5 ok 1 ms … word deadbeef` | The new control word writes |
| `copy compare ok` (first report) | The first copies, frame 1 included, match |
| `copy compare MISMATCH at 92 byte 412 back 33 33 33 33 … front 70 70 70 e6` | One later compare failed, so the GPU copy turned off |
| `copy gpu median 281 us cpu 860 ms per 10 s`, `271 us cpu 270 ms` | Per copy, about 270 µs, all of it waiting for the fence |
| `copy cpu median 0 us cpu 8–29 ms per 10 s` | The CPU copy after the fallback |
| The kept syslog starts at t = 45 s; 6688 of its 7694 lines are `submit ib va …` | The log rotated, and the start of the boot is gone |

The front value `70 70 70 e6` is a value that was in the back buffer earlier. The GPU copy wrote that pixel; it holds an older picture, not the white boot picture of stick 43.

## 1. The compare checks pixels that other windows are drawing (blocking)

`GPUCopy::Copy` turns the dirty region into **full-width bands** and `same_picture` compares those whole rows. In Haiku, windows draw into the back buffer at the same time: drawing takes only the shared lock (`LockParallelAccess`). `CopyBackToFront` holds `fFloatingOverlaysLock`, which the drawing threads do not take. So while one copy and its compare run (about 270 µs), another window can draw on the same rows, outside the copied region. The compare then finds the new pixel in the back buffer and the older one on the screen. Here that was row 92, from x = 103 (`byte 412`) on: back `33 33 33 33`, front `70 70 70 e6`. That is not a copy error, and the screen is right for the region that was copied.

**Fix:**
- `same_picture` compares only the rectangles of the region (`region.RectAtInt(i)`, the same region `Copy` received), not the bands. The copy itself stays band by band.
- The MISMATCH line says whether the pixel was inside a copied rectangle. Since the compare now covers only those rectangles, it is always inside. Add the rectangle as well: `in rect <l>,<t>-<r>,<b>`.

### Why not a CPU cache flush first

Cursor proposed: the GPU read old bytes from memory because the new pixels were still in the CPU cache, so `clflush` the copied rows of the back buffer before each submit. That does not fit this boot:
- `b5` does not test this. Its source is VRAM in the carve-out, not GTT: `gpu_stage_dma(info, "b5", 0xffff800000041000ull, false)`, where the last argument is `srcGtt`, and the source PTE in the log is `06000008110b1071`. That `b5` flushes and passes says nothing about the back buffer.
- The back buffer is snooped: `bo domain 2 … snooped 1`, and system PTEs carry the snoop bit (`b2` PTE `…236ad077`, bit 2 set). With snooping the GPU looks in the CPU cache itself.
- Painter draws and asks for the copy right away. Without working snooping the new pixels would almost always still be in the cache, and almost **every** compare would fail, frame 1 first. Frame 1 passed, and so did the compares after it, until one failed. That fits a rare clash with another window, not a cache problem.
- A flush of the full stride of all copied rows is up to 8 MB per copy, at hundreds of copies per second. It would go straight into the measurement the GPU path already loses (point 4).

If a MISMATCH comes back **inside** a copied rectangle after this fix, the cache explanation is live again. Then the next step is the flush, or the snoop bits of that mapping, on its own stick.

## 2. The GPU copy can erase the mouse pointer (blocking)

Haiku takes the cursor out of the copy region (`region.Exclude(_CursorFrame())` in `HWInterface::CopyBackToFront`) and then draws it onto the front buffer with `_DrawCursor(area)`. The GPU copies **full-width bands**, so it also copies over the cursor. When the cursor sits on those rows but outside `area`, `_DrawCursor` does not redraw it, and the pointer is gone until the next mouse move.

**Fix:** in `AccelerantHWInterface::_CopyBackToFront`, after a successful `GPUCopy::Copy`, call `_DrawCursor` for the bounding rectangle of the bands. `_DrawCursor` is a protected `HWInterface` method, and `fFloatingOverlaysLock` is already held there. `GPUCopy::Copy` gives the band bounds back through a new output parameter, or a new call that returns them; keep the existing behaviour for callers otherwise.

## 3. `gpu_debug` floods the log and skews the measurement (blocking)

`gpu_debug true` switches on two things: the compare and measurement in app_server, **and** one kernel line per submit (`submit ib va …`, `r5_gpu_core.cpp` line 1457). With hundreds of copies per second, `syslog_daemon` rotates twice, and the start of the boot is gone. That alone gives these FAIL lines: `boot marker`, `GFX9 scratch 0xdeadbeef`, `fence landed`, `k2 ok gart`, `gpu copy on scanout`. Those are not proven failures; they are lost lines. The `dprintf` is also inside the timed GPU path, which is part of why one window shows `cpu 860 ms`.

**Fix:**
- A new settings key for app_server only, `gpu_copy_debug true`, read by `GPUCopy` instead of `gpu_debug`. It turns on the compare and the measurement, nothing in the kernel. Document it next to the other keys (`distro/gpu/COMPAT`, `docs/APP-SERVER-GPU.md`).
- The M8 boot files (`distro/graphics/boot/radeon_hd`, `m8-boot2`) use `gpu_copy_debug true` and drop `gpu_debug true`. `m8-boot2` checks for the new key.
- `check.sh` stays as it is; with a quiet log it reads the whole boot again.

## 4. Explain the Mesa package change (should)

Commit `7279f65f41` "Fail the stick check when b5 misses" also replaces `mesa_radv-25.3.6-5-x86_64.hpkg` (4120025 → 4126507 bytes). The message does not mention it. Say in the PR why that package changed and from which recipe change. If it was not on purpose, restore the earlier package in its own commit.

## What to expect from the speed rule

- CPU copy: median 0–1 µs, 8–30 ms CPU per 10 s. Most copies are small pieces.
- GPU copy: about 270 µs per copy, because each copy waits for its fence. Point 3 takes the log line out, but the wait stays.

"The median copy time is not worse than the CPU path" cannot pass with a synchronous wait per small copy. Under the brief (decision 7), M8 still passes when the path works and the numbers are recorded; `gpu_app_server` then stays opt-in. Write that result honestly when boot 2 has run. Do not tune the measurement to pass.

## Next stick

Pre-stick check on paper as usual. Boot 1 only, with `gpu_app_server true` and `gpu_copy_debug true` (no `gpu_debug`). Pass:
- `boot marker`, `GFX9 scratch 0xdeadbeef`, `fence landed`, `k2 ok gart` (the log is complete again);
- `b5 ok`;
- `gpu copy on scanout`;
- `copy compare ok`, never `copy compare MISMATCH`, no `gpu copy off`;
- `copy gpu median` at least three times;
- by eye: the pointer stays visible while Tracker and Terminal windows move.

If a MISMATCH still comes: paste the line with `in rect …` back. Do not add the flush on the same stick.

## Not proven

- That the clash with another window is the cause. It is reasoned from the Haiku locking and the values in the log. The next stick shows it: with the compare limited to the copied rectangles, the MISMATCH should be gone.
- That the full CPU repaint after the fallback fixed the screen on this boot. There is no photo.
- The lost lines of point 3 most likely passed (stick 43 had them), but this boot does not show it.

## Not in this round

Do not change the control word, the GFX9 ring, the fence order, the page tables, or the scanout call.
