# The compositor in app_server

This is the design for M11, step 0. It follows `generated/gpu-m11/BRIEF.md` (as updated on 12 Nov 2026) and `docs/GPU-ARCHITECTURE.md`, "2D in app_server". It is paper only: nothing here is built yet. Where the source disagreed with the brief, the point is under [Decided after step 0](#decided-after-step-0-12-nov-2026).

Source read: the Haiku tree in WSL (`src/servers/app`, commit `4aae8640c9`, 7 Nov 2026), with the R5 copies from `distro/app_server` and `distro/desktop/src/servers/app` applied. Paths below are relative to `src/servers/app` unless they start with `distro/`.

## Today's draw and update path

### Locks

| Lock | Where | Kind | Held by |
|---|---|---|---|
| `Desktop::fWindowLock` | `Desktop.h` | `MultiLocker` | `LockSingleWindow()` = read, `LockAllWindows()` = write |
| `HWInterface` itself | `drawing/HWInterface.h` (`class HWInterface : protected MultiLocker`) | `MultiLocker` | `LockParallelAccess()` = read (drawing), `LockExclusiveAccess()` = write (mode change, `FrameBufferChanged`) |
| `HWInterface::fFloatingOverlaysLock` | `drawing/HWInterface.h` | `BLocker` | cursor and drag bitmap |
| `Desktop::fDirectScreenLock` | `Desktop.h` | `BLocker` | `BWindowScreen` (`AS_DIRECT_SCREEN_LOCK` in `ServerApp.cpp`), taken with a 1 s timeout |
| `Decorator::fLocker` | `decorator/Decorator.h` | read/write | `RegionAt`, `GetFootprint` |

The order today is always `fWindowLock` first, then the `HWInterface` lock. `DrawingEngine::LockParallelAccess()` is only `fGraphicsCard->LockParallelAccess()`.

### A window draws

1. `ServerWindow::_MessageLooper()` (`ServerWindow.cpp`) takes `fDesktop->LockSingleWindow()` for each message, or `LockAllWindows()` when `_MessageNeedsAllWindowsLocked(code)` says so.
2. When `fRedrawRequested` is set, it calls `Window::RedrawDirtyRegion()`. That calls `_DrawBorder()` (the decorator, into `fDirtyRegion ∩ border ∩ fVisibleRegion`, with copy-to-front off, then `CopyToFront` of that region) and `_TriggerContentRedraw()`.
3. `_TriggerContentRedraw()` moves the dirty content into the pending update session (`_TransferToUpdateSession`), sends `_UPDATE_` to the client (`_SendUpdateMessage`), and fills the exposed part with the view colour right away (`fTopView->Draw(...)`). It skips windows with `kWindowScreenFlag`.
4. The client answers with `BeginUpdate`. `Window::BeginUpdate()` swaps the sessions, turns copy-to-front off, and draws the view backgrounds. The client's drawing commands are clipped by `Window::GetEffectiveDrawingRegion()`: `VisibleContentRegion()`, intersected with the update session.
5. `Window::EndUpdate()` turns copy-to-front on and calls `fDrawingEngine->CopyToFront(dirty ∩ VisibleContentRegion())`.
6. `DrawingEngine::CopyToFront()` is `fGraphicsCard->InvalidateRegion(region)`. `HWInterface::Invalidate` → `CopyBackToFront` → `_CopyBackToFront`. In R5, `AccelerantHWInterface::_CopyBackToFront` (`distro/app_server/AccelerantHWInterface.cpp`) is the M8 hook into `GPUCopy` (`docs/APP-SERVER-GPU.md`).

Every window's `DrawingEngine` is made by `fDesktop->HWInterface()->CreateDrawingEngine()` in `ServerWindow::_CreateWindow` (around line 4420), and its `Painter` is attached to the one shared back buffer (`HWInterface::DrawingBuffer()`). Window and view coordinates in app_server are screen coordinates. A window can only draw into its own `VisibleRegion()`: what other windows cover is never drawn and has no pixels anywhere.

### The desktop marks something dirty

`Desktop::MarkDirty()` takes `LockAllWindows()` and calls `_TriggerWindowRedrawing()`, which calls `Window::ProcessDirtyRegion()` on every window whose visible region meets the dirty region. That adds to `fDirtyRegion` and posts `AS_REDRAW` when the region was empty. Clipping is rebuilt in `_RebuildClippingForAllWindows()`, back to front, with `Window::SetClipping()`; it also sends `B_DIRECT_MODIFY | B_CLIPPING_MODIFIED` to windows with direct access. The uncovered rest is filled with the workspace colour in `_SetBackground()`.

### A window moves

`Desktop::MoveWindowBy()` takes the write lock, stops direct access (`HandleDirectConnection(B_DIRECT_STOP)`), moves the window, rebuilds clipping, blits what it can with `DrawingEngine::CopyRegion()`, marks the rest dirty (the window itself and what it uncovered on the windows below), and restarts direct access with `B_DIRECT_START | B_BUFFER_MOVED | B_CLIPPING_MODIFIED`. `ResizeWindowBy()` is the same with `B_BUFFER_RESIZED`. `_ShowWindow()` sends `B_DIRECT_START | B_BUFFER_RESET`, `_HideWindow()` `B_DIRECT_STOP`; the mode change sends `B_MODE_CHANGED`.

### Direct access

`ServerWindow::HandleDirectConnection()` asserts `fWindowLock` is held. It calls `DirectWindowInfo::SetState()` with `HWInterface()->FrontBuffer()`, the frame and `VisibleContentRegion()`, and waits for the client to acknowledge. A client that does not answer in time loses its `DirectWindowInfo`. `BWindowScreen` does not use this; it takes `Desktop::LockDirectScreen()` and sets `kWindowScreenFlag`.

### The mouse

`MouseFilter::Filter()` in `Desktop.cpp` takes `LockAllWindows()` and asks `Desktop::WindowAt(where)`, which returns the front-most visible window whose `VisibleRegion()` contains the point. The window's `DefaultWindowBehaviour` then asks `Decorator::RegionAt()`. `TabDecorator::RegionAt()` (`decorator/TabDecorator.cpp`) returns the tab and buttons, then the four borders, and `REGION_RIGHT_BOTTOM_CORNER` for the last `fBorderResizeLength` (`kBorderResizeLength = 22`, times the font scale) of the bottom and right borders. `B_DOCUMENT_WINDOW_LOOK` has the drawn knob `fResizeRect` (`kResizeKnobSize = 18`). A titled window's border is `fBorderWidth = 5` (floating 3, bordered 1), times `max(font size / 12, 1)`. Only what is in the decorator's footprint can be clicked: nothing outside it reaches the window.

## Where the compositor hooks in

Decision 3: a layer between `Desktop` / `Window` and `HWInterface`. New R5 files under `distro/app_server/` (MIT, SPDX header):

- `Compositor.{h,cpp}`: the Vulkan device, the compose thread, damage, timing, fallback.
- `WindowBuffer.{h,cpp}`: one window's GPU buffer and the `RenderingBuffer` over it.
- `WindowHWInterface.{h,cpp}`: a small `HWInterface` like `BitmapHWInterface`, whose `DrawingBuffer()` is the window buffer and whose `InvalidateRegion()` reports damage to the compositor instead of copying to the front.
- `compositor/*.comp`, `*.frag`, `*.vert` and the generated `compositor_shaders.h`.

Haiku files that change (R5 changes marked with SPDX-Snippet tags):

- `ServerWindow::_CreateWindow`: in the GPU mode, the window's `DrawingEngine` gets a `WindowHWInterface` instead of the screen's. `DrawingEngine::SetHWInterface()` already exists, so the fallback can rebind the same engine object later.
- `Window`: in the GPU mode, the drawing region of a window is its whole frame, not `VisibleRegion()` (decision 5: covered parts are drawn too). `GetEffectiveDrawingRegion()`, `_DrawBorder()`, `RedrawDirtyRegion()` and `MarkContentDirty()` use a new `Window::DrawableRegion()`, which is `VisibleRegion()` in the CPU mode and the full region in the GPU mode. `fVisibleRegion` itself keeps its meaning, so the mouse and the direct-window clipping do not change.
- `Desktop::MoveWindowBy()`: in the GPU mode no `CopyRegion` and no `MarkDirty` of the windows below. It tells the compositor the old and new frame; that is damage, not a redraw.
- `Desktop::_SetBackground()`: in the GPU mode the background is a layer of the compose (the workspace colour, or the desktop window, which is a window like any other).
- `AccelerantHWInterface`: in the GPU mode the compositor writes the scanout; `_CopyBackToFront` is not called for windows.

The coordinate offset: a window buffer starts at the window's footprint left-top, but app_server draws in screen coordinates. The window's engine calls `DrawingEngine::SetRendererOffset(left, top)` (`Painter::SetRendererOffset` → `fBaseRenderer.set_offset`), the call `AlphaMask.cpp` already uses to draw a picture into a bitmap at an offset. It is reset on every move. Not every Painter fast path goes through the base renderer (region fills, bitmap blits, `CopyRegion`); step 2 checks each of them and the compare mode catches a miss. A path that ignores the offset gets the offset added in `DrawingEngine`, not a second mechanism.

## The resize grab area (decision 2)

The R5 frame is a 1-pixel line, so `fBorderWidth` is 1. Proposal:

- **Width:** the grab area is as wide as Haiku's border is today: `fBorderWidth` of the Haiku decorator for that look (5 for titled and document, 3 for floating, times the same font scale). The line is the inner pixel of it. So a user grabs the edge where Haiku's border was, and there is no new constant.
- **Where:** outside the line. The content keeps every pixel, so a scroll bar or a text field at the window edge does not start a resize. The corner zone stays `fBorderResizeLength` along the bottom and right edges, also on the left-bottom and top corners where Haiku has them.
- **Not drawn, not clipped:** the outer part is an **input margin**, not part of the footprint. It does not cover the windows below, it is not in `VisibleRegion()`, it has no pixels in the window buffer, and it gets no shadow of its own. New in the R5 decorator interface: `GetInputMargin()` (the four widths; 0 for decorators that do not have it, so the Haiku decorators are unchanged; see "Decorator hooks"). `Desktop::WindowAt()` walks front to back as today and also returns a window when the point is in its input margin and not in the visible region of a window in front of it. `RegionAt()` of the R5 decorator returns the border and corner regions for the margin.
- **Both modes:** the margin is input only, so it works the same in the CPU mode.
- Only windows that can be resized get the margin. `B_NOT_RESIZABLE`, `B_NOT_H_RESIZABLE` and `B_NOT_V_RESIZABLE` remove the matching sides. The cursor changes over the margin, as `DefaultWindowBehaviour` does today over the border.

## Decorator hooks (decision 10)

Decorators stay Haiku add-ons in `/system/add-ons/decorators`, subclasses of `Decorator` (`decorator/Decorator.h`), loaded by `DecorManager`. New, after the existing members of the R5 decorator interface:

- `GetShadowExtent(BRect* extent)`: how far the shadow reaches on each side. Default: the small, short shadow derived from the decorator's border width.
- `GetGlassRegion(BRegion* region)`: the part of the footprint that is glass (the tab and the frame line for the R5 decorator). Default: empty, so a Haiku decorator has no glass.
- `GetInputMargin(...)`: the resize margin above. Default: 0 on every side.

The R5 decorator implements all three. app_server adds the glass views of the window (`set_view_glass()`) to the decorator's glass region. How the hooks are added without breaking a decorator add-on built earlier: [Decided after step 0](#decided-after-step-0-12-nov-2026), point 2.

## Window buffer life cycle (decision 5)

| Event | In the GPU mode |
|---|---|
| Create (`ServerWindow::_CreateWindow`) | No buffer yet. A window that was never shown uses no GPU memory. |
| First show (`Desktop::_ShowWindow`) | `R5_GPU_BO_CREATE` in GTT, without `R5_GPU_BO_WC` (cached, snooped; Painter writes it all the time), `R5_GPU_BO_MAP` for Painter, imported into Vulkan as the window's image. Size: frame + decorator footprint + the shadow margin from the decorator hook, 4 bytes per pixel, the pitch from the driver. Then the whole window is marked dirty once. On Lavapipe the buffer is host memory imported with `VK_EXT_external_memory_host`, or a Vulkan host-visible buffer when that extension is missing. |
| Move | No new buffer, no redraw. The compositor gets the new position. |
| Resize (`Desktop::ResizeWindowBy`) | A new buffer of the new size. The old one is kept until the next compose fence that no longer samples it, then freed. The window redraws in full into the new one; until it has, the compose shows the old buffer, clipped to the new frame. |
| Hide, minimise, other workspace | The buffer stays (fast show). It is the first to go when the budget is exceeded (below). |
| Decorator change, look change, font change | Treated as a resize: the footprint may change. |
| Close (`Desktop::RemoveWindow`) | Freed after the fence of the last compose that sampled it. |

## The compose pass and its shaders

- **When:** only on damage, at most once per refresh period (decision 7). The period is taken from the current `display_mode` (`timing.pixel_clock`, `h_total`, `v_total`) at each mode change, never a constant. The compose thread sleeps until damage arrives, then until the next period boundary.
- **Damage:** `WindowHWInterface::InvalidateRegion()` adds the region (in screen coordinates) to the compositor; a move or resize adds the old and the new frame; the shadow and blur extend the damage by their own extent.
- **Order:** back to front over `CurrentWindows()` of the current workspace, read under `LockSingleWindow()`. The compose thread copies the list of windows, frames and buffers under that read lock and releases it before recording. It never holds `fWindowLock` while it waits for a fence.
- **A1 (no effects):** each window is one quad with its buffer as a texture, nearest sampling, integer positions, no blending of opaque pixels. With the effects off, every output pixel must be the pixel the CPU mode would have: that is what the compare checks.
- **A3 shadow:** a separate pass before a window's quad: an alpha mask of the frame rectangle, blurred once at window creation or resize, drawn offset under the window. Its extent comes from the decorator hook (decision 10); the default is small and short, derived from the decorator's border width, not a new constant.
- **A4 frosted glass:** for the glass region of a window (the tab, the frame line, and the views a program marked as glass, see "What programs and developers see"), the compose blurs what is already composed beneath it (a downsampled two-pass blur in a compute shader), then draws the window's pixels with their alpha over it. Painter writes alpha into the window buffer; the tab and frame get a fixed alpha from the R5 decorator in the GPU mode only.
- **Shaders:** GLSL in `distro/app_server/compositor/`, compiled at build time by a host `glslangValidator` into `compositor_shaders.h`, never edited by hand (the rule of `gfx9_compute_shader.h`). The command is recorded in the Jamfile and in this document when step 2 adds it.

## The scanout path

- **RADV:** the compose renders into an image of the screen size. Then a Vulkan copy into the scanout buffer from `R5_GPU_SCANOUT_BO`, imported into Vulkan, on the same queue, signalled by the same fence. That keeps one submit and one fence per frame and needs no vendor code. Until A2 the import is not there ([Decided after step 0](#decided-after-step-0-12-nov-2026), point 3): the M8 `DMA_DATA` copy runs after the compose fence; that is AMD-only and stays behind the same fallback.
- **Lavapipe:** there is no render node and no scanout buffer. The compose renders into host memory, and the existing `HWInterface::_CopyToFront` row copy puts the damaged rectangles on the accelerant framebuffer, as the CPU mode does today.
- **The pointer:** the software cursor and the drag bitmap stay in `HWInterface` (`fFloatingOverlaysLock`, `_DrawCursor`) and are drawn after the copy, as in M8. A hardware cursor is unchanged.

## Zero-copy windows and their sync (A2, decision 6)

- A `VK_RFIVE_view_surface` swapchain exports each image with `R5_GPU_BO_EXPORT` and sends the handles to app_server once per swapchain (a new message in the existing view-surface protocol, at the end).
- app_server imports them with `R5_GPU_BO_IMPORT` and creates a Vulkan image for each. On present, the client sends the image index and a sync object that its rendering signals; the compose waits on it before it samples that image, and signals one back when it is done, so the client can reuse the image.
- Whether an interface call is needed for sharing a sync object between teams is checked in step 3. If one is needed, it is additive (`R5_GPU_OP_BASE + 32` onward), vendor-neutral, and documented in the header and `docs/GPU-ARCHITECTURE.md` in the same change.
- When export or import fails, or on Lavapipe, the M7 copy into a `BBitmap` stays, and app_server logs `app_server: window <id> zero-copy off: <reason>` once per window.

## The direct-window pause (decision 9)

A direct window writes to `HWInterface()->FrontBuffer()`, which the compositor also writes. They cannot run at the same time.

- **Triggers:** `HandleDirectConnection()` with `B_DIRECT_START` for a window that has `DirectWindowInfo`, and `Desktop::LockDirectScreen()` for `BWindowScreen`. A `BDirectWindow` that never asks for direct access never triggers it.
- **Pause:** the same switch as the fallback below, but temporary: every window's engine is rebound to the screen's `HWInterface` and the desktop is redrawn the CPU-mode way. It is logged as `app_server: compositor paused: direct window`.
- **Resume:** when the last direct access stops (`B_DIRECT_STOP` from close, hide or `UnlockDirectScreen`), the engines are rebound to their window buffers, every window is redrawn once, and the compose restarts. Logged as `app_server: compositor resumed`.
- **Lock order:** `fWindowLock` (write) → the compositor lock → `HWInterface` exclusive. `HandleDirectConnection()` already runs with `fWindowLock` held, so the pause is called from there. Before it rebinds, it waits for the compose in flight, at most the one 100 ms fence timeout. A timeout there is a fence timeout and turns the GPU mode off (below). `HandleDirectConnection()` itself is called after the pause, so the client never sees a front buffer that the compositor is still writing.
- Moving a direct window stops and restarts its access today (`MoveWindowBy`). That must not resume and pause the compositor each time: the resume waits until the `fWindowLock` write lock that sent the stop is released and no direct access is active.

## The switch to the CPU mode (decision 11)

- **At session start:** no render node, or RADV cannot create the device → Lavapipe, `app_server: gpu compositor on lavapipe: <reason>`. Lavapipe cannot create the device → the CPU mode, nothing else logged. With `gpu_compositor_lavapipe true` Lavapipe is used directly. `gpu_compositor false` or absent before M11 passes → the CPU mode, no compositor line at all.
- **During a session:** a failed submit or one fence timeout (100 ms) → the CPU mode for the rest of the session. Under `LockAllWindows()` the compositor stops its thread, every window's engine gets `SetHWInterface(screen)`, `Desktop::Redraw()` marks the whole screen dirty, and the window buffers are freed after the device is idle or lost. One line: `app_server: gpu compositor off: <reason>`. The reasons: `submit`, `fence timeout`, `device lost`, `window memory`, `forced failure`. No move to Lavapipe in the same session, no retry.
- `gpu_compositor_fail true` makes the first submit fail.

## The memory budget

- **Count:** the sum of all window buffers, logged as `app_server: window memory <n> MB` every 10 s with `gpu_debug true`.
- **Budget:** half of the GTT size the driver reports for RADV; on Lavapipe a quarter of the physical memory from `get_system_info()`. Both come from the system, not from a constant.
- **Above it:** first the buffers of hidden and minimised windows, and of windows on other workspaces, are freed, oldest show first; such a window is redrawn in full when it shows again. If a window on the current workspace still cannot get a buffer, the session switches to the CPU mode with the reason `window memory`.

## What programs and developers see (decision 13)

The compositor, Vulkan and the shaders stay inside app_server. No public header names them. What a program sees follows the Haiku pattern.

### How Haiku does it today

- `ui_color()` and `set_ui_color()` / `set_ui_colors()` (`headers/os/interface/InterfaceDefs.h`, `src/kits/interface/InterfaceDefs.cpp`) are free functions. The setter sends `AS_SET_UI_COLOR` / `AS_SET_UI_COLORS` to app_server. `DesktopSettingsPrivate::SetUIColor()` stores the colour and calls `Save(kAppearanceSettings)`, which writes the `appearance` file in `~/config/settings/system/app_server/`. The reader takes the value from app_server's shared read-only memory.
- `set_decorator()` and `get_decorator()` (`headers/private/interface/DecoratorPrivate.h`, namespace `BPrivate`) are free functions too. They open a `BPrivate::AppServerLink` and send `AS_SET_DECORATOR` / `AS_GET_DECORATOR`. The Appearance program calls them; no program writes app_server's files.

### The new calls

One new header in `libr5ui`, `<WindowEffects.h>`, namespace `BAppearanceKit`, free functions like the two Haiku pairs above:

```cpp
namespace BAppearanceKit {

enum {
	B_WINDOW_SHADOW			= 0x00000001,
	B_WINDOW_FROSTED_GLASS	= 0x00000002
};

// The user's choice, for Settings and the Appearance program.
uint32		window_effects();
status_t	set_window_effects(uint32 effects);

// A program asks for glass behind its own content. Opt-in.
status_t	set_window_glass(BWindow* window, bool glass);
status_t	set_view_glass(BView* view, bool glass);

}
```

- `window_effects()` / `set_window_effects()` are the one call pair for the two settings, like `ui_color()` / `set_ui_color()`. The Settings page and the Appearance program both use it. The reader asks app_server over an `AppServerLink`, like `get_decorator()`; it is not in the shared memory, so `server_read_only_memory` does not change.
- `set_view_glass()` marks one view: in the GPU mode app_server fills that view's background with full transparency instead of its view colour, and the compose blurs what lies behind it. What the view draws itself stays on top. Child views with their own view colour stay opaque. `set_window_glass()` does the same for the window's top view, so the whole content area is glass where no child covers it.
- In the CPU mode, and with "Frosted glass" off, both calls are accepted and stored, and the view colour is drawn as before. When the user turns glass on again, the views marked earlier become glass without a new call.
- **Not used:** `B_TRANSPARENT_COLOR`. Programs set it to mean "do not erase" (`View::Draw`, `View.cpp`, skips the fill for it), and they stay opaque. Nothing is added to `libbe`. A program that links only `libbe` is unchanged.
- **Transport:** new message codes at the end of `headers/private/app/ServerProtocol.h`, before `AS_LAST_CODE` (additive, private). `libr5ui` opens a `BPrivate::AppServerLink`, the way `set_decorator()` does. A view is named by `_get_object_token_(view)`, the token `libbe` already sends as the view token (`View.cpp`); a window by `_get_object_token_(window)`. The `ServerApp` of the calling team finds the `ServerWindow` by its client token and the `View` by its token, so a program can only mark its own windows.
- **Documentation:** the calls go into `docs/LIBR5.md` and `docs/APPEARANCE-KIT.md` (a row for `<WindowEffects.h>`) with a short example, in the change that adds them. Step 1 and later use these names and add no others.

Example, as it would appear in `docs/APPEARANCE-KIT.md`:

```cpp
#include <WindowEffects.h>

MyWindow::MyWindow()
	: BWindow(BRect(100, 100, 500, 400), "Glass", B_TITLED_WINDOW, 0)
{
	BAppearanceKit::set_window_glass(this, true);
}
```

## Settings

- **Shadow** and **Frosted glass** are two checkboxes in a new "Window effects" box on the Look and feel tab of the Appearance page in Settings, and the same two in the separate Appearance program. Each works on its own. Both call `set_window_effects()`.
- **Where app_server keeps them:** with its other appearance settings, in `DesktopSettingsPrivate`: two new keys in the `appearance` file (`~/config/settings/system/app_server/appearance`), written by `Save(kAppearanceSettings)` like the UI colours. An old file without the keys means both on. No new settings file, and no program writes the file.
- A change applies at once, with the log lines `app_server: shadow on` / `off` and `app_server: frosted on` / `off`. Defaults turns both on; Revert restores the two values from when the page was opened. With both off, the GPU mode still composes (A1, A2).
- In the CPU mode the checkboxes stay usable and are stored; they have no visible effect there.
- `radeon_hd` settings: `gpu_compositor`, `gpu_compositor_lavapipe`, `gpu_compositor_fail`, `gpu_debug`. `gpu false` turns the GPU off, and the GPU mode then runs on Lavapipe.

## Measurement

- With `gpu_debug true`: every 100th frame app_server also composes the same frame on the CPU path into a scratch buffer and compares it with the GPU frame, effects off only. One line: `app_server: compose compare ok` or `app_server: compose compare MISMATCH at <x>,<y>`. The compare covers the colour channels; the alpha channel of the scanout is not shown and is not compared.
- Every 10 s: `app_server: compose median <n> us cpu <n> ms per 10 s` (GPU time from timestamp queries where the device has them, otherwise wall time from submit to fence; CPU time of the compose thread), and the window memory line.
- On Lavapipe in VirtualBox the same lines, with and without effects, are recorded in `CHANGELOG.md`. A measurement, not a gate.

## glslang licence report

Read from `KhronosGroup/glslang` `LICENSE.txt` on `main` and the releases page, 12 Nov 2026. Latest release: `16.6.0` (10 Sep 2026).

- **Project:** glslang, the Khronos reference front end for GLSL and HLSL, with SPIR-V output. Repository `github.com/KhronosGroup/glslang`.
- **Licences named in `LICENSE.txt`:** BSD-3-Clause (the core of glslang, minus the preprocessor; "Copyright (C) 2015-2018 Google, Inc." and "<various other dates and companies>"), BSD-2-Clause and MIT (both "Copyright 2020 The Khronos Group Inc"), Apache-2.0 (no holder named in the file), an NVIDIA licence ("Copyright (c) 2002, NVIDIA Corporation.", no clean SPDX id), a Khronos "Materials" licence ("Copyright (c) 2014-2016 The Khronos Group Inc.", MIT-like with a modification notice), and "GPL 3 with special bison exception" (`GPL-3.0` with `Bison-exception-2.2`). The file says that Bison "was removed long ago" and that glslang builds without Bison files. `LICENSE.txt` does not map each licence to files; each source file's own header does.
- **Integration:** a host build tool only. `glslangValidator` runs on the build machine and produces SPIR-V words in a generated header. No glslang code is linked into app_server or shipped in the image.
- **Source-offer duties:** none for the image, because no glslang binary or source is shipped. The recipe pins the version and records the source URL.
- **Combination:** the SPIR-V output is generated from R5's own GLSL (MIT). The GPL part with the Bison exception concerns the parser skeleton, which current glslang no longer uses.
- **Before it goes into the build:** the per-file SPDX headers of the pinned version are checked (no GPL file in the tool that is built), and glslang gets a row in `THIRD_PARTY_NOTICES.md` and `docs/LICENSING.md` as a build tool. The fetch of `LICENSE.txt` for this report was partly a summary after the GPL section, so the NVIDIA and Khronos "Materials" lines are checked against the file again then.

## Decided after step 0 (12 Nov 2026)

The three open questions from step 0, decided by the user.

1. **Covered parts and client drawing (decision 5): accepted.** In the GPU mode every window draws into the full window buffer, also outside an update (for example in `Pulse()`), so its covered parts are drawn too. When a window above it goes away, the content below is already correct, without a redraw. No program can rely on covered parts not being drawn, so nothing breaks; it costs some extra drawing for programs that draw outside an update. The CPU mode keeps today's clipping (`Window::GetEffectiveDrawingRegion()`). This change of behaviour goes into `CHANGELOG.md` with step 2. Test: Pulse, ActivityMonitor and a clock half under another window; move that window away, and the content must be correct at once.
2. **New decorator hooks and old add-ons (decisions 10 and 13): virtual functions at the end of `Decorator`, with an add-on version check.** Only decorator add-ons are affected, not applications: `Decorator` is a private app_server class, and applications talk to app_server through messages and `libbe`. Today `DecorManager` loads an add-on on `instantiate_decor_addon` alone, without a version check.
   - The hooks above (`GetShadowExtent`, `GetGlassRegion`, `GetInputMargin`) are added as virtual functions at the end of `Decorator`, with the defaults above.
   - Every decorator add-on exports a decorator ABI version (an `extern "C"` symbol). `DecorManager` loads only an add-on with the current version. An add-on without it, or with another version, is refused with one line, `app_server: decorator <name> refused: abi <n>, need <m>`, and app_server uses the built-in default decorator. It never crashes on an old add-on.
   - **The Haiku decorators in the tree (Be, Flat, Mac, Win) are migrated:** rebuilt with the version, and they implement the new hooks, each with a shadow and, where it fits the style, glass. They are also made nicer, keeping each one recognisable (Be stays Be). What "nicer" means is designed on paper first, with a picture per decorator, and the user approves it before code (step 1b in the brief).
   - Copied Haiku files keep their MIT header; the R5 parts get SPDX-Snippet tags, MIT.
3. **Importing the scanout buffer into RADV (decision 4): not in A1.** Today the kernel answers `R5_GPU_BO_EXPORT` and `R5_GPU_BO_IMPORT` with `B_UNSUPPORTED`, and the R5 DRM library logs `DRM_IOCTL_PRIME_*` and `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD` / `FD_TO_HANDLE` as unknown. So in A1 the compose renders into an image, and the M8 `DMA_DATA` copy puts it on the scanout after the compose. The import comes in A2 (step 3), together with zero-copy windows, because both need the same work: `BO_EXPORT` / `BO_IMPORT` in the kernel (the existing numbers `+ 17` and `+ 18`, which start to work; no new number), PRIME and sync-object sharing in the R5 DRM library, and a Haiku form of a buffer handle that can pass between teams (Haiku has no dma-buf). Step 3 designs that handle, vendor-neutral. After A2 the Vulkan copy into the imported scanout replaces the extra `DMA_DATA` copy, if it measures no worse.
