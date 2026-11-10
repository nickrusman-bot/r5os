/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-late
 *
 * VulkanTeapot --window. The picture is a swapchain in an ordinary
 * window. --selftest does not call this file.
 */

#ifndef VK_USE_PLATFORM_VIEW_RFIVE
#define VK_USE_PLATFORM_VIEW_RFIVE
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_r5.h>

#include "teapot_spv.h"

#include <Application.h>
#include <OS.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct Vertex {
	float position[3];
	float normal[3];
};

struct Mesh {
	Vertex* vertices;
	uint32_t vertexCount;
	uint32_t* indices;
	uint32_t indexCount;
};

Mesh build_teapot(void);
Mesh build_cube(void);
void mvp_turned(float out[16], float angle);
uint32_t memory_type(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags);


static int gDeviceLost = 0;


static void
finish(const char* why)
{
	if (why != NULL)
		fprintf(stderr, "window %s\n", why);
	printf("device lost %s\n", gDeviceLost ? "yes" : "no");
}


class WindowView : public BView {
public:
	WindowView(BRect frame)
		: BView(frame, "teapot", B_FOLLOW_ALL, B_WILL_DRAW)
	{
	}
};


class WindowHost : public BWindow {
public:
	WindowHost()
		: BWindow(BRect(80, 80, 80 + 383, 80 + 383), "VulkanTeapot",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS),
		fStop(false),
		fFinished(false)
	{
		AddChild(new WindowView(Bounds()));
	}

	BView* View()
	{
		return ChildAt(0);
	}

	bool Stopping()
	{
		return fStop;
	}

	void Finish()
	{
		fFinished = true;
	}

	bool QuitRequested()
	{
		fStop = true;
		return fFinished;
	}

private:
	volatile bool fStop;
	volatile bool fFinished;
};


struct Swap {
	VkSwapchainKHR chain;
	VkImage images[4];
	VkImageView views[4];
	VkFramebuffer frames[4];
	uint32_t count;
	uint32_t width;
	uint32_t height;
	VkImage depth;
	VkDeviceMemory depthMemory;
	VkImageView depthView;
	VkRenderPass pass;
};


static void
swap_clear(Swap* swap)
{
	memset(swap, 0, sizeof(*swap));
}


static void
swap_destroy(VkDevice device, Swap* swap)
{
	if (device == VK_NULL_HANDLE)
		return;
	vkDeviceWaitIdle(device);
	for (uint32_t i = 0; i < swap->count; i++) {
		if (swap->frames[i] != VK_NULL_HANDLE)
			vkDestroyFramebuffer(device, swap->frames[i], NULL);
		if (swap->views[i] != VK_NULL_HANDLE)
			vkDestroyImageView(device, swap->views[i], NULL);
	}
	if (swap->depthView != VK_NULL_HANDLE)
		vkDestroyImageView(device, swap->depthView, NULL);
	if (swap->depth != VK_NULL_HANDLE)
		vkDestroyImage(device, swap->depth, NULL);
	if (swap->depthMemory != VK_NULL_HANDLE)
		vkFreeMemory(device, swap->depthMemory, NULL);
	if (swap->chain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(device, swap->chain, NULL);
	VkRenderPass pass = swap->pass;
	swap_clear(swap);
	swap->pass = pass;
}


static VkResult
swap_create(VkDevice device, VkPhysicalDevice gpu, VkSurfaceKHR surface,
	Swap* swap)
{
	VkRenderPass saved = swap->pass;
	swap_clear(swap);
	swap->pass = saved;
	VkSurfaceCapabilitiesKHR caps;
	VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);
	if (result != VK_SUCCESS)
		return result;
	if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
		return VK_ERROR_OUT_OF_DATE_KHR;

	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color;
	subpass.pDepthStencilAttachment = &depth;
	VkRenderPassCreateInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	passInfo.attachmentCount = 2;
	passInfo.pAttachments = attachments;
	passInfo.subpassCount = 1;
	passInfo.pSubpasses = &subpass;
	if (swap->pass == VK_NULL_HANDLE) {
		result = vkCreateRenderPass(device, &passInfo, NULL, &swap->pass);
		if (result != VK_SUCCESS)
			return result;
	}

	VkSwapchainCreateInfoKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = surface;
	info.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
	info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	info.imageExtent = caps.currentExtent;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	info.clipped = VK_TRUE;
	result = vkCreateSwapchainKHR(device, &info, NULL, &swap->chain);
	if (result != VK_SUCCESS)
		return result;
	swap->width = caps.currentExtent.width;
	swap->height = caps.currentExtent.height;
	vkGetSwapchainImagesKHR(device, swap->chain, &swap->count, NULL);
	if (swap->count > 4)
		swap->count = 4;
	vkGetSwapchainImagesKHR(device, swap->chain, &swap->count, swap->images);

	VkImageCreateInfo depthInfo = {};
	depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	depthInfo.imageType = VK_IMAGE_TYPE_2D;
	depthInfo.format = VK_FORMAT_D32_SFLOAT;
	depthInfo.extent.width = swap->width;
	depthInfo.extent.height = swap->height;
	depthInfo.extent.depth = 1;
	depthInfo.mipLevels = 1;
	depthInfo.arrayLayers = 1;
	depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	result = vkCreateImage(device, &depthInfo, NULL, &swap->depth);
	if (result != VK_SUCCESS)
		return result;
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(device, swap->depth, &requirements);
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(device, &alloc, NULL, &swap->depthMemory);
	if (result != VK_SUCCESS)
		return result;
	vkBindImageMemory(device, swap->depth, swap->depthMemory, 0);
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = swap->depth;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_D32_SFLOAT;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	result = vkCreateImageView(device, &viewInfo, NULL, &swap->depthView);
	if (result != VK_SUCCESS)
		return result;

	for (uint32_t i = 0; i < swap->count; i++) {
		viewInfo.image = swap->images[i];
		viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		result = vkCreateImageView(device, &viewInfo, NULL, &swap->views[i]);
		if (result != VK_SUCCESS)
			return result;
		VkImageView attachmentsView[2] = {swap->views[i], swap->depthView};
		VkFramebufferCreateInfo fb = {};
		fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb.renderPass = swap->pass;
		fb.attachmentCount = 2;
		fb.pAttachments = attachmentsView;
		fb.width = swap->width;
		fb.height = swap->height;
		fb.layers = 1;
		result = vkCreateFramebuffer(device, &fb, NULL, &swap->frames[i]);
		if (result != VK_SUCCESS)
			return result;
	}
	return VK_SUCCESS;
}


struct Loop {
	WindowHost* host;
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice gpu;
	VkSurfaceKHR surface;
	Swap* swap;
	VkQueue queue;
	VkCommandPool pool;
	VkCommandBuffer cmd;
	VkPipeline pipeline;
	VkPipelineLayout layout;
	VkShaderModule vert;
	VkShaderModule frag;
	VkBuffer vertexBuffer;
	VkBuffer indexBuffer;
	VkDeviceMemory vertexMemory;
	VkDeviceMemory indexMemory;
	uint32_t indexCount;
	VkSemaphore imageReady;
	VkSemaphore drawDone;
	VkFence fence;
};

static int32 window_draw(void* data);

/* Same order as vkcube: discrete, integrated, virtual, then CPU. */
static int
device_rank(VkPhysicalDeviceType type)
{
	switch (type) {
		case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
			return 5;
		case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
			return 4;
		case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
			return 3;
		case VK_PHYSICAL_DEVICE_TYPE_CPU:
			return 2;
		case VK_PHYSICAL_DEVICE_TYPE_OTHER:
			return 1;
		default:
			return -1;
	}
}

int
run_window(int cube)
{
	Mesh mesh = cube ? build_cube() : build_teapot();
	BApplication* app = new BApplication("application/x-vnd.R5OS-VulkanTeapot");
	WindowHost* host = new WindowHost();
	host->Show();

	const char* names[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_RFIVE_VIEW_SURFACE_EXTENSION_NAME};
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VulkanTeapot";
	appInfo.apiVersion = VK_API_VERSION_1_0;
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = 2;
	instanceInfo.ppEnabledExtensionNames = names;
	VkInstance instance = VK_NULL_HANDLE;
	if (vkCreateInstance(&instanceInfo, NULL, &instance) != VK_SUCCESS) {
		finish("instance");
		return 1;
	}

	VkViewSurfaceCreateInfoRFIVE create = {};
	create.sType = VK_STRUCTURE_TYPE_VIEW_SURFACE_CREATE_INFO_RFIVE;
	host->Lock();
	create.view = host->View();
	host->Unlock();
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	PFN_vkCreateViewSurfaceRFIVE createView =
		(PFN_vkCreateViewSurfaceRFIVE)vkGetInstanceProcAddr(instance,
			"vkCreateViewSurfaceRFIVE");
	if (createView == NULL
		|| createView(instance, &create, NULL, &surface) != VK_SUCCESS) {
		vkDestroyInstance(instance, NULL);
		finish("surface");
		return 1;
	}

	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, NULL);
	VkPhysicalDevice* gpus = (VkPhysicalDevice*)calloc(count ? count : 1, sizeof(*gpus));
	vkEnumeratePhysicalDevices(instance, &count, gpus);
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	uint32_t family = 0;
	int best = 0;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t families = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &families, NULL);
		VkQueueFamilyProperties* info =
			(VkQueueFamilyProperties*)calloc(families ? families : 1, sizeof(*info));
		vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &families, info);
		int have = 0;
		uint32_t found = 0;
		for (uint32_t q = 0; q < families; q++) {
			if ((info[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				continue;
			VkBool32 supported = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, surface, &supported);
			if (supported == VK_TRUE) {
				found = q;
				have = 1;
				break;
			}
		}
		free(info);
		if (!have)
			continue;
		VkPhysicalDeviceProperties rankProps;
		vkGetPhysicalDeviceProperties(gpus[i], &rankProps);
		int rank = device_rank(rankProps.deviceType);
		if (rank > best) {
			best = rank;
			gpu = gpus[i];
			family = found;
		}
	}
	free(gpus);
	if (gpu == VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(instance, surface, NULL);
		vkDestroyInstance(instance, NULL);
		finish("no device");
		return 1;
	}
	VkPhysicalDeviceProperties picked;
	vkGetPhysicalDeviceProperties(gpu, &picked);
	printf("window driver %s\n", picked.deviceName);
	fflush(stdout);

	float priority = 1;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	const char* devExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = 1;
	deviceInfo.ppEnabledExtensionNames = &devExt;
	VkDevice device = VK_NULL_HANDLE;
	if (vkCreateDevice(gpu, &deviceInfo, NULL, &device) != VK_SUCCESS) {
		vkDestroySurfaceKHR(instance, surface, NULL);
		vkDestroyInstance(instance, NULL);
		finish("device");
		return 1;
	}
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family, 0, &queue);

	Swap swap;
	swap_clear(&swap);
	if (swap_create(device, gpu, surface, &swap) != VK_SUCCESS) {
		vkDestroyDevice(device, NULL);
		vkDestroySurfaceKHR(instance, surface, NULL);
		vkDestroyInstance(instance, NULL);
		finish("swapchain");
		return 1;
	}

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = kTeapotVertSpvWords * 4;
	shaderInfo.pCode = kTeapotVertSpv;
	VkShaderModule vert = VK_NULL_HANDLE;
	vkCreateShaderModule(device, &shaderInfo, NULL, &vert);
	shaderInfo.codeSize = kTeapotFragSpvWords * 4;
	shaderInfo.pCode = kTeapotFragSpv;
	VkShaderModule frag = VK_NULL_HANDLE;
	vkCreateShaderModule(device, &shaderInfo, NULL, &frag);
	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = sizeof(float) * 16;
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	vkCreatePipelineLayout(device, &layoutInfo, NULL, &layout);
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";
	VkVertexInputBindingDescription binding = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription attributes[2] = {};
	attributes[0].location = 0;
	attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[1].location = 1;
	attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[1].offset = 12;
	VkPipelineVertexInputStateCreateInfo vertex = {};
	vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex.vertexBindingDescriptionCount = 1;
	vertex.pVertexBindingDescriptions = &binding;
	vertex.vertexAttributeDescriptionCount = 2;
	vertex.pVertexAttributeDescriptions = attributes;
	VkPipelineInputAssemblyStateCreateInfo assembly = {};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic = {};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamics;
	VkPipelineRasterizationStateCreateInfo raster = {};
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1;
	VkPipelineMultisampleStateCreateInfo samples = {};
	samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo depthState = {};
	depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthState.depthTestEnable = VK_TRUE;
	depthState.depthWriteEnable = VK_TRUE;
	depthState.depthCompareOp = VK_COMPARE_OP_LESS;
	VkPipelineColorBlendAttachmentState blend = {};
	blend.colorWriteMask = 0xf;
	VkPipelineColorBlendStateCreateInfo blendState = {};
	blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blendState.attachmentCount = 1;
	blendState.pAttachments = &blend;
	VkGraphicsPipelineCreateInfo pipeInfo = {};
	pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeInfo.stageCount = 2;
	pipeInfo.pStages = stages;
	pipeInfo.pVertexInputState = &vertex;
	pipeInfo.pInputAssemblyState = &assembly;
	pipeInfo.pViewportState = &viewportState;
	pipeInfo.pRasterizationState = &raster;
	pipeInfo.pMultisampleState = &samples;
	pipeInfo.pDepthStencilState = &depthState;
	pipeInfo.pColorBlendState = &blendState;
	pipeInfo.pDynamicState = &dynamic;
	pipeInfo.layout = layout;
	pipeInfo.renderPass = swap.pass;
	VkPipeline pipeline = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &pipeline)
		!= VK_SUCCESS) {
		swap_destroy(device, &swap);
		vkDestroyDevice(device, NULL);
		vkDestroySurfaceKHR(instance, surface, NULL);
		vkDestroyInstance(instance, NULL);
		finish("pipeline");
		return 1;
	}

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = mesh.vertexCount * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer);
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(device, vertexBuffer, &requirements);
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
	vkAllocateMemory(device, &alloc, NULL, &vertexMemory);
	vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);
	void* mapped = NULL;
	vkMapMemory(device, vertexMemory, 0, bufferInfo.size, 0, &mapped);
	memcpy(mapped, mesh.vertices, bufferInfo.size);
	vkUnmapMemory(device, vertexMemory);
	bufferInfo.size = mesh.indexCount * sizeof(uint32_t);
	bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VkBuffer indexBuffer = VK_NULL_HANDLE;
	vkCreateBuffer(device, &bufferInfo, NULL, &indexBuffer);
	vkGetBufferMemoryRequirements(device, indexBuffer, &requirements);
	alloc.allocationSize = requirements.size;
	VkDeviceMemory indexMemory = VK_NULL_HANDLE;
	vkAllocateMemory(device, &alloc, NULL, &indexMemory);
	vkBindBufferMemory(device, indexBuffer, indexMemory, 0);
	vkMapMemory(device, indexMemory, 0, bufferInfo.size, 0, &mapped);
	memcpy(mapped, mesh.indices, bufferInfo.size);
	vkUnmapMemory(device, indexMemory);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = family;
	VkCommandPool pool = VK_NULL_HANDLE;
	vkCreateCommandPool(device, &poolInfo, NULL, &pool);
	VkCommandBufferAllocateInfo cmdInfo = {};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdInfo.commandPool = pool;
	cmdInfo.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	vkAllocateCommandBuffers(device, &cmdInfo, &cmd);
	VkSemaphoreCreateInfo semInfo = {};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkSemaphore imageReady = VK_NULL_HANDLE;
	VkSemaphore drawDone = VK_NULL_HANDLE;
	vkCreateSemaphore(device, &semInfo, NULL, &imageReady);
	vkCreateSemaphore(device, &semInfo, NULL, &drawDone);
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkFence fence = VK_NULL_HANDLE;
	vkCreateFence(device, &fenceInfo, NULL, &fence);

	Loop loop = {host, instance, device, gpu, surface, &swap, queue, pool, cmd,
		pipeline, layout, vert, frag, vertexBuffer, indexBuffer, vertexMemory,
		indexMemory, mesh.indexCount, imageReady, drawDone, fence};
	thread_id thread = spawn_thread(window_draw, "teapot", B_NORMAL_PRIORITY, &loop);
	if (thread >= 0)
		resume_thread(thread);
	app->Run();
	status_t status = 0;
	if (thread >= 0)
		wait_for_thread(thread, &status);
	free(mesh.vertices);
	free(mesh.indices);
	return 0;
}


static int32
window_draw(void* data)
{
	Loop* loop = (Loop*)data;
	WindowHost* host = loop->host;
	VkDevice device = loop->device;
	VkPhysicalDevice gpu = loop->gpu;
	VkSurfaceKHR surface = loop->surface;
	Swap* swap = loop->swap;
	for (;;) {
		if (!host->Lock())
			break;
		bool stop = host->Stopping();
		host->Unlock();
		if (stop)
			break;
		vkWaitForFences(device, 1, &loop->fence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &loop->fence);
		uint32_t index = 0;
		VkResult acquired = vkAcquireNextImageKHR(device, swap->chain,
			UINT64_C(1000000000), loop->imageReady, VK_NULL_HANDLE, &index);
		if (acquired == VK_ERROR_OUT_OF_DATE_KHR || acquired == VK_SUBOPTIMAL_KHR) {
			swap_destroy(device, swap);
			if (swap_create(device, gpu, surface, swap) != VK_SUCCESS)
				break;
			continue;
		}
		if (acquired == VK_ERROR_SURFACE_LOST_KHR)
			break;
		if (acquired == VK_ERROR_DEVICE_LOST) {
			gDeviceLost = 1;
			break;
		}
		if (acquired != VK_SUCCESS || index >= swap->count)
			break;
		vkResetCommandBuffer(loop->cmd, 0);
		VkCommandBufferBeginInfo begin = {};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(loop->cmd, &begin);
		VkClearValue clears[2] = {};
		clears[0].color.float32[0] = 0.15f;
		clears[0].color.float32[1] = 0.18f;
		clears[0].color.float32[2] = 0.22f;
		clears[0].color.float32[3] = 1;
		clears[1].depthStencil.depth = 1;
		VkRenderPassBeginInfo beginPass = {};
		beginPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		beginPass.renderPass = swap->pass;
		beginPass.framebuffer = swap->frames[index];
		beginPass.renderArea.extent.width = swap->width;
		beginPass.renderArea.extent.height = swap->height;
		beginPass.clearValueCount = 2;
		beginPass.pClearValues = clears;
		vkCmdBeginRenderPass(loop->cmd, &beginPass, VK_SUBPASS_CONTENTS_INLINE);
		VkViewport viewport = {0, 0, (float)swap->width, (float)swap->height, 0, 1};
		VkRect2D scissor = {{0, 0}, {swap->width, swap->height}};
		vkCmdSetViewport(loop->cmd, 0, 1, &viewport);
		vkCmdSetScissor(loop->cmd, 0, 1, &scissor);
		vkCmdBindPipeline(loop->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, loop->pipeline);
		float matrix[16];
		float turns = (float)(system_time() % 4000000) / 4000000.0f;
		mvp_turned(matrix, turns * 6.2831853f);
		vkCmdPushConstants(loop->cmd, loop->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
			sizeof(matrix), matrix);
		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(loop->cmd, 0, 1, &loop->vertexBuffer, &zero);
		vkCmdBindIndexBuffer(loop->cmd, loop->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(loop->cmd, loop->indexCount, 1, 0, 0, 0);
		vkCmdEndRenderPass(loop->cmd);
		vkEndCommandBuffer(loop->cmd);
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &loop->imageReady;
		submit.pWaitDstStageMask = &waitStage;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &loop->cmd;
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &loop->drawDone;
		if (vkQueueSubmit(loop->queue, 1, &submit, loop->fence) == VK_ERROR_DEVICE_LOST) {
			gDeviceLost = 1;
			break;
		}
		VkPresentInfoKHR present = {};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &loop->drawDone;
		present.swapchainCount = 1;
		present.pSwapchains = &swap->chain;
		present.pImageIndices = &index;
		VkResult presented = vkQueuePresentKHR(loop->queue, &present);
		if (presented == VK_ERROR_SURFACE_LOST_KHR)
			break;
		if (presented == VK_ERROR_DEVICE_LOST) {
			gDeviceLost = 1;
			break;
		}
		if (presented == VK_SUBOPTIMAL_KHR || presented == VK_ERROR_OUT_OF_DATE_KHR) {
			swap_destroy(device, swap);
			if (swap_create(device, gpu, surface, swap) != VK_SUCCESS)
				break;
		} else if (presented != VK_SUCCESS)
			break;
	}
	host->Finish();
	if (swap->pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(device, swap->pass, NULL);
	swap->pass = VK_NULL_HANDLE;
	swap_destroy(device, swap);
	vkDestroyFence(device, loop->fence, NULL);
	vkDestroySemaphore(device, loop->drawDone, NULL);
	vkDestroySemaphore(device, loop->imageReady, NULL);
	vkDestroyCommandPool(device, loop->pool, NULL);
	vkDestroyBuffer(device, loop->indexBuffer, NULL);
	vkFreeMemory(device, loop->indexMemory, NULL);
	vkDestroyBuffer(device, loop->vertexBuffer, NULL);
	vkFreeMemory(device, loop->vertexMemory, NULL);
	vkDestroyPipeline(device, loop->pipeline, NULL);
	vkDestroyPipelineLayout(device, loop->layout, NULL);
	vkDestroyShaderModule(device, loop->frag, NULL);
	vkDestroyShaderModule(device, loop->vert, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroySurfaceKHR(loop->instance, surface, NULL);
	vkDestroyInstance(loop->instance, NULL);
	finish(NULL);
	be_app->PostMessage(B_QUIT_REQUESTED);
	return 0;
}
