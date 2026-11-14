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
| First show (`Desktop::_ShowWindow`) | A Vulkan buffer in host-visible, host-coherent memory, host-cached first (decision 5; on RADV an R5 buffer in GTT, cached and snooped), mapped for Painter. Size: frame + decorator footprint + the shadow margin from the decorator hook, 4 bytes per pixel, the pitch from the driver. Then the whole window is marked dirty once. On Lavapipe the buffer is host memory imported with `VK_EXT_external_memory_host`, or a Vulkan host-visible buffer when that extension is missing. |
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
- The design that step 3a worked out from decision 15 is the next section. Where it and this list differ, the next section counts.

## Zero-copy design (step 3a, 13 Nov 2026)

Paper only: nothing below is built yet. It works decision 15 of the brief out, as refined by decision 15b (where they differ, 15b counts) and by the user's choice for the token (13 Nov 2026), against the source as it is on `main` after step 2. Every point names the file it was checked in. What does not fit the source is under [Open questions (step 3a)](#open-questions-step-3a).

### What the source has today

- **Kernel** (`distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/`): `R5_GPU_BO_EXPORT` (`+ 17`) and `R5_GPU_BO_IMPORT` (`+ 18`) return `B_UNSUPPORTED` (`r5_gpu_core.cpp`, before the GPU lock). There is no structure for them in `r5_gpu.h`. A buffer is one slot in the global table `sBo[kMaxBo]`; `kMaxBo` and the per-file `kMaxClientBo` are both 256 (`r5_gpu_internal.h`), so one program can take every slot. The handle is the slot number plus one. A slot has exactly one `owner` (the open file), one user area (`area`, `mapTeam`) and the memory itself (`kernelArea`, `vramOffset`). `gpu_bo_get` refuses a handle of another file. A file that closes while its GPU work runs **parks** its buffers (`parked`, `parkSequence`), and the retire thread frees them after that fence (`gpu_release_parked`). There is no reference count. `borrowed` marks the framebuffer from `R5_GPU_SCANOUT_BO`: release unmaps it and does not free it.
- **Submits:** the kernel gets no buffer list (`r5_translate.c` skips `AMDGPU_CHUNK_ID_BO_HANDLES`); a submit is IBs, waits and signals, and sets `client->lastSequence`, a number on the one GFX ring. `FENCE_WAIT` takes any sequence and a timeout (`gpu_fence_wait`), so timeout 0 asks without waiting.
- **The app_server check:** `gpu_caller_is_app_server()` compares the path that `load_image` stored in `team_info.args` with `/system/servers/app_server` and `/boot/system/servers/app_server`. `R5_GPU_SCANOUT_BO` uses it.
- **Areas:** a GTT buffer is a kernel area (`create_area`, deliberately not `B_CLONEABLE_AREA`) plus a clone into the one team that maps it (`vm_clone_area`, `BO_MAP`). Areas are used for the CPU mapping, but no other team can clone one. Sharing does not go through areas.
- **Randomness in the Haiku kernel:** `secure_random_value()` (`src/system/kernel/util/Random.cpp`) is a hash of a counter, the time, the thread, the CPU and the last values. The random bus manager (`src/add-ons/kernel/bus_managers/random/driver.cpp`) offers other kernel code only `queue_randomness` (module `bus_managers/random/controller/driver_v1`, used by the `ccp` and `virtio_rng` drivers to put entropy in); reading is only through `/dev/random` and the user-only `RANDOM_GET_ENTROPY` generic syscall. The design below does not need it.
- **The R5 DRM library** (`distro/graphics/libdrm-r5/r5_translate.c`): `PRIME_HANDLE_TO_FD`, `PRIME_FD_TO_HANDLE`, `GEM_METADATA` and `GEM_OP` end in `r5_unknown` (`ENOSYS`). The libdrm patches are the four in `patches/`, applied by `haiku.py`.
- **The WSI** (`distro/mesa/wsi/wsi_common_haiku.cpp`): on RADV a `WSI_IMAGE_TYPE_CPU` swapchain with `wsi_configure_buffer_image` (a device image blitted into a linear buffer), then a CPU copy of the rows into a `BBitmap` per image, shown with `BView::SetViewBitmap()` (the link message `AS_VIEW_SET_VIEW_BITMAP`, `src/servers/app/ServerWindow.cpp`). Present waits for `BWindow::Sync()`.
- **app_server** has two GPU files: RADV's (the Vulkan device of `Compositor.cpp`) and the one of the M8 copy (`GPUCopy.cpp` opens `/dev/gpu/radeon_hd/0` itself and holds the `R5_GPU_SCANOUT_BO` handle).

### How RADV and libdrm import a buffer fd

Checked in Mesa 25.3.6 (WSL tree, `src/amd/vulkan/winsys/amdgpu/radv_amdgpu_bo.c`, `src/amd/common/ac_linux_drm.c`) and libdrm 2.4.124 (`amdgpu/amdgpu_bo.c`):

1. `vkAllocateMemory` with `VkImportMemoryFdInfoKHR` calls `radv_amdgpu_winsys_bo_from_fd` and, on success, closes the fd (`radv_device_memory.c`).
2. **RADV does no `lseek` of its own.** `radv_amdgpu_winsys_bo_from_fd` calls `ac_drm_bo_import`, then `ac_drm_bo_query_info`, then maps the buffer with `GEM_VA`. The only `lseek` calls in `src/amd/vulkan`, `src/amd/common` and `src/vulkan/wsi` are in `radv_android.c` and the virtio winsys, neither built for R5. The `lseek(fd, 0, SEEK_END)` for the size is in libdrm's `amdgpu_bo_import`, after `drmPrimeFDToHandle`.
3. `ac_drm_bo_query_info` sends `DRM_AMDGPU_GEM_METADATA` (get) and then `DRM_AMDGPU_GEM_OP` (`AMDGPU_GEM_OP_GET_GEM_CREATE_INFO`: `bo_size`, `alignment`, `domains`, `domain_flags`). **With `ENOSYS` today**, either one returns non-zero, and RADV fails the import: `vkAllocateMemory` returns `VK_ERROR_UNKNOWN` (`error_query`), and `vkGetMemoryFdPropertiesKHR` returns `VK_ERROR_INVALID_EXTERNAL_HANDLE`. `GEM_METADATA` set (`radv_amdgpu_winsys_bo_set_metadata`) ignores its result, and RADV only sends it for GFX12 DCC.
4. `vkGetMemoryFdPropertiesKHR` (dma-buf) imports the fd, queries it and frees it again (`radv_amdgpu_bo_get_flags_from_fd`).
5. Export: `vkGetMemoryFdKHR` is `amdgpu_bo_export(dma_buf_fd)`, that is `drmPrimeHandleToFD`, an ioctl the library sees.

So the token-to-fd step fits without a change in RADV: everything RADV does reaches the library as an ioctl, and the one `lseek` is in libdrm (15b: the fifth libdrm patch).

### The structures

Additive, in `r5_gpu.h`, version 1. Calls `+ 17` and `+ 18` keep their numbers and start to work. The present mark (below) is one new call at the first free number, `+ 32` (`R5_GPU_SCANOUT_BO` is `+ 31`). No sync-object calls are added (15b).

```c
#define R5_GPU_SHARE_TOKEN_BYTES 16u	/* opaque, 128 bits */
#define R5_GPU_SHARE_COMPOSITOR 1u	/* the only target in version 1 */
#define R5_GPU_SHARE_TEAM 2u		/* one named team; refused in version 1 */

/* R5_GPU_BO_EXPORT (+ 17) */
typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	handle;		/* in: a buffer of the calling file */
	uint32_t	target;		/* in: R5_GPU_SHARE_COMPOSITOR */
	int32_t		target_team;	/* in: for R5_GPU_SHARE_TEAM; 0 now */
	uint32_t	flags;		/* in: 0 */
	uint64_t	bytes;		/* out: the real size */
	uint8_t		token[R5_GPU_SHARE_TOKEN_BYTES];	/* out */
} r5_gpu_bo_export;

/* R5_GPU_BO_IMPORT (+ 18) */
typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint8_t		token[R5_GPU_SHARE_TOKEN_BYTES];	/* in */
	int32_t		client_team;	/* in: ServerApp::ClientTeam() */
	uint32_t	flags;		/* in: 0 */
	uint32_t	handle;		/* out: a new handle in the calling file */
	uint32_t	domain;		/* out: R5_GPU_DOMAIN_* */
	uint32_t	bo_flags;	/* out: the flags of BO_CREATE */
	uint32_t	_pad;
	uint64_t	bytes;		/* out: the real size */
} r5_gpu_bo_import;

/* R5_GPU_BO_PRESENT_MARK (+ 32) */
#define R5_GPU_MARK_SET 1u	/* exporter: record the file's last submit */
#define R5_GPU_MARK_GET 2u	/* importer: read what was recorded */
typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	handle;		/* in: the caller's own handle */
	uint32_t	op;		/* in: SET or GET */
	uint64_t	sequence;	/* out: the ring number recorded */
} r5_gpu_bo_mark;
```

### The token: an export number

The BeOS way (user, 13 Nov 2026): the token is an **export number, like an `area_id`**: unique, never reused. It is not a secret, and the kernel does not need a random source for it.

- **What it is:** a 64-bit counter of the kernel driver, which only goes up, mixed with `secure_random_value()` so that numbers are not consecutive. It sits in an **opaque 128-bit field**: programs and app_server only copy it, so its contents can change later without a new structure. No copy of the random bus manager, no cryptography in the driver.
- **The safety boundary is the kernel's rights check, not the number.** An import succeeds only when all of these hold:
  - the caller is app_server (`gpu_caller_is_app_server()`); anyone else gets `B_NOT_ALLOWED` (refusal `BO_IMPORT caller`);
  - the export was pinned to the exporting team at export, and that team is the `client_team` app_server passes, the `ClientTeam()` of the connection the message came on; otherwise `B_NOT_ALLOWED` (`BO_IMPORT team`);
  - the number has not been imported yet: one use (`B_BAD_VALUE`, `BO_IMPORT token`, the same as a number that never existed);
  - it is younger than 5 s (`BO_IMPORT expired`).
- **So a guessed or forwarded number from another team is refused**, whoever writes it into app_server's message: the team pinned at export is not the team of the connection.
- **Target:** only `R5_GPU_SHARE_COMPOSITOR` in version 1. `R5_GPU_SHARE_TEAM` and a non-zero `target_team` are refused with `B_BAD_VALUE` until a later version uses them.
- **The pending export** is a record `{number, backing, exporter file, exporter team, made at, target}`. The import that matches removes it. A record older than 5 s is removed at the next export or import and by the watchdog (`gpu_watchdog`, which already runs). At most **32 pending exports per file** (`B_NO_MEMORY`, `BO_EXPORT limit`). Exports not yet imported are removed when their file closes (`gpu_free`).
- 5 s and 32 are starting values (15b).

### The reference count

Today a slot is the memory and the handle at the same time. Sharing splits it:

- a **backing** record: `refs`, `domain`, `flags`, `bytes`, `vramOffset`, `kernelArea`, `kernel`, `borrowed`, and the present mark (`readySequence`);
- the **slot** per file, as today: `owner`, `ranges`, `area` and `mapTeam` (the CPU mapping stays per team), `parked`, `parkSequence`, `hold`, and a pointer to its backing.

`refs` counts each live slot (also a parked one: that is how GPU work in flight is counted, because a parked slot is freed by the retire thread after its fence) and each pending export. `gpu_bo_release` unmaps that slot's ranges, deletes that slot's user area and drops one reference. Only the last reference frees the VRAM or deletes the kernel area. A `borrowed` backing (the scanout) is never freed. So the program that frees its handle or crashes takes only its own slot; what app_server imported stays valid until app_server closes it.

### Ready and free (15b: no shared sync objects)

- **Ready, at present.** When the program presents image *i*, the WSI calls `BO_PRESENT_MARK` with `SET` on that image's buffer. The kernel records the calling file's `lastSequence` on the backing; the program cannot give a number. Mesa's WSI submits the blit into the linear buffer before it calls the swapchain's present, so that number covers the frame. If another thread of the program submitted in between, the number is later than needed: the frame waits a little longer, it is never shown too early.
- **The compose never waits.** At the start of a frame app_server calls `BO_PRESENT_MARK` with `GET` on its imported handle, then `FENCE_WAIT` with that number and timeout 0. Passed: it samples the new image. Not passed: that window keeps its previous image.
- **Free again: one semaphore per swapchain image** (user, 13 Nov 2026). The WSI creates it (`create_sem`, count 0) and sends its `sem_id` with that image's export number in the once-message.
  - After its compose fence, app_server calls `release_sem_etc(sem, 1, B_DO_NOT_RESCHEDULE)` for each image that the frame no longer samples. It never waits on a program.
  - Before it uses a `sem_id`, app_server checks with `get_sem_info` that the semaphore's `team` is `ClientTeam()` of that connection; otherwise that window's zero-copy is refused (`zero-copy off: sem`). That check is the protection: Haiku's `release_sem_etc` lets any team release a semaphore of another user team and only refuses a user call on a kernel-team semaphore (`src/system/kernel/sem.cpp`).
  - In `vkAcquireNextImageKHR` the WSI waits with `acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, timeout)`, the program's own timeout. When nothing comes in that time, it returns `VK_TIMEOUT`, as the Vulkan specification says and as the copy path does (decided in step 3b, point 5); a semaphore that is gone makes the surface lost. An app_server that stops taking a view's images refuses the next present, and the WSI then returns `VK_ERROR_OUT_OF_DATE_KHR`.

### The link message

New messages next to `SetViewBitmap` (15b); the CPU path stays as it is. **No change in libbe** (user, 13 Nov 2026):

- The WSI opens its own `BPrivate::AppServerLink`, the way `libr5ui` will for `set_view_glass()` (decision 13, "What programs and developers see"). The view is named by `_get_object_token_(view)` (`headers/private/app/AppMisc.h`).
- New codes go at the end of `ServerProtocol.h`, before `AS_LAST_CODE` (the R5 copy in `distro/`).
- app_server's `ServerApp` of that connection looks up the window and the view by that token, and refuses a view of another application.
- `ServerApp::ClientTeam()` of that connection is the team app_server passes to `BO_IMPORT`, and the kernel compares it with the team it pinned at export.

The messages:

- **Once per swapchain (program → app_server):** the view token, the image count, and per image the export number, the free semaphore's `sem_id`, width, height, pitch and format. The reply has one status per image, so the WSI can fall back to the copy path at once.
- **Per present (program → app_server):** the view token and the image index, after `BO_PRESENT_MARK`.
- **Free again (app_server → program):** not a message but the image's semaphore (above), so the link stays a request and reply channel. `AppServerLink` holds the application's link lock while it waits for a reply (`src/kits/app/AppServerLink.cpp`), so app_server cannot send on it by itself.
- **Once at the end (program → app_server):** the swapchain is gone; app_server closes its imports after the next fence.

### The R5 DRM library and the fifth libdrm patch

- **Export side (the program).** `vkGetMemoryFdKHR` reaches `PRIME_HANDLE_TO_FD`; the library calls `BO_EXPORT` with target compositor and returns a local fd that stands for the number, recorded in a small table `fd → number, bytes`. A new library call, `r5_drm_fd_to_token(fd, token)`, gives the WSI the number for the message.
- **Import side (app_server).** A new library call, `r5_drm_token_to_fd(device_fd, token, client_team, &fd)`, makes a local fd for a number that arrived in the message. app_server passes it in `VkImportMemoryFdInfoKHR`. RADV's `PRIME_FD_TO_HANDLE` reaches the library, which calls `BO_IMPORT` once with the number and `client_team`, removes the entry and returns the new handle.
- **The size:** a fifth R5 patch in libdrm (`patches/`, read by `haiku.py`): `amdgpu_bo_import` asks the R5 library for the size (`r5_drm_import_bytes(fd)`, from `bytes` of `BO_IMPORT`) instead of `lseek(fd, 0, SEEK_END)`. No `shm_open`, no RADV patch.
- **`GEM_OP` and `GEM_METADATA`** are translated: `GEM_OP` (`GET_GEM_CREATE_INFO`) returns the real size and domain from the kernel (`bytes`, `domain`, `bo_flags`), alignment 4096; `GEM_METADATA` (get) returns neutral values for a linear image: flags 0, tiling 0, no UMD metadata. Shared swapchain images are linear; the cost of linear rendering is measured in 3b.
- **app_server does not call `vkGetMemoryFdPropertiesKHR`** on such an fd: RADV imports and frees it there (step 4), which would use up the one import. app_server picks the memory type from `domain`.
- What the local fd itself is (it only needs a number the program can close) is decided in 3b inside the library; with the libdrm patch it no longer needs a size.
- The library calls are additive, MIT, and documented in `docs/GPU-ARCHITECTURE.md` in the same change.

### The WSI

- On RADV the swapchain becomes a `WSI_IMAGE_TYPE_DRM` swapchain with a linear, exportable buffer (the prime path of `wsi_common_drm.c`: `wsi_configure_prime_image`, `wsi_create_prime_image_mem`, then `vkGetMemoryFdKHR`). Only `wsi_common_haiku.cpp` (R5's own file) changes; the common Mesa WSI files are used as they are, without the explicit-sync semaphores.
- When export or the first message fails, the WSI keeps today's path (the CPU copy into a `BBitmap`). On Lavapipe there is no render node, so the WSI does not try, and app_server logs `app_server: window <id> zero-copy off: lavapipe`.
- The WSI changes, so a new `mesa_radv` package follows with the pin and the libdrm symbol check (`docs/GPU-ARCHITECTURE.md`, "Updating Mesa").

### app_server's import and check

- In the GPU mode only, on RADV only. For each image: `r5_drm_token_to_fd` with `ClientTeam()` of that window's `ServerApp`, `vkAllocateMemory` with the import, a `VkBuffer` over it (the compose reads `VkBuffer`s, decision 16).
- **app_server believes the kernel:** it checks the width, height, pitch and format from the message against `bytes` of `BO_IMPORT` (`pitch × height ≤ bytes`, pitch at least `width × 4`, a format the copy path accepts). A wrong value refuses that window's zero-copy (`zero-copy off: size`), and the copy path stays.
- The imported buffer is one more source per window in the compose of step 2, back to front, with the same barriers.

### The scanout import into Vulkan

- app_server's M8 file (`GPUCopy.cpp`) already holds the scanout handle. It exports it with `BO_EXPORT` (app_server is the target and its own team), and the Vulkan device imports it with `r5_drm_token_to_fd`, as a `VkBuffer` in VRAM.
- After the compose, a `vkCmdCopyBuffer` of the damaged rows from the frame buffer into the scanout, in the same submit, signalled by the same fence. That replaces the CPU copy into the back buffer and the M8 `DMA_DATA` copy, only if it measures no worse. The pointer is drawn after it, as today.
- A mode change gives a new scanout: the old import is closed after the next fence, and the new one is exported and imported again.

### The reset path

- The M10 policy marks each file with a context `lost`. app_server's imported slots go with its lost device when the compositor restarts it (decision 14, step 2). The backing of a program's buffer stays until the program's slot goes too.
- Pending exports of a lost file stay until they expire or that file closes; an import of them after the reset is refused (`BO_IMPORT lost`).
- **After the restart a Vulkan window shows its window background until the program presents again** (15b): programs get `VK_ERROR_DEVICE_LOST` and make a new swapchain. No CPU copy is kept for this.

### Limits on buffer slots

`kMaxBo` and `kMaxClientBo` are both 256 today (`r5_gpu_internal.h`), so one program can take every slot and no one else, app_server included, gets a buffer.

- **Starting values for 3b:** the system limit goes well up, to 1024, and the per-file limit is half of it, 512. Both are starting values, not the result of a measurement.
- **A fixed reserve for app_server:** a number of the system slots that only `gpu_caller_is_app_server()` can use; other programs get `B_NO_MEMORY` when only the reserve is left. Starting value 64, also measured on the stick (app_server's `bo_count` with the desktop composed and a few windows open).
- **A file that hits its limit** gets `B_NO_MEMORY`, as today, plus one line per file: `radeon_hd: buffer limit <n> reached, team <id>`.
- **The measurement** (user, 13 Nov 2026): the self-test reads `bo_count` from `R5_GPU_GET_COUNTERS` before and after VulkanTeapot, on the next stick, in a boot that runs anyway (no boot of its own). The real values follow from that count and replace the starting values, recorded in `CHANGELOG.md`.

### Pass and fail lines (for step 3b)

Changed in step 3b (user, 13 Nov 2026): the refusal rules are cases of the host test, each with the error it returns; the machine runs a short conformance check with ordinary calls (`r5-share-check`, like `r5-gpu-isolation`); the job that never finishes is the M10 test switch `gpu_hang_test`, no new shader. A failure prints the same text with `fail` and the reason.

| Test | Where | Pass line |
|---|---|---|
| import by another team, a forwarded number, a guessed number, a second import, an expired number, the export limit, the per-file limit, the reserve, the system limit, a lost exporter, a semaphore of another team, the reference count, a size that lies | host test (`scripts/test-r5-share.cpp`, `r5_share.h`), each case with its error | `ok gpu share <n> cases` |
| the import id after close, a reused fd number, RADV's import and query path | host test (`scripts/test-ac-drm.sh`) | `ok ac drm family RV cu 7 firmware map share` |
| a program that sets argv[0] to app_server's path, run as a user (the caller check needs effective uid 0 and the path) | host test, and the machine: `r5-share-check` | `r5-share-check: argv[0] app_server as a user refused: pass` |
| an import by a program that is not app_server | the machine: `r5-share-check` | `r5-share-check: import by a program that is not app_server refused: pass` |
| the same number offered twice (needs the compositor on RADV) | the machine: `r5-share-check` | `r5-share-check: second import of the same number refused: pass` |
| the exporter stopped before an import | the stick script: `r5-share-check count`, `r5-share-check hold &`, `kill` it, `r5-share-check count` | the two `r5-share-check: buffers <n>` lines show the same number |
| a size that lies, a semaphore of another team | app_server, once per window | `app_server: window <id> zero-copy off: size`, `... off: sem` |
| a job that never finishes, with VulkanTeapot open | the stick: `gpu_hang_test 1` (M10) | `app_server: gpu compositor restarted after reset`; the teapot window shows its window background, then turns again |
| Lavapipe | QEMU or VirtualBox | `app_server: window <id> zero-copy off: lavapipe` |
| the two scanout ways (decision 4) | the stick, `gpu_debug`, once with and once without `gpu_compositor_scanout true` | `app_server: present median <us> (vulkan)` and `... (cpu)`, both in `CHANGELOG.md` |

An acquire timeout (`VK_TIMEOUT`, decision 5) has no test of its own: a program that waits with `UINT64_MAX`, as VulkanTeapot does, never sees it.

### Open questions (step 3a)

None left. The user answered the last ones on 13 Nov 2026: the link through the WSI's own `AppServerLink`, the count with `bo_count` on the next stick, and a semaphore per image for "free again".

## The direct-window pause (decision 9)

A direct window writes to `HWInterface()->FrontBuffer()`, which the compositor also writes. They cannot run at the same time.

- **Triggers:** `HandleDirectConnection()` with `B_DIRECT_START` for a window that has `DirectWindowInfo`, and `Desktop::LockDirectScreen()` for `BWindowScreen`. A `BDirectWindow` that never asks for direct access never triggers it.
- **Pause:** the same switch as the fallback below, but temporary: every window's engine is rebound to the screen's `HWInterface` and the desktop is redrawn the CPU-mode way. It is logged as `app_server: compositor paused: direct window`.
- **Resume:** when the last direct access stops (`B_DIRECT_STOP` from close, hide or `UnlockDirectScreen`), the engines are rebound to their window buffers, every window is redrawn once, and the compose restarts. Logged as `app_server: compositor resumed`.
- **Lock order:** `fWindowLock` (write) → the compositor lock → `HWInterface` exclusive. `HandleDirectConnection()` already runs with `fWindowLock` held, so the pause is called from there. Before it rebinds, it waits for the compose in flight. A late fence there is not a failure (decision 14); only a lost device turns the GPU mode off (below). `HandleDirectConnection()` itself is called after the pause, so the client never sees a front buffer that the compositor is still writing.
- Moving a direct window stops and restarts its access today (`MoveWindowBy`). That must not resume and pause the compositor each time: the resume waits until the `fWindowLock` write lock that sent the stop is released and no direct access is active.

## The switch to the CPU mode (decision 11)

- **At session start:** no render node, or RADV cannot create the device → Lavapipe, `app_server: gpu compositor on lavapipe: <reason>`. Lavapipe cannot create the device → the CPU mode, nothing else logged. With `gpu_compositor_lavapipe true` Lavapipe is used directly. `gpu_compositor false` or absent before M11 passes → the CPU mode, no compositor line at all.
- **During a session (as refined by decision 14):** a failed submit, a lost device that does not come back, or a second loss → the CPU mode for the rest of the session. A fence later than 100 ms is not a failure: the compose keeps the last frame, logs `app_server: compose late <n> ms` at most once a second, and waits on. Under `LockAllWindows()` the compositor stops its thread, every window's engine gets `SetHWInterface(screen)`, `Desktop::Redraw()` marks the whole screen dirty, and the window buffers are freed after the device is idle or lost. One line: `app_server: gpu compositor off: <reason>`. The reasons: `submit`, `fence timeout`, `device lost`, `window memory`, `forced failure`. No move to Lavapipe in the same session; one restart after a successful reset (decision 14).
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
	B_WINDOW_FROSTED_GLASS	= 0x00000002,
	B_WINDOW_ANIMATIONS		= 0x00000004	// step 4b
};

// The user's choice, for Settings and the Appearance program.
uint32		window_effects();
status_t	set_window_effects(uint32 effects);

// A program asks for glass behind its own content. Opt-in.
status_t	set_window_glass(BWindow* window, bool glass);
status_t	set_view_glass(BView* view, bool glass);

// One step for the shadow and the glass (decision 8, 13 Nov 2026).
enum window_effect_strength {
	B_WINDOW_EFFECTS_QUIET		= 1,
	B_WINDOW_EFFECTS_BALANCED	= 2,
	B_WINDOW_EFFECTS_GLASSY		= 3
};
window_effect_strength	window_effect_strength();
// the function hides the type's name: write "enum window_effect_strength"
status_t	set_window_effect_strength(enum window_effect_strength strength);

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

## Decided in step 1 (13 Nov 2026)

Where the source differed from the design, the user chose:

1. **The tab colour:** the R5 decorator draws Haiku's yellow. It does not take over the tint per window title that R5's `DefaultDecorator` adds.
2. **Resize by the edge, for every decorator:** Haiku moved a window when its border was dragged with the primary button, and resized only from the bottom-right corner or with the secondary button. Now dragging any border or corner with the primary button resizes on that side, with every decorator (`DefaultWindowBehaviour`); the tab moves the window. A side that cannot be resized moves the window, as before.
3. **The cursor:** Haiku showed the resize cursor only while dragging. Now it shows while the mouse hovers an edge or a corner that resizes (`DefaultWindowBehaviour::_UpdateHoverCursor`), for every decorator; the desktop resets it when the mouse leaves that window.
4. **The Haiku decorators** (Be, Flat, Mac, Win) are built with the ABI version but not installed: the image had none of them (Be and Flat are in `haiku_extras`, which the R5 image does not install; Haiku does not build Mac and Win at all).
5. **The R5 decorator is in the base package:** `build/jam/packages/Haiku` is a native copy in `distro/desktop`, as the four apply scripts leave it, with `AddFilesToPackage add-ons decorators : R5Decorator`.

Built in step 1: the ABI version is the `extern "C"` data symbol `r5_decorator_abi_version` (`R5_DECORATOR_ABI_EXPORT`, `R5_DECORATOR_ABI_VERSION` 1 in `Decorator.h`). Next to the three hooks there is one non-virtual helper, `Decorator::InputMarginContains()`, used by `Desktop::WindowAt()` and `DefaultWindowBehaviour`. The default shadow extent is the border width on the left and right, half of it on top and twice it below. The R5 decorator uses Haiku's border width for that look. The refusal test uses `R5DecoratorAbiTest`, an add-on without the version that only an image built with `R5_DECORATOR_ABI_TEST=1` contains.

## Decided in step 2 (13 Nov 2026)

Where the source differed from the design, the user chose:

1. **The window buffer offset is in the buffer, not in Painter.** `agg_renderer_region.h` clips its region to the buffer before the offset of `SetRendererOffset`, and about ten Painter fast paths write the buffer directly. So `WindowBufferView` (`distro/app_server/WindowBuffer.h`) gives Painter an address as if the buffer started at screen pixel (0, 0), with `uintptr_t` arithmetic, and a width and height that reach the window's right and bottom edge. `WindowBuffer` keeps the real pointer and size. Painter is unchanged. A move only moves that address. A part of a window at a negative screen coordinate is not drawn; when a move brings it on screen, only that strip of that window is drawn (`Desktop::_MoveWindowComposed`).
2. **glslang 16.6.0 is a build tool,** although its generated parser `glslang_tab.cpp` (GPL-3.0-or-later WITH Bison-exception-2.2) is compiled into it. It runs on the build host only (`docs/LICENSING.md`).
3. **Vulkan in app_server** is loaded at run time (`dlopen("libvulkan.so.1")`, `VK_NO_PROTOTYPES`); the headers come from the R5 Vulkan headers on the build host, as for `r5-wsi-test`.
4. **No refresh period** (a mode without a pixel clock): the compose runs as soon as damage arrives, with one line `app_server: compose period unknown`.

Built in step 2, and how it differs from the sections above:

- **Window buffers are allocated through Vulkan: a fixed choice, not a temporary fallback** (decision 5 in the brief, 13 Nov 2026, second opinion agreed). Host-visible and host-coherent memory, host-cached first; `DEVICE_LOCAL` is not excluded. On RADV that is an R5 buffer in GTT underneath; on Lavapipe system memory. app_server makes no `R5_GPU_BO_CREATE` buffer of its own. The first window buffer logs `app_server: window memory type <i> flags <hex>`. The table "Window buffer life cycle" is adjusted to this below.
- **The queue** is a family with the graphics bit (decision 16): the R5 DRM library accepts only GFX submissions. Each family is logged once at start, `app_server: vulkan queue family <i> flags <hex>`.
- **The compose** is one compute shader (`distro/app_server/compositor/compose.comp`): for each rectangle of the damage it fills the background colour or copies a window buffer's words unchanged, back to front, with a barrier between layers. The frame is a host-visible buffer of the screen size.
- **The scanout path, both devices:** after the fence the damaged rectangles of the frame are copied into the screen's back buffer, and `HWInterface::InvalidateRegion` puts them on the front: the CPU row copy (Lavapipe), or the M8 `DMA_DATA` copy when `gpu_app_server` is on (RADV). That CPU copy into the back buffer goes away with the import in A2.
- **The window list** for the compose is taken in `Compositor::ClippingChanged()`, at the end of every clipping rebuild, with all windows locked; the compose thread never takes the window lock. A window's first buffer comes with its first show, and it then draws all of itself once. A new size gets a new buffer with the old pixels copied by screen position.
- **The compare frame** (`gpu_debug`, the first frame and every 100th) holds each window's interface lock from the record to the CPU copy, so both read the same pixels.
- **The memory budget:** over the budget, the buffers of windows that are not on screen (hidden, minimised, another workspace) are freed first, the one shown longest ago first; such a window draws all of itself again when it shows. A window on screen that still cannot get a buffer turns the GPU mode off (`window memory`). A window without a buffer draws nothing: `Window::_ClipToBuffer` limits every drawable region to the window's buffer, and logs `app_server: draw outside buffer: ...` once per window if a region ever reaches past it.
- **Decision 14:** a lost device on RADV is followed by one restart once `GET_INFO` reports the GPU available again (up to 15 s); the buffers of the lost device are not freed through the new one.


## Decided in step 3b (13 Nov 2026)

Where the source differed from the step 3a design, the user chose:

1. **app_server reads the present mark through the R5 DRM library.** RADV does the import itself (`PRIME_FD_TO_HANDLE` on its own device fd), so app_server never sees that fd or the new handle. `r5_drm_token_to_fd(device_fd, token, client_team, &fd, &id)` returns, beside the fd, an id of its own. At `PRIME_FD_TO_HANDLE` the library stores RADV's device fd and the new handle under that id, not under the fd number: RADV closes the fd after a successful import, and the number comes back for the next one. `r5_drm_mark_get(id, &sequence)` sends `BO_PRESENT_MARK` `GET` on that device fd with that handle. `GEM_CLOSE` of that handle ends the id; a `mark_get` after that fails, and app_server puts that window on the copy path. `device_fd` is not used in version 1 (pass -1).
2. **The WSI sets the mark the same way.** `r5_drm_fd_to_token(fd, token, &id)` returns, beside the number, an export id; the library keeps RADV's device fd and the handle of `PRIME_HANDLE_TO_FD` under it. `r5_drm_mark_set(id)` sends `BO_PRESENT_MARK` `SET` there. A second export of the same buffer keeps the same id; `GEM_CLOSE` of that handle ends it.
3. No patch in RADV, no new kernel number, nothing in libbe. The four library calls are additive and MIT (`distro/graphics/libdrm-r5/r5_translate.h`).
4. **The scanout: the frame is the back buffer** (option A, user, 13 Nov 2026). The software cursor reads the back buffer (`HWInterface::_DrawCursor`, and `Invalidate` restores the area under a moved cursor from it), so the back buffer must stay current. `GPUCopy` exports both its GTT back buffer and the scanout to app_server itself; the Vulkan device imports both. The compose writes into the back buffer, and one `vkCmdCopyBuffer` of the damaged rows from the back buffer into the scanout follows in the same submit. The frame stride is the back buffer's `BytesPerRow() / 4`. After the fence app_server lets `HWInterface` draw the cursor again where the damage touches it; the R5 `AccelerantHWInterface::_CopyBackToFront` skips the copy for a region the GPU has already put on the front. This only runs when the back buffer is `GPUCopy`'s GTT buffer (`gpu_app_server` on, RADV); otherwise, and on Lavapipe, `_Present` copies as in step 2. The old way (CPU copy into the back buffer and the M8 `DMA_DATA` copy) stays selectable until the stick measures both. A mode change gives a new back buffer and scanout: the old imports are closed after the next fence and both are exported and imported again.
5. **An acquire timeout is `VK_TIMEOUT`** (user, 13 Nov 2026, after review), not `VK_ERROR_OUT_OF_DATE_KHR`: a program that polls with a short timeout must not rebuild its swapchain each time. The way out of a stuck app_server is the refused present.

Built in step 3b, and how it differs from the sections above:

- **The local fd** is a descriptor of `/dev/null`, a number the caller can close; a table in the library says what it stands for. A row is free again after its one use (`r5_drm_fd_to_token` for an export, `r5_drm_import_bytes` for an import), so the table of 64 rows does not fill.
- **Pending exports per backing:** a buffer whose exporter freed its handle before the import is held only by the pending export; the backing table has the size of the slot table (1024), and such a backing goes at most 5 s later.
- **`vram_usage` and `gtt_usage`** in `GET_INFO` count the memory (backings), not the handles, so a shared buffer is counted once.
- **Refusal words** in the kernel log line (`radeon_hd: refuse BO_IMPORT <why>`): `caller`, `team`, `token`, `expired`, `lost`, `target`; `BO_EXPORT limit`, `target`. An expired number is reported as `expired` when it is still in the table, `token` after the watchdog removed it.


## Built in step 4 (14 Nov 2026)

The shadow (A3), the frosted glass (A4), the three strengths and the "Window effects" box. Night run without the user; the choices below are the agent's and are listed in the step report for review.

- **The alpha of glass views (decision 16, checked on paper first).** Painter's `B_ALPHA_COMPOSITE` paths already write correct straight alpha over a transparent target: `BLEND_COMPOSITE` and `BLEND_COMPOSITE_SUBPIX` (`drawing_modes/DrawingMode.h`) take the target's alpha into account and write `alphaTemp / 255`, and they are what `B_OP_ALPHA` with `B_ALPHA_COMPOSITE` uses for text, shapes, lines, bitmaps and gradients (`DrawingModeAlphaCC`, `AlphaPC`, the `SUBPIX` forms). Every other mode writes alpha 255. `FillRectNoClipping` writes the colour's own alpha. So no Painter change is needed: `View::Draw` fills a glass view's background with its view colour and alpha 0 (only in the GPU mode with frosted glass on), and Painter does the rest. Glass views are in step 4, not M12.
- **Where the compose uses alpha.** Outside the glass region every pixel is opaque. In the glass region of the decorator (`GetGlassRegion()`: the R5 tab and the frame line) the pixel's alpha is ignored and the step's "tab see-through" is used. In a window with a glass view the alpha of its whole content area is used: alpha 0 shows the pixel's colour with the blur showing through by the same see-through value, alpha 255 is the pixel, and anything between is the pixel over the blur by its alpha. The content area and not the views' exact rectangles, because a view can move inside its window (layout) without a clipping rebuild, and the compose thread does not take the window lock.
- **Two pipelines (decision 16).** `compose.comp` is unchanged and its SPIR-V is byte-identical to step 2's; a frame in which no window on screen has a shadow or glass runs exactly step 2's commands. `effects.comp` is the second compute pipeline (three storage buffers, push descriptors), with four modes: shadow, Dual Kawase down, Dual Kawase up, glass. It unpacks to `vec4` and filters bilinearly itself. Both run on the GFX queue in the one submit with one fence per frame. Per window, back to front: the blur of what lies below it, then its shadow, then its opaque pixels with the copy shader and its glass pixels with the glass mode, then a barrier.
- **The shadow** is one byte per pixel per window: the footprint (tab and frame), blurred on the CPU three times along the rows and three times along the columns with a box of radius `softness / 3`, in a host-visible buffer counted in the window memory. It is made again only when the footprint, the window's focus or the step changes, by the compose thread, never under the window lock. It is drawn outside the footprint only, offset down, inside the extent from `GetShadowExtent()`. The R5 decorator's extent comes from the same table. A window without a decorator has no shadow, except menus (`kMenuWindowFeel`), which get the small one. Inactive windows and menus get half the softness and offset (decision 8). Focus changes rebuild the layer list (`Desktop::SetFocusWindow`), so the shadow follows.
- **The glass** keeps one full-size blur per window, one pixel per glass pixel, the rectangles of its glass region one after another. Dual Kawase with the step's levels (two: a quarter of the resolution) and an offset of `blur / 2^levels - 1` source pixels (at least 0.5). It is made again only where something below the window changed, grown by the blur's reach, or all of it when the window moved or resized, its glass region changed, the windows below it changed, or the step changed. Damage now says who caused it: a window's own drawing (`WindowHWInterface::ContentDamage`) and its moves and stack changes from the clipping rebuild count for the windows above it; a window that went away and the background count for everyone. For each glass rectangle the blur covers the box around what is new in it, from an input grown by the reach (Quiet 18, Balanced 29, Glassy 55 pixels), and that input is composed fresh in the same frame. The glass pixels are written only inside the real damage.
- **One table** of the starting values, `src/servers/app/WindowEffectLevels.h` (`distro/desktop/src/servers/app/`): the five values of decision 8 per step and the levels, used by the R5 decorator and the compositor; the shadow and glass geometry are derived there.
- **Settings in app_server:** `DesktopSettingsPrivate` keeps `window shadow`, `window frosted glass` (bools) and `window effect strength` (int32) in the `appearance` file. Missing keys: both on, Balanced. The log lines (`app_server: shadow on`, `frosted off`, `effect strength glassy`, and `animations on/off` for step 4b) come from `ServerApp` for every change, in both modes.
- **The box:** `WindowEffectsBox` (`distro/settings`, one source for Settings' Look and Feel page and the Appearance program's Look and feel tab), under the arrow style: "Shadow", "Frosted glass", "Strength:" Quiet / Balanced / Glassy as a pop-up menu, and the hint "Less effect uses less CPU". Each change is applied at once. Defaults: both on, Balanced. Revert: the values from when the page opened.
- **The compare** (`gpu_debug`) runs only on a frame without effects; with effects on screen it is skipped.
- **Measurement:** with `gpu_debug`, every 10 s after `compose median` a line `app_server: compose effects shadow <on|off> frosted <on|off> strength <step>`. `r5-effects-test` (in `/system/bin`) sets the effects from a shell and opens a glass, plain or full-screen glass window for the measurement.


## Built in step 4b (14 Nov 2026)

Window animations, `generated/gpu-m11/ANIMATIONS.md`. Night run without the user; the choices are listed in the step report.

- **Where:** only in the compositor, on the effects pipeline (decision 16): `effects.comp` mode 4 draws a window scaled about its centre and faded, mode 5 the workspace slide. Nothing changes for programs or Deskbar, and no new call: the flag `B_WINDOW_ANIMATIONS` of `set_window_effects()`, kept as `window animations` in the `appearance` file (missing: on). The "Animations" checkbox is the third in the "Window effects" box; Defaults turns all three on.
- **Open:** a window that gets its first buffer (its first show) fades in and grows from 95 % to 100 % in 150 ms, ease-out. **Close:** a window that is hidden (not minimised) or removed is drawn from its last buffer, over the others, fading and shrinking in 120 ms (a "ghost" that holds the buffer until it ends). A close during an open starts from where the open is. **Workspace:** the picture shown is kept, the new workspace is composed in full each frame, and the two slide past each other in 200 ms (new from the right for a higher workspace). A switch during a slide starts from the picture shown then.
- **The lengths and the scale** are starting values in one place, `WindowEffectLevels.h` (`kOpenAnimation`, `kCloseAnimation`, `kWorkspaceAnimation`, `kOpenAnimationScale`).
- **Never in the way:** a window is a normal window from its first frame; it draws, gets input and moves as always, and only its compose is drawn differently. The compose thread composes every refresh period while an animation runs, and ends each with one line, `app_server: animation <open|close|workspace> <n> ms frames <n>`.
- **Not animated:** windows without a decorator (menus, tool tips, the desktop, Deskbar), windows with direct access (`ServerWindow::IsDirectlyAccessing()`), hiding into Deskbar and showing out of it, a window moved to another workspace, windows that show because of a workspace switch, and the CPU mode (there is no compositor). An opening window is drawn without its shadow and glass until the animation ends.
- **The compare** (`gpu_debug`) runs only on a frame without effects and without an animation, so only on a still picture.


## Built in step 5 (14 Nov 2026)

The direct-window pause (decision 9) and the forced failure (decision 11). Night run without the user; the choices are listed in the step report.

- **Paused is "off for the desktop", for a while.** `Compositor::IsActive()` is false while paused; `IsEnabled()` stays true for the session. Everything in `Desktop`, `Window` and `ServerWindow` that asks `IsActive()` (clipping, moves, the background, the drawing regions) takes the CPU-mode path by itself. Windows made during a pause still get a window buffer (`IsEnabled()`), but draw to the screen until the resume. A view's shared images (3b) can still be offered during a pause.
- **The compose slot.** `fComposeSlot` is held by the compose thread for one frame, from taking the damage to `_Present()`. The compose thread never takes the window lock while it holds the slot: a failed compose is now handled at the top of the compose loop, outside the slot (the restart after a reset and the switch to the CPU mode both take `LockAllWindows()`). Lock order: `fWindowLock` (write) → `fComposeSlot` → `HWInterface` (exclusive in the compose when the scanout copy runs).
- **Pause** (`Compositor::DirectAccessStarting()`), from `ServerWindow::HandleDirectConnection()` with `B_DIRECT_START` before `DirectWindowInfo::SetState()`, and from `Desktop::LockDirectScreen()` (under `LockAllWindows()`): takes the slot, so it waits for the compose in flight (a late fence is not a failure), sets the pause, rebinds every window's engine to the screen, drops the layer list and the animations, and logs `app_server: compositor paused: direct window`. The desktop's CPU-mode redraw (`Desktop::CompositorTurnedOff()`: clipping, background, every window dirty) is done by the compose thread once the caller's write lock is released, because `HandleDirectConnection()` is also called from inside a clipping rebuild.
- **Resume** (`_ApplyMode()`, compose thread, under `LockAllWindows()`): when no window is directly accessing and no team holds the direct screen, every engine goes back to its window buffer, the clipping is rebuilt the GPU-mode way, every window draws once, the screen is damaged, and `app_server: compositor resumed` is logged. A move of a direct window sends stop and start under one write lock; the compose thread gets the lock after it and sees the access again, so a move neither resumes nor pauses.
- **Which windows count:** a set of windows with `IsDirectlyAccessing()` and a `DirectWindowInfo`, kept in `DirectAccessChanged()` after every state change and in `RemoveWindow()`. In Haiku every `BDirectWindow` gets a `DirectWindowInfo` when it is made, so showing any `BDirectWindow` pauses, also when it never draws directly (Chart with its display "Off").
- **A `BWindowScreen` program that dies** with the screen held kept `Desktop::fDirectScreenLock`, and the compositor paused, for good (a Haiku gap that the pause made visible: the whole desktop stayed in the CPU mode). The app's `ServerApp` thread, which took the lock, now gives it back when its message loop ends.
- **Shared images on the way out:** the switch to the CPU mode and a device that goes away (`_ExitVulkan()`, also for the restart after a reset) first give every image app_server holds (current, pending, retiring) back to its program (`_ReturnSharedImages()`), so no program waits in its acquire for an image that will never be shown; its next present is refused and it makes a copy swapchain. During a pause the shares stay; presents keep rotating the pending image and the overtaken one goes back by its mark, as in 3b.
- **During a pause** no compose runs, so no effect, no animation and no zero-copy compose.
- **Open (found in step 5): the forced failure crashes app_server on Lavapipe.** With `gpu_compositor_fail true`, a few seconds into the boot, a new app_server thread (`a:186:x-vnd.Haiku-notification_`) crashed in the runtime loader while copying its thread-local storage (`TLSBlockTemplates::Materialize`, `copy_small`). The switch to the CPU mode ends the compose thread, which destroys the Vulkan device and instance; the likely cause is that `vkDestroyInstance()` makes the Vulkan loader unload the driver (Mesa uses thread-local storage) while another thread starts. Keeping app_server's own `dlopen` handles did not help (tried once, undone). Not fixed: the next try would be to keep the instance (or a `dlopen` of the driver) for the session. This path is from step 2 and had not run before.
