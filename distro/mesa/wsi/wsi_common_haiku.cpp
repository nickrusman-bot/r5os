/*
 * Copyright 2026 R5OS Contributors
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
 *
 * View-surface WSI. Lavapipe keeps a CPU image. RADV does not set
 * wants_linear, so the same CPU image parameters ask Mesa for a buffer
 * image (wsi_configure_buffer_image): a device image copied into a
 * host-visible buffer. That buffer is ordinary host-coherent memory.
 * The M6 write-combining boot never passed, so this is not a
 * write-combined mapping, and there is no second mapping.
 * Written from the R5 view-surface specification. No code is taken from
 * haiku-amdgpu, haiku-rpi, or summit.
 */

#define VK_USE_PLATFORM_VIEW_RFIVE 1

#include "wsi_common_private.h"
#include "vk_alloc.h"
#include "vk_device.h"
#include "vk_instance.h"
#include "vk_queue.h"
#include "vulkan/vulkan_r5.h"

#include <Bitmap.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <OS.h>
#include <syslog.h>

#include <assert.h>
#include <new>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct r5_surface {
	VkIcdSurfaceViewRFIVE icd;
	BMessenger window;
	BView *view;
	uint32_t width;
	uint32_t height;
	bigtime_t refresh;
	BBitmap *view_bitmap;
	pthread_mutex_t bitmap_mu;

	r5_surface()
		: window(),
		  view(NULL),
		  width(1),
		  height(1),
		  refresh(16667),
		  view_bitmap(NULL)
	{
		icd.base.platform = VK_ICD_WSI_PLATFORM_VIEW_RFIVE;
		icd.view = NULL;
		pthread_mutex_init(&bitmap_mu, NULL);
	}

	~r5_surface()
	{
		pthread_mutex_destroy(&bitmap_mu);
	}
};

struct r5_wsi_slot {
	struct wsi_device *wsi;
	uint64_t graphics;
};

static struct r5_wsi_slot g_slots[4];
static pthread_mutex_t g_slot_mu = PTHREAD_MUTEX_INITIALIZER;

static uint64_t
r5_graphics_for(struct wsi_device *wsi)
{
	uint64_t mask = 0;
	pthread_mutex_lock(&g_slot_mu);
	for (int i = 0; i < 4; i++) {
		if (g_slots[i].wsi == wsi)
			mask = g_slots[i].graphics;
	}
	pthread_mutex_unlock(&g_slot_mu);
	return mask;
}

static BBitmap *
r5_view_bitmap(struct r5_surface *surface)
{
	pthread_mutex_lock(&surface->bitmap_mu);
	BBitmap *bitmap = surface->view_bitmap;
	pthread_mutex_unlock(&surface->bitmap_mu);
	return bitmap;
}

static void
r5_set_view_bitmap(struct r5_surface *surface, BBitmap *bitmap)
{
	pthread_mutex_lock(&surface->bitmap_mu);
	surface->view_bitmap = bitmap;
	pthread_mutex_unlock(&surface->bitmap_mu);
}

struct r5_image {
	struct wsi_image base;
	BBitmap *bitmap;
	bool held;
};

struct r5_swapchain {
	struct wsi_swapchain base;
	struct r5_surface *surface;
	struct r5_image *images;
	uint32_t width;
	uint32_t height;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	pthread_mutex_t present_mu;
	bool out_of_date;
	bool lost;
	bigtime_t refresh;
	bigtime_t last_present;
	bool retrace_failed;
	/* The image after the last one presented. Zero until the first present. */
	uint32_t next_image;
};

static struct r5_surface *
r5_surface_from_icd(VkIcdSurfaceBase *base)
{
	return reinterpret_cast<struct r5_surface *>(base);
}

static bigtime_t
r5_refresh_period(BWindow *window)
{
	display_mode mode;
	BScreen screen(window);
	if (screen.GetMode(&mode) != B_OK)
		return 16667;
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0
		|| mode.timing.pixel_clock == 0)
		return 16667;
	double lines = (double)mode.timing.h_total * (double)mode.timing.v_total;
	double hz = (mode.timing.pixel_clock * 1000.0) / lines;
	if (hz < 1.0)
		return 16667;
	return (bigtime_t)(1000000.0 / hz);
}

static bool
r5_lock_window(struct r5_surface *surface, bigtime_t timeout, BWindow **window_out)
{
	status_t status = surface->window.LockTargetWithTimeout(timeout);
	if (status != B_OK)
		return false;
	BLooper *looper = NULL;
	surface->window.Target(&looper);
	BWindow *window = static_cast<BWindow *>(looper);
	if (window == NULL || surface->view == NULL
		|| surface->view->Window() != window) {
		if (window != NULL)
			window->Unlock();
		return false;
	}
	*window_out = window;
	return true;
}

static void
r5_read_extent(struct r5_surface *surface)
{
	BWindow *window = NULL;
	if (!r5_lock_window(surface, surface->refresh, &window))
		return;
	BRect bounds = surface->view->Bounds();
	surface->width = (uint32_t)(bounds.IntegerWidth() + 1);
	surface->height = (uint32_t)(bounds.IntegerHeight() + 1);
	if (surface->width == 0)
		surface->width = 1;
	if (surface->height == 0)
		surface->height = 1;
	surface->refresh = r5_refresh_period(window);
	window->Unlock();
}

static void
r5_fill_capabilities(struct r5_surface *surface, VkSurfaceCapabilitiesKHR *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->minImageCount = 2;
	caps->maxImageCount = 3;
	caps->currentExtent.width = surface->width;
	caps->currentExtent.height = surface->height;
	caps->minImageExtent.width = 1;
	caps->minImageExtent.height = 1;
	caps->maxImageExtent = caps->currentExtent;
	caps->maxImageArrayLayers = 1;
	caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	caps->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		| VK_IMAGE_USAGE_TRANSFER_DST_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
}

static bool gSettingsReady;
static bool gGpuPresent;
static bool gGpuPresentFail;
static bool gRefused;

static void
r5_read_settings(void)
{
	if (gSettingsReady)
		return;
	gSettingsReady = true;
	char base[512];
	char path[640];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, base, sizeof(base)) != B_OK)
		return;
	snprintf(path, sizeof(path), "%s/kernel/drivers/radeon_hd", base);
	FILE *file = fopen(path, "r");
	if (file == NULL)
		return;
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		char key[64];
		char value[64];
		if (sscanf(line, " %63s %63s", key, value) != 2)
			continue;
		if (key[0] == '#')
			continue;
		bool on = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
		if (strcmp(key, "gpu_present") == 0)
			gGpuPresent = on;
		else if (strcmp(key, "gpu_present_fail") == 0)
			gGpuPresentFail = on;
	}
	fclose(file);
}

static bool
r5_radv_may_present(void)
{
	r5_read_settings();
	return gGpuPresent && !gGpuPresentFail;
}

static void
r5_log_refuse_once(void)
{
	if (gRefused)
		return;
	gRefused = true;
	syslog(LOG_INFO, "RADV view surface: no, window through Lavapipe");
}

static VkResult
r5_get_support(VkIcdSurfaceBase *icd, struct wsi_device *wsi,
	uint32_t queueFamilyIndex, VkBool32 *supported)
{
	(void)icd;
	uint64_t mask = r5_graphics_for(wsi);
	bool graphics = queueFamilyIndex < 64
		&& (mask & ((uint64_t)1 << queueFamilyIndex)) != 0;
	if (!wsi->sw) {
		if (!r5_radv_may_present()) {
			r5_log_refuse_once();
			*supported = VK_FALSE;
			return VK_SUCCESS;
		}
		*supported = graphics ? VK_TRUE : VK_FALSE;
		return VK_SUCCESS;
	}
	*supported = graphics ? VK_TRUE : VK_FALSE;
	return VK_SUCCESS;
}

static VkResult
r5_get_capabilities2(VkIcdSurfaceBase *icd, struct wsi_device *wsi,
	const void *info_next, VkSurfaceCapabilities2KHR *caps2)
{
	(void)wsi;
	(void)info_next;
	struct r5_surface *surface = r5_surface_from_icd(icd);
	if (surface->window.IsValid())
		r5_read_extent(surface);
	r5_fill_capabilities(surface, &caps2->surfaceCapabilities);
	return VK_SUCCESS;
}

static const VkSurfaceFormatKHR r5_formats[2] = {
	{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
};

static VkResult
r5_get_formats(VkIcdSurfaceBase *icd, struct wsi_device *wsi,
	uint32_t *count, VkSurfaceFormatKHR *formats)
{
	(void)icd;
	(void)wsi;
	if (formats == NULL) {
		*count = 2;
		return VK_SUCCESS;
	}
	uint32_t n = *count < 2 ? *count : 2;
	for (uint32_t i = 0; i < n; i++)
		formats[i] = r5_formats[i];
	*count = n;
	return n < 2 ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult
r5_get_formats2(VkIcdSurfaceBase *icd, struct wsi_device *wsi,
	const void *info_next, uint32_t *count, VkSurfaceFormat2KHR *formats)
{
	(void)info_next;
	if (formats == NULL)
		return r5_get_formats(icd, wsi, count, NULL);
	VkSurfaceFormatKHR plain[2];
	uint32_t n = 2;
	VkResult result = r5_get_formats(icd, wsi, &n, plain);
	uint32_t write = *count < n ? *count : n;
	for (uint32_t i = 0; i < write; i++) {
		formats[i].sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
		formats[i].pNext = NULL;
		formats[i].surfaceFormat = plain[i];
	}
	*count = write;
	if (result == VK_INCOMPLETE || write < 2)
		return VK_INCOMPLETE;
	return VK_SUCCESS;
}

static VkResult
r5_get_present_modes(VkIcdSurfaceBase *icd, struct wsi_device *wsi,
	uint32_t *count, VkPresentModeKHR *modes)
{
	(void)icd;
	(void)wsi;
	if (modes == NULL) {
		*count = 1;
		return VK_SUCCESS;
	}
	if (*count < 1) {
		*count = 0;
		return VK_INCOMPLETE;
	}
	modes[0] = VK_PRESENT_MODE_FIFO_KHR;
	*count = 1;
	return VK_SUCCESS;
}

static VkResult
r5_get_present_rectangles(VkIcdSurfaceBase *icd, struct wsi_device *wsi,
	uint32_t *count, VkRect2D *rects)
{
	(void)wsi;
	struct r5_surface *surface = r5_surface_from_icd(icd);
	if (rects == NULL) {
		*count = 1;
		return VK_SUCCESS;
	}
	if (*count < 1) {
		*count = 0;
		return VK_INCOMPLETE;
	}
	rects[0].offset.x = 0;
	rects[0].offset.y = 0;
	rects[0].extent.width = surface->width;
	rects[0].extent.height = surface->height;
	*count = 1;
	return VK_SUCCESS;
}

static void
r5_release_image(struct r5_swapchain *chain, uint32_t index)
{
	pthread_mutex_lock(&chain->mu);
	if (index < chain->base.image_count)
		chain->images[index].held = false;
	pthread_cond_broadcast(&chain->cv);
	pthread_mutex_unlock(&chain->mu);
}

static struct wsi_image *
r5_get_wsi_image(struct wsi_swapchain *wsi_chain, uint32_t image_index)
{
	struct r5_swapchain *chain = (struct r5_swapchain *)wsi_chain;
	return &chain->images[image_index].base;
}

static VkResult
r5_acquire_next_image(struct wsi_swapchain *wsi_chain,
	const VkAcquireNextImageInfoKHR *info, uint32_t *image_index)
{
	struct r5_swapchain *chain = (struct r5_swapchain *)wsi_chain;
	struct timespec deadline;
	bool timed = info->timeout != UINT64_MAX;
	if (timed && info->timeout != 0) {
		clock_gettime(CLOCK_REALTIME, &deadline);
		deadline.tv_sec += info->timeout / 1000000000ull;
		deadline.tv_nsec += info->timeout % 1000000000ull;
		if (deadline.tv_nsec >= 1000000000L) {
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000L;
		}
	}

	pthread_mutex_lock(&chain->mu);
	for (;;) {
		if (chain->lost) {
			pthread_mutex_unlock(&chain->mu);
			return VK_ERROR_SURFACE_LOST_KHR;
		}
		uint32_t count = chain->base.image_count;
		uint32_t start = chain->next_image;
		for (uint32_t n = 0; n < count; n++) {
			uint32_t i = (start + n) % count;
			if (chain->images[i].held)
				continue;
			/* Still on screen. The next present must not write this bitmap. */
			if (chain->images[i].bitmap != NULL
				&& chain->images[i].bitmap == r5_view_bitmap(chain->surface))
				continue;
			chain->images[i].held = true;
			*image_index = i;
			pthread_mutex_unlock(&chain->mu);
			return VK_SUCCESS;
		}
		if (info->timeout == 0) {
			pthread_mutex_unlock(&chain->mu);
			return VK_TIMEOUT;
		}
		int wait;
		if (!timed)
			wait = pthread_cond_wait(&chain->cv, &chain->mu);
		else
			wait = pthread_cond_timedwait(&chain->cv, &chain->mu, &deadline);
		if (wait != 0) {
			pthread_mutex_unlock(&chain->mu);
			return VK_TIMEOUT;
		}
	}
}

static VkResult
r5_release_images(struct wsi_swapchain *wsi_chain, uint32_t count,
	const uint32_t *indices)
{
	struct r5_swapchain *chain = (struct r5_swapchain *)wsi_chain;
	pthread_mutex_lock(&chain->mu);
	for (uint32_t i = 0; i < count; i++) {
		uint32_t index = indices[i];
		if (index < chain->base.image_count)
			chain->images[index].held = false;
	}
	pthread_cond_broadcast(&chain->cv);
	pthread_mutex_unlock(&chain->mu);
	return VK_SUCCESS;
}

static void
r5_copy_rows(struct wsi_image *image, BBitmap *bitmap, uint32_t width,
	uint32_t height)
{
	uint8_t *src = (uint8_t *)image->cpu_map;
	uint8_t *dst = (uint8_t *)bitmap->Bits();
	uint32_t src_pitch = image->row_pitches[0];
	uint32_t dst_pitch = (uint32_t)bitmap->BytesPerRow();
	uint32_t row = width * 4;
	if (src_pitch == 0)
		src_pitch = row;
	for (uint32_t y = 0; y < height; y++)
		memcpy(dst + y * dst_pitch, src + y * src_pitch, row);
}

static void
r5_fifo_wait(struct r5_swapchain *chain, BWindow *window)
{
	bigtime_t now = system_time();
	if (chain->retrace_failed && chain->last_present != 0) {
		bigtime_t elapsed = now - chain->last_present;
		if (elapsed < chain->refresh)
			snooze(chain->refresh - elapsed);
	}
	BScreen screen(window);
	status_t status = screen.WaitForRetrace(chain->refresh);
	chain->retrace_failed = status != B_OK;
	chain->last_present = system_time();
}

static VkResult
r5_queue_present(struct wsi_swapchain *wsi_chain, uint32_t image_index,
	uint64_t present_id, const VkPresentRegionKHR *damage)
{
	(void)present_id;
	(void)damage;
	struct r5_swapchain *chain = (struct r5_swapchain *)wsi_chain;
	struct r5_surface *surface = chain->surface;
	struct r5_image *image = &chain->images[image_index];

	pthread_mutex_lock(&chain->present_mu);

	pthread_mutex_lock(&chain->mu);
	bool lost = chain->lost || !surface->window.IsValid();
	bool later = chain->out_of_date;
	pthread_mutex_unlock(&chain->mu);
	if (lost) {
		pthread_mutex_lock(&chain->mu);
		chain->lost = true;
		pthread_mutex_unlock(&chain->mu);
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_ERROR_SURFACE_LOST_KHR;
	}
	if (later) {
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_ERROR_OUT_OF_DATE_KHR;
	}

	if (image->base.cpu_map == NULL || image->bitmap == NULL) {
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_ERROR_SURFACE_LOST_KHR;
	}

	/*
	 * The bitmap on the view is the one app_server is reading. Copying into
	 * it would replace the frame that is still on screen.
	 */
	BBitmap *showing = r5_view_bitmap(surface);
	assert(image->bitmap != showing);
	if (image->bitmap == showing) {
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_SUCCESS;
	}

	/*
	 * RADV's blit runs on its own queue. The graphics-queue fence does not
	 * cover it, and Mesa waits that fence only for a software device.
	 * Wait for the blit queue before reading the host-visible buffer.
	 */
	if (chain->base.blit.type == WSI_SWAPCHAIN_BUFFER_BLIT
		&& chain->base.blit.queue != NULL) {
		VK_FROM_HANDLE(vk_device, vkdev, chain->base.device);
		VkResult waited = vkdev->dispatch_table.QueueWaitIdle(
			vk_queue_to_handle(chain->base.blit.queue));
		if (waited != VK_SUCCESS) {
			r5_release_image(chain, image_index);
			pthread_mutex_unlock(&chain->present_mu);
			return waited;
		}
	}

	r5_copy_rows(&image->base, image->bitmap, chain->width, chain->height);

	BWindow *window = NULL;
	if (!r5_lock_window(surface, chain->refresh, &window)) {
		bool gone = !surface->window.IsValid();
		if (gone) {
			pthread_mutex_lock(&chain->mu);
			chain->lost = true;
			pthread_mutex_unlock(&chain->mu);
			r5_release_image(chain, image_index);
			pthread_mutex_unlock(&chain->present_mu);
			return VK_ERROR_SURFACE_LOST_KHR;
		}
		/* The window was busy for a whole frame. Do not draw. */
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_SUCCESS;
	}

	BRect bounds = surface->view->Bounds();
	uint32_t live_w = (uint32_t)(bounds.IntegerWidth() + 1);
	uint32_t live_h = (uint32_t)(bounds.IntegerHeight() + 1);
	bool mismatch = live_w != chain->width || live_h != chain->height;
	pthread_mutex_lock(&chain->mu);
	bool already = chain->out_of_date;
	pthread_mutex_unlock(&chain->mu);
	if (already) {
		window->Unlock();
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_ERROR_OUT_OF_DATE_KHR;
	}

	/* WaitForRetrace can need the window thread. Do not hold its lock. */
	window->Unlock();
	r5_fifo_wait(chain, window);
	if (!r5_lock_window(surface, chain->refresh, &window)) {
		bool gone = !surface->window.IsValid();
		if (gone) {
			pthread_mutex_lock(&chain->mu);
			chain->lost = true;
			pthread_mutex_unlock(&chain->mu);
			r5_release_image(chain, image_index);
			pthread_mutex_unlock(&chain->present_mu);
			return VK_ERROR_SURFACE_LOST_KHR;
		}
		r5_release_image(chain, image_index);
		pthread_mutex_unlock(&chain->present_mu);
		return VK_SUCCESS;
	}

	BRect src = image->bitmap->Bounds();
	BRect dst = surface->view->Bounds();
	surface->view->SetViewBitmap(image->bitmap, src, dst, B_FOLLOW_ALL, 0);
	surface->view->Invalidate();
	r5_set_view_bitmap(surface, image->bitmap);
	chain->next_image = (image_index + 1) % chain->base.image_count;
	/*
	 * BView::Sync() calls CheckOwnerLock() and stops in the debugger if the
	 * looper is not locked. BWindow::Sync() takes the lock itself, sends
	 * AS_SYNC, and app_server replies when those drawing commands are done.
	 * That reply does not run Draw on the window thread.
	 */
	window->Sync();
	window->Unlock();

	r5_release_image(chain, image_index);
	pthread_mutex_unlock(&chain->present_mu);

	if (mismatch) {
		pthread_mutex_lock(&chain->mu);
		chain->out_of_date = true;
		pthread_mutex_unlock(&chain->mu);
		return VK_SUBOPTIMAL_KHR;
	}
	return VK_SUCCESS;
}

static VkResult
r5_swapchain_destroy(struct wsi_swapchain *wsi_chain,
	const VkAllocationCallbacks *allocator)
{
	struct r5_swapchain *chain = (struct r5_swapchain *)wsi_chain;
	struct r5_surface *surface = chain->surface;

	BBitmap *showing = r5_view_bitmap(surface);
	bool ours = false;
	for (uint32_t i = 0; i < chain->base.image_count; i++) {
		if (chain->images[i].bitmap != NULL && chain->images[i].bitmap == showing)
			ours = true;
	}
	BWindow *window = NULL;
	if (ours && r5_lock_window(surface, chain->refresh, &window)) {
		surface->view->ClearViewBitmap();
		r5_set_view_bitmap(surface, NULL);
		window->Unlock();
	}

	for (uint32_t i = 0; i < chain->base.image_count; i++) {
		if (chain->images[i].base.image != VK_NULL_HANDLE)
			wsi_destroy_image(&chain->base, &chain->images[i].base);
		delete chain->images[i].bitmap;
		chain->images[i].bitmap = NULL;
	}

	pthread_mutex_destroy(&chain->mu);
	pthread_cond_destroy(&chain->cv);
	pthread_mutex_destroy(&chain->present_mu);
	wsi_swapchain_finish(&chain->base);
	vk_free(allocator, chain->images);
	vk_free(allocator, chain);
	return VK_SUCCESS;
}

static VkResult
r5_create_swapchain(VkIcdSurfaceBase *icd, VkDevice device,
	struct wsi_device *wsi_device, const VkSwapchainCreateInfoKHR *info,
	const VkAllocationCallbacks *allocator, struct wsi_swapchain **out)
{
	struct r5_surface *surface = r5_surface_from_icd(icd);

	if (!wsi_device->sw && !r5_radv_may_present()) {
		r5_log_refuse_once();
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	if (info->presentMode != VK_PRESENT_MODE_FIFO_KHR)
		return VK_ERROR_INITIALIZATION_FAILED;
	if (info->minImageCount > 3 || info->imageArrayLayers != 1)
		return VK_ERROR_INITIALIZATION_FAILED;
	bool format_ok = false;
	for (uint32_t i = 0; i < 2; i++) {
		if (info->imageFormat == r5_formats[i].format
			&& info->imageColorSpace == r5_formats[i].colorSpace)
			format_ok = true;
	}
	if (!format_ok)
		return VK_ERROR_INITIALIZATION_FAILED;

	uint32_t count = info->minImageCount;
	if (count < 2)
		count = 2;

	struct r5_swapchain *chain = (struct r5_swapchain *)vk_zalloc(allocator,
		sizeof(*chain), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (chain == NULL)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	/*
	 * WSI_IMAGE_TYPE_CPU. Lavapipe sets wants_linear and stays a CPU image.
	 * RADV does not, so Mesa calls wsi_configure_buffer_image.
	 */
	struct wsi_cpu_image_params params = {};
	params.base.image_type = WSI_IMAGE_TYPE_CPU;

	VkResult result = wsi_swapchain_init(wsi_device, &chain->base, device,
		info, &params.base, allocator);
	if (result != VK_SUCCESS) {
		vk_free(allocator, chain);
		return result;
	}

	chain->images = (struct r5_image *)vk_zalloc(allocator,
		sizeof(chain->images[0]) * count, 8,
		VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (chain->images == NULL) {
		wsi_swapchain_finish(&chain->base);
		vk_free(allocator, chain);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	chain->surface = surface;
	chain->width = info->imageExtent.width;
	chain->height = info->imageExtent.height;
	chain->refresh = surface->refresh;
	pthread_mutex_init(&chain->mu, NULL);
	pthread_cond_init(&chain->cv, NULL);
	pthread_mutex_init(&chain->present_mu, NULL);
	chain->base.destroy = r5_swapchain_destroy;
	chain->base.get_wsi_image = r5_get_wsi_image;
	chain->base.acquire_next_image = r5_acquire_next_image;
	chain->base.release_images = r5_release_images;
	chain->base.queue_present = r5_queue_present;
	chain->base.present_mode = VK_PRESENT_MODE_FIFO_KHR;
	chain->base.image_count = count;

	BRect frame(0, 0, (float)chain->width - 1, (float)chain->height - 1);
	for (uint32_t i = 0; i < count; i++) {
		chain->images[i].bitmap = new (std::nothrow) BBitmap(frame, B_RGBA32, false);
		if (chain->images[i].bitmap == NULL
			|| chain->images[i].bitmap->InitCheck() != B_OK) {
			chain->base.image_count = i;
			r5_swapchain_destroy(&chain->base, allocator);
			return VK_ERROR_INITIALIZATION_FAILED;
		}
		result = wsi_create_image(&chain->base, &chain->base.image_info,
			&chain->images[i].base);
		if (result != VK_SUCCESS) {
			chain->base.image_count = i + 1;
			r5_swapchain_destroy(&chain->base, allocator);
			return result;
		}
	}

	*out = &chain->base;
	return VK_SUCCESS;
}

static struct wsi_interface r5_interface = {
	r5_get_support,
	r5_get_capabilities2,
	r5_get_formats,
	r5_get_formats2,
	r5_get_present_modes,
	r5_get_present_rectangles,
	r5_create_swapchain,
};

extern "C" {

void
wsi_view_r5_init_wsi(struct wsi_device *wsi, uint64_t graphics_mask)
{
	pthread_mutex_lock(&g_slot_mu);
	int free_slot = -1;
	for (int i = 0; i < 4; i++) {
		if (g_slots[i].wsi == wsi) {
			g_slots[i].graphics = graphics_mask;
			free_slot = -2;
			break;
		}
		if (g_slots[i].wsi == NULL && free_slot < 0)
			free_slot = i;
	}
	if (free_slot >= 0) {
		g_slots[free_slot].wsi = wsi;
		g_slots[free_slot].graphics = graphics_mask;
	}
	pthread_mutex_unlock(&g_slot_mu);
	wsi->wsi[VK_ICD_WSI_PLATFORM_VIEW_RFIVE] = &r5_interface;
}

void
wsi_view_r5_finish_wsi(struct wsi_device *wsi)
{
	wsi->wsi[VK_ICD_WSI_PLATFORM_VIEW_RFIVE] = NULL;
	pthread_mutex_lock(&g_slot_mu);
	for (int i = 0; i < 4; i++) {
		if (g_slots[i].wsi == wsi) {
			g_slots[i].wsi = NULL;
			g_slots[i].graphics = 0;
		}
	}
	pthread_mutex_unlock(&g_slot_mu);
}

void
wsi_view_r5_surface_destroy(VkIcdSurfaceBase *icd, VkInstance instance,
	const VkAllocationCallbacks *allocator)
{
	VK_FROM_HANDLE(vk_instance, vk, instance);
	struct r5_surface *surface = r5_surface_from_icd(icd);
	const VkAllocationCallbacks *alloc = &vk->alloc;
	surface->~r5_surface();
	vk_free2(alloc, allocator, surface);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_CreateViewSurfaceRFIVE(VkInstance _instance,
	const VkViewSurfaceCreateInfoRFIVE *info,
	const VkAllocationCallbacks *allocator, VkSurfaceKHR *out)
{
	VK_FROM_HANDLE(vk_instance, instance, _instance);

	if (info == NULL || info->sType != VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE
		|| info->pNext != NULL || info->flags != 0 || info->view == NULL)
		return VK_ERROR_INITIALIZATION_FAILED;

	BWindow *window = info->view->Window();
	if (window == NULL)
		return VK_ERROR_INITIALIZATION_FAILED;
	if (window->LockWithTimeout(16667) != B_OK)
		return VK_ERROR_INITIALIZATION_FAILED;

	void *mem = vk_alloc2(&instance->alloc, allocator, sizeof(struct r5_surface),
		16, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (mem == NULL) {
		window->Unlock();
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	struct r5_surface *surface = new (mem) r5_surface();
	surface->view = info->view;
	surface->icd.view = info->view;
	surface->window = BMessenger(window);
	BRect bounds = info->view->Bounds();
	surface->width = (uint32_t)(bounds.IntegerWidth() + 1);
	surface->height = (uint32_t)(bounds.IntegerHeight() + 1);
	if (surface->width == 0)
		surface->width = 1;
	if (surface->height == 0)
		surface->height = 1;
	surface->refresh = r5_refresh_period(window);
	window->Unlock();

	*out = VkIcdSurfaceBase_to_handle(&surface->icd.base);
	return VK_SUCCESS;
}

}
