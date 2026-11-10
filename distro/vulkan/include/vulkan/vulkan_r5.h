/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
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
