# Vulkan in a window

M7 step 0, written 10 Nov 2026. This is the design. Step 1 adds the loader recipe and the two headers. The Mesa surface is not in that step.

M7 shows a Vulkan picture in an ordinary window: VulkanTeapot and `vkcube`, through RADV on the Ryzen 5 5625U and through Lavapipe in VirtualBox. The window can be resized, moved between workspaces, minimised and closed. If the GPU cannot present, the same program draws through Lavapipe. app_server still paints the window on the CPU. Sharing a GPU buffer with app_server is M8.

The choices below are the ones made on 10 Nov 2026. The names, the queries, the files and the settings are the ones in the M7 brief.

## What R5 does differently

Read on 10 Nov 2026. None of these registered an extension with Khronos.

| Project | What it does | Licence, as read |
|---|---|---|
| [hans2294/haiku-amdgpu](https://github.com/hans2294/haiku-amdgpu) | A headless surface, plus a `pNext` struct `VkHaikuViewSurfaceCreateInfoHAIKU` with a self-chosen structure type. Mesa draws into the view through a `BBitmap`. Later patches also draw straight into screen memory, and notice when app_server has dropped a direct window. | MIT, copyright 2026 hans2294. May be adapted, with that notice kept. |
| [haiku-pi/haiku-rpi](https://github.com/haiku-pi/haiku-rpi) `patches/vulkan-loader/vulkan-loader-1.4.311-haiku-view-surface.patch` | Teaches the loader to copy one self-chosen structure chained onto a headless surface, so Mesa can see the view. The repository root has no licence file. | No licence. Reference only. Copy nothing. |
| air/OS, including [airos-works/summit](https://github.com/airos-works/summit) | Presents by importing the screen's frame buffer (`VkImportScanoutMemoryHAIKU` in the air/OS Mesa and kernel trees). | Licence unclear (`NOASSERTION`). Copy nothing. Scanout is M8. |
| RenkuOS | No Vulkan window. Its Chromium port has a `haiku_surface` only. | Not relevant. |

R5 does not chain a private structure onto the headless surface. In loader 1.4.367, `copy_surface_create_info` (`loader/wsi.c` line 769) returns `VK_ERROR_EXTENSION_NOT_PRESENT` for an unknown structure in that chain. The log at line 787 says "Unsupported extension structure". A program such as SDL also cannot ask whether a view surface exists if the only name on the instance is the headless extension. R5 adds its own instance extension, `VK_RFIVE_view_surface`, and asks Khronos for the author tag and the extension number at step 1, once a working loader can be pointed at. The request is [KhronosGroup/Vulkan-Docs#2851](https://github.com/KhronosGroup/Vulkan-Docs/issues/2851): author ID `RFIVE`, extension 732. It is posted and not merged. This document is the text that request points at. The header still uses the placeholder until that request lands.

The structure type `1000999000` in the header below is an R5 placeholder. hans2294 used that same unofficial number for a different struct. It is not a shared assignment. No release ships with it.

What R5 may take from hans2294, with the MIT notice, is the idea of the locked view and the `BBitmap`, and the later observation that app_server can abandon a direct window without saying so. M7 does not draw into the frame buffer, and it does not use `LockLooper()`, which waits with no limit. The direct-window observation belongs to M8.

## 1. The surface extension

`VK_RFIVE_view_surface` is an instance extension. It depends on `VK_KHR_surface`. The only new command is `vkCreateViewSurfaceRFIVE`. The create info carries the `BView*` and nothing else. The window is the view's window. A second pointer would be a copy that can go stale when the view moves, so it is not in the struct.

The view must already be attached to a window when the surface is created. The create info still carries only the `BView*`. From that view the implementation takes a `BMessenger` to the window and keeps the messenger, not a lock on the raw pointer. `vkQueuePresentKHR` may be called from any thread. The presentation code never blocks the window's own thread for longer than one frame: it locks with `BMessenger::LockTargetWithTimeout`, and if that lock does not arrive in time it skips the frame. When the user closes the window, the window deletes its views. The messenger lock fails without touching the deleted view, and present returns `VK_ERROR_SURFACE_LOST_KHR`.

The header is `vulkan_r5.h`, MIT, installed next to the Vulkan headers. A program defines `VK_USE_PLATFORM_VIEW_RFIVE` and includes `<vulkan/vulkan_r5.h>` itself. R5 does not patch `vulkan.h`. The full text is under [Exact interfaces](#exact-interfaces). The behaviour an implementer needs is the specification below. That section does not depend on Mesa.

## 2. The loader

R5 builds the Vulkan loader from its own recipe, next to the Mesa recipe, pinned at loader 1.4.367. The HaikuPorts package is left as it is. A patch on that package would be dropped the next time the package is bumped, and the pin would no longer sit with Mesa.

The patch adds the platform the way Metal is added in that tag. Metal is the pattern, and these are the lines in v1.4.367:

| What | Where in v1.4.367 |
|---|---|
| Platform enum. Metal is one value. The new value is appended after the last one, `VK_ICD_WSI_PLATFORM_UBM`. Inserting it earlier would renumber every platform after it. | `include/vulkan/vk_icd.h` lines 105–121 (Vulkan-Headers commit `9fbc2a78`, the `v1.4.367` tag the loader pins) |
| ICD surface. Metal stores the layer pointer after `VkIcdSurfaceBase`. | `vk_icd.h` lines 227–231, `VkIcdSurfaceMetal` |
| Terminator. Checks the extension is enabled, fills the ICD surface, then copies the create-info chain. | `loader/wsi.c` `terminator_CreateMetalSurfaceEXT`, lines 1711–1752 |
| Dispatch to the driver when the surface is first used. | `loader/wsi.c` lines 187–198, `case VK_ICD_WSI_PLATFORM_METAL` |
| Exported trampoline. The application calls this, and it forwards to the layer dispatch. | `loader/wsi.c` `vkCreateMetalSurfaceEXT`, lines 1696–1709 |
| Name lookup, so `vkGetInstanceProcAddr` returns the trampoline only when the extension is enabled. | `loader/wsi.c` `wsi_swapchain_instance_gpa`, lines 3211–3217 |
| ICD dispatch entry, filled from the driver's function. | `loader/generated/vk_loader_extensions.c` line 268, `LOOKUP_GIPA(CreateMetalSurfaceEXT)`, and `vk_loader_extensions.h` lines 435–438. Line 1694 is a different table: `loader_init_instance_extension_dispatch_table` writes the same command into `VkLayerInstanceDispatchTable`. That field is declared in `loader/generated/vk_layer_dispatch_table.h` lines 242–245 |
| Layer dispatch entry, the terminator the trampoline calls. | `loader/generated/vk_loader_extensions.c` line 18721, `.CreateMetalSurfaceEXT = terminator_CreateMetalSurfaceEXT` |
| Union member on the loader's `VkIcdSurface`, so the surface object can hold the platform struct. | `loader/wsi.h` lines 57–58, `metal_surf` |
| Platform define that compiles those blocks in. Metal's is `VK_USE_PLATFORM_METAL_EXT` on Apple. R5 adds `VK_USE_PLATFORM_VIEW_RFIVE` for this build the same way. | `CMakeLists.txt` line 132 |
| Structure types the terminator asks `copy_surface_create_info` to accept. The function itself does not change. An unknown type makes it return `VK_ERROR_EXTENSION_NOT_PRESENT` (line 769, log at line 787). | `loader/wsi.c` line 1737, Metal's `ci_types` list |
| Turn the extension bit on when the application enables the name, and publish the name. | `loader/generated/vk_loader_extensions.c` lines 18376–18378 and 18871–18872 |

`copy_surface_create_info` stays as it is. The new terminator passes `VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE` in its type list, the way Metal passes its own type at line 1737.

That name list is a filter. The loader passes on the names a driver reports. It does not add `VK_RFIVE_view_surface` itself. Lavapipe exports the name, so `vulkaninfo` prints it for the software driver.

Until Khronos publishes the enum, R5's copy of `vk_icd.h` carries `VK_ICD_WSI_PLATFORM_VIEW_RFIVE` after `VK_ICD_WSI_PLATFORM_UBM`, and `VkIcdSurfaceViewRFIVE` next to `VkIcdSurfaceMetal`. The loader and Mesa are built against that same header so the number matches. The public structure type in `vulkan_r5.h` is the other placeholder. Both go away together when the registered values exist. A release does not ship either placeholder.

The loader patch is Apache-2.0, like the loader. Each changed function keeps the loader's notice and gains an SPDX snippet for the R5 lines.

## 3. Present

A successful present makes the image appear in the view, and stay there when app_server asks the view to draw again. Lavapipe and RADV use the same bitmaps and the same present. They differ only in where the pixels are rendered:

- Lavapipe renders into a CPU image (`wsi_configure_cpu_image`).
- RADV renders into a device image and copies into a host-visible buffer (`wsi_configure_buffer_image`), the blit Mesa already uses for software presentation. Write-combined memory for that buffer is used only if the M6 write-combining boot has passed. Otherwise the buffer is ordinary memory, and the step 5 report says so.

Each swapchain image has its own `BBitmap`. Present copies that image's pixels into its bitmap. It does not share one bitmap across images, so a later present does not overwrite pixels app_server is still reading. Acquire hands out the image after the last one presented, and it does not hand out the bitmap that is currently the view bitmap. Present checks that before the copy. The graphics-queue mask is kept per device, and the view bitmap is read and written under the surface lock.

The present thread is whichever thread called `vkQueuePresentKHR`. That thread locks the window with `BMessenger::LockTargetWithTimeout` for at most one frame. The messenger was taken at surface creation. The window thread is not asked to wait. If the caller is already the window thread, the lock succeeds at once, which is how Haiku's looper works. The view is touched only after that lock has succeeded.

- If the lock times out, that frame is not drawn and present returns `VK_SUCCESS`. A skipped frame is not a lost surface and not an out-of-date swapchain.
- If the window has quit, the lock fails and present returns `VK_ERROR_SURFACE_LOST_KHR`. The deleted view is not used.
- If the lock succeeds and the view is no longer attached, present returns `VK_ERROR_SURFACE_LOST_KHR`.
- If the bounds differ from the swapchain's extent, that present still shows the image and returns `VK_SUBOPTIMAL_KHR`. The next present on that swapchain returns `VK_ERROR_OUT_OF_DATE_KHR` and does not draw. The application creates a new swapchain at the new size.

While the lock is held, the size in pixels is `IntegerWidth() + 1` by `IntegerHeight() + 1`. Present then calls `SetViewBitmap` with this image's bitmap and `Invalidate()`. app_server draws that bitmap itself whenever the view must be drawn again: after minimise and restore, after a workspace change, and when the view is uncovered. The implementation does not replace the program's `Draw()`.

The image goes back to `vkAcquireNextImageKHR` only after app_server has finished drawing it. That is `Sync()` under the same lock, or waiting until that bitmap is no longer the view bitmap. Destroying the surface does not destroy the view. Closing the window may delete the view. The next present then returns `VK_ERROR_SURFACE_LOST_KHR` and does not crash.

## 4. Present modes

FIFO is the only mode M7 offers. `vkGetPhysicalDeviceSurfacePresentModesKHR` returns one entry, `VK_PRESENT_MODE_FIFO_KHR`. MAILBOX and IMMEDIATE are not offered. They wait for M8, when app_server draws on the GPU. A swapchain that asks for a mode this query did not return fails with `VK_ERROR_INITIALIZATION_FAILED`.

FIFO waits for the display on the present thread, not on the window thread. The wait is `BScreen::WaitForRetrace` for the screen that holds the window, with a timeout of one refresh. That is what keeps a new frame from replacing the one the display has not finished. `SetViewBitmap` is what makes the pixels visible, including on a later redraw. It is not the pace. If the retrace wait cannot be started, the present still shows the bitmap, and the next FIFO present waits out whatever is left of the refresh measured from the previous present.

## 5. The fallback

Lavapipe always reports that it can present to a view surface. That answer is true only for a software device. RADV's answer stays false until step 5 reads the `radeon_hd` setting `gpu_present` and the present path can be built. The query is `vkGetPhysicalDeviceSurfaceSupportKHR`. A program that picks a device by that query, which is what VulkanTeapot, `vkcube` and `r5-wsi-test` do, then opens Lavapipe.

`gpu_present` defaults to false, so a window is on Lavapipe until the setting is turned on. `gpu_present_fail true` forces the RADV answer to false, so the fallback can be tested. It stays off except in that test.

The first time RADV refuses a view surface, one syslog line says the program is drawing its window through Lavapipe. There is no dialog. The window looks the same. The line is not repeated for later frames.

If a program ignores the query and still opens RADV, creating the swapchain fails with `VK_ERROR_INITIALIZATION_FAILED` and the same single line is written. The fallback does not switch device in the middle of a frame.

## 6. Later layers

`BVulkanView` in the Graphics Kit is a client of this extension. It comes after M7 and is not built here. It will create the surface with `vkCreateViewSurfaceRFIVE` on its own view, own the swapchain, call the program's `Render()` each frame, build a new swapchain when present returns out of date, and build a new device when the device is lost. It uses Vulkan, the public Haiku API, and this presentation. It does not add a loader platform, a Mesa file, or a new structure to this extension. This layer does not gain a kit type, a kit header, or a call that exists only for the kit.

## 7. Ported programs

An R5 program gets the extension above. `vkcube` from Vulkan-Tools 1.4.367 is that program: a `BWindow` and a `BView`, then `vkCreateViewSurfaceRFIVE`, in the same place `cube.c` creates a Metal surface. It keeps `vkcube`'s own device choice. `r5-wsi-window-test.sh` takes the program as its argument. `VulkanTeapot` is the default.

SDL, GLFW and other ports get nothing new until they grow a backend. That backend is a `BView` in a `BWindow`, then `vkCreateViewSurfaceRFIVE`. It is the same extension. It is not a second one.

HaiWay, which ties a Wayland surface to a Haiku window, stays available later as an extra for a program that already speaks Wayland. It is not the native path, and M7 does not build it.

## Extension specification

This is the behaviour. An implementation that follows it, on any system that has a view attached to a window, presents correctly. Names that Khronos later assigns replace the placeholders. The behaviour does not change when the numbers change.

**Name.** `VK_RFIVE_view_surface`

**Type.** Instance extension.

**Depends on.** `VK_KHR_surface`

**Spec version.** 1

**Contact.** R5 Operating System. The author ID `RFIVE` and extension 732 are requested in [KhronosGroup/Vulkan-Docs#2851](https://github.com/KhronosGroup/Vulkan-Docs/issues/2851). That request is not merged. The header keeps the placeholder until it is.

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

`sType` is `VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE`. Until Khronos assigns it, that name is a macro in `vulkan_r5.h` only:

```
#define VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE ((VkStructureType)1000999000)
```

The comment on that line is `R5 placeholder, replace with the registered value`. It is not an enumerator in `vulkan_core.h`. `pNext` must be `NULL`. `view` is a pointer to a `BView` that is attached to a window. In C the type is an incomplete struct `BView`. In C++ it is the class `BView`, declared outside `extern "C"`.

**New enumerant, ICD side.** In R5's copy of `vk_icd.h`, append `VK_ICD_WSI_PLATFORM_VIEW_RFIVE` to `VkIcdWsiPlatform` after `VK_ICD_WSI_PLATFORM_UBM`, and add `VkIcdSurfaceViewRFIVE` next to `VkIcdSurfaceMetal`. The loader and Mesa are built against that copy, so they share the value. The struct is not in `vulkan_r5.h`.

```
typedef struct VkIcdSurfaceViewRFIVE {
    VkIcdSurfaceBase    base;
    BView*              view;
} VkIcdSurfaceViewRFIVE;
```

`base.platform` is `VK_ICD_WSI_PLATFORM_VIEW_RFIVE`. `view` is the pointer from the create info. The loader stores this, copies the create-info chain, and passes the chain to the driver's `vkCreateViewSurfaceRFIVE`. The driver presents to `view`.

**Enabling.** The application defines `VK_USE_PLATFORM_VIEW_RFIVE`, includes `<vulkan/vulkan_r5.h>` itself, enables `VK_KHR_surface` and `VK_RFIVE_view_surface` on the instance, and calls `vkCreateViewSurfaceRFIVE`. `vulkan.h` is not patched to include this header. If the extension was not enabled, the command returns `VK_ERROR_EXTENSION_NOT_PRESENT`.

**Create.**

- `pCreateInfo` must be a valid `VkViewSurfaceCreateInfoRFIVE`.
- `flags` must be 0.
- `pNext` must be `NULL`.
- `view` must not be `NULL` and must be attached to a window.
- If `view` is `NULL` or not attached, the command returns `VK_ERROR_INITIALIZATION_FAILED`.
- On success, `pSurface` receives a surface that presents into that view. The implementation keeps a `BMessenger` to that window. Destroying the surface does not destroy the view.
- When the window quits, the window deletes its views. The next present returns `VK_ERROR_SURFACE_LOST_KHR` and does not use the view.

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
- The implementation locks the window with `BMessenger::LockTargetWithTimeout` for at most one frame. It does not call a lock that waits without a limit, and it does not run the wait on the window's thread. It does not call a method on the view until that lock has succeeded.
- If the lock times out, the frame is not drawn and that present returns `VK_SUCCESS`.
- If the window has quit, or the lock succeeds and the view is no longer attached, present returns `VK_ERROR_SURFACE_LOST_KHR`. A quit window is not a use of the deleted view.
- If the view's bounds differ from the swapchain extent, that present returns `VK_SUBOPTIMAL_KHR` and still presents. Every later present on that swapchain returns `VK_ERROR_OUT_OF_DATE_KHR` until the application creates a new swapchain.
- Each swapchain image has its own bitmap. Present shows it with `SetViewBitmap` and `Invalidate()`, so a later expose draws the same frame. The image is available to `vkAcquireNextImageKHR` only after that draw has finished.
- After `VK_SUCCESS` or `VK_SUBOPTIMAL_KHR`, the view shows the presented image, including when app_server asks the view to draw again, except for a frame skipped because the lock timed out.
- FIFO does not let the application reuse an image until the display has had one refresh. The wait is on the present thread.

**Lost device.** A lost GPU is `VK_ERROR_DEVICE_LOST`, from the device, not from this surface. The surface can be used again with a new device. Lavapipe is that device when the fallback in section 5 applies.

## Exact interfaces

### `vulkan_r5.h`

MIT. Installed next to the Vulkan headers. A program includes `<vulkan/vulkan_r5.h>` itself after defining `VK_USE_PLATFORM_VIEW_RFIVE`. R5 does not add an include to `vulkan.h`.

```c
#ifndef VULKAN_R5_H_
#define VULKAN_R5_H_ 1

#include "vulkan_core.h"

#ifdef __cplusplus
class BView;
extern "C" {
#else
typedef struct BView BView;
#endif

#define VK_RFIVE_view_surface 1
#define VK_RFIVE_VIEW_SURFACE_SPEC_VERSION 1
#define VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME "VK_RFIVE_view_surface"

/* R5 placeholder, replace with the registered value. */
#define VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE ((VkStructureType)1000999000)

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

#ifdef __cplusplus
}
#endif

#endif
```

`VkIcdSurfaceViewRFIVE` is not in this header. It is in R5's copy of `vk_icd.h`, with the platform enumerator.

### Files

New R5 files carry an SPDX header. Mesa and loader files keep their own licence. R5 lines in those files are SPDX snippets.

| File | What | Licence |
|---|---|---|
| `vulkan_r5.h` | The header above | MIT. Applications include it. |
| Loader patch, in R5's loader recipe | The Metal-shaped pieces in section 2: platform enum, ICD struct, trampoline, `wsi_swapchain_instance_gpa`, ICD and layer dispatch, `VkIcdSurface` union member, CMake define, terminator, and that terminator's structure-type list. `copy_surface_create_info` is not edited. | Apache-2.0, like the loader. |
| `src/vulkan/wsi/wsi_common_haiku.cpp` in R5's Mesa recipe | Surface, swapchain, present, the CPU image for Lavapipe, the buffer image for RADV | MIT, like Mesa. |
| `build-radv.sh` | The platform turned on for Lavapipe and RADV | — |
| `r5-wsi-test`, new, under `distro/graphics` | The step 2 test program | GPL-3.0-or-later |

Step 1 adds `vulkan_r5.h`, R5's copy of `vk_icd.h`, and the loader recipe. The loader patch is Apache-2.0. `vulkan_r5.h` is MIT because applications include it. `vk_icd.h` keeps the Vulkan-Headers licence, Apache-2.0 OR MIT, and the two R5 placeholders are Apache-2.0 snippets. Step 2 adds the Mesa surface for Lavapipe: a CPU image, one `BBitmap` per swapchain image, and `r5-wsi-test`. The RADV buffer image is step 5. `wsi_common_haiku.cpp` is MIT. `r5-wsi-test` is GPL-3.0-or-later. Nothing is copied from haiku-amdgpu, haiku-rpi, or summit.

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

The only thread that calls it on a timer is `gpu_retire_thread` in the same file (line 325 on `origin/main` `a7670e3f73`). It sleeps here:

```
acquire_sem_etc(sRetireSem, 1, B_RELATIVE_TIMEOUT, 1000000);
```

The comment on that call says the one-second timeout is the check when no interrupt arrives. The wait in `gpu_wait_value` does not read the fence again while it sleeps. It sleeps on the condition variable until `r5_gpu_eop` signals it, or until the application's own timeout, which is many seconds. So a frame returns when the retire thread next runs.

The interrupt is supposed to run that thread at once. `ih_handler` in `gfx9_k12.cpp` calls `r5_gpu_retire_wake()` when it sees a command-processor end-of-pipe entry, client `0x14`, source 181, ring 0. The user job does ask for that interrupt. `r5_cs_user_body` in `r5_cs.h` writes the 64-bit sequence with no interrupt, then a 32-bit fence with `INT_SEL` 2 (`kR5CsRel32Int`). K1 at boot received one such interrupt (`k1 ok msi irq 61 eop 1` in `syslog.1`). The stick does not print a count of later interrupts. The timestamps do the proving: if that wake had run, the gap would be the GPU time plus a little scheduling, not 999 ms, ninety-eight times in a row.

The fence value itself is not late. The one-second poll reads it and the wait returns 0, and `seen` keeps up with the frames. The GPU wrote the sequence in about 0.3 ms. Nothing looked until the semaphore timed out.

### Why the interrupt path stops

`ih_handler` in `gfx9_k12.cpp` reads the write pointer and returns at lines 418–419 when it equals the read pointer. That return does not write `IH_RB_RPTR`. The write of the read pointer is at line 492, after the loop, so an empty visit never reaches it.

`ih_on` sets `RPTR_REARM` for MSI, bit 21 of `IH_RB_CNTL` at line 737. In that mode the GPU's interrupt unit (IH) sends the next interrupt only after the read pointer is written. Linux does the same: `vega10_ih.c` sets `RPTR_REARM` for MSI, and `amdgpu_ih_process` always calls `amdgpu_ih_set_rptr`. One interrupt that sees nothing new therefore stops every later interrupt. The retire thread is left with its one-second poll. That matches the log. K1 received its one interrupt (`k1 ok msi irq 61 eop 1`), and the frames after that are the 98 gaps of about 999 ms.

A write-combining load is not the cause. On x86 that load is not kept in the CPU cache. It can be satisfied early. It does not stay stale for 0.3 ms.

The likely reason the handler saw nothing new is which window it read. The IH write-pointer word and the user fence are read through the pool mapping. `map_pool` (line 516) maps the PCI frame-buffer window (`vram_window_phys`). Those reads go through the GPU's host data path. On GFX9 that path has a read cache (`hdp_v4_0_invalidate_hdp` in Linux). On an APU, Linux does not read these words through that window. Its interrupt ring and write pointer sit in snooped system memory, and its VRAM reads use the carve-out. The comment on `map_carveout` records that. This is a likely cause. Stick 33 does not prove it, because the log has no count of later interrupts.

### The fix, in its own change

The driver change is this pull request. The one-second semaphore stays the backup for a missing interrupt. It is not shortened. The fence packet, the ring, and the GPU addresses stay as they are. VirtualBox has no Radeon, so this change is compiled there and not measured. The proof is still the step 5 stick.

- `ih_handler` writes `IH_RB_RPTR` even when it found nothing, and reads the write pointer once more after that write. When both reads are empty and the line is shared legacy (`sMsi` is false), the handler returns `B_UNHANDLED_INTERRUPT` after that write. An MSI empty visit stays handled.
- If the writeback word still equals the read pointer, the handler reads the `IH_RB_WPTR` register over MMIO.
- The user fence and the IH writeback are read through the carve-out mapping, `r5_gfx9_vram_cpu`, not through the frame-buffer window. `map_carveout` (line 2037) runs after `run_k1` (line 2033), so until the carve-out exists the handler uses a fallback and still writes the read pointer. The same page is not mapped uncached beside the write-combining mapping. x86 forbids two memory types on one page.
- The self-test prints four counters once, at the end: MSIs received, early returns, end-of-pipe entries, and retire wakes. `R5_GPU_IH_COUNTS` and `R5_GPU_RING_FILL` copy the caller buffer with `gpu_copy_in` and `gpu_copy_out`. A kernel address is rejected.

The proof rides on step 5, boot 1, in `check.sh`. The pass line is VulkanTeapot's `cpu span ms` for 100 frames under 2000, and the end-of-pipe counter at least 100. The later `fence timeout` and fault `00700c10` stay a separate problem. They are the hang with no reset, and this fix does not claim to clear them.
