/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 A1: compose only, with exactly the pixels of the CPU mode. Every
 * window draws into its own buffer (WindowHWInterface). The compose
 * thread copies the damaged parts of the background and of each window,
 * back to front, into a frame with one compute shader, waits for the
 * fence, and puts the frame on the screen through the screen's back
 * buffer and CopyBackToFront: the CPU row copy on Lavapipe, the M8
 * DMA_DATA copy on RADV when gpu_app_server is on.
 */


#include "Compositor.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <new>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <Autolock.h>
#include <FindDirectory.h>

#include <r5_gpu.h>
#include <r5_share.h>
#include <TokenSpace.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "Desktop.h"
#include "DrawingEngine.h"
#include "AccelerantHWInterface.h"
#include "GPUCopy.h"
#include "HWInterface.h"
#include "RenderingBuffer.h"
#include "ServerApp.h"
#include "ServerWindow.h"
#include "View.h"
#include "Window.h"
#include "WindowBuffer.h"
#include "WindowHWInterface.h"

#include "compositor_shaders.h"


static const bigtime_t kFenceSlice = 100000;
static const bigtime_t kFenceLimit = 15000000;
static const bigtime_t kReportInterval = 10000000;


static void
log_line(const char* format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	fprintf(stderr, "app_server: %s\n", line);
	syslog(LOG_INFO, "app_server: %s\n", line);
}


struct settings {
	bool	want;
	bool	lavapipe;
	bool	fail;
	bool	debug;
	// M11 step 3b, decision 4: the frame is the back buffer and one
	// vkCmdCopyBuffer puts it on the scanout. Opt-in until the stick has
	// measured both ways.
	bool	scanout;
};


/*!	Read the way GPUCopy reads gpu_app_server: the radeon_hd settings of
	the account app_server runs as.
*/
static void
load_settings(settings& out)
{
	memset(&out, 0, sizeof(out));
	char path[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
			sizeof(path)) != B_OK)
		return;
	if (strlcat(path, "/kernel/drivers/radeon_hd", sizeof(path))
			>= sizeof(path))
		return;
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return;
	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, "gpu_compositor true", 19) == 0)
			out.want = true;
		if (strncmp(line, "gpu_compositor_lavapipe true", 28) == 0)
			out.lavapipe = true;
		if (strncmp(line, "gpu_compositor_fail true", 24) == 0)
			out.fail = true;
		if (strncmp(line, "gpu_debug true", 14) == 0)
			out.debug = true;
		if (strncmp(line, "gpu_compositor_scanout true", 27) == 0)
			out.scanout = true;
	}
	fclose(file);
}


/*!	Whether the GPU passed its checks at boot (R5_GPU_CAP_AVAILABLE) and
	is not lost now. The bit decides RADV or Lavapipe, not the settings.
*/
static bool
gpu_available(uint64* _gttBytes, const char** _reason)
{
	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (fd < 0) {
		*_reason = "no render node";
		return false;
	}
	r5_gpu_info info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	info.version = 1;
	bool ok = ioctl(fd, R5_GPU_GET_INFO, &info, sizeof(info)) == 0;
	close(fd);
	if (!ok) {
		*_reason = "no gpu info";
		return false;
	}
	if ((info.capabilities & R5_GPU_CAP_AVAILABLE) == 0) {
		*_reason = "gpu not available";
		return false;
	}
	if ((info.capabilities & R5_GPU_CAP_LOST) != 0) {
		*_reason = "gpu lost";
		return false;
	}
	if (_gttBytes != NULL)
		*_gttBytes = info.gtt_bytes;
	return true;
}


static bigtime_t
thread_cpu_time()
{
	thread_info info;
	if (get_thread_info(find_thread(NULL), &info) != B_OK)
		return 0;
	return info.user_time + info.kernel_time;
}


//	#pragma mark - Vulkan


#define VK_INSTANCE_FUNCTIONS(F) \
	F(vkDestroyInstance) \
	F(vkEnumeratePhysicalDevices) \
	F(vkGetPhysicalDeviceProperties) \
	F(vkGetPhysicalDeviceQueueFamilyProperties) \
	F(vkGetPhysicalDeviceMemoryProperties) \
	F(vkEnumerateDeviceExtensionProperties) \
	F(vkCreateDevice) \
	F(vkGetDeviceProcAddr)

#define VK_DEVICE_FUNCTIONS(F) \
	F(vkDestroyDevice) \
	F(vkGetDeviceQueue) \
	F(vkCreateBuffer) \
	F(vkDestroyBuffer) \
	F(vkGetBufferMemoryRequirements) \
	F(vkAllocateMemory) \
	F(vkFreeMemory) \
	F(vkBindBufferMemory) \
	F(vkMapMemory) \
	F(vkCreateShaderModule) \
	F(vkDestroyShaderModule) \
	F(vkCreateDescriptorSetLayout) \
	F(vkDestroyDescriptorSetLayout) \
	F(vkCreatePipelineLayout) \
	F(vkDestroyPipelineLayout) \
	F(vkCreateComputePipelines) \
	F(vkDestroyPipeline) \
	F(vkCreateCommandPool) \
	F(vkDestroyCommandPool) \
	F(vkAllocateCommandBuffers) \
	F(vkBeginCommandBuffer) \
	F(vkEndCommandBuffer) \
	F(vkResetCommandBuffer) \
	F(vkCmdBindPipeline) \
	F(vkCmdPushConstants) \
	F(vkCmdPushDescriptorSetKHR) \
	F(vkCmdDispatch) \
	F(vkCmdPipelineBarrier) \
	F(vkCreateFence) \
	F(vkDestroyFence) \
	F(vkResetFences) \
	F(vkWaitForFences) \
	F(vkQueueSubmit) \
	F(vkDeviceWaitIdle) \
	F(vkCmdCopyBuffer)

#define VK_DECLARE(name) PFN_##name name;

// M11 step 3b: the R5 DRM library that RADV loaded (r5_translate.h, MIT).
typedef int (*r5_token_to_fd_hook)(int, const uint8_t[16], int32_t, int*,
	uint32_t*);
typedef int (*r5_mark_get_hook)(uint32_t, uint64_t*);
typedef int (*r5_import_size_hook)(uint32_t, uint64_t*);


struct CompositorVulkan {
	void*						library;
	PFN_vkGetInstanceProcAddr	vkGetInstanceProcAddr;
	PFN_vkCreateInstance		vkCreateInstance;
	VK_INSTANCE_FUNCTIONS(VK_DECLARE)
	VK_DEVICE_FUNCTIONS(VK_DECLARE)

	VkInstance					instance;
	VkPhysicalDevice			physical;
	VkDevice					device;
	VkQueue						queue;
	uint32						memoryType;
	VkDescriptorSetLayout		setLayout;
	VkPipelineLayout			pipelineLayout;
	VkPipeline					pipeline;
	VkCommandPool				commandPool;
	VkCommandBuffer				commands;
	VkFence						fence;

	VkBuffer					frame;
	VkDeviceMemory				frameMemory;
	uint32*						frameBits;
	char						deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];

	// M11 step 3b: imports of shared views (RADV only).
	bool						canImport;
	void*						drm;
	r5_token_to_fd_hook			tokenToFd;
	r5_mark_get_hook			markGet;
	r5_import_size_hook			importSize;

	// M11 step 3b, decision 4: GPUCopy's back buffer and the scanout,
	// imported from app_server's own exports.
	bool						scanActive;
	uint32						scanGeneration;
	// The GPUCopy generation of the last attempt that failed: no new
	// export until the back buffer or the scanout is a new one.
	uint32						scanFailedGeneration;
	VkBuffer					back;
	VkDeviceMemory				backMemory;
	uint32						backPitch;
	VkBuffer					scanout;
	VkDeviceMemory				scanoutMemory;
	uint32						scanPitch;
};


struct compose_rect {
	uint32	sourceX;
	uint32	sourceY;
	uint32	sourceStride;
	uint32	frameX;
	uint32	frameY;
	uint32	frameStride;
	uint32	width;
	uint32	height;
	uint32	fill;
	uint32	color;
};


//	#pragma mark - Entry and Layer


/*!	A window the compositor knows. It owns the window's interface, which
	must outlive the window's engine and every compose that reads it.
*/
struct Compositor::Entry : public BReferenceable {
	Entry(Window* window, WindowHWInterface* iface)
		:
		window(window),
		iface(iface),
		lastShown(0),
		shareLogged(false)
	{
	}

	virtual ~Entry()
	{
		delete iface;
	}

	Window*				window;
	WindowHWInterface*	iface;
	bigtime_t			lastShown;
	// "window <id> zero-copy off: <reason>" was logged for this window.
	bool				shareLogged;
};


/*!	M11 step 3b: the images of one Vulkan view's swapchain, imported from
	the program. current is the image the compose samples, pending the one
	presented last and not shown yet. Every other image is the program's;
	its semaphore was released when it left current or pending. The
	compose waits for its fence every frame, so an image that left current
	at the start of a frame is read by no GPU work.
*/
struct Compositor::Share {
	struct Image {
		VkBuffer		buffer;
		VkDeviceMemory	memory;
		uint32			importId;
		sem_id			sem;
		uint32			width;
		uint32			height;
		uint32			pitch;
		// overtaken while pending; back to the program once its mark has
		// passed (_ShareRetireDone)
		bool			retiring;
	};

	Window*				window;
	int32				viewToken;
	team_id				team;
	int32				id;
	uint32				count;
	uint32				generation;
	Image				images[kMaxShareImages];
	int32				current;
	int32				pending;
	// No more semaphores released, no more presents taken; freed by the
	// compose thread at the start of its next frame.
	bool				ended;
	// The import is gone (mark_get failed): the program gets a refusal
	// at its next present and makes a copy swapchain.
	bool				broken;
	// From the last clipping rebuild: the view's part of the screen and
	// the screen position of the image's pixel (0, 0).
	BRegion				shape;
	int32				left;
	int32				top;
};


//!	One shared image in one frame, copied under fShareLock.
struct Compositor::ShareDraw {
	Window*				window;
	VkBuffer			buffer;
	uint32				width;
	uint32				height;
	uint32				pitch;
	BRegion				shape;
	int32				left;
	int32				top;
};


/*!	One window in the compose, back to front. A copy taken under the
	Desktop lock, so the compose thread never needs that lock.
*/
struct Compositor::Layer {
	BReference<Entry>			entry;
	BReference<WindowBuffer>	buffer;
	int32						left;
	int32						top;
	BRegion						shape;
};


//	#pragma mark - Compositor


Compositor*
Compositor::Create(Desktop* desktop, HWInterface* screen)
{
	settings options;
	load_settings(options);
	if (!options.want)
		return NULL;

	Compositor* compositor = new(std::nothrow) Compositor(desktop, screen);
	if (compositor == NULL)
		return NULL;
	compositor->fDebug = options.debug;
	compositor->fForceFail = options.fail;
	compositor->fWantScanout = options.scanout;

	const char* reason = NULL;
	uint64 gttBytes = 0;
	bool radv = !options.lavapipe && gpu_available(&gttBytes, &reason);
	if (options.lavapipe)
		reason = "forced";

	if (radv) {
		const char* radvReason = NULL;
		if (compositor->_InitVulkan(true, &radvReason) == B_OK) {
			compositor->fBudget = gttBytes / 2;
			if (compositor->fBudget == 0) {
				// no GTT size from the driver: the Lavapipe rule
				system_info info;
				get_system_info(&info);
				compositor->fBudget = (uint64)info.max_pages * B_PAGE_SIZE / 4;
			}
			log_line("gpu compositor on radv (%s)",
				compositor->fVulkan->deviceName);
			// M11 step 3b: FENCE_WAIT with timeout 0 for the present mark
			// of a shared view. The sequence is the one GFX ring's, so any
			// file of the render node can ask.
			compositor->fFenceFd = open("/dev/gpu/radeon_hd/0",
				O_RDWR | O_CLOEXEC);
		} else {
			reason = radvReason;
			radv = false;
		}
	}
	if (!radv) {
		const char* lavapipeReason = NULL;
		if (compositor->_InitVulkan(false, &lavapipeReason) != B_OK) {
			// Lavapipe cannot start either: the CPU mode, nothing else
			// logged (docs/APP-SERVER-COMPOSITOR.md).
			delete compositor;
			return NULL;
		}
		compositor->fOnLavapipe = true;
		system_info info;
		get_system_info(&info);
		compositor->fBudget = (uint64)info.max_pages * B_PAGE_SIZE / 4;
		log_line("gpu compositor on lavapipe: %s", reason);
	}

	if (compositor->_Init() != B_OK) {
		delete compositor;
		return NULL;
	}
	return compositor;
}


Compositor::Compositor(Desktop* desktop, HWInterface* screen)
	:
	fDesktop(desktop),
	fScreen(screen),
	fVulkan(NULL),
	fActive(false),
	fOnLavapipe(false),
	fDebug(false),
	fForceFail(false),
	fWantScanout(false),
	fScanoutLogged(false),
	fFrontByVulkan(false),
	fPresentCount(0),
	fRestarted(false),
	fGeneration(0),
	fEntryLock("compositor entries"),
	fEntries(20),
	fShareLock("compositor shares"),
	fShares(10),
	fShareCounter(0),
	fFenceFd(-1),
	fShareRetiring(false),
	fLayerLock("compositor layers"),
	fLayers(NULL),
	fBackground(0xff000000),
	fDamageLock("compositor damage"),
	fWakePending(false),
	fWakeSem(-1),
	fThread(-1),
	fQuit(false),
	fOutOfMemory(false),
	fPeriod(0),
	fLastCompose(0),
	fFrameWidth(0),
	fFrameHeight(0),
	fScratch(NULL),
	fWindowBytes(0),
	fBudget(0),
	fFrames(0),
	fSampleCount(0),
	fReportStart(0),
	fCpuAtReport(0),
	fLateLogged(0)
{
}


Compositor::~Compositor()
{
	if (fThread >= 0) {
		fQuit = true;
		release_sem(fWakeSem);
		status_t result;
		wait_for_thread(fThread, &result);
	}
	if (fWakeSem >= 0)
		delete_sem(fWakeSem);
	delete fLayers;
	_ExitVulkan();
	free(fScratch);
	if (fFenceFd >= 0)
		close(fFenceFd);
}


status_t
Compositor::_Init()
{
	fLayers = new(std::nothrow) LayerList(20);
	fWakeSem = create_sem(0, "compositor wake");
	if (fLayers == NULL || fWakeSem < 0)
		return B_NO_MEMORY;

	_UpdatePeriod();
	fActive = true;
	fReportStart = system_time();

	fThread = spawn_thread(_ComposeThread, "compositor",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fThread < 0) {
		fActive = false;
		return fThread;
	}
	resume_thread(fThread);
	return B_OK;
}


status_t
Compositor::_InitVulkan(bool radv, const char** _reason)
{
	_ExitVulkan();

	CompositorVulkan* vk = new(std::nothrow) CompositorVulkan;
	if (vk == NULL) {
		*_reason = "no memory";
		return B_NO_MEMORY;
	}
	memset(vk, 0, sizeof(*vk));
	fVulkan = vk;
	// buffers of an earlier device are not freed through this one
	fGeneration++;

	vk->library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
	if (vk->library == NULL)
		vk->library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (vk->library == NULL) {
		*_reason = "no vulkan loader";
		return B_ERROR;
	}
	vk->vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vk->library,
		"vkGetInstanceProcAddr");
	if (vk->vkGetInstanceProcAddr == NULL) {
		*_reason = "no vulkan loader";
		return B_ERROR;
	}
	vk->vkCreateInstance = (PFN_vkCreateInstance)vk->vkGetInstanceProcAddr(
		NULL, "vkCreateInstance");

	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "app_server";
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &app;
	if (vk->vkCreateInstance == NULL
		|| vk->vkCreateInstance(&instanceInfo, NULL, &vk->instance)
			!= VK_SUCCESS) {
		*_reason = "no vulkan instance";
		return B_ERROR;
	}

#define VK_LOAD_INSTANCE(name) \
	vk->name = (PFN_##name)vk->vkGetInstanceProcAddr(vk->instance, #name); \
	if (vk->name == NULL) { *_reason = "vulkan " #name; return B_ERROR; }
	VK_INSTANCE_FUNCTIONS(VK_LOAD_INSTANCE)

	// RADV is any device that is not a CPU; Lavapipe is the CPU device.
	uint32 count = 8;
	VkPhysicalDevice devices[8];
	if (vk->vkEnumeratePhysicalDevices(vk->instance, &count, devices)
			< VK_SUCCESS || count == 0) {
		*_reason = "no vulkan device";
		return B_ERROR;
	}
	vk->physical = VK_NULL_HANDLE;
	for (uint32 i = 0; i < count; i++) {
		VkPhysicalDeviceProperties properties;
		vk->vkGetPhysicalDeviceProperties(devices[i], &properties);
		bool isCpu = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
		if (isCpu != radv) {
			vk->physical = devices[i];
			strlcpy(vk->deviceName, properties.deviceName,
				sizeof(vk->deviceName));
			break;
		}
	}
	if (vk->physical == VK_NULL_HANDLE) {
		*_reason = radv ? "radv not found" : "lavapipe not found";
		return B_ERROR;
	}

	uint32 extensionCount = 0;
	vk->vkEnumerateDeviceExtensionProperties(vk->physical, NULL,
		&extensionCount, NULL);
	VkExtensionProperties* extensions
		= new(std::nothrow) VkExtensionProperties[extensionCount + 1];
	bool pushDescriptor = false;
	bool memoryFd = false;
	bool dmaBuf = false;
	if (extensions != NULL) {
		vk->vkEnumerateDeviceExtensionProperties(vk->physical, NULL,
			&extensionCount, extensions);
		for (uint32 i = 0; i < extensionCount; i++) {
			if (strcmp(extensions[i].extensionName,
					VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) == 0)
				pushDescriptor = true;
			if (strcmp(extensions[i].extensionName,
					VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) == 0)
				memoryFd = true;
			if (strcmp(extensions[i].extensionName,
					VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME) == 0)
				dmaBuf = true;
		}
		delete[] extensions;
	}
	// M11 step 3b: shared views are imported on RADV only.
	vk->canImport = radv && memoryFd && dmaBuf;
	if (!pushDescriptor) {
		*_reason = "no push descriptor";
		return B_ERROR;
	}

	uint32 familyCount = 8;
	VkQueueFamilyProperties families[8];
	vk->vkGetPhysicalDeviceQueueFamilyProperties(vk->physical, &familyCount,
		families);
	// The queue stays a graphics queue (decision 16): the R5 DRM library
	// accepts only GFX submissions, so a compute-only family would make
	// every submit fail. The compose is a compute dispatch on that queue.
	uint32 family = UINT32_MAX;
	for (uint32 i = 0; i < familyCount; i++) {
		log_line("vulkan queue family %" B_PRIu32 " flags %#" B_PRIx32, i,
			(uint32)families[i].queueFlags);
		const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT
			| VK_QUEUE_COMPUTE_BIT;
		if (family == UINT32_MAX
			&& (families[i].queueFlags & wanted) == wanted)
			family = i;
	}
	if (family == UINT32_MAX) {
		*_reason = "no graphics queue";
		return B_ERROR;
	}

	// Host-visible and coherent memory for the window buffers and the
	// frame: Painter writes the window buffers, the compose reads them.
	// Host-cached first. DEVICE_LOCAL is not excluded: RADV may mark GTT
	// that way on an APU (decision 5).
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	vk->memoryType = UINT32_MAX;
	const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (int pass = 0; pass < 2 && vk->memoryType == UINT32_MAX; pass++) {
		for (uint32 i = 0; i < memory.memoryTypeCount; i++) {
			VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
			if ((flags & wanted) != wanted)
				continue;
			// first pass: also cached, because Painter reads what it
			// blends
			if (pass == 0 && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == 0)
				continue;
			vk->memoryType = i;
			break;
		}
	}
	if (vk->memoryType == UINT32_MAX) {
		*_reason = "no host memory";
		return B_ERROR;
	}

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	const char* deviceExtensions[] = { VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME };
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = vk->canImport ? 3 : 1;
	deviceInfo.ppEnabledExtensionNames = deviceExtensions;
	if (vk->vkCreateDevice(vk->physical, &deviceInfo, NULL, &vk->device)
			!= VK_SUCCESS) {
		*_reason = "no vulkan device";
		return B_ERROR;
	}

#define VK_LOAD_DEVICE(name) \
	vk->name = (PFN_##name)vk->vkGetDeviceProcAddr(vk->device, #name); \
	if (vk->name == NULL) { *_reason = "vulkan " #name; return B_ERROR; }
	VK_DEVICE_FUNCTIONS(VK_LOAD_DEVICE)

	vk->vkGetDeviceQueue(vk->device, family, 0, &vk->queue);

	// The R5 DRM library RADV loaded: dlopen of a loaded library returns
	// that image, so these calls see RADV's import table.
	if (vk->canImport) {
		vk->drm = dlopen("libdrm.so.2", RTLD_NOW | RTLD_LOCAL);
		if (vk->drm != NULL) {
			vk->tokenToFd = (r5_token_to_fd_hook)dlsym(vk->drm,
				"r5_drm_token_to_fd");
			vk->markGet = (r5_mark_get_hook)dlsym(vk->drm, "r5_drm_mark_get");
			vk->importSize = (r5_import_size_hook)dlsym(vk->drm,
				"r5_drm_import_size");
		}
		if (vk->tokenToFd == NULL || vk->markGet == NULL
			|| vk->importSize == NULL)
			vk->canImport = false;
	}

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = sizeof(kComposeSpirv);
	shaderInfo.pCode = kComposeSpirv;
	VkShaderModule shader;
	if (vk->vkCreateShaderModule(vk->device, &shaderInfo, NULL, &shader)
			!= VK_SUCCESS) {
		*_reason = "shader";
		return B_ERROR;
	}

	VkDescriptorSetLayoutBinding bindings[2] = {};
	for (int i = 0; i < 2; i++) {
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	setInfo.bindingCount = 2;
	setInfo.pBindings = bindings;
	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	range.size = sizeof(compose_rect);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &vk->setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VkComputePipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType
		= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = shader;
	pipelineInfo.stage.pName = "main";
	bool pipelineOk
		= vk->vkCreateDescriptorSetLayout(vk->device, &setInfo, NULL,
			&vk->setLayout) == VK_SUCCESS
		&& vk->vkCreatePipelineLayout(vk->device, &layoutInfo, NULL,
			&vk->pipelineLayout) == VK_SUCCESS;
	if (pipelineOk) {
		pipelineInfo.layout = vk->pipelineLayout;
		pipelineOk = vk->vkCreateComputePipelines(vk->device, VK_NULL_HANDLE,
			1, &pipelineInfo, NULL, &vk->pipeline) == VK_SUCCESS;
	}
	vk->vkDestroyShaderModule(vk->device, shader, NULL);
	if (!pipelineOk) {
		*_reason = "pipeline";
		return B_ERROR;
	}

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = family;
	VkCommandBufferAllocateInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (vk->vkCreateCommandPool(vk->device, &poolInfo, NULL, &vk->commandPool)
			!= VK_SUCCESS) {
		*_reason = "command pool";
		return B_ERROR;
	}
	commandInfo.commandPool = vk->commandPool;
	if (vk->vkAllocateCommandBuffers(vk->device, &commandInfo, &vk->commands)
			!= VK_SUCCESS
		|| vk->vkCreateFence(vk->device, &fenceInfo, NULL, &vk->fence)
			!= VK_SUCCESS) {
		*_reason = "command buffer";
		return B_ERROR;
	}
	return B_OK;
}


void
Compositor::_ExitVulkan()
{
	CompositorVulkan* vk = fVulkan;
	if (vk == NULL)
		return;
	fVulkan = NULL;

	// A device that failed half way has some functions not loaded; they
	// are loaded in this order, so the last one tells all were.
	if (vk->device != VK_NULL_HANDLE && vk->vkDeviceWaitIdle != NULL) {
		vk->vkDeviceWaitIdle(vk->device);
		_ScanoutFree(vk);
		// M11 step 3b: the imports of shared views go with this device;
		// the programs get a refusal at their next present.
		{
			BAutolock _(fShareLock);
			fVulkan = vk;
			for (int32 i = fShares.CountItems(); i-- > 0;) {
				Share* share = fShares.ItemAt(i);
				_ShareFree(share);
				fShares.RemoveItemAt(i);
				delete share;
			}
			fVulkan = NULL;
		}
		if (vk->frame != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->frame, NULL);
		if (vk->frameMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->frameMemory, NULL);
		if (vk->fence != VK_NULL_HANDLE)
			vk->vkDestroyFence(vk->device, vk->fence, NULL);
		if (vk->commandPool != VK_NULL_HANDLE)
			vk->vkDestroyCommandPool(vk->device, vk->commandPool, NULL);
		if (vk->pipeline != VK_NULL_HANDLE)
			vk->vkDestroyPipeline(vk->device, vk->pipeline, NULL);
		if (vk->pipelineLayout != VK_NULL_HANDLE)
			vk->vkDestroyPipelineLayout(vk->device, vk->pipelineLayout, NULL);
		if (vk->setLayout != VK_NULL_HANDLE)
			vk->vkDestroyDescriptorSetLayout(vk->device, vk->setLayout, NULL);
	}
	if (vk->device != VK_NULL_HANDLE && vk->vkDestroyDevice != NULL)
		vk->vkDestroyDevice(vk->device, NULL);
	if (vk->instance != VK_NULL_HANDLE && vk->vkDestroyInstance != NULL)
		vk->vkDestroyInstance(vk->instance, NULL);
	if (vk->drm != NULL)
		dlclose(vk->drm);
	if (vk->library != NULL)
		dlclose(vk->library);
	delete vk;
	fFrameWidth = 0;
	fFrameHeight = 0;
}


static bool
allocate_host_buffer(CompositorVulkan* vk, uint64 bytes, VkBuffer* _buffer,
	VkDeviceMemory* _memory, void** _bits)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bytes;
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, _buffer)
			!= VK_SUCCESS)
		return false;

	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *_buffer, &requirements);
	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = vk->memoryType;
	if ((requirements.memoryTypeBits & (1u << vk->memoryType)) == 0
		|| vk->vkAllocateMemory(vk->device, &allocateInfo, NULL, _memory)
			!= VK_SUCCESS) {
		vk->vkDestroyBuffer(vk->device, *_buffer, NULL);
		return false;
	}
	if (vk->vkBindBufferMemory(vk->device, *_buffer, *_memory, 0)
			!= VK_SUCCESS
		|| vk->vkMapMemory(vk->device, *_memory, 0, VK_WHOLE_SIZE, 0, _bits)
			!= VK_SUCCESS) {
		vk->vkDestroyBuffer(vk->device, *_buffer, NULL);
		vk->vkFreeMemory(vk->device, *_memory, NULL);
		return false;
	}
	return true;
}


WindowBuffer*
Compositor::_AllocateBuffer(uint32 width, uint32 height)
{
	if (fVulkan == NULL || width == 0 || height == 0)
		return NULL;
	uint64 bytes = (uint64)width * height * 4;
	if (fWindowBytes + bytes > fBudget)
		return NULL;

	VkBuffer buffer;
	VkDeviceMemory memory;
	void* bits;
	if (!allocate_host_buffer(fVulkan, bytes, &buffer, &memory, &bits))
		return NULL;

	WindowBuffer* windowBuffer = new(std::nothrow) WindowBuffer(this, bits,
		width, height, (uint64)buffer, (uint64)memory, fGeneration);
	if (windowBuffer == NULL) {
		fVulkan->vkDestroyBuffer(fVulkan->device, buffer, NULL);
		fVulkan->vkFreeMemory(fVulkan->device, memory, NULL);
		return NULL;
	}
	atomic_add64((int64*)&fWindowBytes, bytes);

	static bool sLogged = false;
	if (!sLogged) {
		sLogged = true;
		VkPhysicalDeviceMemoryProperties memory;
		fVulkan->vkGetPhysicalDeviceMemoryProperties(fVulkan->physical,
			&memory);
		log_line("window memory type %" B_PRIu32 " flags %#" B_PRIx32,
			fVulkan->memoryType,
			(uint32)memory.memoryTypes[fVulkan->memoryType].propertyFlags);
	}
	return windowBuffer;
}


void
Compositor::_FreeBuffer(WindowBuffer* buffer, uint64 vulkanBuffer,
	uint64 vulkanMemory)
{
	atomic_add64((int64*)&fWindowBytes, -(int64)buffer->Bytes());
	if (fVulkan == NULL || buffer->Generation() != fGeneration)
		return;
	fVulkan->vkDestroyBuffer(fVulkan->device, (VkBuffer)vulkanBuffer, NULL);
	fVulkan->vkFreeMemory(fVulkan->device, (VkDeviceMemory)vulkanMemory,
		NULL);
}


/*!	Allocates a window buffer. Over the budget, the buffers of windows that
	are not on screen now (hidden, minimised, on another workspace) go
	first, the one shown longest ago first; such a window draws all of
	itself again when it shows. Called with all windows and the entry list
	locked. A window without a buffer draws nothing: its drawable regions
	are empty (Window::_ClipToBuffer).
*/
WindowBuffer*
Compositor::_AllocateOrMakeRoom(uint32 width, uint32 height,
	const Entry* keep)
{
	WindowBuffer* buffer = _AllocateBuffer(width, height);
	int32 workspace = fDesktop->CurrentWorkspace();
	while (buffer == NULL) {
		Entry* oldest = NULL;
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry == keep || entry->iface->Buffer() == NULL
				|| entry->window == NULL)
				continue;
			// only windows that are not on screen now
			if (!entry->window->IsHidden()
				&& entry->window->InWorkspace(workspace))
				continue;
			if (oldest == NULL || entry->lastShown < oldest->lastShown)
				oldest = entry;
		}
		if (oldest == NULL)
			return NULL;
		oldest->iface->SetBuffer(NULL, 0, 0);
		buffer = _AllocateBuffer(width, height);
	}
	return buffer;
}


bool
Compositor::_EnsureFrame(uint32 width, uint32 height)
{
	CompositorVulkan* vk = fVulkan;
	if (width == fFrameWidth && height == fFrameHeight && vk->frame != 0)
		return true;

	if (vk->frame != VK_NULL_HANDLE) {
		vk->vkDestroyBuffer(vk->device, vk->frame, NULL);
		vk->vkFreeMemory(vk->device, vk->frameMemory, NULL);
		vk->frame = VK_NULL_HANDLE;
		vk->frameMemory = VK_NULL_HANDLE;
	}
	free(fScratch);
	fScratch = NULL;
	fFrameWidth = 0;
	fFrameHeight = 0;

	void* bits;
	if (!allocate_host_buffer(vk, (uint64)width * height * 4, &vk->frame,
			&vk->frameMemory, &bits))
		return false;
	vk->frameBits = (uint32*)bits;
	fFrameWidth = width;
	fFrameHeight = height;
	if (fDebug)
		fScratch = (uint32*)malloc((size_t)width * height * 4);
	_UpdatePeriod();
	return true;
}


/*!	One refresh period of the current display mode. Without a pixel clock
	there is no period, and the compose runs as soon as damage arrives.
*/
void
Compositor::_UpdatePeriod()
{
	display_mode mode;
	memset(&mode, 0, sizeof(mode));
	fScreen->GetMode(&mode);
	bigtime_t period = 0;
	if (mode.timing.pixel_clock > 0 && mode.timing.h_total > 0
		&& mode.timing.v_total > 0) {
		period = (bigtime_t)mode.timing.h_total * mode.timing.v_total
			* 1000 / mode.timing.pixel_clock;
	}
	if (period == 0) {
		static bool sLogged = false;
		if (!sLogged) {
			sLogged = true;
			log_line("compose period unknown");
		}
	}
	fPeriod = period;
}


//	#pragma mark - windows


DrawingEngine*
Compositor::CreateWindowEngine(WindowHWInterface** _iface)
{
	WindowHWInterface* iface = new(std::nothrow) WindowHWInterface(this);
	if (iface == NULL || iface->Initialize() != B_OK) {
		delete iface;
		return NULL;
	}
	DrawingEngine* engine = iface->CreateDrawingEngine();
	if (engine == NULL) {
		delete iface;
		return NULL;
	}
	*_iface = iface;
	return engine;
}


void
Compositor::AddWindow(Window* window, WindowHWInterface* iface)
{
	Entry* entry = new(std::nothrow) Entry(window, iface);
	if (entry == NULL)
		return;
	BAutolock _(fEntryLock);
	if (!fEntries.AddItem(entry))
		entry->ReleaseReference();
}


void
Compositor::RemoveWindow(Window* window)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL)
		return;
	fEntries.RemoveItem(entry);
	entry->window = NULL;
	entry->ReleaseReference();

	// M11 step 3b: the window's shared views end with it.
	BAutolock shareLock(fShareLock);
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		if (share->window == window) {
			share->ended = true;
			share->window = NULL;
		}
	}
}


bool
Compositor::BufferBounds(Window* window, clipping_rect* _bounds)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->iface->Buffer() == NULL)
		return false;
	WindowBuffer* buffer = entry->iface->Buffer();
	_bounds->left = entry->iface->Left();
	_bounds->top = entry->iface->Top();
	_bounds->right = _bounds->left + (int32)buffer->Width() - 1;
	_bounds->bottom = _bounds->top + (int32)buffer->Height() - 1;
	return true;
}


Compositor::Entry*
Compositor::_FindEntry(Window* window) const
{
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window == window)
			return entry;
	}
	return NULL;
}


/*!	Called at the end of every clipping rebuild, with all windows locked.
	Allocates the buffer of a window on its first show and on a resize,
	moves the Painter view on a move, and damages what changed on screen.
*/
void
Compositor::ClippingChanged()
{
	if (!fActive)
		return;

	LayerList* layers = new(std::nothrow) LayerList(20);
	if (layers == NULL)
		return;

	BObjectList<Window, false> fresh(10);
	BRegion damage;
	int32 workspace = fDesktop->CurrentWorkspace();

	fEntryLock.Lock();
	for (Window* window = fDesktop->CurrentWindows().FirstWindow();
			window != NULL; window = window->NextWindow(workspace)) {
		if (window->IsHidden() || window->IsOffscreenWindow())
			continue;
		Entry* entry = _FindEntry(window);
		if (entry == NULL)
			continue;

		BRegion full;
		window->GetFullRegion(&full);
		if (!full.Frame().IsValid())
			continue;
		clipping_rect bounds = full.FrameInt();
		uint32 width = bounds.right - bounds.left + 1;
		uint32 height = bounds.bottom - bounds.top + 1;

		WindowHWInterface* iface = entry->iface;
		WindowBuffer* buffer = iface->Buffer();
		if (buffer == NULL || buffer->Width() != width
			|| buffer->Height() != height) {
			WindowBuffer* newBuffer = _AllocateOrMakeRoom(width, height,
				entry);
			if (newBuffer == NULL) {
				fEntryLock.Unlock();
				delete layers;
				// Not enough room for a window on this workspace: the
				// CPU mode for the rest of the session. The compose
				// thread switches, because that takes the window lock.
				BAutolock damageLock(fDamageLock);
				fOutOfMemory = true;
				release_sem(fWakeSem);
				return;
			}
			if (buffer != NULL) {
				// Keep what was drawn where the two overlap on screen; the
				// window draws the new parts itself. The overlap is found
				// in screen coordinates, so a window that grows to the left
				// or to the top (an outline, an edge drag) keeps its pixels
				// in place.
				int32 left = max_c(bounds.left, iface->Left());
				int32 top = max_c(bounds.top, iface->Top());
				int32 right = min_c(bounds.right,
					iface->Left() + (int32)buffer->Width() - 1);
				int32 bottom = min_c(bounds.bottom,
					iface->Top() + (int32)buffer->Height() - 1);
				for (int32 y = top; y <= bottom && left <= right; y++) {
					memcpy((uint8*)newBuffer->Bits()
							+ (y - bounds.top) * newBuffer->BytesPerRow()
							+ (left - bounds.left) * 4,
						(uint8*)buffer->Bits()
							+ (y - iface->Top()) * buffer->BytesPerRow()
							+ (left - iface->Left()) * 4,
						(right - left + 1) * 4);
				}
			} else {
				fresh.AddItem(window);
			}
			iface->SetBuffer(newBuffer, bounds.left, bounds.top);
			newBuffer->ReleaseReference();
		} else if (iface->Left() != bounds.left || iface->Top() != bounds.top)
			iface->SetBuffer(buffer, bounds.left, bounds.top);

		entry->lastShown = system_time();
		Layer* layer = new(std::nothrow) Layer;
		if (layer == NULL)
			continue;
		layer->entry.SetTo(entry);
		layer->buffer.SetTo(iface->Buffer());
		layer->left = bounds.left;
		layer->top = bounds.top;
		layer->shape = full;
		layers->AddItem(layer);
	}
	fEntryLock.Unlock();

	// M11 step 3b: where each shared view is now. A view that moved or
	// changed its shape is damaged where it was and where it is.
	{
		BAutolock shareLock(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			BRegion before = share->shape;
			int32 left = share->left;
			int32 top = share->top;
			_ShareShape(share);
			if (before == share->shape && left == share->left
				&& top == share->top)
				continue;
			damage.Include(&before);
			damage.Include(&share->shape);
		}
	}

	// What changed on screen: a window that came, went, moved, changed
	// shape or changed its place in the stack.
	fLayerLock.Lock();
	LayerList* old = fLayers;
	int32 count = max_c(old->CountItems(), layers->CountItems());
	for (int32 i = 0; i < count; i++) {
		Layer* before = old->ItemAt(i);
		Layer* after = layers->ItemAt(i);
		if (before != NULL && after != NULL
			&& before->entry.Get() == after->entry.Get()
			&& before->buffer.Get() == after->buffer.Get()
			&& before->left == after->left && before->top == after->top
			&& before->shape == after->shape)
			continue;
		if (before != NULL)
			damage.Include(&before->shape);
		if (after != NULL)
			damage.Include(&after->shape);
	}
	fLayers = layers;
	fLayerLock.Unlock();
	delete old;

	// A window that got its first buffer draws all of it once.
	for (int32 i = 0; i < fresh.CountItems(); i++) {
		Window* window = fresh.ItemAt(i);
		BRegion full;
		window->GetFullRegion(&full);
		window->ProcessDirtyRegion(full, full);
	}

	Damage(damage);
}


void
Compositor::SetBackgroundColor(rgb_color color)
{
	uint32 pixel = 0xff000000 | ((uint32)color.red << 16)
		| ((uint32)color.green << 8) | color.blue;
	if (pixel == fBackground)
		return;
	fBackground = pixel;
	BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
		(float)fFrameHeight - 1));
	Damage(screen);
}


void
Compositor::Damage(const BRegion& region)
{
	if (!fActive || region.CountRects() == 0)
		return;
	BAutolock _(fDamageLock);
	fDamage.Include(&region);
	if (!fWakePending) {
		fWakePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


//	#pragma mark - compose thread


status_t
Compositor::_ComposeThread(void* data)
{
	Compositor* compositor = (Compositor*)data;
	compositor->_ComposeLoop();
	if (!compositor->fActive)
		compositor->_ExitVulkan();
	return B_OK;
}


void
Compositor::_ComposeLoop()
{
	fCpuAtReport = thread_cpu_time();

	while (true) {
		// An overtaken image goes back once the GPU is done with it; until
		// then the thread looks again every refresh period, also without
		// damage (a hidden window).
		bigtime_t wait = fDebug ? kReportInterval : B_INFINITE_TIMEOUT;
		if (fShareRetiring) {
			bigtime_t slice = fPeriod > 0 ? fPeriod : 16667;
			if (wait == B_INFINITE_TIMEOUT || slice < wait)
				wait = slice;
		}
		status_t status = acquire_sem_etc(fWakeSem, 1, B_RELATIVE_TIMEOUT,
			wait);
		if (status != B_OK && status != B_TIMED_OUT)
			return;
		if (fDebug)
			_Report();

		// app_server ends: no switch, the desktop is going away
		if (fQuit)
			return;
		if (fOutOfMemory) {
			_TurnOff("window memory");
			return;
		}
		_ShareRetireDone();
		if (status == B_TIMED_OUT)
			continue;

		// at most once per refresh period
		if (fPeriod > 0) {
			bigtime_t next = fLastCompose + fPeriod;
			if (system_time() < next)
				snooze_until(next, B_SYSTEM_TIMEBASE);
		}

		BRegion damage;
		{
			BAutolock _(fDamageLock);
			fWakePending = false;
			damage = fDamage;
			fDamage.MakeEmpty();
		}

		// the frame follows the screen size (a mode change)
		RenderingBuffer* back = fScreen->BackBuffer();
		if (back == NULL)
			continue;
		if (back->Width() != fFrameWidth || back->Height() != fFrameHeight) {
			if (!_EnsureFrame(back->Width(), back->Height())) {
				_TurnOff("window memory");
				return;
			}
			damage.Set(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
		}
		// Decision 4: between two composes the GPU is idle (each one waits
		// for its fence), so a new back buffer or scanout is imported here.
		if (_ScanoutPrepare(back))
			damage.Set(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
		BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
			(float)fFrameHeight - 1));
		damage.IntersectWith(&screen);
		if (damage.CountRects() == 0)
			continue;

		LayerList layers(20);
		{
			BAutolock _(fLayerLock);
			for (int32 i = 0; i < fLayers->CountItems(); i++) {
				Layer* source = fLayers->ItemAt(i);
				Layer* copy = new(std::nothrow) Layer;
				if (copy == NULL)
					continue;
				copy->entry = source->entry;
				copy->buffer = source->buffer;
				copy->left = source->left;
				copy->top = source->top;
				copy->shape = source->shape;
				layers.AddItem(copy);
			}
		}

		// M11 step 3b: shared views. A presented image that is not ready
		// yet keeps the previous one, and its view comes back next period.
		ShareDrawList shares(4);
		BRegion retry;
		_ShareFrame(shares, retry);

		fLastCompose = system_time();
		if (!_Compose(damage, layers, shares)) {
			if (!fActive)
				return;
			// keep the damage for the next try after a restart
			Damage(damage);
		}
		Damage(retry);
	}
}


/*!	Records one frame: the background in the damage, then each window's
	part of the damage, back to front. Waits for the fence, then puts the
	damage on the screen. Returns false when the GPU mode ended.
*/
bool
Compositor::_Compose(const BRegion& damage, const LayerList& layers,
	const ShareDrawList& shares)
{
	CompositorVulkan* vk = fVulkan;
	fFrames++;
	bool compare = fDebug && fFrames % 100 == 1;
	uint32 background = fBackground;

	// A compare frame keeps the windows from drawing until the CPU copy
	// is done, so both read the same pixels. The window lock is never
	// held here.
	if (compare) {
		for (int32 i = 0; i < layers.CountItems(); i++)
			layers.ItemAt(i)->entry->iface->LockExclusiveAccess();
	}

	vk->vkResetCommandBuffer(vk->commands, 0);
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vk->vkBeginCommandBuffer(vk->commands, &begin);
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->pipeline);

	VkMemoryBarrier between = {};
	between.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	between.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	between.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_SHADER_WRITE_BIT;

	VkDescriptorBufferInfo frameInfo = {};
	frameInfo.buffer = vk->scanActive ? vk->back : vk->frame;
	frameInfo.range = VK_WHOLE_SIZE;

	compose_rect rect;
	memset(&rect, 0, sizeof(rect));
	rect.frameStride = vk->scanActive ? vk->backPitch / 4 : fFrameWidth;

	for (int32 layerIndex = -1; layerIndex < layers.CountItems();
			layerIndex++) {
		Layer* layer = layerIndex >= 0 ? layers.ItemAt(layerIndex) : NULL;
		BRegion part(damage);
		if (layer != NULL) {
			if (!layer->buffer.IsSet())
				continue;
			part.IntersectWith(&layer->shape);
			if (part.CountRects() == 0)
				continue;
		}

		VkDescriptorBufferInfo sourceInfo = frameInfo;
		if (layer != NULL)
			sourceInfo.buffer = (VkBuffer)layer->buffer->VulkanBuffer();
		VkWriteDescriptorSet writes[2] = {};
		for (int i = 0; i < 2; i++) {
			writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstBinding = i;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		}
		writes[0].pBufferInfo = &sourceInfo;
		writes[1].pBufferInfo = &frameInfo;
		vk->vkCmdPushDescriptorSetKHR(vk->commands,
			VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipelineLayout, 0, 2, writes);

		rect.fill = layer == NULL ? 1 : 0;
		rect.color = background;
		rect.sourceStride = layer != NULL ? layer->buffer->Width() : 0;
		for (int32 i = 0; i < part.CountRects(); i++) {
			clipping_rect r = part.RectAtInt(i);
			rect.frameX = r.left;
			rect.frameY = r.top;
			rect.width = r.right - r.left + 1;
			rect.height = r.bottom - r.top + 1;
			if (layer != NULL) {
				rect.sourceX = r.left - layer->left;
				rect.sourceY = r.top - layer->top;
			}
			vk->vkCmdPushConstants(vk->commands, vk->pipelineLayout,
				VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), &rect);
			vk->vkCmdDispatch(vk->commands, (rect.width + 15) / 16,
				(rect.height + 15) / 16, 1);
		}
		// the next layer writes over this one
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL,
			0, NULL);

		// M11 step 3b: the window's shared views, over its own pixels and
		// under the next window.
		for (int32 s = 0; layer != NULL && s < shares.CountItems(); s++) {
			ShareDraw* draw = shares.ItemAt(s);
			if (draw->window == NULL || draw->window != layer->entry->window)
				continue;
			BRegion view(part);
			view.IntersectWith(&draw->shape);
			BRegion image(BRect((float)draw->left, (float)draw->top,
				(float)draw->left + draw->width - 1,
				(float)draw->top + draw->height - 1));
			view.IntersectWith(&image);
			if (view.CountRects() == 0)
				continue;
			VkDescriptorBufferInfo shareInfo = frameInfo;
			shareInfo.buffer = draw->buffer;
			writes[0].pBufferInfo = &shareInfo;
			writes[1].pBufferInfo = &frameInfo;
			vk->vkCmdPushDescriptorSetKHR(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipelineLayout, 0, 2,
				writes);
			rect.fill = 0;
			rect.sourceStride = draw->pitch / 4;
			for (int32 i = 0; i < view.CountRects(); i++) {
				clipping_rect r = view.RectAtInt(i);
				rect.frameX = r.left;
				rect.frameY = r.top;
				rect.width = r.right - r.left + 1;
				rect.height = r.bottom - r.top + 1;
				rect.sourceX = r.left - draw->left;
				rect.sourceY = r.top - draw->top;
				vk->vkCmdPushConstants(vk->commands, vk->pipelineLayout,
					VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), &rect);
				vk->vkCmdDispatch(vk->commands, (rect.width + 15) / 16,
					(rect.height + 15) / 16, 1);
			}
			vk->vkCmdPipelineBarrier(vk->commands,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL,
				0, NULL);
		}
	}

	// Decision 4: the damaged rows of the back buffer onto the scanout, in
	// this submit, under this fence. Each pitch is its buffer's own.
	// The race with the software cursor: from here until _Present has drawn
	// the cursor, the GPU writes the back buffer and the front, which the
	// mouse thread reads and writes (MoveCursorTo, under the parallel
	// lock). The exclusive lock keeps it out for that time, usually a
	// millisecond; a late fence holds the cursor that long.
	bool exclusive = false;
	if (vk->scanActive) {
		exclusive = fScreen->LockExclusiveAccess();
		// The software cursor's rectangle is not copied: _DrawCursor puts
		// it on the front, blended from the new back buffer, as
		// CopyBackToFront leaves it out. So it never shows without cursor.
		BRegion copy(damage);
		clipping_rect cursor;
		if (static_cast<AccelerantHWInterface*>(fScreen)
				->SoftwareCursorFrame(&cursor))
			copy.Exclude(cursor);
		VkMemoryBarrier toCopy = {};
		toCopy.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		toCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &toCopy, 0, NULL, 0, NULL);
		uint32 count = 0;
		for (int32 i = 0; i < copy.CountRects(); i++) {
			clipping_rect r = copy.RectAtInt(i);
			count += r.bottom - r.top + 1;
		}
		VkBufferCopy* regions = new(std::nothrow) VkBufferCopy[count + 1];
		uint32 n = 0;
		for (int32 i = 0; regions != NULL && i < copy.CountRects(); i++) {
			clipping_rect r = copy.RectAtInt(i);
			for (int32 y = r.top; y <= r.bottom; y++) {
				regions[n].srcOffset = (uint64)y * vk->backPitch
					+ (uint64)r.left * 4;
				regions[n].dstOffset = (uint64)y * vk->scanPitch
					+ (uint64)r.left * 4;
				regions[n].size = (uint64)(r.right - r.left + 1) * 4;
				n++;
			}
		}
		if (regions != NULL && n > 0)
			vk->vkCmdCopyBuffer(vk->commands, vk->back, vk->scanout, n,
				regions);
		delete[] regions;
		// Without memory for the list the back buffer still holds the
		// frame; _Present then lets CopyBackToFront take it to the front.
		fFrontByVulkan = regions != NULL;
	} else
		fFrontByVulkan = false;

	VkMemoryBarrier toHost = {};
	toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT
		| VK_ACCESS_TRANSFER_WRITE_BIT;
	toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, NULL, 0, NULL);
	vk->vkEndCommandBuffer(vk->commands);

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &vk->commands;
	vk->vkResetFences(vk->device, 1, &vk->fence);
	bigtime_t started = system_time();
	VkResult result = vk->vkQueueSubmit(vk->queue, 1, &submit, vk->fence);
	bool forced = fForceFail;
	if (forced) {
		// gpu_compositor_fail: the first submit counts as failed
		fForceFail = false;
		result = VK_ERROR_UNKNOWN;
	}

	// Decision 14: a late fence is not a failure. Keep the last frame,
	// keep collecting damage, and wait on; the watchdog decides first.
	const char* failure = NULL;
	if (result != VK_SUCCESS)
		failure = forced ? "forced failure" : "submit";
	while (failure == NULL) {
		result = vk->vkWaitForFences(vk->device, 1, &vk->fence, VK_TRUE,
			kFenceSlice * 1000);
		if (result == VK_SUCCESS)
			break;
		if (result != VK_TIMEOUT) {
			failure = "device lost";
			break;
		}
		bigtime_t late = system_time() - started;
		if (late >= kFenceLimit) {
			failure = "fence timeout";
			break;
		}
		if (system_time() - fLateLogged >= 1000000) {
			fLateLogged = system_time();
			log_line("compose late %" B_PRId64 " ms", late / 1000);
		}
	}
	if (failure != NULL) {
		if (exclusive)
			fScreen->UnlockExclusiveAccess();
		if (compare) {
			for (int32 i = 0; i < layers.CountItems(); i++)
				layers.ItemAt(i)->entry->iface->UnlockExclusiveAccess();
		}
		// one restart after a successful reset (RADV only)
		if (!fOnLavapipe && !fRestarted && strcmp(failure, "device lost") == 0
			&& _RestartAfterReset())
			return false;
		_TurnOff(failure);
		return false;
	}

	bigtime_t took = system_time() - started;
	if (fSampleCount < (int32)(sizeof(fSamples) / sizeof(fSamples[0])))
		fSamples[fSampleCount++] = took;

	if (compare) {
		// The CPU cannot read an imported image; those parts are left out.
		BRegion shared;
		for (int32 s = 0; s < shares.CountItems(); s++)
			shared.Include(&shares.ItemAt(s)->shape);
		_Compare(damage, layers, background, shared);
		for (int32 i = 0; i < layers.CountItems(); i++)
			layers.ItemAt(i)->entry->iface->UnlockExclusiveAccess();
	}

	_Present(damage);
	if (exclusive)
		fScreen->UnlockExclusiveAccess();
	return true;
}


/*!	Copies the composed damage into the screen's back buffer and lets the
	screen interface put it on the front: the row copy, or the M8 DMA_DATA
	copy when gpu_app_server is on. The cursor is drawn there, as before.
*/
void
Compositor::_Present(const BRegion& damage)
{
	if (!fScreen->LockParallelAccess())
		return;

	bigtime_t started = system_time();
	RenderingBuffer* back = fScreen->BackBuffer();
	if (fFrontByVulkan && back != NULL) {
		// Decision 4: the back buffer and the front are both this frame
		// already. Invalidate only draws the cursor where the copy went.
		GPUCopy::SetFrontCurrent(true);
		fScreen->InvalidateRegion(damage);
		GPUCopy::SetFrontCurrent(false);
	} else if (fVulkan->scanActive && back != NULL) {
		// The compose wrote the back buffer itself: no CPU copy.
		fScreen->InvalidateRegion(damage);
	} else if (back != NULL && back->Width() == fFrameWidth
		&& back->Height() == fFrameHeight) {
		const uint8* frame = (const uint8*)fVulkan->frameBits;
		uint32 frameRow = fFrameWidth * 4;
		uint8* bits = (uint8*)back->Bits();
		uint32 backRow = back->BytesPerRow();
		for (int32 i = 0; i < damage.CountRects(); i++) {
			clipping_rect r = damage.RectAtInt(i);
			uint32 bytes = (r.right - r.left + 1) * 4;
			for (int32 y = r.top; y <= r.bottom; y++) {
				memcpy(bits + y * backRow + r.left * 4,
					frame + y * frameRow + r.left * 4, bytes);
			}
		}
		fScreen->InvalidateRegion(damage);
	}
	bigtime_t took = system_time() - started;
	if (fPresentCount < (int32)(sizeof(fPresentSamples)
			/ sizeof(fPresentSamples[0])))
		fPresentSamples[fPresentCount++] = took;
	fScreen->UnlockParallelAccess();
}


/*!	gpu_debug: the same frame on the CPU, from the same window buffers,
	compared with what the GPU wrote. Colour channels only.
*/
bool
Compositor::_Compare(const BRegion& damage, const LayerList& layers,
	uint32 background, const BRegion& shared)
{
	if (fScratch == NULL)
		return false;
	uint32 stride = fFrameWidth;
	// Decision 4: the GPU wrote into the back buffer, with its own pitch.
	const uint32* frame = fVulkan->frameBits;
	uint32 frameStride = fFrameWidth;
	RenderingBuffer* back = fScreen->BackBuffer();
	if (fVulkan->scanActive && back != NULL) {
		frame = (const uint32*)back->Bits();
		frameStride = back->BytesPerRow() / 4;
	}
	for (int32 i = 0; i < damage.CountRects(); i++) {
		clipping_rect r = damage.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			for (int32 x = r.left; x <= r.right; x++)
				fScratch[y * stride + x] = background;
		}
	}
	for (int32 l = 0; l < layers.CountItems(); l++) {
		Layer* layer = layers.ItemAt(l);
		if (!layer->buffer.IsSet())
			continue;
		BRegion part(damage);
		part.IntersectWith(&layer->shape);
		const uint32* source = (const uint32*)layer->buffer->Bits();
		uint32 sourceStride = layer->buffer->Width();
		for (int32 i = 0; i < part.CountRects(); i++) {
			clipping_rect r = part.RectAtInt(i);
			for (int32 y = r.top; y <= r.bottom; y++) {
				memcpy(fScratch + y * stride + r.left,
					source + (y - layer->top) * sourceStride
						+ (r.left - layer->left),
					(r.right - r.left + 1) * 4);
			}
		}
	}
	BRegion checked(damage);
	checked.Exclude(&shared);
	for (int32 i = 0; i < checked.CountRects(); i++) {
		clipping_rect r = checked.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			for (int32 x = r.left; x <= r.right; x++) {
				uint32 at = y * stride + x;
				uint32 framed = frame[y * frameStride + x];
				if (((fScratch[at] ^ framed) & 0x00ffffff) != 0) {
					log_line("compose compare MISMATCH at %" B_PRId32 ",%"
						B_PRId32, x, y);
					return false;
				}
			}
		}
	}
	log_line("compose compare ok");
	return true;
}


/*!	Decision 14: after the GPU was reset and passed its self-test, make the
	device once more on RADV. Every window buffer is new, so every window
	draws once. Returns false when the GPU does not come back.
*/
bool
Compositor::_RestartAfterReset()
{
	fRestarted = true;
	bigtime_t until = system_time() + kFenceLimit;
	const char* reason = NULL;
	while (!gpu_available(NULL, &reason)) {
		if (system_time() >= until)
			return false;
		snooze(500000);
	}

	if (!fDesktop->LockAllWindows())
		return false;

	// Every engine points at memory of the lost device. Park each engine
	// on the screen first; Painter keeps that attachment until the window
	// gets a new buffer, so a hidden window never writes freed memory.
	fEntryLock.Lock();
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window != NULL
			&& entry->window->GetDrawingEngine() != NULL)
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
		entry->iface->SetBuffer(NULL, 0, 0);
	}
	fEntryLock.Unlock();
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}

	const char* initReason = NULL;
	RenderingBuffer* back = fScreen->BackBuffer();
	bool ok = back != NULL && _InitVulkan(true, &initReason) == B_OK
		&& _EnsureFrame(back->Width(), back->Height());
	if (ok) {
		// all buffers are new now: every window draws once
		ClippingChanged();
		fEntryLock.Lock();
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry->window != NULL
				&& entry->window->GetDrawingEngine() != NULL)
				entry->window->GetDrawingEngine()->SetHWInterface(entry->iface);
		}
		fEntryLock.Unlock();
		log_line("gpu compositor restarted after reset");
	}
	fDesktop->UnlockAllWindows();
	return ok;
}


/*!	The CPU mode for the rest of the session (decision 11): every window's
	engine goes back to the screen, the desktop redraws, and the window
	buffers are freed. Never back to the GPU mode in this session.
*/
void
Compositor::_TurnOff(const char* reason)
{
	if (!fDesktop->LockAllWindows())
		return;

	fActive = false;
	fEntryLock.Lock();
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window != NULL
			&& entry->window->GetDrawingEngine() != NULL)
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
		entry->iface->SetBuffer(NULL, 0, 0);
	}
	fEntryLock.Unlock();
	{
		// M11 step 3b: the programs get a refusal at their next present
		// and make copy swapchains; the imports go with the device.
		BAutolock _(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++)
			fShares.ItemAt(i)->broken = true;
	}
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}
	log_line("gpu compositor off: %s", reason);

	fDesktop->CompositorTurnedOff();
	fDesktop->UnlockAllWindows();
	// The device goes when the compose thread ends, after its last
	// references to the window buffers are gone.
}


//	#pragma mark - scanout (M11 step 3b, decision 4)


//!	Frees the imports of the back buffer and the scanout. GPU idle.
void
Compositor::_ScanoutFree(CompositorVulkan* vk)
{
	if (vk->back != VK_NULL_HANDLE)
		vk->vkDestroyBuffer(vk->device, vk->back, NULL);
	if (vk->backMemory != VK_NULL_HANDLE)
		vk->vkFreeMemory(vk->device, vk->backMemory, NULL);
	if (vk->scanout != VK_NULL_HANDLE)
		vk->vkDestroyBuffer(vk->device, vk->scanout, NULL);
	if (vk->scanoutMemory != VK_NULL_HANDLE)
		vk->vkFreeMemory(vk->device, vk->scanoutMemory, NULL);
	vk->back = VK_NULL_HANDLE;
	vk->backMemory = VK_NULL_HANDLE;
	vk->scanout = VK_NULL_HANDLE;
	vk->scanoutMemory = VK_NULL_HANDLE;
	vk->scanActive = false;
}


//!	One import of app_server's own export: a VkBuffer over it.
static bool
import_own(CompositorVulkan* vk, const uint8 token[16], uint64 size,
	VkBufferUsageFlags usage, bool deviceLocal, VkBuffer* _buffer,
	VkDeviceMemory* _memory)
{
	VkExternalMemoryBufferCreateInfo external = {};
	external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
	external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = &external;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, _buffer)
			!= VK_SUCCESS) {
		*_buffer = VK_NULL_HANDLE;
		return false;
	}
	VkMemoryRequirements requirements;
	vk->vkGetBufferMemoryRequirements(vk->device, *_buffer, &requirements);
	VkPhysicalDeviceMemoryProperties memory;
	vk->vkGetPhysicalDeviceMemoryProperties(vk->physical, &memory);
	uint32 type = UINT32_MAX;
	for (uint32 i = 0; i < memory.memoryTypeCount; i++) {
		if ((requirements.memoryTypeBits & (1u << i)) == 0)
			continue;
		bool local = (memory.memoryTypes[i].propertyFlags
			& VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
		if (type == UINT32_MAX || (deviceLocal && local)
			|| (!deviceLocal && i == vk->memoryType)) {
			type = i;
			if ((deviceLocal && local) || (!deviceLocal && i == vk->memoryType))
				break;
		}
	}
	int fd = -1;
	uint32 id = 0;
	team_id self = getpid();
	if (type == UINT32_MAX
		|| vk->tokenToFd(-1, token, self, &fd, &id) != 0)
		return false;
	VkImportMemoryFdInfoKHR import = {};
	import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
	import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	import.fd = fd;
	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.pNext = &import;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = type;
	if (vk->vkAllocateMemory(vk->device, &allocateInfo, NULL, _memory)
			!= VK_SUCCESS) {
		*_memory = VK_NULL_HANDLE;
		close(fd);
		return false;
	}
	uint64 bytes = 0;
	if (vk->importSize(id, &bytes) != 0 || requirements.size > bytes
		|| size > bytes)
		return false;
	return vk->vkBindBufferMemory(vk->device, *_buffer, *_memory, 0)
		== VK_SUCCESS;
}


/*!	Before a compose: imports GPUCopy's back buffer and the scanout when
	gpu_compositor_scanout is on and they are new (the first frame, a mode
	change). True when the way changed, so the whole screen is damaged.
	The GPU is idle here: the last compose waited for its fence.
*/
bool
Compositor::_ScanoutPrepare(RenderingBuffer* back)
{
	CompositorVulkan* vk = fVulkan;
	if (!fWantScanout || fOnLavapipe || vk == NULL || !vk->canImport
		|| back == NULL)
		return false;
	uint32 current = GPUCopy::Generation();
	if (vk->scanActive && vk->scanGeneration == current)
		return false;
	if (!vk->scanActive && vk->scanFailedGeneration == current)
		return false;

	bool was = vk->scanActive;
	_ScanoutFree(vk);
	uint8 backToken[16];
	uint8 scanToken[16];
	uint32 scanPitch = 0;
	uint32 generation = 0;
	const char* reason = NULL;
	if (!GPUCopy::ExportForCompositor(back, backToken, scanToken, &scanPitch,
			&generation))
		reason = "back buffer not on the gpu";
	else if (!import_own(vk, backToken,
			(uint64)back->BytesPerRow() * back->Height(),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
				| VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, &vk->back,
			&vk->backMemory)
		|| !import_own(vk, scanToken, (uint64)scanPitch * back->Height(),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &vk->scanout,
			&vk->scanoutMemory))
		reason = "import";
	if (reason != NULL) {
		_ScanoutFree(vk);
		vk->scanFailedGeneration = GPUCopy::Generation();
		if (!fScanoutLogged) {
			fScanoutLogged = true;
			log_line("scanout through vulkan off: %s", reason);
		}
		return was;
	}
	vk->backPitch = back->BytesPerRow();
	vk->scanPitch = scanPitch;
	vk->scanGeneration = generation;
	vk->scanActive = true;
	log_line("scanout through vulkan %" B_PRIu32 "x%" B_PRIu32
		" back pitch %" B_PRIu32 " scanout pitch %" B_PRIu32,
		back->Width(), back->Height(), vk->backPitch, vk->scanPitch);
	return true;
}


//	#pragma mark - shared views (M11 step 3b)


/*!	"app_server: window <id> zero-copy off: <reason>", once per window.
	Called with all windows locked.
*/
void
Compositor::_ShareLog(Window* window, const char* reason)
{
	BAutolock _(fEntryLock);
	Entry* entry = _FindEntry(window);
	if (entry == NULL || entry->shareLogged)
		return;
	entry->shareLogged = true;
	int32 id = window->ServerWindow() != NULL
		? window->ServerWindow()->ClientToken() : -1;
	log_line("window %" B_PRId32 " zero-copy off: %s", id, reason);
}


/*!	The view's part of the screen and the screen position of its pixel
	(0, 0), from its window. Called with all windows and fShareLock locked.
	A view that is gone, or that is in another window now, has no shape.
*/
void
Compositor::_ShareShape(Share* share)
{
	share->shape.MakeEmpty();
	Window* window = share->window;
	if (window == NULL || share->ended || window->ServerWindow() == NULL)
		return;
	View* view = NULL;
	if (window->ServerWindow()->App()->ViewTokens().GetToken(share->viewToken,
			B_HANDLER_TOKEN, (void**)&view) != B_OK
		|| view == NULL || view->Window() != window || !view->IsVisible())
		return;
	BRegion content;
	window->GetContentRegion(&content);
	share->shape = view->ScreenAndUserClipping(&content);
	IntRect bounds = view->Bounds();
	IntPoint origin(bounds.left, bounds.top);
	view->LocalToScreenTransform().Apply(&origin);
	share->left = origin.x;
	share->top = origin.y;
}


/*!	Frees the imports of a share, with the device they were made on. The
	library's GEM_CLOSE ends each import's id. Called with fShareLock.
*/
void
Compositor::_ShareFree(Share* share)
{
	CompositorVulkan* vk = fVulkan;
	for (uint32 i = 0; i < share->count; i++) {
		Share::Image& image = share->images[i];
		if (vk != NULL && share->generation == fGeneration) {
			if (image.buffer != VK_NULL_HANDLE)
				vk->vkDestroyBuffer(vk->device, image.buffer, NULL);
			if (image.memory != VK_NULL_HANDLE)
				vk->vkFreeMemory(vk->device, image.memory, NULL);
		}
		image.buffer = VK_NULL_HANDLE;
		image.memory = VK_NULL_HANDLE;
	}
	share->count = 0;
}


//!	Frees the shares that ended. Compose thread, between frames.
void
Compositor::_ShareRetire()
{
	for (int32 i = fShares.CountItems(); i-- > 0;) {
		Share* share = fShares.ItemAt(i);
		if (!share->ended)
			continue;
		_ShareFree(share);
		fShares.RemoveItemAt(i);
		delete share;
	}
}


/*!	Whether the ring has passed an image's present mark (from
	r5_drm_mark_get). FENCE_WAIT with timeout 0: the compose never waits
	for a program.
*/
bool
Compositor::_ShareReady(uint64 sequence)
{
	// 0: no submit was ever marked on this image (a new buffer, or a mark
	// set before the file's first submit). It is not shown; the WSI marks
	// after the copy into the image, so its mark is never 0.
	if (sequence == 0)
		return false;
	r5_gpu_fence wait;
	memset(&wait, 0, sizeof(wait));
	wait.size = sizeof(wait);
	wait.version = 1;
	wait.sequence = sequence;
	wait.timeout_us = 0;
	return fFenceFd >= 0
		&& ioctl(fFenceFd, R5_GPU_FENCE_WAIT, &wait, sizeof(wait)) == 0;
}


/*!	Gives back each overtaken image whose present mark the ring has
	passed: until then the copy into it may still run (review, M11 step
	3b). Compose thread; sets fShareRetiring while one still waits.
*/
void
Compositor::_ShareRetireDone()
{
	if (!fShareRetiring)
		return;
	BAutolock _(fShareLock);
	bool waiting = false;
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		for (uint32 n = 0; n < share->count; n++) {
			Share::Image& image = share->images[n];
			if (!image.retiring)
				continue;
			uint64 sequence = 0;
			bool gone = share->ended || share->broken || fVulkan == NULL
				|| share->generation != fGeneration
				|| fVulkan->markGet(image.importId, &sequence) != 0;
			if (gone) {
				// nothing reads it any more; an ended share's program
				// has stopped waiting on it
				image.retiring = false;
				continue;
			}
			if (!_ShareReady(sequence)) {
				waiting = true;
				continue;
			}
			image.retiring = false;
			release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
		}
	}
	fShareRetiring = waiting;
}


/*!	The start of a frame: frees what ended, shows each pending image whose
	present mark the ring has passed, gives the image it replaces back to
	the program, and copies what this frame draws. A pending image that is
	not ready yet keeps the previous one; its view goes into retry.
*/
void
Compositor::_ShareFrame(ShareDrawList& draws, BRegion& retry)
{
	BAutolock _(fShareLock);
	_ShareRetire();
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		if (share->broken || share->generation != fGeneration)
			continue;
		if (share->pending >= 0) {
			Share::Image& next = share->images[share->pending];
			uint64 sequence = 0;
			if (fVulkan->markGet(next.importId, &sequence) != 0) {
				share->broken = true;
				continue;
			}
			if (_ShareReady(sequence)) {
				int32 old = share->current;
				share->current = share->pending;
				share->pending = -1;
				// the last frame that read it is done (its fence)
				if (old >= 0)
					release_sem_etc(share->images[old].sem, 1,
						B_DO_NOT_RESCHEDULE);
			} else
				retry.Include(&share->shape);
		}
		if (share->current < 0 || share->shape.CountRects() == 0)
			continue;
		Share::Image& image = share->images[share->current];
		ShareDraw* draw = new(std::nothrow) ShareDraw;
		if (draw == NULL)
			continue;
		draw->window = share->window;
		draw->buffer = image.buffer;
		draw->width = image.width;
		draw->height = image.height;
		draw->pitch = image.pitch;
		draw->shape = share->shape;
		draw->left = share->left;
		draw->top = share->top;
		if (!draws.AddItem(draw))
			delete draw;
	}
}


void
Compositor::ShareDeclined(Window* window)
{
	_ShareLog(window, "lavapipe");
}


/*!	Imports a Vulkan view's swapchain images. app_server believes the
	kernel: each image's real size (BO_IMPORT, through the library) must
	hold its pitch times its height, and its semaphore must belong to the
	connection's team. Any refusal refuses the whole swapchain, and the
	program keeps the copy path.
*/
status_t
Compositor::ShareImages(Window* window, int32 viewToken, team_id team,
	uint32 count, const compositor_share_image* images, int32* _shareId)
{
	if (!fActive)
		return B_NOT_ALLOWED;
	if (fOnLavapipe) {
		_ShareLog(window, "lavapipe");
		return B_NOT_ALLOWED;
	}
	CompositorVulkan* vk = fVulkan;
	if (vk == NULL || !vk->canImport || fFenceFd < 0) {
		_ShareLog(window, "no import");
		return B_NOT_ALLOWED;
	}
	if (count == 0 || count > kMaxShareImages)
		return B_BAD_VALUE;

	for (uint32 i = 0; i < count; i++) {
		sem_info info;
		if (get_sem_info(images[i].sem, &info) != B_OK
			|| !r5_share_sem_ok(info.team, team)) {
			_ShareLog(window, "sem");
			return B_NOT_ALLOWED;
		}
		if (images[i].format != (uint32)B_RGBA32
			&& images[i].format != (uint32)B_RGB32) {
			_ShareLog(window, "format");
			return B_BAD_VALUE;
		}
		if (images[i].pitch % 4 != 0
			|| !r5_share_size_ok(images[i].width, images[i].height,
				images[i].pitch, R5_SHARE_MAX_IMAGE_BYTES)) {
			_ShareLog(window, "size");
			return B_BAD_VALUE;
		}
	}

	Share* share = new(std::nothrow) Share;
	if (share == NULL)
		return B_NO_MEMORY;
	share->window = window;
	share->viewToken = viewToken;
	share->team = team;
	share->count = 0;
	share->generation = fGeneration;
	share->current = -1;
	share->pending = -1;
	share->ended = false;
	share->broken = false;
	share->left = 0;
	share->top = 0;

	const char* reason = NULL;
	for (uint32 i = 0; i < count && reason == NULL; i++) {
		const compositor_share_image& in = images[i];
		Share::Image& image = share->images[i];
		memset(&image, 0, sizeof(image));
		share->count = i + 1;
		image.sem = in.sem;
		image.width = in.width;
		image.height = in.height;
		image.pitch = in.pitch;

		VkExternalMemoryBufferCreateInfo external = {};
		external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
		external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.pNext = &external;
		bufferInfo.size = (uint64)in.pitch * in.height;
		bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vk->vkCreateBuffer(vk->device, &bufferInfo, NULL, &image.buffer)
				!= VK_SUCCESS) {
			image.buffer = VK_NULL_HANDLE;
			reason = "import";
			break;
		}
		VkMemoryRequirements requirements;
		vk->vkGetBufferMemoryRequirements(vk->device, image.buffer,
			&requirements);

		int fd = -1;
		if (vk->tokenToFd(-1, in.token, team, &fd, &image.importId) != 0) {
			reason = "import";
			break;
		}
		// RADV takes the fd on success; on a failure it stays ours.
		VkImportMemoryFdInfoKHR import = {};
		import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
		import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
		import.fd = fd;
		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.pNext = &import;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = vk->memoryType;
		if ((requirements.memoryTypeBits & (1u << vk->memoryType)) == 0
			|| vk->vkAllocateMemory(vk->device, &allocateInfo, NULL,
				&image.memory) != VK_SUCCESS) {
			image.memory = VK_NULL_HANDLE;
			close(fd);
			reason = "import";
			break;
		}
		// The kernel's size, not the program's: pitch times height, and
		// the buffer RADV binds, must fit in it.
		uint64 bytes = 0;
		if (vk->importSize(image.importId, &bytes) != 0
			|| !r5_share_size_ok(in.width, in.height, in.pitch, bytes)
			|| requirements.size > bytes) {
			reason = "size";
			break;
		}
		if (vk->vkBindBufferMemory(vk->device, image.buffer, image.memory, 0)
				!= VK_SUCCESS) {
			reason = "import";
			break;
		}
	}

	if (reason != NULL) {
		// Not in the list yet, so no lock: and _ShareLog takes fEntryLock,
		// which comes before fShareLock (RemoveWindow).
		_ShareFree(share);
		delete share;
		_ShareLog(window, reason);
		return B_NOT_ALLOWED;
	}
	BAutolock _(fShareLock);
	// A view has one swapchain: an older one of the same view ends.
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* old = fShares.ItemAt(i);
		if (old->team == team && old->viewToken == viewToken)
			old->ended = true;
	}
	share->id = ++fShareCounter;
	if (!fShares.AddItem(share)) {
		_ShareFree(share);
		delete share;
		return B_NO_MEMORY;
	}
	_ShareShape(share);
	*_shareId = share->id;
	return B_OK;
}


/*!	A present: the image becomes pending. An image that was pending and
	was never shown goes back to the program at once. The compose shows
	the new one once its mark has passed.
*/
status_t
Compositor::SharePresent(team_id team, int32 viewToken, int32 shareId,
	uint32 index)
{
	BRegion damage;
	{
		BAutolock _(fShareLock);
		Share* share = NULL;
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* scan = fShares.ItemAt(i);
			if (scan->id == shareId && scan->team == team
				&& scan->viewToken == viewToken) {
				share = scan;
				break;
			}
		}
		// B_BAD_VALUE: not the view's swapchain any more (a newer one
		// replaced it); the program's new swapchain still shares.
		// B_NOT_ALLOWED: this view takes no shared images now (a lost
		// import, a reset, the CPU mode); the program takes the copy path.
		if (share == NULL || share->ended || index >= share->count)
			return B_BAD_VALUE;
		if (share->broken || !fActive)
			return B_NOT_ALLOWED;
		if ((int32)index != share->current && (int32)index != share->pending) {
			// The overtaken image is never shown, but the copy into it
			// may still run: it goes back once its mark has passed
			// (_ShareRetireDone), not at once.
			if (share->pending >= 0) {
				share->images[share->pending].retiring = true;
				fShareRetiring = true;
			}
			share->pending = index;
		}
		damage = share->shape;
	}
	Damage(damage);
	if (fShareRetiring)
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}


/*!	The program's swapchain is gone. From the reply on, no semaphore of it
	is released; the imports are freed at the start of the next frame.
*/
void
Compositor::ShareEnd(team_id team, int32 viewToken, int32 shareId)
{
	BRegion damage;
	{
		BAutolock _(fShareLock);
		for (int32 i = 0; i < fShares.CountItems(); i++) {
			Share* share = fShares.ItemAt(i);
			if (share->id == shareId && share->team == team
				&& share->viewToken == viewToken) {
				share->ended = true;
				damage = share->shape;
			}
		}
	}
	Damage(damage);
}


void
Compositor::_Report()
{
	bigtime_t now = system_time();
	if (now - fReportStart < kReportInterval)
		return;

	bigtime_t cpu = thread_cpu_time();
	bigtime_t median = 0;
	if (fSampleCount > 0) {
		// insertion sort: at most 128 samples
		for (int32 i = 1; i < fSampleCount; i++) {
			bigtime_t value = fSamples[i];
			int32 j = i - 1;
			while (j >= 0 && fSamples[j] > value) {
				fSamples[j + 1] = fSamples[j];
				j--;
			}
			fSamples[j + 1] = value;
		}
		median = fSamples[fSampleCount / 2];
	}
	log_line("compose median %" B_PRId64 " us cpu %" B_PRId64
		" ms per 10 s", median, (cpu - fCpuAtReport) / 1000);
	// Decision 4: the time after the fence, per way, for the stick report
	// (vulkan: the cursor only; cpu: the copy into the back buffer plus
	// CopyBackToFront, the M8 DMA_DATA copy when gpu_app_server is on).
	if (fPresentCount > 0) {
		for (int32 i = 1; i < fPresentCount; i++) {
			bigtime_t value = fPresentSamples[i];
			int32 j = i - 1;
			while (j >= 0 && fPresentSamples[j] > value) {
				fPresentSamples[j + 1] = fPresentSamples[j];
				j--;
			}
			fPresentSamples[j + 1] = value;
		}
		log_line("present median %" B_PRId64 " us (%s)",
			fPresentSamples[fPresentCount / 2],
			fVulkan != NULL && fVulkan->scanActive ? "vulkan" : "cpu");
		fPresentCount = 0;
	}
	log_line("window memory %" B_PRIu64 " MB", fWindowBytes / (1024 * 1024));

	fSampleCount = 0;
	fCpuAtReport = cpu;
	fReportStart = now;
}
