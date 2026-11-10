# Vulkan in a window

M7 step 0, written 10 Nov 2026. This is the design. It contains no code. The next step waits until this text is reviewed.

M7 shows a Vulkan picture in an ordinary window: VulkanTeapot and `vkcube`, through RADV on the Ryzen 5 5625U and through Lavapipe in VirtualBox. The window can be resized, moved between workspaces, minimised and closed. If the GPU cannot present, the same program draws through Lavapipe. app_server still paints the window on the CPU. Sharing a GPU buffer with app_server is M8.

The choices below are the ones made on 10 Nov 2026. The names, the queries, the files and the settings are the ones in the M7 brief.

## What R5 does differently

Read on 10 Nov 2026. None of these registered an extension with Khronos.

| Project | What it does | Licence, as read |
|---|---|---|
| [hans2294/haiku-amdgpu](https://github.com/hans2294/haiku-amdgpu) | A headless surface, plus a `pNext` struct `VkHaikuViewSurfaceCreateInfoHAIKU` with a self-chosen structure type. Mesa draws into the view through a `BBitmap`. Later patches also draw straight into screen memory, and notice when app_server has dropped a direct window. | MIT, copyright 2026 hans2294. May be adapted, with that notice kept. |
| [haiku-pi/haiku-rpi](https://github.com/haiku-pi/haiku-rpi) `patches/vulkan-loader/vulkan-loader-1.4.311-haiku-view-surface.patch` | Teaches the loader to copy one self-chosen structure chained onto a headless surface, so Mesa can see the view. The repository root has no licence file. | No licence. Reference only. Copy nothing. |
| air/OS, including [airos-works/summit](https://github.com/airos-works/summit) | Presents by importing the screen's frame buffer (`VkImportScanoutMemoryHAIKU` in the air/OS Mesa and kernel trees). Summit's own repository has an MIT file. The scanout path does not live in that repository. | Copy nothing. Scanout is M8, not M7. |

R5 does not chain a private structure onto the headless surface. Loader 1.4.367 rejects an unknown structure in that chain (`copy_surface_create_info` in `loader/wsi.c` logs "Unsupported extension structure" and drops the chain). A program such as SDL also cannot ask whether a view surface exists if the only name on the instance is the headless extension. R5 adds its own instance extension, `VK_RFIVE_view_surface`, and asks Khronos for the author tag and the extension number at step 1, once a working loader can be pointed at. The request is drafted then, shown to the user, and posted only after that approval. This document is the text that request can point at. Nothing is sent from this step.

The structure type `1000999000` in the header below is an R5 placeholder. hans2294 used that same unofficial number for a different struct. It is not a shared assignment. No release ships with it.

What R5 may take from hans2294, with the MIT notice, is the idea of the locked view and the `BBitmap`, and the later observation that app_server can abandon a direct window without saying so. M7 does not draw into the frame buffer, and it does not use `LockLooper()`, which waits with no limit. The direct-window observation belongs to M8.

## 1. The surface extension

`VK_RFIVE_view_surface` is an instance extension. It depends on `VK_KHR_surface`. The only new command is `vkCreateViewSurfaceRFIVE`. The create info carries the `BView*` and nothing else. The window is the view's window. A second pointer would be a copy that can go stale when the view moves, so it is not in the struct.

The view must already be attached to a window when the surface is created. The application keeps that view alive until it destroys the surface. `vkQueuePresentKHR` may be called from any thread. The presentation code never blocks the window's own thread for longer than one frame: it locks with `LockLooperWithTimeout`, and if that lock does not arrive it skips the frame.

The header is `vulkan_r5.h`, MIT, installed next to the Vulkan headers. It is included only when `VK_USE_PLATFORM_VIEW_RFIVE` is defined, the same gate `vulkan_metal.h` uses. The full text is under [Exact interfaces](#exact-interfaces). The behaviour an implementer needs is the next section. That section does not depend on Mesa.

## 2. The loader

R5 builds the Vulkan loader from its own recipe, next to the Mesa recipe, pinned at loader 1.4.367. The HaikuPorts package is left as it is. A patch on that package would be dropped the next time the package is bumped, and the pin would no longer sit with Mesa.

The patch adds the platform the way Metal is added in that tag. Metal is the pattern, and these are the lines in v1.4.367:

| What | Where in v1.4.367 |
|---|---|
| Platform enum. Metal is one value. The new value is appended after the last one, `VK_ICD_WSI_PLATFORM_UBM`. Inserting it earlier would renumber every platform after it. | `include/vulkan/vk_icd.h` lines 105–121 (Vulkan-Headers commit `9fbc2a78`, the `v1.4.367` tag the loader pins) |
| ICD surface. Metal stores the layer pointer after `VkIcdSurfaceBase`. | `vk_icd.h` lines 227–231, `VkIcdSurfaceMetal` |
| Terminator. Checks the extension is enabled, fills the ICD surface, then copies the create-info chain. | `loader/wsi.c` `terminator_CreateMetalSurfaceEXT`, lines 1711–1752 |
| Dispatch to the driver when the surface is first used. | `loader/wsi.c` lines 187–198, `case VK_ICD_WSI_PLATFORM_METAL` |
| Reject an unknown `pNext` structure. | `loader/wsi.c` `copy_surface_create_info`, line 769, log at line 787 |
| Turn the extension bit on when the application enables the name, and publish the name. | `loader/generated/vk_loader_extensions.c` lines 18376–18378 and 18871–18872 |

Until Khronos publishes the enum, the recipe's copy of `vk_icd.h` carries one extra enumerator, marked as a placeholder, and Mesa is built against that same header so the number matches. The public structure type in `vulkan_r5.h` is the other placeholder. Both go away together when the registered values exist. A release does not ship either placeholder.

The loader patch is Apache-2.0, like the loader. Each changed function keeps the loader's notice and gains an SPDX snippet for the R5 lines.

## 3. Present

A successful present makes the image appear in the view. R5 does that by copying into a `BBitmap` and drawing that bitmap in the view. Lavapipe and RADV use the same bitmap and the same draw. They differ only in where the pixels are rendered:

- Lavapipe renders into a CPU image (`wsi_configure_cpu_image`).
- RADV renders into a device image and copies into a host-visible buffer (`wsi_configure_buffer_image`), the blit Mesa already uses for software presentation. Write-combined memory for that buffer is used only if the M6 write-combining boot has passed. Otherwise the buffer is ordinary memory, and the step 5 report says so.

The present thread is whichever thread called `vkQueuePresentKHR`. That thread locks the view's looper with `LockLooperWithTimeout` for at most one frame. The window thread is not asked to wait. If the caller is already the window thread, the lock succeeds at once, which is how Haiku's looper works. If the timeout expires, that frame is not drawn and present still returns `VK_SUCCESS`. A skipped frame is not a lost surface and not an out-of-date swapchain.

While the lock is held, the code reads the view's bounds. The size in pixels is `IntegerWidth() + 1` by `IntegerHeight() + 1`, which is the size `DrawBitmap` uses.

- If the view is no longer attached, or the window has closed, or the lock fails because the looper is gone, present returns `VK_ERROR_SURFACE_LOST_KHR`.
- If the bounds differ from the swapchain's extent, that present still draws and returns `VK_SUBOPTIMAL_KHR`. The next present on that swapchain returns `VK_ERROR_OUT_OF_DATE_KHR` and does not draw. The application creates a new swapchain at the new size.

Destroying the surface does not destroy the view. The application owns the view.

## 4. Present modes

FIFO is the only mode M7 offers. `vkGetPhysicalDeviceSurfacePresentModesKHR` returns one entry, `VK_PRESENT_MODE_FIFO_KHR`. MAILBOX and IMMEDIATE are not offered. They wait for M8, when app_server draws on the GPU. A swapchain that asks for a mode this query did not return fails with `VK_ERROR_INITIALIZATION_FAILED`.

FIFO waits for the display on the present thread, not on the window thread. The wait is `BScreen::WaitForRetrace` for the screen that holds the window, with a timeout of one refresh. That is what keeps a new frame from replacing the one the display has not finished. Drawing the bitmap makes the pixels visible. It is not the pace. If the retrace wait cannot be started, the present still draws, and the next FIFO present waits out whatever is left of the refresh measured from the previous present.

## 5. The fallback

Lavapipe always reports that it can present to a view surface. RADV reports that it can only when the `radeon_hd` setting `gpu_present` is true and the present path can be built. The query is `vkGetPhysicalDeviceSurfaceSupportKHR`. A program that picks a device by that query, which is what VulkanTeapot, `vkcube` and `r5-wsi-test` do, then opens Lavapipe.

`gpu_present` defaults to false, so a window is on Lavapipe until the setting is turned on. `gpu_present_fail true` forces the RADV answer to false, so the fallback can be tested. It stays off except in that test.

The first time RADV refuses a view surface, one syslog line says the program is drawing its window through Lavapipe. There is no dialog. The window looks the same. The line is not repeated for later frames.

If a program ignores the query and still opens RADV, creating the swapchain fails with `VK_ERROR_INITIALIZATION_FAILED` and the same single line is written. The fallback does not switch device in the middle of a frame.

## 6. Later layers

`BVulkanView` in the Graphics Kit is a client of this extension. It comes after M7 and is not built here. It will create the surface with `vkCreateViewSurfaceRFIVE` on its own view, own the swapchain, call the program's `Render()` each frame, build a new swapchain when present returns out of date, and build a new device when the device is lost. It uses Vulkan, the public Haiku API, and this presentation. It does not add a loader platform, a Mesa file, or a new structure to this extension. This layer does not gain a kit type, a kit header, or a call that exists only for the kit.

## 7. Ported programs

An R5 program gets the extension above.

SDL, GLFW and other ports get nothing new until they grow a backend. That backend is a `BView` in a `BWindow`, then `vkCreateViewSurfaceRFIVE`. It is the same extension. It is not a second one.

HaiWay, which ties a Wayland surface to a Haiku window, stays available later as an extra for a program that already speaks Wayland. It is not the native path, and M7 does not build it.

## Extension specification

This is the behaviour. An implementation that follows it, on any system that has a view attached to a window, presents correctly. Names that Khronos later assigns replace the placeholders. The behaviour does not change when the numbers change.

**Name.** `VK_RFIVE_view_surface`

**Type.** Instance extension.

**Depends on.** `VK_KHR_surface`

**Spec version.** 1

**Contact.** R5 Operating System. The author tag `RFIVE` and the extension number are requested from Khronos. They are not chosen here.

**New command.**

```
VkResult vkCreateViewSurfaceRFIVE(
    VkInstance                                  instance,
    const VkViewSurfaceCreateInfoRFIVE*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface);
```

**New structures.**

`VkViewSurfaceCreateFlagsRFIVE` is `VkFlags`. Every flag is reserved. The value must be 0.

```
typedef struct VkViewSurfaceCreateInfoRFIVE {
    VkStructureType                sType;
    const void*                    pNext;
    VkViewSurfaceCreateFlagsRFIVE  flags;
    BView*                         view;
} VkViewSurfaceCreateInfoRFIVE;
```

`sType` is `VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE`. Until Khronos assigns it, that enumerator is `1000999000`, defined only in `vulkan_r5.h`, with the comment `R5 placeholder, replace with the registered value`. `pNext` must be `NULL`. `view` is a pointer to a `BView` that is attached to a window. In C the type is an incomplete struct `BView`. In C++ it is the class `BView`.

**New enumerant, ICD side.** Append `VK_ICD_WSI_PLATFORM_VIEW_RFIVE` to `VkIcdWsiPlatform` after `VK_ICD_WSI_PLATFORM_UBM`. The driver and the loader must be built against one header, so they share the value.

```
typedef struct VkIcdSurfaceViewRFIVE {
    VkIcdSurfaceBase    base;
    BView*              view;
} VkIcdSurfaceViewRFIVE;
```

`base.platform` is `VK_ICD_WSI_PLATFORM_VIEW_RFIVE`. `view` is the pointer from the create info. The loader stores this, copies the create-info chain, and passes the chain to the driver's `vkCreateViewSurfaceRFIVE`. The driver presents to `view`.

**Enabling.** The application sets `VK_USE_PLATFORM_VIEW_RFIVE` before including the Vulkan headers, enables `VK_KHR_surface` and `VK_RFIVE_view_surface` on the instance, and calls `vkCreateViewSurfaceRFIVE`. If the extension was not enabled, the command returns `VK_ERROR_EXTENSION_NOT_PRESENT`.

**Create.**

- `pCreateInfo` must be a valid `VkViewSurfaceCreateInfoRFIVE`.
- `flags` must be 0.
- `pNext` must be `NULL`.
- `view` must not be `NULL` and must be attached to a window.
- If `view` is `NULL` or not attached, the command returns `VK_ERROR_INITIALIZATION_FAILED`.
- On success, `pSurface` receives a surface that presents into that view. Destroying the surface does not destroy the view. The application does not destroy the view while the surface, or a swapchain built from it, still exists.

**Queries.** `vkGetPhysicalDeviceSurfaceSupportKHR`, `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, `vkGetPhysicalDeviceSurfaceFormatsKHR` and `vkGetPhysicalDeviceSurfacePresentModesKHR` answer as follows for every device that can present to the surface. Lavapipe and RADV on R5 give the same answers. A device that cannot present answers support with false and is not described here.

| Query | Value |
|---|---|
| `vkGetPhysicalDeviceSurfaceSupportKHR` | `VK_TRUE` for every graphics queue. `VK_FALSE` for a queue that is not graphics. |
| Formats | `VK_FORMAT_B8G8R8A8_UNORM` and `VK_FORMAT_B8G8R8A8_SRGB`, each with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`. A `B_RGBA32` bitmap is BGRA in memory. |
| Present modes | `VK_PRESENT_MODE_FIFO_KHR` only. |
| `currentExtent` | The view's bounds in pixels. Width is `IntegerWidth() + 1`. Height is `IntegerHeight() + 1`. |
| `minImageExtent` | 1×1 |
| `maxImageExtent` | The current extent. A larger image waits for a new swapchain after the view grows. |
| `minImageCount` | 2 |
| `maxImageCount` | 3 |
| `supportedTransforms` | `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` |
| `currentTransform` | `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` |
| `supportedCompositeAlpha` | `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` |
| `supportedUsageFlags` | `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, `VK_IMAGE_USAGE_TRANSFER_DST_BIT`, `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` |
| `maxImageArrayLayers` | 1 |

There is no separate "presentation support" command. Support is the surface-support query.

**Present.**

- `vkQueuePresentKHR` may run on any thread.
- The implementation locks the view's window with `LockLooperWithTimeout` for at most one frame. It does not call a lock that waits without a limit, and it does not run the wait on the window's thread.
- If the lock times out, the frame is not drawn and that present returns `VK_SUCCESS`.
- If the view has been removed from its window, or the window is gone, present returns `VK_ERROR_SURFACE_LOST_KHR`.
- If the view's bounds differ from the swapchain extent, that present returns `VK_SUBOPTIMAL_KHR` and still presents. Every later present on that swapchain returns `VK_ERROR_OUT_OF_DATE_KHR` until the application creates a new swapchain.
- After `VK_SUCCESS` or `VK_SUBOPTIMAL_KHR`, the view shows the presented image, except for a frame skipped because the lock timed out.
- FIFO does not let the application reuse an image until the display has had one refresh. The wait is on the present thread.

**Lost device.** A lost GPU is `VK_ERROR_DEVICE_LOST`, from the device, not from this surface. The surface can be used again with a new device. Lavapipe is that device when the fallback in section 5 applies.

## Exact interfaces

### `vulkan_r5.h`

MIT. Installed next to the Vulkan headers. Included only when `VK_USE_PLATFORM_VIEW_RFIVE` is defined.

```c
#define VK_RFIVE_view_surface 1
#define VK_RFIVE_VIEW_SURFACE_SPEC_VERSION 1
#define VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME "VK_RFIVE_view_surface"

#ifdef __cplusplus
class BView;
#else
typedef struct BView BView;
#endif

typedef VkFlags VkViewSurfaceCreateFlagsRFIVE;   /* reserved, must be 0 */

typedef struct VkViewSurfaceCreateInfoRFIVE {
	VkStructureType             sType;    /* VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE */
	const void*                 pNext;    /* NULL */
	VkViewSurfaceCreateFlagsRFIVE  flags;    /* 0 */
	BView*                      view;     /* attached to a BWindow */
} VkViewSurfaceCreateInfoRFIVE;

typedef VkResult (VKAPI_PTR *PFN_vkCreateViewSurfaceRFIVE)(VkInstance instance,
	const VkViewSurfaceCreateInfoRFIVE* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateViewSurfaceRFIVE(VkInstance instance,
	const VkViewSurfaceCreateInfoRFIVE* pCreateInfo,
	const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
```

`VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE` is `1000999000` until Khronos assigns the number. The comment on that line is `R5 placeholder, replace with the registered value`.

### Files

New R5 files carry an SPDX header. Mesa and loader files keep their own licence. R5 lines in those files are SPDX snippets.

| File | What | Licence |
|---|---|---|
| `vulkan_r5.h` | The header above | MIT. Applications include it. |
| Loader patch, in R5's loader recipe | `VK_ICD_WSI_PLATFORM_VIEW_RFIVE`, `terminator_CreateViewSurfaceRFIVE`, the instance-extension entry, and `copy_surface_create_info` accepting this struct. The Metal lines to follow are listed in section 2. | Apache-2.0, like the loader. |
| `src/vulkan/wsi/wsi_common_haiku.cpp` in R5's Mesa recipe | Surface, swapchain, present, the CPU image for Lavapipe, the buffer image for RADV | MIT, like Mesa. |
| `build-radv.sh` | The platform turned on for Lavapipe and RADV | — |
| `r5-wsi-test`, new, under `distro/graphics` | The step 2 test program | GPL-3.0-or-later |

Before any of those files are added, the licence line above is the report this step owes. Nothing in this change adds the code.

### Settings

Both keys live in the `radeon_hd` driver settings, with the other GPU keys. Both default to off.

- `gpu_present true` lets RADV present into a window. Off, a program that asks for a view surface gets Lavapipe.
- `gpu_present_fail true` makes the RADV present path fail, so the fallback can be shown. It is never on outside that test.

## Before step 5

A RADV frame on stick 33 takes about a second of wall time. The GPU work is about 0.3 ms. This has to be fixed before M7 step 5, or a window draws one frame a second. This section is the finding. It does not change the driver.

### What the stick showed

The capture is `generated/boot-5625u-33`. VulkanTeapot's offscreen RADV run printed, in `r5-gpu-selftest.txt`:

```
frames 100
radeon ms per frame 0.305
gpu span ms 97977.856 cpu span ms 99202.012
```

`vkQueueWaitIdle` returned 0 after every one of those frames. The GPU timestamp span and the CPU span are both about 98 seconds, so the frames really were a second apart. The 0.305 ms is the GPU's own time for one frame, not the gap between frames.

`syslog.1` logs every user job as `user job fence` with a `system_time()` stamp. Of the 932 gaps between those lines, 782 are between 0.9 and 1.1 seconds. One run is 98 gaps in a row, each about 998.9 to 999.1 ms. That is 1 ms under a 1,000,000 µs wait. A second submit sometimes follows about 1 ms later. That second submit found the fence already retired and did not wait. It is the other frame of a pair, not a fast frame.

After those frames, `syslog` (the file after the rotate) prints `fence timeout` for sequence 940, with `seen 939` and fault `00700c10`, and then `gpu lost watchdog` with the same fault. That is the later hang, the one that still has no reset. It is not the one-second gap. The teapot waits returned success. The timeout line is a self-test stage that ran afterwards and did not retire.

### What is waiting

`vkQueueWaitIdle` waits on the job's sync object. The object is signalled in `r5_gpu_eop()` (`distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp`). That function reads the user fence and then wakes every waiter.

The only thread that calls it on a timer is `gpu_retire_thread` in the same file. It sleeps here:

```
acquire_sem_etc(sRetireSem, 1, B_RELATIVE_TIMEOUT, 1000000);
```

The comment on that call says the one-second timeout is the check when no interrupt arrives. The wait in `gpu_wait_value` does not read the fence again while it sleeps. It sleeps on the condition variable until `r5_gpu_eop` signals it, or until the application's own timeout, which is many seconds. So a frame returns when the retire thread next runs.

The interrupt is supposed to run that thread at once. `ih_handler` in `gfx9_k12.cpp` calls `r5_gpu_retire_wake()` when it sees a command-processor end-of-pipe entry, client `0x14`, source 181, ring 0. The user job does ask for that interrupt. `r5_cs_user_body` in `r5_cs.h` writes the 64-bit sequence with no interrupt, then a 32-bit fence with `INT_SEL` 2 (`kR5CsRel32Int`). K1 at boot received one such interrupt (`k1 ok msi irq 61 eop 1` in `syslog.1`). The stick does not print a count of later interrupts. The timestamps do the proving: if that wake had run, the gap would be the GPU time plus a little scheduling, not 999 ms, ninety-eight times in a row.

The fence value itself is not late. The one-second poll reads it and the wait returns 0, and `seen` keeps up with the frames. The GPU wrote the sequence in about 0.3 ms. Nothing looked until the semaphore timed out.

### Why the interrupt path does not see it

The interrupt handler starts by reading the IH write pointer:

```
uint32 wptr = *sIhWriteback;
```

If that word still looks equal to the read pointer, the handler returns without calling `r5_gpu_retire_wake`. The write pointer and the ring sit in the VRAM pool. `map_pool` in `gfx9_k12.cpp` maps that pool as `B_WRITE_COMBINING_MEMORY`. A write-combining load may be speculative, and it is not coherent with a store the GPU just did. The user fence is in the same pool. `r5_gfx9_user_seen` is a plain load of that word. A wake that did run could still read the old sequence, signal nobody, and go back to sleep for another second. Both failures look like the log. The poll a second later sees the new value, which is what happened.

K1 can still pass. The first store into a fresh page is the one a write-combining reader is most likely to see. The frames after that are the ones that miss.

### The fix, when driver code is allowed

Leave the one-second semaphore as the backup. It is the right behaviour when no interrupt arrives. Do not shorten it, and do not change the fence packet, the ring, the registers, or the fence's GPU address.

Give the CPU a coherent look at two words the GPU writes:

- the IH write pointer, so `ih_handler` sees the new entry and calls `r5_gpu_retire_wake` for every GFX end-of-pipe;
- the user-fence word, so `r5_gpu_eop` on that wake reads the sequence the GPU has already stored.

The GFX packet stays pointed at the same device address. What changes is the CPU mapping of those reads: an uncached load, or a cache-coherent page for the IH writeback, which is not part of the GFX ring contract. Linux keeps that writeback in cache-coherent system memory for this reason.

After that, a waited frame's gap in the `user job fence` log should sit with the GPU time, about 0.3 ms plus scheduling, not with the 1,000,000 µs semaphore. The proof is a 100-frame run on the 5625U whose gaps do that, with `vkQueueWaitIdle` still returning 0. The later `fence timeout` and fault `00700c10` stay a separate problem. They are the hang with no reset, and this fix does not claim to clear them.
