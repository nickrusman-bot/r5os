# M11 brief: windows composed on the GPU (11 Nov 2026, completed 13 Nov 2026)

For Cursor or Claude. It does not override `AGENTS.md` or `docs/GPU-ARCHITECTURE.md`. Read those first, plus `docs/GPU-ARCHITECTURE.md` "2D in app_server", "Default and fallback", "When things go wrong", "From opt-in to default", `docs/APP-SERVER-COMPOSITOR.md` (the step 0 design, including "Decided after step 0"), `docs/GRAPHICS.md` "GFX9 reset on GC 9.3 (fixed)", and `generated/gpu-m8/BRIEF.md` (the copy this builds on). Where this brief and `docs/APP-SERVER-COMPOSITOR.md` disagree, stop and ask; do not pick one.

**Rules for the agent (read before anything else):**
1. **Read `AGENTS.md` in full first**, then this brief. Your first reply names the `AGENTS.md` rules that apply to your step (at most five lines) and what you will and will not do. No file is changed before that reply.
2. **Only the step the prompt names.** No other M11 step, no item from the roadmap, the after-M10 list or another brief, no clean-up or refactor on the side. Something you notice goes under "Seen, not done" in your report.
3. **Stop and ask** when the brief, `docs/APP-SERVER-COMPOSITOR.md`, `AGENTS.md` or the source disagree, when a step needs something outside its files, or when a test fails twice. Do not choose on your own and do not work around it.
4. **Ask before every `git push`, PR, stick write, merge into `main` or change outside the repository** (`AGENTS.md`, development loop step 8). Never merge, never force-push, never push to `main`. PRs as `AGENTS.md` says (Cursor `ManagePullRequest`, otherwise the compare URL; `gh pr create` only when the user asks).
5. **Do not change** `AGENTS.md`, the fixed GFX9 contracts in `docs/GRAPHICS.md`, `distro/BUILD` or What's new, unless the prompt says so.
6. **Only one agent jams at a time.** Ask before you jam in the shared WSL tree.
7. **Never claim a test that did not run.** Report Veranderd / Getest / Onbewezen / Documentatie, with the pass lines quoted, and stop. The next step starts only with a new prompt from the user.

**Status (13 Nov 2026):**
- M10 is done (build 10061, PR #4233): GC 9.3 `0x15e7` is release-supported, the reset and the hang policy are tested on the 5625U. The condition "a hang must not freeze the composed screen" now has a kernel to stand on.
- Step 0 is done and merged (`docs/APP-SERVER-COMPOSITOR.md`, three PRs up to `2e513bbe9f`). Its three open questions are decided ("Decided after step 0").
- **Order:** first PR 2 of the after-M10 round (`generated/CURSOR-AFTER-M10.md`: the Network pages in Settings and `.local`). PR 1 (the air/OS kernel fixes) is not part of this round (user, 13 Nov 2026). M11 starts with step 1 once PR 2 is merged into `main`. Before that, only paper work of M11 may run (the pictures of steps 1b and 1c), because only one agent jams at a time and that round's stick must be built from a quiet `main`.
- Decision 14 (a hang by another program) is decided: restart once after a successful reset.

**M11 is done when, on the 5625U:**
- in the **GPU mode**, every window has its own GPU buffer, and app_server composes the screen with Vulkan on RADV;
- with effects off, the composed screen is the same picture as the CPU mode (a read-back compare passes);
- Vulkan windows (VulkanTeapot, `vkcube`) are shown without a copy through the CPU;
- the R5 window frame of decision 2 is visible, with its small shadow and frosted glass;
- "Shadow" and "Frosted glass" in Settings, Appearance, Look and feel each turn their effect off and on at once, independently;
- a `BDirectWindow` program still works;
- when the GPU path fails (forced, or a fence timeout), the desktop switches to the CPU mode without a freeze, and the log says why in one line;
- in VirtualBox, and on every machine without a supported GPU, the GPU mode runs on Lavapipe with the same look and effects (the Windows 8 way: always composed, in software when there is no GPU), and its compose time is measured and recorded;
- only when Lavapipe fails too does the desktop fall back to the CPU mode.

---

## What exists today

- **One shared back buffer.** Painter (AGG) draws every window on the CPU into app_server's single back buffer, clipped to each window's visible region. A window has no buffer of its own. When a window moves, the windows under it get an update and redraw.
- **The last copy is on the GPU (M8):** the back buffer is a GTT buffer, and `DMA_DATA` copies the dirty bands to the scanout. `R5_GPU_SCANOUT_BO = R5_GPU_OP_BASE + 31` returns the scanout surface. Fallback per session to the CPU copy.
- **Vulkan in a window (M7):** `VK_RFIVE_view_surface` (Khronos-registered). The swapchain image is copied into a `BBitmap` that the window draws. `R5_GPU_BO_EXPORT` (`+ 17`) and `R5_GPU_BO_IMPORT` (`+ 18`) are in the interface, but app_server does not use them yet.
- **app_server is not a Vulkan client.** It talks to the GPU only for the M8 copy.
- **Decorators:** Haiku's `DefaultDecorator` is copied in `distro/desktop/src/servers/app/decorator/`. There is no R5 decorator yet. The Haiku decorators (Be, Flat, Mac, Win) are installed.
- **M10 (13 Nov):** at boot four checks set `R5_GPU_CAP_AVAILABLE` (8) in `GET_INFO`; one line `radeon_hd: gpu available` or `gpu not available: <reason>`. The GFX9 switches default to on for `0x15e7`; `0x1636` and `0x1638` stay opt-in. The compositor reads the bit, not the settings file, to decide RADV or Lavapipe (`gpu_compositor` itself stays a setting). M8's copy (`gpu_app_server`) stays opt-in: on stick 46 it measured 112 µs and 217 ms CPU per 10 s against the CPU copy's 0 µs and 8 ms, because every small copy waits for its fence. A1 uses that copy only once per composed frame, after the compose fence, so this cost is per frame, not per band; step 2 measures it.
- **The hang policy (M10):** the watchdog declares a hang after 10 s. Then every file with a context is marked lost (`ECANCELED` on submits and waits, `R5_GPU_CAP_LOST`) until it destroys its context; a mode-2 reset follows, then the self-test, and new contexts work again. A second hang, a failed reset or a failed self-test: `gpu off until reboot`, no new contexts. There is one GFX ring and no priority: a long job of another program delays the compose.
- **Mesa** is a release build (`mesa-25.3.6-5`, `mesa_radv-25.3.6-7`, `mesa_lavapipe-25.3.6-4`); the pin is `pin-mesa-packages.sh`.
- **Open from M10, relevant here:** as the standard account `test`, r5-gl-test and VulkanTeapot timed out (180 s, no output). app_server itself runs as uid 0, so the compositor is not affected, but a zero-copy test of VulkanTeapot in a standard account can be.
- **No vsync:** DCN is not programmed; the firmware scanout stays. Tearing is not an M11 failure.
- **Other forks (read, nothing copied):** none of them composes windows on the GPU. air/OS: app_server still draws with Painter and copies back to front; only the browser Summit composes its own page with GL and Skia. RenkuOS: nothing in app_server. haiku-pi (`haiku-rpi/docs/design/gpu-present.md`, no licence, read only): measured that app_server's two CPU copies per GL frame (bitmap into the back buffer, back buffer to the screen) are the main cost. That is why the zero-copy step comes right after the first compose.

---

## Decisions (fixed for this brief)

1. **Two modes for the whole desktop, never mixed.**
   - **GPU mode:** the compositor composes on the GPU. In M11 Painter still draws each window's content into that window's buffer; from M12 the GPU draws it too.
   - **CPU mode:** app_server as Haiku has it today (one back buffer, Painter, the M8 copy when that is on). It is the emergency fallback, not a normal mode.
   The choice at session start, in this order: GPU mode on RADV when a supported GPU passes its checks; otherwise **GPU mode on Lavapipe** (software, decided by the user on 11 Nov 2026: hardware is fast enough today, as with Windows 8 and WARP); only if Lavapipe cannot start either, the CPU mode. Until M11 passes, the GPU mode is opt-in on every machine (`gpu_compositor true` in the `radeon_hd` settings). After M11 passes it is the default: on Lavapipe everywhere, and on RADV for GC 9.3 under "From opt-in to default". `gpu_compositor false` is the opt-out to the CPU mode. `gpu false` turns off the GPU, and the GPU mode then runs on Lavapipe.
2. **The R5 window frame, chosen by the user on 12 Nov 2026.** One R5 decorator, the same in both modes. It starts from Haiku's `DefaultDecorator` and changes only this:
   - **Kept as Haiku has it:** the tab (square, compact height, bold title, Haiku yellow with its gradient), the grey tab of an inactive window, the close box on the left and the zoom box on the right with their relief, and square window corners. Sizes and colours come from the Haiku decorator and the user's colour settings, not from new constants.
   - **Frame:** a single 1-pixel line instead of Haiku's wide bevelled border.
   - **Resize:** no drawn knob. The window is resized by dragging its edge. Because the line is only 1 pixel, the grab area is wider than what is drawn. Step 0 proposes its width and where it lies (inside or outside the line).
   - **Shadow:** a small, short shadow, in the GPU mode only. The CPU mode has none.
   - **Frosted glass:** the tab and the window background are translucent and blur what lies behind them, in the GPU mode only. The CPU mode draws them opaque, as Haiku does. The window background is only glass where the program asks for it (decision 13); a program that does not ask is unchanged, and then only the tab and the frame line are glass.
   The round variant (`docs/design/sign-in-reference.jpg`, large round corners) is not in M11. It can come later as a second R5 decorator. The Haiku decorators stay selectable and keep working in both modes.
3. **The compositor lives inside app_server,** as a new layer between the windows (`Desktop`, `Window`) and the screen output (`HWInterface`). No separate process, no new protocol for applications. New R5 files are MIT with an SPDX header (the default since 12 Nov 2026); Haiku files keep their MIT header, and R5 changes in them get SPDX-Snippet tags.
4. **Built on Vulkan.** app_server becomes a Vulkan client of RADV, through the same loader and ICD as applications. No vendor code in the compositor: the same code runs on ANV later, and on Lavapipe as the software path. Shaders are GLSL, compiled to SPIR-V at build time by a host `glslangValidator` into a generated header that is never edited by hand (the same rule as `gfx9_compute_shader.h`). Step 0 reports glslang's licence (project, SPDX, copyright) before it is added to the build. The composed frame reaches the scanout through `R5_GPU_SCANOUT_BO`; decided after step 0 (user, 12 Nov 2026): in A1 the M8 `DMA_DATA` copy after the compose; the import of the scanout buffer into RADV comes in A2 (step 3), with zero-copy windows, because both need `BO_EXPORT` / `BO_IMPORT` and PRIME in the R5 DRM library.
5. **One GPU buffer per window,** allocated through Vulkan (decided 13 Nov 2026, second opinion agreed): host-visible and host-coherent memory, host-cached first (Painter reads what it blends), mapped for Painter. On RADV that is an R5 buffer in GTT underneath, which the kernel keeps cached and snooped unless `gfx9_wc_gtt true`; on Lavapipe it is system memory. One code path for RADV, Lavapipe and later ANV; no import of an app_server-made `R5_GPU_BO_CREATE` buffer. `DEVICE_LOCAL` is not excluded (RADV may mark GTT that way on an APU); the domain and the caching are what count, and they are checked:
   - one line at the first window buffer: `app_server: window memory type <i> flags <hex>`; on the stick, with `gpu_selftest true`, next to the kernel line `bo domain 2 ... snooped 1`;
   - a short Painter write test on the stick: 100 typical redraws at 1080p into a window buffer and into a `malloc` buffer, both times logged; much slower in the window buffer means the memory type is wrong;
   - the budget from `VK_EXT_memory_budget` where it gives real numbers, otherwise the rule of "The memory budget". Freeing the buffers of hidden windows first stays required.
   - Later: on ARM64 the CPU cache and the GPU are not always coherent by themselves; checked again there. In M12 the GPU draws the content, and the window buffer becomes a Vulkan image.
   - The size is the window frame plus the decorator and the shadow margin.
   - A resize allocates a new buffer.
   - Each window draws its whole content, including parts that other windows cover. Moving a window no longer makes the windows below redraw.
   - Memory: 4 bytes per pixel, about 8 MB for a 1080p window. app_server logs the total every 10 s with `gpu_debug true`. There is no hard limit in M11; step 0 proposes a budget and what happens above it.
6. **Vulkan and OpenGL windows without a copy (A2).** The program exports its swapchain image (`R5_GPU_BO_EXPORT`), and app_server imports it (`R5_GPU_BO_IMPORT`) and samples it directly. Sync objects order the program's rendering and the compose. When export or import is not possible (Lavapipe, an old program), the M7 copy path stays. A new interface call, if one is needed, is additive (`R5_GPU_OP_BASE + 32` onward), vendor-neutral, and documented in the header and `docs/GPU-ARCHITECTURE.md` in the same change.
7. **When it composes.** Only when something changed (damage), at most once per refresh period. The period comes from the current display mode, never from a constant. app_server waits for the fence with a timeout of 100 ms, as in M8.
8. **Effects, in this order:**
   - A1: compose only, with exactly the same pixels as the CPU mode;
   - A2: zero-copy Vulkan and OpenGL windows (point 6);
   - A3: the small shadow of decision 2;
   - A4: the frosted glass of decision 2.

   Round corners belong to the later round decorator, not to M11.
   **Two separate settings** (user decision, 12 Nov 2026), in a "Window effects" box on the Look and feel tab of the Appearance page: a "Shadow" checkbox and a "Frosted glass" checkbox. Each works on its own. A new control, not a reorder: the tabs and the layout in `AGENTS.md` stay. A change applies at once; Defaults turns both on; Revert restores both values from when the page was opened. With both off, GPU mode still composes (A1 and A2 stay). The separate Appearance program gets the same two checkboxes.
   **An effect strength with three steps** (user decision, 13 Nov 2026): **Quiet**, **Balanced** and **Glassy**, a three-step slider (or pop-up menu) in the same "Window effects" box, under the two checkboxes, with the hint "Less effect uses less CPU". **Balanced is the default**; Defaults sets Balanced, Revert restores the step from when the page was opened. One step sets both the shadow and the glass. Starting values, from the variant page chosen on 13 Nov 2026 (`https://claude.ai/artifact/8ybnaN7yRuAa49kX7NZJGw`), browser blur standing in for Dual Kawase:

   | | Quiet | Balanced | Glassy |
   |---|---|---|---|
   | Shadow offset down | 2 px | 4 px | 7 px |
   | Shadow softness | 6 px | 14 px | 24 px |
   | Shadow strength | 28 % | 34 % | 40 % |
   | Glass blur | 4 px | 9 px | 16 px |
   | Tab see-through | 14 % | 28 % | 44 % |

   The values live in one table in the R5 decorator and the compositor (no constants spread around), as starting values for the measurement. Inactive windows and menus (menus are windows in Haiku) get a smaller shadow than the active window, as macOS and Windows do: half the softness and offset of the chosen step. Glass is the same step on RADV and Lavapipe; the compose time per step is measured on both and recorded (a measurement, not a gate). The step is its own call pair in `<WindowEffects.h>`, after the existing ones, BeOS style like `ui_color()`: `enum window_effect_strength { B_WINDOW_EFFECTS_QUIET = 1, B_WINDOW_EFFECTS_BALANCED = 2, B_WINDOW_EFFECTS_GLASSY = 3 };`, `window_effect_strength()` and `set_window_effect_strength(window_effect_strength)`; app_server stores it as a third key in its `appearance` file (missing means Balanced). Documented in `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md` and `docs/APP-SERVER-COMPOSITOR.md` in the same change.
   **A third checkbox, "Animations"** (user decision, 13 Nov 2026, step 4b), in the same box, default on: open and close (fade and scale) and the workspace switch (slide). Off is instant. It is the third flag of the same call pair, `B_WINDOW_ANIMATIONS = 0x00000004` after `B_WINDOW_SHADOW` and `B_WINDOW_FROSTED_GLASS` in `<WindowEffects.h>`; no new call. Defaults turns all three on; Revert restores all three. Hide and show into Deskbar are not in M11 (they need Deskbar to tell app_server where a window's line is; with Deskbar NextGen).
9. **Programs that write straight to the screen** (`BDirectWindow` with direct access, `BWindowScreen`): while such a window has direct access, the compositor pauses and the screen is drawn the CPU-mode way. When it closes, composing resumes and every window is redrawn once. Step 0 designs that switch, including the lock order. `BDirectWindow` programs that do not ask for direct access are ordinary windows.
10. **Decorator API: hooks at the end, with a version check (user, 12 Nov 2026).** Only decorator add-ons are affected, not applications. The hooks for the shadow extent and the glass region are virtual functions at the end of `Decorator`, with defaults (the default shadow, no glass). Every decorator add-on exports a decorator ABI version; `DecorManager` refuses an add-on without the current version with one line and uses the built-in default, so an old add-on never crashes app_server. The Haiku decorators in the tree (Be, Flat, Mac, Win) are migrated: rebuilt with the version, they implement the hooks, and they are made nicer (step 1b). Details: `docs/APP-SERVER-COMPOSITOR.md`, "Decided after step 0". A hook for a corner radius comes with the round decorator, later, also at the end. The decorator header stays MIT.
11. **Fallback.**
    - At session start: no render node, or RADV cannot create the device → Lavapipe, with one line `app_server: gpu compositor on lavapipe: <reason>`. Lavapipe cannot create the device either → CPU mode.
    - During a session, as refined by decision 14: a submit fails, the context is lost and the restart after a reset fails or is the second one, or the GPU is off until reboot → the CPU mode for the rest of the session. A late fence alone is not a failure. app_server redraws every window into the shared back buffer and logs one line: `app_server: gpu compositor off: <reason>`. It does not move to Lavapipe mid-session, never retries in the same session, and never blocks the desktop for longer than the one timeout.
12. **Lavapipe is the software path, and is measured.** In VirtualBox the compose time per frame and the CPU time are logged with and without effects, and recorded in `CHANGELOG.md`. That is a measurement, not a gate: the effects stay on by default, and the user can turn them off. `gpu_compositor_lavapipe true` (test switch, off by default) forces Lavapipe on a machine with a GPU, for comparison on the 5625U.

13. **The BeOS/Haiku way for everything a program or developer sees.** The compositor, Vulkan and the shaders are an engine inside app_server; no program and no public header sees them. What a developer does see follows the Haiku pattern:
    - **Programs:** a program asks for a glass background through an R5 kit call, opt-in, in `libr5ui` (the Appearance Kit, namespace `BAppearanceKit`), on a `BWindow` or `BView` it already has. Not in `libbe` (`AGENTS.md`, LibR5). Do not reuse `B_TRANSPARENT_COLOR` for this: existing programs set it to mean "do not erase", and they must not turn into glass. A program that links only `libbe` looks and behaves as today. In the CPU mode the call is accepted and the background stays opaque.
    - **Settings:** Shadow and Frosted glass are read and set through one `libr5ui` call pair, used by both the Settings page and the Appearance program, the way Haiku's Appearance uses `set_decorator()` and the `ui_color` calls. app_server stores them with its other appearance settings. No new settings file for programs to write.
    - **Decorators:** the existing Haiku decorator add-on model (`/system/add-ons/decorators`, the `Decorator` class); the new hooks at the end, plus the add-on version.
    - **Documentation:** each new call in `docs/LIBR5.md` and `docs/APPEARANCE-KIT.md` in the same change, with a short example. Step 0 names the calls and their header; step 1 and later do not invent others.

14. **A hang by another program (decided by the user, 13 Nov 2026: restart after a reset).** With one GFX ring, a heavy job of a game or a hang makes the compose wait. Decision 11 as written would turn the GPU mode off for the whole session after one late frame, and after every reset, because the reset marks app_server's context lost too. Proposal:
    - A compose fence that misses the 100 ms is **not** a failure by itself. The compositor keeps the last frame on the screen, accumulates the damage, and logs `app_server: compose late <n> ms` once per second at most. Input keeps working; the mouse pointer is drawn by `HWInterface` and stays live.
    - The compositor turns the GPU mode off only when the kernel says so: its context is lost (`ECANCELED`, `R5_GPU_CAP_LOST`), the GPU is off until reboot, or a submit fails. A wait longer than the 10 s watchdog does not happen, because the watchdog decides first.
    - After a **successful reset** (`gpu selftest after reset ok`, the bit is set again), app_server destroys its lost context and its Vulkan device and creates them once on RADV again; every window buffer is redrawn once. Logged as `app_server: gpu compositor restarted after reset`. A second loss in the same session, or a failed restart, turns the GPU mode off for the session, as decision 11. `gpu off until reboot` → the CPU mode at once (not Lavapipe mid-session, decision 11).
    - The desktop never waits longer than one 100 ms period for the GPU while holding a lock that input or drawing needs (`docs/APP-SERVER-COMPOSITOR.md`, lock order).
    - Not chosen: every fence timeout and every reset → the CPU mode for the session. Simpler, but one game hang would end the composed look until the next Sign in, and a heavy job of a game could trip it without any hang.

15. **Sharing a GPU buffer between teams (decided 13 Nov 2026, second opinion agreed; step 3a works it out).** Haiku has no dma-buf. The basis:
    - **A token for one target.** `R5_GPU_BO_EXPORT` (the existing number) takes the local handle and a target and returns a token the kernel makes from a secure random source, at least 128 bits. In the first version the only target is "the compositor": `R5_GPU_BO_IMPORT` (the existing number) succeeds only when the caller is `/system/servers/app_server`, checked the way `R5_GPU_SCANOUT_BO` checks it. A field for one named target team is in the structure for later (for example screen recording), unused now. Any other caller gets `B_NOT_ALLOWED`.
    - **One use, short life, a limit.** A token works for one import and is never reused. A token that is not imported within a short time expires. Each file has a maximum number of open exports, so a program cannot use up GTT with exports.
    - **The token travels in the existing `BMessage`** of the view-surface protocol to app_server; no second channel, no file-descriptor passing.
    - **The kernel keeps the buffer alive** with a reference count: each file that holds a handle, plus the GPU work in flight. A program that frees its handle or crashes takes away only its own handle; what app_server imported stays valid until app_server closes it. Exports not yet imported are removed when their file closes.
    - **app_server believes the kernel, not the program:** `BO_IMPORT` returns the real size, and app_server checks width, height, pitch and format against it. A wrong value can only spoil that program's own window.
    - **Sync:** two one-way sync objects per swapchain image (program → app_server "ready", app_server → program "free"), shared with the same token rule. The compose never waits for a program's sync: when it has not signalled, the previous frame of that window is shown. New calls for exporting and importing a sync object are additive, at the first free numbers, vendor-neutral, documented in the header and `docs/GPU-ARCHITECTURE.md`.
    - **No patch in RADV:** on app_server's side the R5 DRM library turns an imported token into a local file descriptor, so RADV sees the ordinary `VK_KHR_external_memory_fd` (and the sync-object fd import) it already has. Step 3a checks this against RADV's import path and the WSI (`wsi_common_haiku.cpp`); if it does not fit without a RADV change, stop and ask.
    - **A GPU reset:** the imported buffers of a lost file are closed with it; decision 14 restarts the compositor, and the windows of Vulkan programs fall back to their last frame until the program presents again.

15b. **The open questions of step 3a (decided 13 Nov 2026, after two rounds of second opinion, checked against the source).** These refine decision 15; where they differ, 15b wins.
    - **Size at import:** a fifth R5 patch in libdrm (`distro/graphics/libdrm-r5/patches/`, read by `haiku.py`): `amdgpu_bo_import` asks the R5 library for the size instead of `lseek(fd, 0, SEEK_END)`. No `shm_open`, no patch in RADV. `r5_translate.c` cannot catch it, because `lseek` is not an ioctl. Step 3a checks in Mesa (WSL tree) that RADV does no `lseek` of its own (`radv_amdgpu_winsys_bo_from_fd`).
    - **The token, the BeOS way (user, 13 Nov 2026):** an export number like an `area_id`: unique and never reused (a 64-bit counter mixed with `secure_random_value()`), in an opaque 128-bit field so its contents can change later without changing the interface. The security boundary is the kernel's rights check (the exporting team pinned at export and matched against `ClientTeam()`, only app_server imports, one use, 5 s), not a secret. No copy of the random bus manager (it offers only `queue_randomness`, no read). Test: a guessed or forwarded number from another team is refused.
    - **"Ready" is marked at present, not shared as a sync object.** The kernel gets no buffer list per submit today (`r5_translate.c` skips `AMDGPU_CHUNK_ID_BO_HANDLES`; `gpu_submit` takes IBs, waits and signals). So: when the program presents an image, the WSI asks the kernel (one new additive call, vendor-neutral) to record on that exported buffer the ring number of that file's last submit. The kernel takes the number itself; the program cannot give one. app_server asks with timeout 0 whether the ring has passed it; if not, the previous frame of that window stays. **"Free again"** goes back as a link message after app_server's compose fence; the WSI waits for it. No sync objects are shared between teams, so the sync part of decision 15 is replaced.
    - **Transport and sender:** a new link message next to `SetViewBitmap` (the CPU path stays as it is). The kernel pins the exporting team at export; app_server passes `ClientTeam()` of the connection to the import, and the kernel refuses a token from another team.
    - **`GEM_OP` and `GEM_METADATA`** (today `ENOSYS` through `r5_unknown`): `GEM_OP` returns the real size and domain from the kernel; `GEM_METADATA` returns neutral values for a linear image. Shared swapchain images are linear (the WSI is R5 code); the cost of linear rendering is measured. Step 3a checks in Mesa how RADV reacts today.
    - **Numbers, as starting values:** a token lives 5 s; at most 32 open exports per file. **`kMaxBo` is a problem already today:** the system limit and the per-client limit are both 256 (`r5_gpu_internal.h`), so one program can take every slot and no other program, app_server included, gets a buffer. In step 3b the system limit goes up and the per-client limit goes well below it; both values come from measuring how many buffers RADV makes for VulkanTeapot (step 3a measures or reads it), not from a guess.
    - **After a reset:** a Vulkan window shows its window background until the program presents again (programs get `DEVICE_LOST` and make a new swapchain). No CPU copy is kept for this.

16. **How the compose draws the effects (decided 13 Nov 2026, second opinion agreed).**
    - **Compute and `VkBuffer`s stay** (as built in step 2). A graphics pipeline only pays off once the GPU draws the windows into images (M12) and the compositor presents straight to the screen.
    - **Two paths.** With the effects off, the copy shader of step 2 stays as it is: 32-bit words copied without conversion or filtering, so the compare stays bit-exact. The effects get a second compute pipeline: unpack to `vec4`, blend with alpha, bilinear filtering written in the shader where a step needs it.
    - **Shadow:** an alpha mask per window, blurred once at create and resize, kept; per frame it is only blended. The damage of a window that changes or moves grows on the shadow side by the offset plus the shadow radius.
    - **Frosted glass:** Dual Kawase, starting at two levels down and two up (a quarter of the resolution); the level is a starting value for the measurement, not a constant. The blurred result is kept per glass area and redone only when there is damage beneath it, or the glass window moves or changes size. Damage under or next to glass is grown by the blur radius for the input; the blur runs only for `(grown damage) ∩ (glass regions)` and skips entirely when that is empty; it writes only inside the real damage.
    - **One submit and one fence per frame,** as in step 2, with barriers between dependent layers.
    - **Alpha only inside glass (13 Nov 2026).** Painter does not write reliable alpha into a 32-bit buffer. The compose uses alpha only inside the glass region (the tab, the frame line, and views that asked for glass); outside it every pixel is opaque, whatever its alpha says. Glass views need correct alpha from what the program draws on them: step 4 first checks Painter's `B_ALPHA_COMPOSITE` paths on paper; if that is not small, glass views come with M12 (Skia keeps alpha) and M11 has glass on the tab and the frame line, with `set_view_glass()` accepted and stored.
    - **The queue stays the GFX queue in M11.** The R5 DRM library accepts only GFX submissions (`r5_translate.c`, the IB check), so a compute-only queue family would make every submit fail. Step 2 logs the queue families once (`app_server: vulkan queue family <i> flags <hex>`) and keeps the choice on a family with the graphics bit. Priority through the compute queue (MEC) for user work is after M11 (see "Not in M11").

**References (read, do not copy):** haiku-pi `docs/design/gpu-present.md` (no licence). air/OS Summit (licence unclear). hans2294/haiku-amdgpu (MIT) patches `0010` and `0011` (app_server giving up a direct window) may be adapted with credit. X547's HaiWay (MIT) for surface locking and resizing.

---

## Steps

| Step | What | Where | Pass line |
|---|---|---|---|
| 0 | **Design on paper**, `docs/APP-SERVER-COMPOSITOR.md`: today's draw and update path (files, functions, locks); where the compositor hooks in; the grab area for resizing by the edge (decision 2); the window buffer life cycle (create, resize, hide, close); the compose pass and its shaders; the scanout path; zero-copy and its sync; the direct-window pause; the switch to the CPU mode; the memory budget; the glslang licence report; the measurement. Also the edits to `docs/GPU-ARCHITECTURE.md` ("2D in app_server" steps 2 and 3, milestone rows M11 and M12). The user reviews it before code | – | **done** (12 Nov 2026), decisions recorded |
| 1 | **The R5 decorator** (both modes), as decision 2: Haiku's tab and buttons, the 1-pixel frame, resizing by the edge without a knob (the input margin of `docs/APP-SERVER-COMPOSITOR.md`, "The resize grab area": as wide as Haiku's border, outside the line, `GetInputMargin()`, `Desktop::WindowAt()`), the shadow hook with its default. Selectable next to the Haiku decorators. Also the three hooks at the end of `Decorator` (`GetShadowExtent`, `GetGlassRegion`, `GetInputMargin`), the add-on ABI version and its check in `DecorManager`, and Be, Flat, Mac and Win rebuilt with the version (no visual change yet). No compositor code, no `libr5ui` call yet | VirtualBox or QEMU: Sign in, Tracker, Terminal, drag, resize from each edge and corner just outside the line, a scroll bar at the right edge of Terminal still scrolls, a `B_NOT_RESIZABLE` window has no margin, switch to each decorator; an add-on without the version is refused | `decorator: r5 loaded`; `app_server: decorator <name> refused: abi <n>, need <m>` for the old add-on; screenshot of the CPU look in the report |
| 1b | **Nicer Haiku decorators.** First on paper: one picture per decorator (Be, Flat, Mac, Win), before and after, each one still recognisable, for the user to approve. Then each gets its shadow and, where it fits, glass through the hooks, plus the approved changes | VirtualBox: switch to each decorator, active and inactive, CPU mode and GPU mode on Lavapipe | screenshots of each decorator, before and after, in the report |
| 1c | **A new decorator in the style of Windows Whistler build 2296** (the user's wish, 12 Nov 2026). Drawn entirely in code (shapes, gradients, its own buttons); no bitmaps or other parts from Windows or a `.msstyles` file, and no Microsoft name or logo in the decorator's name or files. The user picks the name. Reference: `generated/gpu-m11/whistler-2296-reference.png` (the About window of build 2296; look only, never in git or the image, because it shows Microsoft logos). What it shows: a flat mid-blue title bar with a bold white title on the left; a small pattern of fading blue squares towards the right of the bar; a square close box at the far right with a white X and a thin light outline; a thin blue frame of the same colour around the whole window; square corners; a light grey body. The "Comments?" link of the beta is not taken. Colours are measured from the reference into the picture, not guessed. First on paper: a picture of the active and inactive window, with the colours and buttons it takes from that build, and three name proposals, for the user to approve and pick. Then the add-on, with the version and the shadow hook (no glass). New files MIT | VirtualBox: switch to it, active and inactive, CPU mode and GPU mode on Lavapipe | `decorator: <name> loaded`; screenshots in the report |
| 2 | **The compositor core (A1)** behind `gpu_compositor`, with the window buffers, the damage and timer logic, the fallback, and a compare mode (`gpu_debug true`: every 100th frame app_server composes the same frame on the CPU path and compares) Also the memory budget with freeing the buffers of hidden windows first (user, 13 Nov 2026: in step 2, not later), decision 14, and the covered-parts change ("Decided after step 0", point 1) in `CHANGELOG.md` | VirtualBox with `gpu_compositor true` (Lavapipe); Pulse, ActivityMonitor and a clock half under another window, then that window moved away | `app_server: gpu compositor on lavapipe: no render node`, `app_server: compose compare ok`, never `compose compare MISMATCH`; the half-covered content is correct at once; with the setting off, no `compositor` line |
| 3a | **Design on paper** for zero-copy, from decision 15: the export and import structures (`+ 17`, `+ 18`), the token, its target, expiry and limit, the reference count, the two sync objects and their new calls, the protocol message with the tokens, the R5 DRM library's token-to-fd step and how RADV and the WSI use it (checked in the Mesa source), the scanout import into Vulkan, the reset path, the pass and fail lines. Checked in the Haiku source (WSL tree): the kernel's secure random source, `get_port_message_info_etc` / `BMessage` sender, that areas are not used for GPU buffers. In `docs/APP-SERVER-COMPOSITOR.md` and `docs/GPU-ARCHITECTURE.md`. The user reviews it before code | – | – |
| 3b | **Zero-copy windows (A2)** as the 3a design (`docs/APP-SERVER-COMPOSITOR.md`, #4251, #4252) and decision 15b: the kernel calls and limits, the R5 DRM library with the fifth libdrm patch, the WSI (own `AppServerLink`, linear images, a semaphore per image) in a new `mesa_radv` package, app_server's import, `r5-share-test`, and the scanout in Vulkan (one `vkCmdCopyBuffer` in the compose submit, replacing the CPU copy and the M8 `DMA_DATA` copy only if it measures no worse; its own commit). Kernel code: SMAP review and the QEMU SMAP boot before the jam | QEMU or VirtualBox: the copy path on Lavapipe only, desktop as after step 2; a host test of the number, rights, expiry, limit and reference-count logic. The real path, `r5-share-test` and the scanout import only on the 5625U (stick) | `app_server: window <id> zero-copy off: lavapipe`; the host test passes; on the stick the `r5-share-test` lines of the design and both copy times side by side in `CHANGELOG.md` |
| 4 | **The shadow (A3) and frosted glass (A4)** plus the two checkboxes in Settings and in Appearance | VirtualBox with Lavapipe, the M11 scenario for 60 s | `app_server: shadow on` / `shadow off` and `app_server: frosted on` / `frosted off` when toggled; `app_server: compose median <n> us cpu <n> ms per 10 s` with and without effects, recorded in `CHANGELOG.md` |
| 4b | **Window animations** as `generated/gpu-m11/ANIMATIONS.md`: open and close (fade, scale 95 % → 100 %), the workspace switch (slide), and the "Animations" checkbox (decision 8). In the compositor only, on the effects pipeline of decision 16; programs and Deskbar unchanged. An animation never delays input; a new event starts from where the running one is. Not animated: windows with direct access, the CPU mode. The compare runs only on a still frame with effects and animations off. Lengths are starting values, logged | VirtualBox or QEMU with Lavapipe: open and close Terminal ten times, switch workspaces ten times, type into a window while it opens, the checkbox off and on | `app_server: animations on` / `animations off` when toggled; `app_server: animation <open\|close\|workspace> <n> ms frames <n>` per animation; compose median during animations, recorded in `CHANGELOG.md` |
| 5 | **The direct-window pause and the forced failure** (`gpu_compositor_fail true`, test switch: the first submit fails) | VirtualBox: a `BDirectWindow` test program, and the forced failure | `app_server: compositor paused: direct window` and `resumed`; `app_server: gpu compositor off: forced failure` exactly once |
| 6 | **The stick** (one, after M10 has passed) | 5625U | below |

PRs: step 0 alone (documentation, done). Step 1 alone (the decorator does not need the compositor). Step 1b: the pictures first (for approval), then its own PR. Step 1c: the same, its own PR. Steps 2–5 (with 3a, 3b and 4b): **one PR per step** (user, 13 Nov 2026; step 2 merged as PR #4247), each from `origin/main` after the previous step is merged, not stacked. Every step stays off unless `gpu_compositor true`. If the review of step 0 changes a decision, that PR follows it before merge.

---

## Stick (step 6)

**Before:** steps 0–5 merged; the pre-stick check on paper (every step, every setting, every pass and fail line) and `scripts/check-pre-stick.py`; the image fully built before anything copies it (`r5-os-anyboot.iso`); the settings file planted for boot 1; the M10 stick tools (`check`, `boot-next`, `check-user` in `/boot/system/bin`) extended with the M11 lines and checked to be in the image; a short card per boot with named commands. Written with `scripts/write-stick.py`, read back with `scripts/read-stick-logs.py`. No periodic sync on the stick (it stalled every program in M10).

**Boot 1:** M10 needs no switches any more on `0x15e7`. The settings file has only `gpu_compositor true` and `gpu_debug true`. The R5 decorator selected.
1. Sign in. The desktop must come up as usual.
2. Scenario, 60 s: drag a Tracker window in circles over other windows for 20 s, scroll a long Terminal output for 20 s, run VulkanTeapot and `vkcube` side by side for 20 s.
3. Toggle "Shadow" off and on once, then "Frosted glass" off and on once. Open and close Terminal five times and switch workspaces five times; toggle "Animations" off and on once.
4. Start the `BDirectWindow` test program, then close it.
5. Open Terminal and type `check`. It prints PASS or FAIL per line:
   - `radeon_hd: gpu available`;
   - `app_server: gpu compositor on <device>`;
   - `app_server: compose compare ok` at least once (effects off) and never `MISMATCH`;
   - `app_server: window <id> zero-copy on` for VulkanTeapot and `vkcube`;
   - every `r5-share-test` line of the design with `pass` (run `r5-share-test` once after the scenario), and `bo_count` before and after VulkanTeapot for the buffer limits;
   - `app_server: compose median <n> us cpu <n> ms per 10 s` at least three times, with and without effects;
   - `app_server: window memory <n> MB` at least once;
   - `app_server: window memory type <i> flags <hex>` with the cached bit, and the kernel `bo` line with `snooped 1`;
   - the Painter write test: the window buffer not much slower than `malloc` (both times recorded);
   - VulkanTeapot FPS with zero-copy and with the copy path (`gpu_compositor_zero_copy false` or the switch step 3b provides), on the same boot, and app_server's CPU time with both, recorded in `CHANGELOG.md`;
   - `app_server: animation open`, `close` and `workspace` lines, and `animations off` / `on`;
   - `compositor paused: direct window` and `resumed`;
   - no `gpu compositor off`.

**Boot 2:** the same settings plus `gpu_compositor_fail true`. Run the same scenario.
- Pass: `app_server: gpu compositor off: forced failure` exactly once; the desktop keeps working through the whole scenario in the CPU mode with the R5 decorator's CPU look.

**Boot 3:** the same settings plus `gpu_compositor_lavapipe true`. The same scenario, for the comparison of RADV and Lavapipe on the same machine.

**Boot 4:** boot 1's settings plus `gpu_hang_test 1` (M10's test hang, one hang while the desktop is composed). Run the scenario once, the hang comes during it.
- Pass (decision 14): `gpu reset mode2 ok`, `gpu selftest after reset ok`, then `app_server: gpu compositor restarted after reset` and composing again, with no `gpu compositor off`; the desktop never froze (the pointer moved the whole time; the picture may stand still up to the 10 s watchdog).

Then `boot-next` back to no settings file, so the machine does not keep the test switches.

**Result in `CHANGELOG.md`:** compose time with and without effects on RADV and on Lavapipe (5625U and VirtualBox), CPU time, window memory, and whether a compose with the shadow and the frosted glass stays within one refresh period. Photos of the GPU mode and the CPU mode in the report.

**Failed:** a frozen desktop (pointer stuck), `compose compare MISMATCH`, a missing contract line, a Vulkan window without zero-copy on RADV, a `BDirectWindow` program that draws wrongly, more than one `gpu compositor off` line in a boot, or a `gpu compositor off` in boot 1 or 3.

**After a pass:** the M11 row in `docs/GPU-ARCHITECTURE.md`, `CHANGELOG.md` with the measurements, What's new (one build bump), and the default of decision 1 (GPU mode by default on Lavapipe everywhere and on RADV for GC 9.3) as its own small PR, so the opt-in stick and the default change are not one change.

---

## Lessons from M6–M8 (apply to every step)

- PRs from `origin/main`, not stacked. Push and open a PR only after the user says yes (rule 4 at the top); never merge, never force-push, never push to `main`.
- Only one agent jams at a time in the shared WSL tree, and nobody copies `r5-os.image` while a jam runs.
- Cursor cannot operate VirtualBox. It jams and leaves a short card (start command, what to do, which pass lines to paste back); the user boots.
- Each VirtualBox check is one fresh boot: Sign in, the step's test, Tracker and Terminal once, shut down.
- Every test prints a fixed pass line and a fixed fail line; reports quote them. Every report says which branch the image was built from.
- Settings files planted in the image belong to root; change them on the machine by removing and rewriting (`boot2.sh`).
- No hardcoding: sizes, pitch, format and refresh come from the driver or the display mode.
- Never mix: in one session the whole desktop is GPU mode (RADV or Lavapipe) or CPU mode.
- Kernel and driver code: inspect the path and the failure cases. Compiling is not proof.
- From M10: the card says in plain words what to type (open Terminal, type `check`). Stick scripts must be packed into the image; check that, do not assume it. Nothing on the stick syncs periodically. A self-test runs once per boot, not on every Sign in.
- Kernel code that reads user memory (step 3): manual SMAP review of the diff and the QEMU SMAP boot before the jam.
- A new Windows worktree checks out CRLF: check out `distro` and `scripts` with LF before `apply-overlay.sh` or the jam.

---

## Rounds

Counted in sessions, not weeks; the bottleneck is testing.

| Round | What | PRs | Test |
|---|---|---|---|
| 0 (may run during the after-M10 round) | The pictures of steps 1b and 1c on one design page, `docs/design/decorators/` (HTML, drawn in code, no Windows assets), for the user's approval and the 1c name | none, the page is shown to the user | – |
| 1 | Step 1 | one | VirtualBox or QEMU, one boot |
| 2 | Steps 1b and 1c, after the pictures are approved | two, not stacked | VirtualBox, one boot for both |
| 3 | Steps 2 and 3a (merged), 3b, 4, 4b, 5 | one PR per step | VirtualBox or QEMU, one boot per step |
| 4 | The stick (step 6) | results in `CHANGELOG.md` | 5625U, four boots |

Rounds 2 and 3 may overlap (decorator add-ons against `Desktop`/`Window`/compositor), but only one agent jams at a time.

---

## Not in M11

- The GPU drawing the window content (M12, `generated/gpu-m12/BRIEF.md`).
- Effects in the CPU mode (the emergency fallback) beyond the decorator's shape and colours.
- A software path other than Lavapipe.
- Programming DCN, vsync, scanout planes for a full-screen window, more than one screen.
- Hide and show animated into Deskbar (with Deskbar NextGen; open, close and the workspace switch are step 4b). HiDPI scaling.
- Changes to the Haiku decorators beyond the version, the hooks and what the user approves in step 1b.
- The round R5 decorator (large round corners).
- Glass, shadow and the pill for Deskbar (Deskbar NextGen, after step 4, through `set_view_glass()`; `generated/deskbar-nextgen/DESIGN.md`).
- Making the GPU mode the default (the small PR after the stick passes).
- The compositor on the compute queue (MEC) with priority over programs on the GFX ring, so a heavy game does not hold up the compose. Needs user submissions to MEC in the kernel and the R5 DRM library, which touches the fixed GFX9 contracts (`AGENTS.md`): its own brief and a boot on the 5625U, after M11. The R5 DRM library already reports a compute IP to RADV while it refuses compute submissions; that mismatch is checked then (or earlier if step 2's queue-family log shows RADV offering a compute-only family).
- The CPU mode only in Safe mode, and RADV to Lavapipe during a session (after M12, `generated/gpu-m12/SAFE-MODE.md`). In M11 the CPU mode stays the automatic fallback of decisions 11 and 14.
- `0x1636` and `0x1638`: they stay opt-in until one is tested; the GPU mode runs there on Lavapipe.

**PR titles:** "Area - what changes for the user", in plain English. Examples: "Desktop - A slimmer R5 window frame", "Graphics - Windows are composed on the GPU".
