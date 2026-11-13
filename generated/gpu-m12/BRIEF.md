# M12 brief: the GPU draws the windows (11 Nov 2026)

For Cursor. It does not override `AGENTS.md` or `docs/GPU-ARCHITECTURE.md`. Read those first, plus `generated/gpu-m11/BRIEF.md` (the two modes, the window buffers, the fallback), and `docs/GPU-ARCHITECTURE.md` "Release mechanism" (the model for the Skia recipe).

**Start after the M11 stick has passed.** Steps 0–5 need no GPU (Lavapipe in QEMU or VirtualBox) and can be written before that; nothing of M12 goes on a stick before M11 has passed.

**M12 is done when, on the 5625U:**
- in the GPU mode, the GPU draws everything: every drawing operation app_server offers, text, bitmaps and offscreen `BBitmap` views, through Skia on Vulkan (RADV);
- the drawing-compare set passes for every operation (GPU against Painter, within the tolerance from step 0);
- the GPU mode looks approximately the same as the CPU mode: the same layout, colours, fonts and decorator;
- drawing time and CPU time are measured for both modes and recorded;
- when the GPU fails, the desktop switches to the CPU mode (Painter) without a freeze, as in M11;
- without a supported GPU (VirtualBox), the same Skia path runs on Lavapipe, and its draw time is measured and recorded;
- in the CPU mode (the emergency fallback), app_server draws with Painter exactly as before.

---

## What exists today (after M11)

- **The two modes from M11.** GPU mode, on RADV or on Lavapipe when there is no GPU: each window has its own buffer, and the GPU composes. But Painter still draws each window's content into that buffer on the CPU. CPU mode: Haiku's app_server, only as the emergency fallback.
- **Painter (AGG)** behind the `DrawingEngine` interface draws lines, rectangles, round rectangles, ellipses, arcs, polygons, Bézier curves, `BShape`, five gradient types, bitmaps (scaled, filtered), the `B_OP_*` drawing modes, clipping regions, `BAffineTransform`, `BAlphaMask`, and text (FreeType through `ServerFont`).
- **app_server is a Vulkan client of RADV (M11),** with Lavapipe for development.
- **Skia is not in the tree.** air/OS builds Skia on Haiku for its browser (licence of their recipe unclear: read only).

---

## Decisions (fixed for this brief)

1. **All or nothing.** In the GPU mode the GPU draws everything, and Painter draws nothing; in the CPU mode Painter draws everything. No mixing per window or per operation, and no "this one on the CPU". So the GPU drawing only goes on when every operation passes the compare set. Until then it is behind a test switch (`gpu_draw true`). After the stick passes, it becomes part of the GPU mode by default, with the same opt-out as M11 (`gpu_compositor false` returns to the CPU mode). This replaces "only if measurements show it is faster" in `docs/GPU-ARCHITECTURE.md` "2D in app_server" step 3. Step 0 writes that edit.
2. **Skia, with its Vulkan backend.** BSD-3-Clause; it combines with GPL-3.0 and MIT.
   - Before it is added, step 0 reports, as `AGENTS.md` asks: project, SPDX, copyright, the exact version, which of its bundled libraries are built, integration, source-offer duties, and combination questions. Keep the build minimal: FreeType and the image codecs R5 already ships; no ICU unless step 0 shows it is needed.
   - Step 0 also chooses Ganesh or Graphite, with the reason.
   - Pinned to one version, built from **R5's own recipe in this repository**, with a version contract and update steps in `docs/GPU-ARCHITECTURE.md`, as for Mesa.
3. **Skia stays hidden.** It is linked only by app_server (statically or as a private library: step 0 decides). No Skia header in a public header, nothing in `libbe`, nothing in the `libr5*` libraries. Applications see only the Haiku API. That is the BeOS way: Skia is an engine under the hood, like Mesa.
4. **`SkiaDrawingEngine`** behind the same `DrawingEngine` interface, drawing into the M11 window buffers through Vulkan. New R5 files are GPL-3.0-or-later.
   - Step 0 lists **every** `DrawingEngine` call and Painter feature with its Skia mapping. Where Skia has no direct equivalent (for example `B_OP_INVERT`, `B_OP_SELECT`, `B_OP_ERASE`, the diamond gradient), the mapping is a Skia runtime effect or blender.
   - A call without a mapping blocks the GPU drawing; it is never quietly drawn on the CPU.
5. **Text.** Skia's glyph cache, on FreeType, with the same font files. Hinting, antialiasing and subpixel rendering come from the Antialiasing tab of Appearance. Text metrics (`StringWidth`, escapements, line heights) stay `ServerFont`'s, so layout is identical in both modes. Only the rasterisation of the letters can differ slightly.
6. **"Approximately the same".** Step 0 sets a tolerance per class of operation, and the compare set holds every operation to it:
   - solid fills and copies: exact;
   - anti-aliased edges and text: within a stated maximum per pixel and a stated share of pixels.
   The decorator, colours, sizes and layout are the same in both modes by construction.
7. **Offscreen `BBitmap` views** (`B_BITMAP_ACCEPTS_VIEWS`) are drawn by the GPU too, into a buffer the CPU can read. app_server finishes the GPU work for that bitmap at `BView::Sync()`, and before any read of its pixels by app_server itself (`ReadBitmap`, drag bitmaps, screenshots). Haiku programs must already call `Sync()` before they read the bits, so a correct program sees no change. A program that reads without `Sync()` already races in Haiku. Step 0 names test programs that do this.
8. **Fallback** is M11's: on GPU loss, a failed submit or a fence timeout, the whole desktop switches to the CPU mode for the rest of the session, every window is redrawn by Painter, and one line is logged: `app_server: gpu draw off: <reason>`.
9. **Measurement.** Both modes over the same scenario, and the GPU mode on RADV and on Lavapipe (5625U and VirtualBox): median draw time per frame, CPU time. Lavapipe being slower than Painter is recorded, not a reason to change the path. The GPU mode stays the default even when it is slower on one operation. The result lists the operations that were slower, so they can be improved. It is not a reason to draw them on the CPU (point 1).

**References (read, do not copy):** air/OS Summit's Skia build on Haiku (licence unclear). Skia's own documentation and examples (BSD-3, may be used with the notice).

---

## Steps

| Step | What | Where | Pass line |
|---|---|---|---|
| 0 | **Design on paper**, `docs/APP-SERVER-GPU-DRAWING.md`: the Skia component report (decision 2); Ganesh or Graphite; static or private library; the full mapping table of `DrawingEngine` calls and Painter features; the tolerance per class; text; offscreen bitmaps and `Sync()`; the compare set; the measurement; the edits to `docs/GPU-ARCHITECTURE.md` and `THIRD_PARTY_NOTICES.md`. The user reviews it, and approves Skia, before code | – | – |
| 1 | **The Skia recipe** for x86_64 in the repository, the version contract, and a host test that draws one frame on Lavapipe. arm64 and riscv64: state that they were not built | WSL build, VirtualBox | `skia: <version> ok lavapipe frame` |
| 2 | **The compare program** `r5-draw-compare`: every operation from the mapping table, drawn by Painter and by `SkiaDrawingEngine`, compared with the tolerance. Prints one line per operation | VirtualBox with Lavapipe | `draw-compare <op> ok` per operation; summary `draw-compare <n>/<n> ok` |
| 3 | **`SkiaDrawingEngine` part 1:** fills, rectangles, lines, round rectangles, ellipses, arcs, bitmaps (scaled, filtered), clipping regions, all `B_OP_*` modes | VirtualBox with Lavapipe, `gpu_draw true` | those operations `ok` in the compare set |
| 4 | **Part 2:** polygons, Bézier curves, `BShape`, the five gradient types, `BAffineTransform`, `BAlphaMask`, and **text** | the same | the compare set complete: `draw-compare <n>/<n> ok` |
| 5 | **Offscreen bitmaps and `Sync()`**, the fallback to the CPU mode (`gpu_draw_fail true`, test switch), and the measurement | VirtualBox with Lavapipe; the test programs from step 0; the M11 scenario for 60 s | `app_server: gpu draw off: forced failure` exactly once; offscreen test `ok`; `app_server: draw median <n> us cpu <n> ms per 10 s` recorded in `CHANGELOG.md` |
| 6 | **The stick** (one, after M11 has passed) | 5625U | below |

PRs: step 0 alone (documentation). Step 1 alone (the recipe). Steps 2–5 depend on each other: **one PR**, one commit per step, from `origin/main`, not stacked.

---

## Stick (step 6)

**Before:** steps 0–5 merged; the pre-stick check on paper; the image fully built before anything copies it; the settings file planted; `check.sh` and `boot2.sh` in `/boot/home`; a short card.

**Boot 1:** the M11 settings plus `gpu_draw true` and `gpu_debug true`.
1. Sign in. The desktop must come up as usual, with the R5 decorator.
2. Scenario, 60 s: the M11 scenario plus 20 s of WebPositive on a long page, and the Appearance page with every tab opened once.
3. Run `r5-draw-compare` on RADV.
4. `sh ~/check.sh` prints PASS or FAIL per line:
   - the M11 lines;
   - `app_server: gpu draw on <device> skia <version>`;
   - `draw-compare <n>/<n> ok`;
   - `app_server: draw median <n> us cpu <n> ms per 10 s` at least three times;
   - no `gpu draw off`.

**Boot 2:** the same settings with `gpu_compositor false` (the CPU mode). The same scenario, for the comparison: `app_server: draw cpu median …` lines. Photos of the same windows in both modes.

**Boot 3:** the M11 settings plus `gpu_draw true` and `gpu_compositor_lavapipe true`. The same scenario on Lavapipe.

**Result in `CHANGELOG.md`:** the medians and CPU times of RADV, Lavapipe and the CPU mode side by side, plus the VirtualBox numbers from step 5, the operations that were slower on the GPU, and the photos compared in words.

**Failed:** a frozen desktop, any `draw-compare <op> FAIL`, a missing M11 line, wrong text layout, or more than one `gpu draw off` line in a boot.

---

## Lessons (apply to every step)

The list in `generated/gpu-m11/BRIEF.md`, plus:
- A new third-party component is reported to the user and approved before it enters the build.
- Skia's version changes only through the recipe and its update steps, never by a drive-by bump.
- Never "this one operation on the CPU". A gap blocks the GPU drawing and is reported.

---

## Not in M12

- Changes to the Haiku drawing API or `libbe`.
- Skia for applications, or a Skia-based kit. A later Graphics Kit decision, not this one.
- GPU-rendered letter outlines (Slug-style), colour fonts beyond what FreeType already does.
- DCN, vsync, scanout planes, more than one screen.
- Skia's own CPU raster backend as another software path. Only if the Lavapipe numbers ask for it, and only after a user decision.

**PR titles:** "Area - what changes for the user", in plain English. Example: "Graphics - The GPU draws your windows".
