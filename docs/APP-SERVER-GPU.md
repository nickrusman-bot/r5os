# The last copy in app_server

This is the design for M8, steps 1 to 3. It follows `generated/gpu-m8/BRIEF.md` and does not change `docs/GPU-ARCHITECTURE.md` except where step 1 adds the new call. Painter keeps drawing on the CPU. Only the copy from the back buffer to the screen moves, and only when `gpu_app_server true` is set.

## The copy today

A view invalidates a rectangle. `HWInterface::Invalidate` (`src/servers/app/drawing/HWInterface.cpp`) calls `CopyBackToFront` when the screen is double buffered. The object is already locked. `CopyBackToFront` intersects the rectangle with the back buffer, locks `fFloatingOverlaysLock`, excludes the software cursor, and calls `_CopyBackToFront`.

`HWInterface::_CopyBackToFront` walks the region. The back buffer is `B_RGBA32`. For each rectangle it calls `_CopyToFront` with the source row pointer, the source bytes per row, and the rectangle. `_CopyToFront` switches on the front buffer's color space. `B_RGBA32` is a `memcpy` of each row. Other spaces convert. An unsupported space prints to stderr and copies nothing.

`AccelerantHWInterface::_CopyBackToFront` (`drawing/interface/local/AccelerantHWInterface.cpp`) calls that base function and does nothing else. The front buffer is the accelerant framebuffer. The back buffer is a `MallocBuffer` created in `SetMode`: width times height times 4, so its bytes per row are `width * 4`. `distro/app_server` does not yet contain either file. It contains `AppServer.cpp` and `ServerConfig.h` only.

## Where the GPU copy hooks in

The hook is `AccelerantHWInterface::_CopyBackToFront`, not the base class. A software screen never enters it. With `gpu_app_server` absent or false, the function calls the base and returns. Nothing is logged.

With the setting on, `GPUCopy` tries the GPU copy for that region. Success skips the CPU copy. Any failure logs one line, `app_server: gpu copy off: <reason>`, and the base copy runs for that call and for every later call in the session. The reasons are `no render node`, `scanout refused`, `pitch`, `format`, `submit`, `fence timeout`, and `forced failure`.

`gpu false` turns the GPU off with everything else. The kernel then refuses the new call.

## The back buffer

On the mode change that creates the back buffer, and only when the setting is on, app_server allocates it with `R5_GPU_BO_CREATE` in `R5_GPU_DOMAIN_GTT` and without `R5_GPU_BO_WC`. The page stays cached and snooped. `R5_GPU_BO_MAP` is the pointer Painter writes. `R5_GPU_VA_MAP` is the GPU address the copy reads. No user pointer is passed into the kernel.

If that allocation fails, the `MallocBuffer` is used and the session falls back.

## The scanout buffer

`R5_GPU_SCANOUT_BO` is `R5_GPU_OP_BASE + 31`. The struct is added at the end of `headers/private/r5gpu/r5_gpu.h`. The kernel fills width, height, bytes per row, the color space, the byte size, a buffer handle, and a GPU address in the caller's address space. The numbers come from the driver's `shared_info`: `current_mode`, `bytes_per_row`, and the byte offset of the framebuffer inside the PCI BAR (`frame_buffer_phys - vram_bar_phys`). The page tables add `MC_VM_FB_OFFSET << 24` when they map that offset. On the 5625U the BAR is `0xfce0000000` and the carveout is `0x810000000`, so those bases are not subtracted from each other. The numbers are not constants.

The kernel allows the call only when the calling team is app_server. It reads the executable from that team's info, not from a value the caller passes. The path is `/system/servers/app_server`, or `/boot/system/servers/app_server` after `/system` is resolved. Anyone else gets `B_NOT_ALLOWED`. The handle refers to that existing framebuffer. Freeing it unmaps the GPU pages and does not free the framebuffer.

`docs/GPU-ARCHITECTURE.md` names the call in the same change as the header.

## The band and the packet

A band is the full bytes-per-row of a contiguous run of dirty rows. Rectangles on the same rows, or on rows that touch, are one band. An empty region is no band. One row is one band. A full screen is one band.

`DMA_DATA` carries the byte count in 21 bits, so a band longer than 2097151 bytes is several packets. Each packet is a whole number of rows. The control word is `0x60200000`, the word the DMA stage already ran. The header is `0xc0055000`. The last word sets bit 31 and the byte count, as `r5_stage_p.h` does.

One indirect buffer is submitted per `CopyBackToFront`. app_server waits on `R5_GPU_FENCE_WAIT` for 100000 microseconds. A timeout is the fallback above.

The linear copy is valid when the front buffer is `B_RGB32` or `B_RGBA32` and its bytes per row equal the back buffer's (`width * 4`). A wider front pitch, or another color space, uses the CPU copy. The CPU path still converts those spaces. A raw byte copy would not.

The words are built by one function, `r5_copy_bands_pm4` in `distro/app_server/GPUCopyBands.h`. `scripts/check-copy-bands.cpp` prints `ok band builder N cases` and the streams. `scripts/check-pm4.py` checks every stream.

## Measurement

`gpu_debug true` turns the counters on even when `gpu_app_server` is off. VirtualBox is double buffered: app_server keeps a software back buffer and `_CopyBackToFront` runs when a window moves, so the CPU line below appears. Each copy records its duration. Every 10 seconds app_server logs the median and the CPU time spent copying:

`app_server: copy cpu median <n> us cpu <n> ms per 10 s`

After a fallback, that same line also carries `gpu copy off: <reason>`, so a rotated one-shot line is not the only copy of the reason.

`app_server: copy gpu median <n> us cpu <n> ms per 10 s`

Every 100th GPU frame, app_server reads the copied rows back from the front buffer and compares them with the back buffer. A match logs `app_server: copy compare ok` once per 10 seconds. A mismatch logs `app_server: copy compare MISMATCH` and falls back for the session.

The GPU path is better only if the CPU time spent copying drops by at least half and the median copy time is not worse. Otherwise the path can still be correct, and the setting stays opt-in.

## What this does not do

No compositing, no GPU drawing, no DCN, no vsync, and no GPU reset. The copy tears the same way the CPU copy tears. VirtualBox has no render node. With the setting off, the desktop is the one it is today.
