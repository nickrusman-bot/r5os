/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Opens a window and clears it to one colour for 100 frames through
 * VK_RFIVE_view_surface. Reads one pixel back from the screen.
 */

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <OS.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define VK_USE_PLATFORM_VIEW_RFIVE 1
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_r5.h>

static int g_status = 1;

static int
fail(const char *reason)
{
	printf("wsi clear FAIL %s\n", reason);
	return 1;
}

struct vk {
	PFN_vkGetInstanceProcAddr get;
	PFN_vkCreateInstance create_instance;
	PFN_vkDestroyInstance destroy_instance;
	PFN_vkEnumeratePhysicalDevices enumerate;
	PFN_vkGetPhysicalDeviceProperties properties;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties queues;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR support;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR caps;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR formats;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR modes;
	PFN_vkCreateViewSurfaceRFIVE create_surface;
	PFN_vkDestroySurfaceKHR destroy_surface;
	PFN_vkCreateDevice create_device;
	PFN_vkDestroyDevice destroy_device;
	PFN_vkGetDeviceQueue get_queue;
	PFN_vkCreateSwapchainKHR create_swapchain;
	PFN_vkDestroySwapchainKHR destroy_swapchain;
	PFN_vkGetSwapchainImagesKHR images;
	PFN_vkAcquireNextImageKHR acquire;
	PFN_vkQueueSubmit submit;
	PFN_vkQueueWaitIdle wait;
	PFN_vkQueuePresentKHR present;
	PFN_vkCreateCommandPool create_pool;
	PFN_vkDestroyCommandPool destroy_pool;
	PFN_vkAllocateCommandBuffers allocate;
	PFN_vkBeginCommandBuffer begin;
	PFN_vkEndCommandBuffer end;
	PFN_vkResetCommandBuffer reset;
	PFN_vkCmdPipelineBarrier barrier;
	PFN_vkCmdClearColorImage clear;
	PFN_vkCreateSemaphore create_sem;
	PFN_vkDestroySemaphore destroy_sem;
	PFN_vkCreateFence create_fence;
	PFN_vkDestroyFence destroy_fence;
	PFN_vkWaitForFences wait_fences;
	PFN_vkResetFences reset_fences;
};

static int
load_vk(struct vk *v)
{
	void *lib = dlopen("libvulkan.so.1", RTLD_NOW);
	if (lib == NULL)
		lib = dlopen("libvulkan.so", RTLD_NOW);
	if (lib == NULL)
		return fail("loader");
	v->get = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	if (v->get == NULL)
		return fail("loader");
	v->create_instance = (PFN_vkCreateInstance)v->get(NULL, "vkCreateInstance");
	v->enumerate = (PFN_vkEnumeratePhysicalDevices)v->get(NULL,
		"vkEnumeratePhysicalDevices");
	if (v->create_instance == NULL)
		return fail("loader");
	return 0;
}

static int
load_instance(struct vk *v, VkInstance instance)
{
#define GET(field, name) \
	v->field = (PFN_##name)v->get(instance, #name); \
	if (v->field == NULL) \
		return fail("proc " #name)
	GET(destroy_instance, vkDestroyInstance);
	GET(enumerate, vkEnumeratePhysicalDevices);
	GET(properties, vkGetPhysicalDeviceProperties);
	GET(queues, vkGetPhysicalDeviceQueueFamilyProperties);
	GET(support, vkGetPhysicalDeviceSurfaceSupportKHR);
	GET(caps, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	GET(formats, vkGetPhysicalDeviceSurfaceFormatsKHR);
	GET(modes, vkGetPhysicalDeviceSurfacePresentModesKHR);
	GET(destroy_surface, vkDestroySurfaceKHR);
	GET(create_device, vkCreateDevice);
	GET(destroy_device, vkDestroyDevice);
	GET(get_queue, vkGetDeviceQueue);
	GET(create_swapchain, vkCreateSwapchainKHR);
	GET(destroy_swapchain, vkDestroySwapchainKHR);
	GET(images, vkGetSwapchainImagesKHR);
	GET(acquire, vkAcquireNextImageKHR);
	GET(submit, vkQueueSubmit);
	GET(wait, vkQueueWaitIdle);
	GET(present, vkQueuePresentKHR);
	GET(create_pool, vkCreateCommandPool);
	GET(destroy_pool, vkDestroyCommandPool);
	GET(allocate, vkAllocateCommandBuffers);
	GET(begin, vkBeginCommandBuffer);
	GET(end, vkEndCommandBuffer);
	GET(reset, vkResetCommandBuffer);
	GET(barrier, vkCmdPipelineBarrier);
	GET(clear, vkCmdClearColorImage);
	GET(create_sem, vkCreateSemaphore);
	GET(destroy_sem, vkDestroySemaphore);
	GET(create_fence, vkCreateFence);
	GET(destroy_fence, vkDestroyFence);
	GET(wait_fences, vkWaitForFences);
	GET(reset_fences, vkResetFences);
#undef GET
	v->create_surface = (PFN_vkCreateViewSurfaceRFIVE)v->get(instance,
		"vkCreateViewSurfaceRFIVE");
	if (v->create_surface == NULL)
		return fail("proc vkCreateViewSurfaceRFIVE");
	return 0;
}

static int
run_clear(BView *view, BWindow *window)
{
	struct vk v;
	memset(&v, 0, sizeof(v));
	if (load_vk(&v) != 0)
		return 1;

	const char *names[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME,
	};
	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "r5-wsi-test";
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ici = {};
	ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ici.pApplicationInfo = &app;
	ici.enabledExtensionCount = 2;
	ici.ppEnabledExtensionNames = names;
	VkInstance instance = VK_NULL_HANDLE;
	if (v.create_instance(&ici, NULL, &instance) != VK_SUCCESS)
		return fail("instance");
	if (load_instance(&v, instance) != 0) {
		v.destroy_instance(instance, NULL);
		return 1;
	}

	VkViewSurfaceCreateInfoRFIVE sci = {};
	sci.sType = VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE;
	sci.view = view;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	if (v.create_surface(instance, &sci, NULL, &surface) != VK_SUCCESS) {
		v.destroy_instance(instance, NULL);
		return fail("surface");
	}

	uint32_t ndev = 0;
	v.enumerate(instance, &ndev, NULL);
	if (ndev == 0 || ndev > 8) {
		v.destroy_surface(instance, surface, NULL);
		v.destroy_instance(instance, NULL);
		return fail("no device");
	}
	VkPhysicalDevice devs[8];
	v.enumerate(instance, &ndev, devs);
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	uint32_t family = 0;
	for (uint32_t i = 0; i < ndev; i++) {
		uint32_t nq = 0;
		v.queues(devs[i], &nq, NULL);
		VkQueueFamilyProperties props[8];
		if (nq > 8)
			nq = 8;
		v.queues(devs[i], &nq, props);
		for (uint32_t q = 0; q < nq; q++) {
			VkBool32 ok = VK_FALSE;
			v.support(devs[i], q, surface, &ok);
			if (ok && (props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				gpu = devs[i];
				family = q;
				break;
			}
		}
		if (gpu != VK_NULL_HANDLE)
			break;
	}
	if (gpu == VK_NULL_HANDLE) {
		v.destroy_surface(instance, surface, NULL);
		v.destroy_instance(instance, NULL);
		return fail("no device");
	}

	VkPhysicalDeviceProperties dp;
	v.properties(gpu, &dp);

	float priority = 1.0f;
	VkDeviceQueueCreateInfo qci = {};
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = family;
	qci.queueCount = 1;
	qci.pQueuePriorities = &priority;
	const char *dev_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	VkDeviceCreateInfo dci = {};
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = 1;
	dci.ppEnabledExtensionNames = &dev_ext;
	VkDevice device = VK_NULL_HANDLE;
	if (v.create_device(gpu, &dci, NULL, &device) != VK_SUCCESS) {
		v.destroy_surface(instance, surface, NULL);
		v.destroy_instance(instance, NULL);
		return fail("device");
	}
	VkQueue queue = VK_NULL_HANDLE;
	v.get_queue(device, family, 0, &queue);

	VkSurfaceCapabilitiesKHR caps;
	if (v.caps(gpu, surface, &caps) != VK_SUCCESS) {
		v.destroy_device(device, NULL);
		v.destroy_surface(instance, surface, NULL);
		v.destroy_instance(instance, NULL);
		return fail("caps");
	}
	uint32_t nfmt = 0;
	v.formats(gpu, surface, &nfmt, NULL);
	VkSurfaceFormatKHR fmts[8];
	if (nfmt > 8)
		nfmt = 8;
	v.formats(gpu, surface, &nfmt, fmts);
	VkSurfaceFormatKHR chosen = fmts[0];
	for (uint32_t i = 0; i < nfmt; i++) {
		if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM)
			chosen = fmts[i];
	}

	VkSwapchainCreateInfoKHR sci2 = {};
	sci2.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	sci2.surface = surface;
	sci2.minImageCount = caps.minImageCount;
	sci2.imageFormat = chosen.format;
	sci2.imageColorSpace = chosen.colorSpace;
	sci2.imageExtent = caps.currentExtent;
	sci2.imageArrayLayers = 1;
	sci2.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	sci2.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci2.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	sci2.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	sci2.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	sci2.clipped = VK_TRUE;
	VkSwapchainKHR swap = VK_NULL_HANDLE;
	if (v.create_swapchain(device, &sci2, NULL, &swap) != VK_SUCCESS) {
		v.destroy_device(device, NULL);
		v.destroy_surface(instance, surface, NULL);
		v.destroy_instance(instance, NULL);
		return fail("swapchain");
	}
	uint32_t nimg = 0;
	v.images(device, swap, &nimg, NULL);
	VkImage images[4];
	if (nimg > 4)
		nimg = 4;
	v.images(device, swap, &nimg, images);
	VkImageLayout layouts[4];
	for (uint32_t i = 0; i < nimg; i++)
		layouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;

	VkCommandPoolCreateInfo pci = {};
	pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pci.queueFamilyIndex = family;
	VkCommandPool pool = VK_NULL_HANDLE;
	v.create_pool(device, &pci, NULL, &pool);
	VkCommandBufferAllocateInfo bai = {};
	bai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	bai.commandPool = pool;
	bai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	bai.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	v.allocate(device, &bai, &cmd);
	VkSemaphoreCreateInfo semci = {};
	semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkSemaphore image_ready = VK_NULL_HANDLE;
	VkSemaphore draw_done = VK_NULL_HANDLE;
	v.create_sem(device, &semci, NULL, &image_ready);
	v.create_sem(device, &semci, NULL, &draw_done);
	VkFenceCreateInfo fci = {};
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkFence fence = VK_NULL_HANDLE;
	v.create_fence(device, &fci, NULL, &fence);

	VkClearColorValue color = {};
	color.float32[0] = 51.0f / 255.0f;
	color.float32[1] = 102.0f / 255.0f;
	color.float32[2] = 153.0f / 255.0f;
	color.float32[3] = 1.0f;

	for (int frame = 0; frame < 100; frame++) {
		uint32_t index = 0;
		VkResult ar = v.acquire(device, swap, UINT64_C(1000000000),
			image_ready, VK_NULL_HANDLE, &index);
		if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
			v.destroy_fence(device, fence, NULL);
			v.destroy_sem(device, draw_done, NULL);
			v.destroy_sem(device, image_ready, NULL);
			v.destroy_pool(device, pool, NULL);
			v.destroy_swapchain(device, swap, NULL);
			v.destroy_device(device, NULL);
			v.destroy_surface(instance, surface, NULL);
			v.destroy_instance(instance, NULL);
			return fail("acquire");
		}
		v.wait_fences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		v.reset_fences(device, 1, &fence);
		v.reset(cmd, 0);
		VkCommandBufferBeginInfo bi = {};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		v.begin(cmd, &bi);
		VkImageMemoryBarrier to = {};
		to.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		to.srcAccessMask = 0;
		to.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		to.oldLayout = layouts[index];
		to.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		to.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		to.image = images[index];
		to.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		to.subresourceRange.levelCount = 1;
		to.subresourceRange.layerCount = 1;
		v.barrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to);
		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.levelCount = 1;
		range.layerCount = 1;
		v.clear(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&color, 1, &range);
		VkImageMemoryBarrier from = to;
		from.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		from.dstAccessMask = 0;
		from.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		from.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		v.barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &from);
		v.end(cmd);
		layouts[index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo si = {};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.waitSemaphoreCount = 1;
		si.pWaitSemaphores = &image_ready;
		si.pWaitDstStageMask = &wait_stage;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cmd;
		si.signalSemaphoreCount = 1;
		si.pSignalSemaphores = &draw_done;
		if (v.submit(queue, 1, &si, fence) != VK_SUCCESS) {
			v.destroy_fence(device, fence, NULL);
			v.destroy_sem(device, draw_done, NULL);
			v.destroy_sem(device, image_ready, NULL);
			v.destroy_pool(device, pool, NULL);
			v.destroy_swapchain(device, swap, NULL);
			v.destroy_device(device, NULL);
			v.destroy_surface(instance, surface, NULL);
			v.destroy_instance(instance, NULL);
			return fail("submit");
		}
		VkPresentInfoKHR pi = {};
		pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		pi.waitSemaphoreCount = 1;
		pi.pWaitSemaphores = &draw_done;
		pi.swapchainCount = 1;
		pi.pSwapchains = &swap;
		pi.pImageIndices = &index;
		VkResult pr = v.present(queue, &pi);
		if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) {
			v.destroy_fence(device, fence, NULL);
			v.destroy_sem(device, draw_done, NULL);
			v.destroy_sem(device, image_ready, NULL);
			v.destroy_pool(device, pool, NULL);
			v.destroy_swapchain(device, swap, NULL);
			v.destroy_device(device, NULL);
			v.destroy_surface(instance, surface, NULL);
			v.destroy_instance(instance, NULL);
			return fail("present");
		}
	}

	v.wait(queue);
	window->Lock();
	BPoint point = view->ConvertToScreen(BPoint(
		caps.currentExtent.width / 2.0f, caps.currentExtent.height / 2.0f));
	window->Unlock();
	BBitmap pixel(BRect(0, 0, 0, 0), B_RGBA32);
	BRect spot(point.x, point.y, point.x, point.y);
	BScreen screen(window);
	if (screen.ReadBitmap(&pixel, false, &spot) != B_OK || pixel.Bits() == NULL) {
		v.destroy_fence(device, fence, NULL);
		v.destroy_sem(device, draw_done, NULL);
		v.destroy_sem(device, image_ready, NULL);
		v.destroy_pool(device, pool, NULL);
		v.destroy_swapchain(device, swap, NULL);
		v.destroy_device(device, NULL);
		v.destroy_surface(instance, surface, NULL);
		v.destroy_instance(instance, NULL);
		return fail("read");
	}
	const uint8_t *bytes = (const uint8_t *)pixel.Bits();
	bool match = bytes[0] >= 148 && bytes[0] <= 158
		&& bytes[1] >= 97 && bytes[1] <= 107
		&& bytes[2] >= 46 && bytes[2] <= 56
		&& bytes[3] >= 250;

	v.destroy_fence(device, fence, NULL);
	v.destroy_sem(device, draw_done, NULL);
	v.destroy_sem(device, image_ready, NULL);
	v.destroy_pool(device, pool, NULL);
	v.destroy_swapchain(device, swap, NULL);
	v.destroy_device(device, NULL);
	v.destroy_surface(instance, surface, NULL);
	v.destroy_instance(instance, NULL);

	if (!match) {
		char reason[64];
		snprintf(reason, sizeof(reason), "pixel %u %u %u %u",
			bytes[0], bytes[1], bytes[2], bytes[3]);
		return fail(reason);
	}
	printf("wsi clear ok %s %ux%u frames 100\n", dp.deviceName,
		caps.currentExtent.width, caps.currentExtent.height);
	return 0;
}

class TestView : public BView {
public:
	TestView(BRect frame)
		: BView(frame, "wsi", B_FOLLOW_ALL, B_WILL_DRAW)
	{
	}
};

class TestApp : public BApplication {
public:
	TestApp()
		: BApplication("application/x-vnd.R5-wsi-test")
	{
	}

	void ReadyToRun()
	{
		BRect frame(80, 80, 80 + 63, 80 + 63);
		BWindow *window = new BWindow(frame, "r5-wsi-test", B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
		BView *view = new TestView(window->Bounds());
		window->AddChild(view);
		window->Show();
		g_status = run_clear(view, window);
		PostMessage(B_QUIT_REQUESTED);
	}
};

int
main()
{
	TestApp app;
	app.Run();
	return g_status;
}
