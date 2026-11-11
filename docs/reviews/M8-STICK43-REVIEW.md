# Review of M8 stick 43, boot 1 (11 Nov 2026)

For Cursor. Branch `cursor/m8-stick` (`5bc72190d4`). Checked against the stick 43 capture (`check.txt`, `syslog.txt`, `r5-gpu-selftest.txt`, `gpu.txt`, `m8-run.txt`), a photo of the screen, and the code on that branch.

**Earlier findings (`M8-GPU-COPY-REVIEW.md`, stick 41):** both bugs are fixed on `cursor/m8-stick` and confirmed by this boot. The scanout call now uses the BAR offset (`scanout bar+0x0`, `gpu copy on scanout`). The back buffer has the scanout pitch (`pitch 7680` on both sides). The syslog rotation that hid the early lines is fixed too (`boot marker` passes). Nothing from that review stays open.

**Verdict: M8 boot 1 failed. The cause is found and is in the code, not in the hardware.** Fix points 1–3 in one PR from `origin/main`, with one commit per point. Points 4 and 5 go in the same PR. Then one stick (boot 1 and boot 2 on the same stick).

## What the screen showed

Mostly the white boot picture. Scattered squares of desktop blue, a long arc of them on the right, and loose pieces of the first-start language window ("…ating System", "US-International"). The squares and the arc are the mouse trail: see point 2.

## What the log shows

| Line | Meaning |
|---|---|
| `scanout bar+0x0 1920 x 1080 pitch 7680`, `gpu copy on scanout 1920x1080 pitch 7680` | The scanout buffer and the pitch are right. Stick 41's pitch fix holds |
| 100 × `submit ib … dw 7`, all fences back | The GPU runs each copy packet and finishes it |
| `copy compare MISMATCH at 1078 byte 0 back 98 66 33 ff … front ff ff ff 00` | After 100 copies the screen still holds the white boot picture. Nothing reached it |
| `copy gpu median 281 us` | The GPU reads the source (that takes time) |
| `b5 miss 1 ms … word 00000000` (self-test, same boot) | The kernel DMA stage with the **same control word** also wrote nothing |
| `b1 ok … word deadbeef phys 00000008110b1000`, `b3 ok …` | A GPU write into the VRAM carve-out through VMID 1 does work |
| `k2 dma pattern 262144 cafe 0 other 0` | A `DMA_DATA` with destination select 0 does work |

Everything else passes: the GFX9 contract lines, K2 GART, WSI clear, the VulkanTeapot and vkcube window tests, no `device lost`.

## 1. The copy packet's destination is "nowhere" (blocking)

`GPUCopyBands.h` line 126 writes control word `0x60200000`:
- bits 30:29 = 3: source through L2;
- bits 21:20 = **2**: destination select 2.

Linux `soc15d.h` names destination select 0 (memory, DAS), 1 (GDS) and 3 (memory through L2), and does not name 2. Older Mesa `sid.h` (before the register tables moved to generated files, so it is not in our 25.3.6 copy) has `V_411_DST_NOWHERE 2 /* new for GFX9 */`. Mesa uses that value for L2 prefetch: the CP reads the source and writes nothing. Do not rely on the Mesa citation alone. The proof is in this boot: the copy (MISMATCH) and the kernel stage `b5` (`miss`, `word 00000000`) both use this word, and both wrote nothing. The fill that works (`radeon_gfx9_dma_ib`, K2) uses destination select 0.

The code already doubted this. `r5_gpu.cpp` line 1031 says "Whether 2 should be 3 waits for its own diagnostic boot", and `r5_stage_p.h` line 291 says "DST_SEL 3 is L2. The dump had 2, which soc15d.h does not name". `docs/APP-SERVER-GPU.md` line 39 still took the word because "the DMA stage already ran" it. That stage ran, but it never passed.

**Fix:**
- The copy word becomes `0x80000000`: `CP_SYNC`, source select 0, destination select 0, engine 0. Both ends then go straight to memory, the same select as the proven K2 fill. Source select 0 is chosen on purpose. The back buffer is snooped GTT that the CPU rewrites every frame, and reading it through L2 could return lines from the previous copy unless L2 is invalidated first. Destination select 3 (through L2) is not chosen, because then the fence must write L2 back before the CPU compare or the display sees the data, and that is not proven on this machine.
- Use the same word in the kernel stage `b5` (`r5_gpu.cpp` 1031–1036) and in `r5_stage_p.h` 291–292. Remove both "waits for a diagnostic boot" comments and say what this boot showed.
- `check-pm4.py`: reject destination select 2 everywhere, with no exception for this word.
- `docs/APP-SERVER-GPU.md` line 39: the new word and the reason, with this boot as the evidence.
- The `GPUCopyBands` host test checks the new word.

## 2. When the GPU copy turns off, the screen is never repaired (blocking)

`AccelerantHWInterface.cpp` 1404–1408: when `GPUCopy::Copy` returns false, the CPU copies only the **current** dirty region. Everything the GPU "copied" before that (here 99 frames: desktop, wallpaper, the first-start window) is never copied again. The screen stays wrong until each part happens to be redrawn. On the photo, that means the cursor trail and a few window pieces. The brief requires "the desktop keeps running on the CPU copy". A desktop that shows the boot picture is not that.

**Fix:** on the call where the GPU path turns off, the CPU copies the **whole** back buffer to the front buffer once (the full frame, not the region). This covers every reason: compare, fence timeout, submit, forced failure. Give `GPUCopy` a way to say "I turned off just now" (for example a separate `bool GPUCopy::TookOff()` that is true once), and keep `Copy`'s signature. That full copy does not count in the CPU median (it is not a scenario frame). Log nothing extra; the existing `gpu copy off: <reason>` line stays the only line.

Boot 2 (`gpu_app_server_fail true`) did not catch this, because the forced failure happens on the first submit, before the GPU path has been "on" for any frame.

## 3. Compare the first copy too (blocking)

The compare runs only on every 100th frame. A path that never writes therefore shows a wrong desktop for 100 frames (here 1.4 s, and the whole start-up picture) before anyone notices. With `gpu_debug true`, also compare frame 1, then every 100th as now. Together with point 2, a broken GPU path then costs one frame.

Do not compare every frame: the brief's measurement must stay comparable.

## 4. `b5` must count as a failure line (should)

`b5 miss` was in the self-test of stick 43 and of earlier sticks, and nothing failed. Add one line to `check.sh`: `PASS b5 ok` when the syslog has `b5 ok`, otherwise `FAIL b5 ok`. With point 1, `b5` should pass on the next stick. That proves the word before app_server uses it, in the same boot.

## 5. Record stick 43 (should)

`CHANGELOG.md`, newest first: stick 43 boot 1 failed; the cause (destination select 2 writes nothing, proven by `b5 miss` in the same boot); the second mistake (the fallback did not repaint the screen); the fixes. State that boot 2 did not run. No result for the speed rule yet.

## Next stick

Pre-stick check on paper as usual. Same two boots as the brief. Pass lines for boot 1, in addition to the current `check.sh`:
- `b5 ok`;
- `copy compare ok` at least once, the first one within the first 10 s;
- no `copy compare MISMATCH`, no `gpu copy off`;
- the photo test: after Sign in, the desktop, Deskbar and Tracker are drawn completely.

If boot 1 still shows a MISMATCH: with point 2 the desktop must still look right after the fallback. Then paste the `copy compare MISMATCH` line and the `b5` line back. Do not guess at a third word on the stick.

## Not proven

- That destination select 0 reaches the scanout in the carve-out. That is reasoned from `b1`/`b3` (VMID 1 writes into the carve-out) and from the K2 fill (select 0 writes). The next stick proves it.
- Whether source select 0 reads the snooped GTT back buffer correctly. The K2 fill uses source select 2 (data), so no source address has been read this way yet. The first-frame compare (point 3) shows it at once.
- The 281 µs median measured a read without a write. It says nothing about the real copy time. Measure it again.

## Not in this round

The login screen work (separate, with another tool) does not touch these files. Do not change the GFX9 ring, the fence order, or the scanout call.
