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
#include "WindowEffectLevels.h"
#include "decorator/Decorator.h"

#include <WindowPrivate.h>

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

	// M11 step 4 (decision 16): the effects pipeline, and the levels of
	// the Dual Kawase blur.
	VkDescriptorSetLayout		effectsSetLayout;
	VkPipelineLayout			effectsPipelineLayout;
	VkPipeline					effectsPipeline;
	VkBuffer					scratch;
	VkDeviceMemory				scratchMemory;
	uint64						scratchBytes;

	// M11 step 4b: the workspace slide, the old picture and the new one
	VkBuffer					slideOld;
	VkDeviceMemory				slideOldMemory;
	VkBuffer					slideNew;
	VkDeviceMemory				slideNewMemory;
	uint64						slideBytes;
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


// M11 step 4: the push constants of effects.comp.
struct effect_rect {
	uint32	mode;
	uint32	width;
	uint32	height;
	uint32	targetX;
	uint32	targetY;
	uint32	targetStride;
	uint32	sourceX;
	uint32	sourceY;
	uint32	sourceStride;
	uint32	sourceWidth;
	uint32	sourceHeight;
	uint32	auxX;
	uint32	auxY;
	uint32	auxStride;
	uint32	amount;
	float	offset;
	int32	shape[16];		// mode 4: four rectangles, left top right bottom
};

enum {
	kEffectShadow = 0,
	kEffectDown = 1,
	kEffectUp = 2,
	kEffectGlass = 3,
	kEffectWindow = 4,
	kEffectSlide = 5
};

static const uint32 kEffectFixedGlass = 0x100;


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
		shareLogged(false),
		hasGlassViews(false),
		maskWidth(0),
		maskHeight(0),
		maskReach(0),
		maskSmall(false),
		maskStrength(0),
		glassLeft(0),
		glassTop(0),
		glassStrength(0),
		glassBelow(0),
		glassValid(false),
		animOpen(false),
		animStart(0),
		animFrom(0),
		animFrames(0)
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
	// M11 step 4: ContentDamage taken by the compose thread this frame.
	BRegion				takenDamage;

	// M11 step 4. Written with all windows locked: one of its views asked
	// for glass. Everything below is the compose thread's own.
	bool				hasGlassViews;

	// The shadow mask: one byte per pixel, the footprint blurred once
	// when its shape, its focus or the step changes; kept.
	BReference<WindowBuffer> mask;
	int32				maskWidth;
	int32				maskHeight;
	int32				maskReach;
	bool				maskSmall;
	int32				maskStrength;
	BRegion				maskShape;		// footprint, window coordinates

	// The kept blur behind the glass: one full-size pixel per glass pixel,
	// the rectangles of glassShape one after another.
	BReference<WindowBuffer> glass;
	BRegion				glassShape;		// screen coordinates
	int32				glassLeft;
	int32				glassTop;
	int32				glassStrength;
	uint32				glassBelow;		// which windows lay below
	bool				glassValid;

	// M11 step 4b, under fAnimLock: the open animation
	bool				animOpen;
	bigtime_t			animStart;
	float				animFrom;
	int32				animFrames;
	BRegion				animArea;
};


/*!	M11 step 4b: a window that closed, drawn from its last buffer while it
	fades and shrinks. Under fAnimLock.
*/
struct Compositor::Ghost {
	BReference<Entry>			entry;
	BReference<WindowBuffer>	buffer;
	int32						left;
	int32						top;
	BRegion						shape;
	BRegion						area;
	bigtime_t					start;
	float						from;
	float						visible;
	int32						frames;
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
	Layer()
		:
		left(0),
		top(0),
		shadow(false),
		shadowSmall(false),
		belowHash(0),
		animatable(false),
		visible(-1)
	{
	}

	bool HasEffects() const
	{
		return shadow || glassFixed.CountRects() > 0
			|| glassAlpha.CountRects() > 0;
	}

	BReference<Entry>			entry;
	BReference<WindowBuffer>	buffer;
	int32						left;
	int32						top;
	BRegion						shape;

	// M11 step 4, from the clipping rebuild: where the shadow falls
	// (outside the footprint, inside the decorator's extent), and the
	// glass of the tab and frame line (fixed) and of glass views (alpha).
	bool						shadow;
	bool						shadowSmall;
	BRegion						shadowArea;
	BRegion						glassFixed;
	BRegion						glassAlpha;

	// Per frame: what this window changed, what the blur is redone for,
	// and which windows lie below it.
	BRegion						contentDamage;
	BRegion						blurRegion;
	uint32						belowHash;

	// M11 step 4b: may be animated (a decorated window, not a menu, no
	// direct access), and this frame's visibility while it opens (-1:
	// not animating)
	bool						animatable;
	float						visible;
};


//!	M11 step 4b: ease out, cubic.
static float
ease_out(float t)
{
	if (t >= 1.0f)
		return 1.0f;
	if (t <= 0.0f)
		return 0.0f;
	float rest = 1.0f - t;
	return 1.0f - rest * rest * rest;
}


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
	fLateLogged(0),
	fEffects(kWindowEffectShadow | kWindowEffectFrostedGlass
		| kWindowEffectAnimations),
	fStrength(kWindowEffectsBalanced),
	fEffectsLogged(false),
	fAnimLock("compositor animations"),
	fGhosts(4),
	fLastWorkspace(-1),
	fSlideActive(false),
	fSlideSnapshot(false),
	fSlideStart(0),
	fSlideDirection(1),
	fSlideFrames(0),
	fAnimationsRunning(false),
	fFrameGhosts(NULL),
	fFrameAnimating(false),
	fFrameSlide(false),
	fFrameSlideSnapshot(false),
	fFrameSlideShift(0),
	fComposeSlot("compositor slot"),
	fPaused(false),
	fModePending(false),
	fPauseRedraw(false),
	fDirectWindows(4),
	fDirectScreen(false),
	fFailure(NULL)
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
	delete fFrameGhosts;
	fGhosts.MakeEmpty();
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

	// M11 step 4 (decision 16): the second compute pipeline, for the
	// effects. Three storage buffers: a source, the target, and the kept
	// blur for the glass.
	shaderInfo.codeSize = sizeof(kEffectsSpirv);
	shaderInfo.pCode = kEffectsSpirv;
	if (vk->vkCreateShaderModule(vk->device, &shaderInfo, NULL, &shader)
			!= VK_SUCCESS) {
		*_reason = "effects shader";
		return B_ERROR;
	}
	VkDescriptorSetLayoutBinding effectBindings[3] = {};
	for (int i = 0; i < 3; i++) {
		effectBindings[i].binding = i;
		effectBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		effectBindings[i].descriptorCount = 1;
		effectBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	setInfo.bindingCount = 3;
	setInfo.pBindings = effectBindings;
	range.size = sizeof(effect_rect);
	layoutInfo.pSetLayouts = &vk->effectsSetLayout;
	pipelineInfo.stage.module = shader;
	pipelineOk
		= vk->vkCreateDescriptorSetLayout(vk->device, &setInfo, NULL,
			&vk->effectsSetLayout) == VK_SUCCESS
		&& vk->vkCreatePipelineLayout(vk->device, &layoutInfo, NULL,
			&vk->effectsPipelineLayout) == VK_SUCCESS;
	if (pipelineOk) {
		pipelineInfo.layout = vk->effectsPipelineLayout;
		pipelineOk = vk->vkCreateComputePipelines(vk->device, VK_NULL_HANDLE,
			1, &pipelineInfo, NULL, &vk->effectsPipeline) == VK_SUCCESS;
	}
	vk->vkDestroyShaderModule(vk->device, shader, NULL);
	if (!pipelineOk) {
		*_reason = "effects pipeline";
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
		// the programs get a refusal at their next present. Step 5: the
		// images app_server holds go back first.
		_ReturnSharedImages();
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
		if (vk->scratch != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->scratch, NULL);
		if (vk->scratchMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->scratchMemory, NULL);
		if (vk->slideOld != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		if (vk->slideOldMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		if (vk->slideNew != VK_NULL_HANDLE)
			vk->vkDestroyBuffer(vk->device, vk->slideNew, NULL);
		if (vk->slideNewMemory != VK_NULL_HANDLE)
			vk->vkFreeMemory(vk->device, vk->slideNewMemory, NULL);
		if (vk->effectsPipeline != VK_NULL_HANDLE)
			vk->vkDestroyPipeline(vk->device, vk->effectsPipeline, NULL);
		if (vk->effectsPipelineLayout != VK_NULL_HANDLE) {
			vk->vkDestroyPipelineLayout(vk->device, vk->effectsPipelineLayout,
				NULL);
		}
		if (vk->effectsSetLayout != VK_NULL_HANDLE) {
			vk->vkDestroyDescriptorSetLayout(vk->device, vk->effectsSetLayout,
				NULL);
		}
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
	if (!fEntries.AddItem(entry)) {
		entry->ReleaseReference();
		return;
	}
	// M11 step 5: made during a pause; the resume binds it to its buffer
	if (fPaused && window->GetDrawingEngine() != NULL)
		window->GetDrawingEngine()->SetHWInterface(fScreen);
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

	// M11 step 5: it cannot hold direct access any more
	if (fDirectWindows.RemoveItem(window) && fDirectWindows.IsEmpty()) {
		fModePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}

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
	if (!IsActive())
		return;

	LayerList* layers = new(std::nothrow) LayerList(20);
	if (layers == NULL)
		return;

	BObjectList<Window, false> fresh(10);
	BRegion damage;
	int32 workspace = fDesktop->CurrentWorkspace();

	// M11 step 4b: a workspace switch slides; it opens and closes nothing
	bool animations = (fEffects & kWindowEffectAnimations) != 0;
	bool switched = fLastWorkspace >= 0 && workspace != fLastWorkspace;
	if (switched && animations) {
		BAutolock _(fAnimLock);
		fSlideDirection = workspace > fLastWorkspace ? 1 : -1;
		fSlideStart = system_time();
		fSlideSnapshot = true;
		fSlideFrames = 0;
		fSlideActive = true;
	}
	fLastWorkspace = workspace;

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
		bool firstBuffer = buffer == NULL;
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

		// M11 step 4: the shadow and the glass of this window, from its
		// decorator's hooks (decision 10) and the glass views.
		uint32 effects = fEffects;
		int32 strength = fStrength;
		::Decorator* decorator = window->Decorator();
		bool menu = window->Feel() == kMenuWindowFeel;
		if ((effects & kWindowEffectShadow) != 0
			&& (decorator != NULL || menu)) {
			layer->shadowSmall = !window->IsFocus() || menu;
			window_shadow_geometry shadow = window_shadow_for(strength,
				layer->shadowSmall);
			int32 extentLeft;
			int32 extentTop;
			int32 extentRight;
			int32 extentBottom;
			if (decorator != NULL) {
				BRect extent;
				decorator->GetShadowExtent(&extent);
				extentLeft = (int32)extent.left;
				extentTop = (int32)extent.top;
				extentRight = (int32)extent.right;
				extentBottom = (int32)extent.bottom;
			} else {
				window_shadow_extent(strength, &extentLeft, &extentTop,
					&extentRight, &extentBottom);
			}
			clipping_rect area;
			area.left = max_c(bounds.left - shadow.reach,
				bounds.left - extentLeft);
			area.top = max_c(bounds.top - shadow.reach + shadow.offset,
				bounds.top - extentTop);
			area.right = min_c(bounds.right + shadow.reach,
				bounds.right + extentRight);
			area.bottom = min_c(bounds.bottom + shadow.reach + shadow.offset,
				bounds.bottom + extentBottom);
			if (area.left <= area.right && area.top <= area.bottom) {
				layer->shadowArea.Set(area);
				layer->shadowArea.Exclude(&full);
			}
			layer->shadow = layer->shadowArea.CountRects() > 0;
		}
		if ((effects & kWindowEffectFrostedGlass) != 0) {
			if (decorator != NULL) {
				decorator->GetGlassRegion(&layer->glassFixed);
				layer->glassFixed.IntersectWith(&full);
			}
			// A window with a glass view uses the alpha of its content;
			// only glass views are filled with alpha 0 (View::Draw).
			if (entry->hasGlassViews) {
				window->GetContentRegion(&layer->glassAlpha);
				layer->glassAlpha.IntersectWith(&full);
				layer->glassAlpha.Exclude(&layer->glassFixed);
			}
		}

		// M11 step 4b: a window that shows for the first time opens
		layer->animatable = decorator != NULL && !menu
			&& !window->ServerWindow()->IsDirectlyAccessing();
		if (firstBuffer && animations && !switched && layer->animatable) {
			BAutolock _(fAnimLock);
			entry->animOpen = true;
			entry->animStart = system_time();
			entry->animFrom = 0;
			entry->animFrames = 0;
			entry->animArea = layer->shape;
			entry->animArea.Include(&layer->shadowArea);
		} else if (!firstBuffer) {
			// A window that grows or moves while it opens (a layout after
			// its first show): the last frame must cover where it is now.
			BAutolock _(fAnimLock);
			if (entry->animOpen) {
				entry->animArea.Include(&layer->shape);
				entry->animArea.Include(&layer->shadowArea);
			}
		}
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
	// shape or changed its place in the stack. M11 step 4: with its
	// shadow, and told to the compose as that window's own change, so the
	// glass of the windows above it is blurred again there; a window that
	// went is nobody's.
	struct WindowDamage {
		WindowHWInterface*	iface;
		BRegion				region;
	};
	BObjectList<WindowDamage, true> windowDamage(10);
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
			&& before->shape == after->shape
			&& before->shadow == after->shadow
			&& before->shadowSmall == after->shadowSmall
			&& before->shadowArea == after->shadowArea
			&& before->glassFixed == after->glassFixed
			&& before->glassAlpha == after->glassAlpha)
			continue;
		for (int32 side = 0; side < 2; side++) {
			Layer* layer = side == 0 ? before : after;
			if (layer == NULL)
				continue;
			BRegion area(layer->shape);
			area.Include(&layer->shadowArea);
			bool stays = false;
			for (int32 j = 0; !stays && j < layers->CountItems(); j++)
				stays = layers->ItemAt(j)->entry.Get() == layer->entry.Get();
			// M11 step 4b: a window that closed (not one that went to
			// Deskbar or to another workspace) fades and shrinks away
			Window* gone = layer->entry->window;
			if (side == 0 && !stays && animations && !switched
				&& layer->animatable && layer->buffer.IsSet()
				&& (gone == NULL
					|| (gone->IsHidden() && !gone->IsMinimized()))) {
				Ghost* ghost = new(std::nothrow) Ghost;
				if (ghost != NULL) {
					BAutolock _(fAnimLock);
					Entry* entry = layer->entry.Get();
					ghost->entry = layer->entry;
					ghost->buffer = layer->buffer;
					ghost->left = layer->left;
					ghost->top = layer->top;
					ghost->shape = layer->shape;
					ghost->area = area;
					ghost->start = system_time();
					// from where a running open animation is
					ghost->from = 1.0f;
					if (entry->animOpen) {
						ghost->from = entry->animFrom + (1.0f - entry->animFrom)
							* ease_out((float)(ghost->start - entry->animStart)
								/ kOpenAnimation);
						entry->animOpen = false;
					}
					ghost->visible = ghost->from;
					ghost->frames = 0;
					if (!fGhosts.AddItem(ghost))
						delete ghost;
				}
			}
			WindowDamage* item = stays ? new(std::nothrow) WindowDamage
				: NULL;
			if (item != NULL) {
				item->iface = layer->entry->iface;
				item->region = area;
				if (!windowDamage.AddItem(item)) {
					delete item;
					item = NULL;
				}
			}
			if (item == NULL)
				damage.Include(&area);
		}
	}
	fLayers = layers;
	fLayerLock.Unlock();
	// old still holds its entries, so each interface is alive here
	for (int32 i = 0; i < windowDamage.CountItems(); i++) {
		WindowDamage* item = windowDamage.ItemAt(i);
		Damage(item->region, item->iface);
	}
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
	Damage(region, NULL);
}


void
Compositor::Damage(const BRegion& region, WindowHWInterface* source)
{
	if (!IsActive() || region.CountRects() == 0)
		return;
	BAutolock _(fDamageLock);
	fDamage.Include(&region);
	// M11 step 4: who changed it, for the kept blur of the glass
	if (source != NULL)
		source->ContentDamage().Include(&region);
	else
		fGenericDamage.Include(&region);
	if (!fWakePending) {
		fWakePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


//	#pragma mark - window effects (M11 step 4)


void
Compositor::SetInitialEffects(uint32 effects, int32 strength)
{
	fEffects = effects & kWindowEffectsAll;
	fStrength = window_effect_strength_valid(strength);
}


/*!	All windows locked. The layers get their new shadow and glass, the
	whole screen is composed again, and windows with glass views draw their
	background again (alpha 0 only while frosted glass is on).
*/
void
Compositor::SetEffects(uint32 effects, int32 strength)
{
	effects &= kWindowEffectsAll;
	strength = window_effect_strength_valid(strength);
	bool glassChanged = ((fEffects ^ effects) & kWindowEffectFrostedGlass)
		!= 0;
	fEffects = effects;
	fStrength = strength;
	if (!IsActive())
		return;

	ClippingChanged();
	if (glassChanged) {
		BObjectList<Window, false> redraw(10);
		fEntryLock.Lock();
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (entry->hasGlassViews && entry->window != NULL)
				redraw.AddItem(entry->window);
		}
		fEntryLock.Unlock();
		for (int32 i = 0; i < redraw.CountItems(); i++) {
			BRegion content;
			redraw.ItemAt(i)->GetContentRegion(&content);
			redraw.ItemAt(i)->ProcessDirtyRegion(content, content);
		}
	}
	BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
		(float)fFrameHeight - 1));
	Damage(screen);
}


bool
Compositor::GlassActive() const
{
	return IsActive() && (fEffects & kWindowEffectFrostedGlass) != 0;
}


//!	All windows locked: a view of \a window was marked or unmarked as glass.
void
Compositor::GlassViewChanged(Window* window, View* view)
{
	(void)view;
	if (!IsActive() || window == NULL)
		return;
	bool hasGlass = window->TopView() != NULL
		&& window->TopView()->HasGlass();
	fEntryLock.Lock();
	Entry* entry = _FindEntry(window);
	if (entry != NULL)
		entry->hasGlassViews = hasGlass;
	fEntryLock.Unlock();

	if (GlassActive()) {
		BRegion content;
		window->GetContentRegion(&content);
		window->ProcessDirtyRegion(content, content);
	}
	ClippingChanged();
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
		// A compose that failed, outside the compose slot: the switch to
		// the CPU mode and the restart take the window lock (step 5).
		if (fFailure != NULL) {
			const char* failure = fFailure;
			fFailure = NULL;
			// one restart after a successful reset (RADV only)
			if (!fOnLavapipe && !fRestarted
				&& strcmp(failure, "device lost") == 0
				&& _RestartAfterReset()) {
				Damage(BRegion(BRect(0, 0, (float)fFrameWidth - 1,
					(float)fFrameHeight - 1)));
				continue;
			}
			_TurnOff(failure);
			return;
		}

		// An overtaken image goes back once the GPU is done with it; until
		// then the thread looks again every refresh period, also without
		// damage (a hidden window).
		bigtime_t wait = fDebug ? kReportInterval : B_INFINITE_TIMEOUT;
		// M11 step 4b: an animation composes every refresh period
		if (fShareRetiring || fAnimationsRunning) {
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
		// M11 step 5: the pause's redraw, and the resume
		if (fModePending)
			_ApplyMode();
		if (fPaused)
			continue;
		bool animating = _AnimationDamage(system_time());
		if (status == B_TIMED_OUT && !animating)
			continue;

		// One frame, in the compose slot: a pause waits for it to end.
		BAutolock slot(fComposeSlot);
		if (fPaused)
			continue;

		// at most once per refresh period
		if (fPeriod > 0) {
			bigtime_t next = fLastCompose + fPeriod;
			if (system_time() < next)
				snooze_until(next, B_SYSTEM_TIMEBASE);
		}

		BRegion damage;
		BRegion generic;
		{
			// M11 step 4: with it, what each window changed itself
			// (WindowHWInterface::ContentDamage) and what nobody did.
			BAutolock entryLock(fEntryLock);
			BAutolock _(fDamageLock);
			fWakePending = false;
			damage = fDamage;
			fDamage.MakeEmpty();
			generic = fGenericDamage;
			fGenericDamage.MakeEmpty();
			for (int32 i = 0; i < fEntries.CountItems(); i++) {
				Entry* entry = fEntries.ItemAt(i);
				entry->takenDamage = entry->iface->ContentDamage();
				entry->iface->ContentDamage().MakeEmpty();
			}
		}

		// the frame follows the screen size (a mode change)
		RenderingBuffer* back = fScreen->BackBuffer();
		if (back == NULL)
			continue;
		if (back->Width() != fFrameWidth || back->Height() != fFrameHeight) {
			if (!_EnsureFrame(back->Width(), back->Height())) {
				fFailure = "window memory";
				continue;
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
				copy->shadow = source->shadow;
				copy->shadowSmall = source->shadowSmall;
				copy->shadowArea = source->shadowArea;
				copy->glassFixed = source->glassFixed;
				copy->glassAlpha = source->glassAlpha;
				copy->animatable = source->animatable;
				layers.AddItem(copy);
			}
		}
		bool effects = false;
		{
			BAutolock _(fEntryLock);
			for (int32 i = 0; i < layers.CountItems(); i++) {
				Layer* layer = layers.ItemAt(i);
				layer->contentDamage = layer->entry->takenDamage;
				layer->entry->takenDamage.MakeEmpty();
				effects |= layer->HasEffects();
			}
		}
		// M11 step 4b: where each animation is in this frame
		_AnimationFrame(layers, system_time());

		// M11 step 4: the shadow masks and the kept blur, and the damage
		// grown where the blur needs fresh input. Without an effect on
		// screen nothing of this runs, and the compose is step 2's.
		if (effects && !_PrepareEffects(damage, layers, generic))
			effects = false;

		// M11 step 3b: shared views. A presented image that is not ready
		// yet keeps the previous one, and its view comes back next period.
		ShareDrawList shares(4);
		BRegion retry;
		_ShareFrame(shares, retry);

		fLastCompose = system_time();
		if (!_Compose(damage, layers, shares, effects)) {
			// fFailure is set; the top of the loop handles it, after the
			// slot is released
			continue;
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
	const ShareDrawList& shares, bool effects)
{
	CompositorVulkan* vk = fVulkan;
	fFrames++;
	// M11 step 4: the CPU compare knows no effects; it runs only on a
	// frame without them.
	bool compare = fDebug && fFrames % 100 == 1 && !effects
		&& !fFrameAnimating;
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

	// M11 step 4b: the picture shown now is the old side of the slide
	bool slide = fFrameSlide && _EnsureSlideBuffers(rect.frameStride);
	if (slide && fFrameSlideSnapshot) {
		_RecordSlideCopy((uint64)frameInfo.buffer, (uint64)vk->slideOld,
			rect.frameStride);
	}

	for (int32 layerIndex = -1; layerIndex < layers.CountItems();
			layerIndex++) {
		Layer* layer = layerIndex >= 0 ? layers.ItemAt(layerIndex) : NULL;
		BRegion part(damage);
		// M11 step 4: a window with a shadow or glass. Without one, this
		// loop is step 2's copy.
		bool withEffects = effects && layer != NULL && layer->HasEffects()
			&& layer->buffer.IsSet();
		BRegion glassPart;
		if (layer != NULL && layer->visible >= 0 && layer->buffer.IsSet()) {
			// M11 step 4b: opening, scaled and faded, without its effects
			_RecordWindowAnimation(layer->buffer->VulkanBuffer(),
				layer->buffer->Width(), layer->buffer->Height(), layer->left,
				layer->top, layer->shape, layer->visible, damage,
				(uint64)frameInfo.buffer, rect.frameStride);
			vk->vkCmdPipelineBarrier(vk->commands,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL,
				0, NULL);
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
			continue;
		}
		if (layer != NULL) {
			if (!layer->buffer.IsSet())
				continue;
			part.IntersectWith(&layer->shape);
			bool shadowHit = withEffects && layer->shadow
				&& damage.Intersects(layer->shadowArea.Frame());
			if (part.CountRects() == 0 && !shadowHit)
				continue;
		}
		if (withEffects) {
			// The blur reads what lies below, before this window's own
			// shadow darkens it; then the shadow; then the window.
			if (layer->blurRegion.CountRects() > 0)
				_RecordBlur(layer, (uint64)frameInfo.buffer, rect.frameStride);
			if (layer->shadow) {
				_RecordShadow(layer, damage, (uint64)frameInfo.buffer,
					rect.frameStride);
			}
			glassPart = layer->glassFixed;
			glassPart.Include(&layer->glassAlpha);
			glassPart.IntersectWith(&part);
			part.Exclude(&glassPart);
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
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
		if (withEffects && glassPart.CountRects() > 0) {
			// next to the copy above, on other pixels: no barrier between
			_RecordGlass(layer, glassPart, (uint64)frameInfo.buffer,
				rect.frameStride);
			vk->vkCmdBindPipeline(vk->commands,
				VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipeline);
			// a shared view below is drawn over all of the window's part
			part.Include(&glassPart);
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

	// M11 step 4b: windows that closed, over the others, then the slide
	for (int32 i = 0; fFrameGhosts != NULL && i < fFrameGhosts->CountItems();
			i++) {
		Layer* ghost = fFrameGhosts->ItemAt(i);
		_RecordWindowAnimation(ghost->buffer->VulkanBuffer(),
			ghost->buffer->Width(), ghost->buffer->Height(), ghost->left,
			ghost->top, ghost->shape, ghost->visible, damage,
			(uint64)frameInfo.buffer, rect.frameStride);
		vk->vkCmdPipelineBarrier(vk->commands,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, NULL, 0,
			NULL);
	}
	if (slide) {
		_RecordSlideCopy((uint64)frameInfo.buffer, (uint64)vk->slideNew,
			rect.frameStride);
		_RecordSlide((uint64)frameInfo.buffer, rect.frameStride);
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
		// handled by _ComposeLoop() once the compose slot is free: the
		// restart after a reset, or the CPU mode (step 5)
		fFailure = failure;
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

	_DropEffects();
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
	fPaused = false;
	_DropEffects();
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
		_ReturnSharedImages();
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


//	#pragma mark - effects (M11 step 4, decision 16)


//!	\a region with every rectangle grown by \a by on each side.
static BRegion
grow_region(const BRegion& region, int32 by)
{
	BRegion grown;
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		r.left -= by;
		r.top -= by;
		r.right += by;
		r.bottom += by;
		grown.Include(r);
	}
	return grown;
}


//!	One box blur of radius \a radius along rows or columns, zero outside.
static void
box_blur(uint8* bits, int32 width, int32 height, int32 radius, bool rows,
	uint8* line)
{
	int32 count = rows ? height : width;
	int32 length = rows ? width : height;
	int32 step = rows ? 1 : width;
	int32 divisor = 2 * radius + 1;
	for (int32 n = 0; n < count; n++) {
		uint8* start = rows ? bits + n * width : bits + n;
		for (int32 i = 0; i < length; i++)
			line[i] = start[i * step];
		int32 sum = 0;
		for (int32 i = 0; i <= radius && i < length; i++)
			sum += line[i];
		for (int32 i = 0; i < length; i++) {
			start[i * step] = (uint8)(sum / divisor);
			int32 add = i + radius + 1;
			int32 drop = i - radius;
			if (add < length)
				sum += line[add];
			if (drop >= 0)
				sum -= line[drop];
		}
	}
}


/*!	The shadow mask of a layer's window: its footprint, blurred three
	times along the rows and three times along the columns. Made again only
	when the footprint, the focus or the step changes (decision 16).
*/
bool
Compositor::_EnsureShadowMask(Layer* layer)
{
	Entry* entry = layer->entry.Get();
	int32 strength = fStrength;
	BRegion local(layer->shape);
	local.OffsetBy(-layer->left, -layer->top);
	if (entry->mask.IsSet() && entry->mask->Generation() == fGeneration
		&& entry->maskSmall == layer->shadowSmall
		&& entry->maskStrength == strength && entry->maskShape == local)
		return true;

	window_shadow_geometry shadow = window_shadow_for(strength,
		layer->shadowSmall);
	clipping_rect frame = local.FrameInt();
	int32 width = frame.right + 1 + 2 * shadow.reach;
	int32 height = frame.bottom + 1 + 2 * shadow.reach;
	uint64 bytes = (uint64)width * height;
	uint8* line = (uint8*)malloc(max_c(width, height));
	WindowBuffer* buffer = line != NULL
		? _AllocateBuffer((uint32)((bytes + 3) / 4), 1) : NULL;
	if (buffer == NULL) {
		free(line);
		entry->mask.Unset();
		if (!fEffectsLogged) {
			fEffectsLogged = true;
			log_line("shadow skipped: window memory");
		}
		return false;
	}
	uint8* bits = (uint8*)buffer->Bits();
	memset(bits, 0, buffer->Bytes());
	for (int32 i = 0; i < local.CountRects(); i++) {
		clipping_rect r = local.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			memset(bits + (y + shadow.reach) * width + r.left + shadow.reach,
				255, r.right - r.left + 1);
		}
	}
	for (int32 pass = 0; pass < 3; pass++)
		box_blur(bits, width, height, shadow.radius, true, line);
	for (int32 pass = 0; pass < 3; pass++)
		box_blur(bits, width, height, shadow.radius, false, line);
	free(line);

	entry->mask.SetTo(buffer, true);
	entry->maskWidth = width;
	entry->maskHeight = height;
	entry->maskReach = shadow.reach;
	entry->maskSmall = layer->shadowSmall;
	entry->maskStrength = strength;
	entry->maskShape = local;
	return true;
}


//!	A kept blur buffer as large as the glass of the window.
bool
Compositor::_EnsureGlassCache(Layer* layer, const BRegion& glass)
{
	Entry* entry = layer->entry.Get();
	uint64 pixels = 0;
	for (int32 i = 0; i < glass.CountRects(); i++) {
		clipping_rect r = glass.RectAtInt(i);
		pixels += (uint64)(r.right - r.left + 1) * (r.bottom - r.top + 1);
	}
	if (pixels == 0 || pixels > UINT32_MAX)
		return false;
	if (!entry->glass.IsSet() || entry->glass->Generation() != fGeneration
		|| entry->glass->Width() < pixels) {
		entry->glass.Unset();
		WindowBuffer* buffer = _AllocateBuffer((uint32)pixels, 1);
		if (buffer == NULL) {
			entry->glassValid = false;
			if (!fEffectsLogged) {
				fEffectsLogged = true;
				log_line("frosted skipped: window memory");
			}
			return false;
		}
		entry->glass.SetTo(buffer, true);
	}
	entry->glassShape = glass;
	entry->glassLeft = layer->left;
	entry->glassTop = layer->top;
	entry->glassStrength = fStrength;
	entry->glassBelow = layer->belowHash;
	entry->glassValid = true;
	return true;
}


//!	The levels of the Dual Kawase blur, for a frame of the current size.
bool
Compositor::_EnsureScratch()
{
	CompositorVulkan* vk = fVulkan;
	uint64 bytes = 0;
	for (int32 k = 1; k <= 4; k++) {
		bytes += 2 * (uint64)((fFrameWidth >> k) + 2)
			* ((fFrameHeight >> k) + 2) * 4;
	}
	if (vk->scratch != VK_NULL_HANDLE && vk->scratchBytes >= bytes)
		return true;
	if (vk->scratch != VK_NULL_HANDLE) {
		// between two composes the GPU is idle
		vk->vkDestroyBuffer(vk->device, vk->scratch, NULL);
		vk->vkFreeMemory(vk->device, vk->scratchMemory, NULL);
		vk->scratch = VK_NULL_HANDLE;
		vk->scratchMemory = VK_NULL_HANDLE;
		vk->scratchBytes = 0;
	}
	void* bits;
	if (!allocate_host_buffer(vk, bytes, &vk->scratch, &vk->scratchMemory,
			&bits))
		return false;
	vk->scratchBytes = bytes;
	return true;
}


/*!	Before the compose: the shadow masks, the kept blur of each glass
	window, and the damage grown so the blur gets fresh input. The blur of
	a window is made again only where something below it changed (grown by
	the blur's reach), or all of it when the window moved, changed its
	glass or the windows below it, or the step changed.
*/
bool
Compositor::_PrepareEffects(BRegion& damage, const LayerList& layers,
	const BRegion& generic)
{
	BRegion screen(BRect(0, 0, (float)fFrameWidth - 1,
		(float)fFrameHeight - 1));
	int32 strength = fStrength;
	window_glass_geometry glassGeometry = window_glass_for(strength);
	bool scratch = _EnsureScratch();

	BRegion below(generic);
	uint32 belowHash = 0;
	BRegion grown;
	for (int32 i = 0; i < layers.CountItems(); i++) {
		Layer* layer = layers.ItemAt(i);
		Entry* entry = layer->entry.Get();
		layer->belowHash = belowHash;
		layer->blurRegion.MakeEmpty();

		if (layer->shadow && !_EnsureShadowMask(layer))
			layer->shadow = false;

		BRegion glass(layer->glassFixed);
		glass.Include(&layer->glassAlpha);
		glass.IntersectWith(&screen);
		if (glass.CountRects() > 0 && layer->buffer.IsSet() && scratch) {
			bool valid = entry->glassValid && entry->glass.IsSet()
				&& entry->glass->Generation() == fGeneration
				&& entry->glassShape == glass
				&& entry->glassLeft == layer->left
				&& entry->glassTop == layer->top
				&& entry->glassStrength == strength
				&& entry->glassBelow == belowHash;
			if (!valid) {
				if (_EnsureGlassCache(layer, glass))
					layer->blurRegion = glass;
				else {
					layer->glassFixed.MakeEmpty();
					layer->glassAlpha.MakeEmpty();
				}
			} else {
				layer->blurRegion = grow_region(below, glassGeometry.reach);
				layer->blurRegion.IntersectWith(&glass);
			}
			// The blur is made per glass rectangle, for the box around
			// what is new in it; its input must be composed fresh.
			for (int32 g = 0; g < glass.CountRects()
					&& layer->blurRegion.CountRects() > 0; g++) {
				BRegion part(layer->blurRegion);
				BRegion rectangle;
				rectangle.Set(glass.RectAtInt(g));
				part.IntersectWith(&rectangle);
				if (part.CountRects() == 0)
					continue;
				BRegion box(part.Frame());
				BRegion input = grow_region(box, glassGeometry.reach);
				grown.Include(&input);
			}
		} else if (glass.CountRects() > 0) {
			layer->glassFixed.MakeEmpty();
			layer->glassAlpha.MakeEmpty();
		}

		below.Include(&layer->contentDamage);
		belowHash = belowHash * 31 + (uint32)((addr_t)entry >> 4);
	}
	damage.Include(&grown);
	damage.IntersectWith(&screen);
	return true;
}


//!	Pushes the three buffers of effects.comp.
static void
push_effect_buffers(CompositorVulkan* vk, VkBuffer source, VkBuffer target,
	VkBuffer aux)
{
	VkDescriptorBufferInfo infos[3] = {};
	infos[0].buffer = source;
	infos[1].buffer = target;
	infos[2].buffer = aux;
	VkWriteDescriptorSet writes[3] = {};
	for (int i = 0; i < 3; i++) {
		infos[i].range = VK_WHOLE_SIZE;
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &infos[i];
	}
	vk->vkCmdPushDescriptorSetKHR(vk->commands,
		VK_PIPELINE_BIND_POINT_COMPUTE, vk->effectsPipelineLayout, 0, 3,
		writes);
}


static void
dispatch_effect(CompositorVulkan* vk, const effect_rect& effect)
{
	vk->vkCmdPushConstants(vk->commands, vk->effectsPipelineLayout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(effect), &effect);
	vk->vkCmdDispatch(vk->commands, (effect.width + 15) / 16,
		(effect.height + 15) / 16, 1);
}


static void
effect_barrier(CompositorVulkan* vk)
{
	VkMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
		| VK_ACCESS_SHADER_WRITE_BIT;
	vk->vkCmdPipelineBarrier(vk->commands,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0,
		NULL);
}


/*!	Dual Kawase over what is composed below the window (the frame now),
	into the window's kept blur: per glass rectangle, the box around what is
	new in it, down the step's levels and up again (a quarter of the
	resolution with two levels).
*/
void
Compositor::_RecordBlur(Layer* layer, uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	Entry* entry = layer->entry.Get();
	window_glass_geometry geometry = window_glass_for(entry->glassStrength);
	VkBuffer frameBuffer = (VkBuffer)frame;
	VkBuffer cache = (VkBuffer)entry->glass->VulkanBuffer();
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);

	uint32 cacheOffset = 0;
	const BRegion& glass = entry->glassShape;
	for (int32 g = 0; g < glass.CountRects(); g++) {
		clipping_rect area = glass.RectAtInt(g);
		uint32 areaWidth = area.right - area.left + 1;
		uint32 areaPixels = areaWidth * (area.bottom - area.top + 1);
		BRegion part(layer->blurRegion);
		BRegion rectangle;
		rectangle.Set(area);
		part.IntersectWith(&rectangle);
		if (part.CountRects() == 0) {
			cacheOffset += areaPixels;
			continue;
		}
		clipping_rect box = part.FrameInt();
		clipping_rect input;
		input.left = max_c(box.left - geometry.reach, 0);
		input.top = max_c(box.top - geometry.reach, 0);
		input.right = min_c(box.right + geometry.reach,
			(int32)fFrameWidth - 1);
		input.bottom = min_c(box.bottom + geometry.reach,
			(int32)fFrameHeight - 1);

		uint32 width[5];
		uint32 height[5];
		uint32 down[5];
		uint32 up[5];
		width[0] = input.right - input.left + 1;
		height[0] = input.bottom - input.top + 1;
		uint32 next = 0;
		for (int32 k = 1; k <= geometry.levels; k++) {
			width[k] = (width[k - 1] + 1) / 2;
			height[k] = (height[k - 1] + 1) / 2;
			down[k] = next;
			next += width[k] * height[k];
			up[k] = next;
			next += width[k] * height[k];
		}

		effect_rect effect;
		memset(&effect, 0, sizeof(effect));
		effect.offset = geometry.offset;
		for (int32 k = 1; k <= geometry.levels; k++) {
			effect.mode = kEffectDown;
			effect.width = width[k];
			effect.height = height[k];
			effect.targetX = down[k];
			effect.targetY = 0;
			effect.targetStride = width[k];
			if (k == 1) {
				effect.sourceX = input.left;
				effect.sourceY = input.top;
				effect.sourceStride = frameStride;
				push_effect_buffers(vk, frameBuffer, vk->scratch, frameBuffer);
			} else {
				effect.sourceX = down[k - 1];
				effect.sourceY = 0;
				effect.sourceStride = width[k - 1];
				push_effect_buffers(vk, vk->scratch, vk->scratch, frameBuffer);
			}
			effect.sourceWidth = width[k - 1];
			effect.sourceHeight = height[k - 1];
			effect.auxX = 0;
			effect.auxY = 0;
			dispatch_effect(vk, effect);
			effect_barrier(vk);
		}
		for (int32 k = geometry.levels; k >= 1; k--) {
			effect.mode = kEffectUp;
			effect.sourceX = k == geometry.levels ? down[k] : up[k];
			effect.sourceY = 0;
			effect.sourceStride = width[k];
			effect.sourceWidth = width[k];
			effect.sourceHeight = height[k];
			if (k > 1) {
				effect.width = width[k - 1];
				effect.height = height[k - 1];
				effect.targetX = up[k - 1];
				effect.targetY = 0;
				effect.targetStride = width[k - 1];
				effect.auxX = 0;
				effect.auxY = 0;
				push_effect_buffers(vk, vk->scratch, vk->scratch, frameBuffer);
			} else {
				// the last level writes only the box, into the kept blur
				effect.width = box.right - box.left + 1;
				effect.height = box.bottom - box.top + 1;
				effect.targetX = cacheOffset + (box.left - area.left);
				effect.targetY = box.top - area.top;
				effect.targetStride = areaWidth;
				effect.auxX = box.left - input.left;
				effect.auxY = box.top - input.top;
				push_effect_buffers(vk, vk->scratch, cache, frameBuffer);
			}
			dispatch_effect(vk, effect);
			effect_barrier(vk);
		}
		cacheOffset += areaPixels;
	}
}


//!	The shadow under the window, where the damage touches it.
void
Compositor::_RecordShadow(Layer* layer, const BRegion& damage, uint64 frame,
	uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	Entry* entry = layer->entry.Get();
	BRegion part(damage);
	part.IntersectWith(&layer->shadowArea);
	if (part.CountRects() == 0 || !entry->mask.IsSet())
		return;
	window_shadow_geometry shadow = window_shadow_for(entry->maskStrength,
		entry->maskSmall);
	int32 maskLeft = layer->left - shadow.reach;
	int32 maskTop = layer->top - shadow.reach + shadow.offset;

	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, (VkBuffer)entry->mask->VulkanBuffer(),
		(VkBuffer)frame, (VkBuffer)frame);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectShadow;
	effect.targetStride = frameStride;
	effect.sourceStride = entry->maskWidth;
	effect.amount = shadow.strength;
	for (int32 i = 0; i < part.CountRects(); i++) {
		clipping_rect r = part.RectAtInt(i);
		// the mask covers the whole shadow area by construction
		if (r.left < maskLeft || r.top < maskTop
			|| r.right >= maskLeft + entry->maskWidth
			|| r.bottom >= maskTop + entry->maskHeight)
			continue;
		effect.width = r.right - r.left + 1;
		effect.height = r.bottom - r.top + 1;
		effect.targetX = r.left;
		effect.targetY = r.top;
		effect.sourceX = r.left - maskLeft;
		effect.sourceY = r.top - maskTop;
		dispatch_effect(vk, effect);
	}
	effect_barrier(vk);
}


//!	The window's glass pixels over its kept blur, where the damage is.
void
Compositor::_RecordGlass(Layer* layer, const BRegion& damage, uint64 frame,
	uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	Entry* entry = layer->entry.Get();
	if (!entry->glass.IsSet())
		return;
	window_glass_geometry geometry = window_glass_for(entry->glassStrength);
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, (VkBuffer)layer->buffer->VulkanBuffer(),
		(VkBuffer)frame, (VkBuffer)entry->glass->VulkanBuffer());
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectGlass;
	effect.targetStride = frameStride;
	effect.sourceStride = layer->buffer->Width();

	uint32 cacheOffset = 0;
	const BRegion& glass = entry->glassShape;
	for (int32 g = 0; g < glass.CountRects(); g++) {
		clipping_rect area = glass.RectAtInt(g);
		uint32 areaWidth = area.right - area.left + 1;
		for (int32 kind = 0; kind < 2; kind++) {
			BRegion part(damage);
			BRegion rectangle;
			rectangle.Set(area);
			part.IntersectWith(&rectangle);
			if (kind == 0)
				part.IntersectWith(&layer->glassFixed);
			else
				part.IntersectWith(&layer->glassAlpha);
			effect.amount = geometry.seeThrough
				| (kind == 0 ? kEffectFixedGlass : 0);
			effect.auxStride = areaWidth;
			for (int32 i = 0; i < part.CountRects(); i++) {
				clipping_rect r = part.RectAtInt(i);
				effect.width = r.right - r.left + 1;
				effect.height = r.bottom - r.top + 1;
				effect.targetX = r.left;
				effect.targetY = r.top;
				effect.sourceX = r.left - layer->left;
				effect.sourceY = r.top - layer->top;
				effect.auxX = cacheOffset + (r.left - area.left);
				effect.auxY = r.top - area.top;
				dispatch_effect(vk, effect);
			}
		}
		cacheOffset += areaWidth * (area.bottom - area.top + 1);
	}
}


//!	After a restart or the switch to the CPU mode: no mask, no kept blur.
void
Compositor::_DropEffects()
{
	BAutolock _(fEntryLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		entry->mask.Unset();
		entry->glass.Unset();
		entry->glassValid = false;
	}
	BAutolock animLock(fAnimLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++)
		fEntries.ItemAt(i)->animOpen = false;
	fGhosts.MakeEmpty();
	fSlideActive = false;
	fAnimationsRunning = false;
	delete fFrameGhosts;
	fFrameGhosts = NULL;
}


//	#pragma mark - animations (M11 step 4b)


/*!	Before a frame: damages where an animation draws, ends the ones that
	are over (one line each, with its length and frames), and says whether
	one still runs. An animation never holds input: the windows draw and
	get their events as always; only the compose is drawn differently.
*/
bool
Compositor::_AnimationDamage(bigtime_t now)
{
	BRegion area;
	bool running = false;
	{
		BAutolock entryLock(fEntryLock);
		BAutolock _(fAnimLock);
		for (int32 i = 0; i < fEntries.CountItems(); i++) {
			Entry* entry = fEntries.ItemAt(i);
			if (!entry->animOpen)
				continue;
			area.Include(&entry->animArea);
			bigtime_t took = now - entry->animStart;
			if (took < kOpenAnimation) {
				running = true;
				continue;
			}
			entry->animOpen = false;
			log_line("animation open %" B_PRId64 " ms frames %" B_PRId32,
				took / 1000, entry->animFrames);
		}
		for (int32 i = fGhosts.CountItems(); i-- > 0;) {
			Ghost* ghost = fGhosts.ItemAt(i);
			area.Include(&ghost->area);
			bigtime_t took = now - ghost->start;
			if (took < kCloseAnimation) {
				running = true;
				continue;
			}
			log_line("animation close %" B_PRId64 " ms frames %" B_PRId32,
				took / 1000, ghost->frames);
			delete fGhosts.RemoveItemAt(i);
		}
		if (fSlideActive) {
			area.Include(BRect(0, 0, (float)fFrameWidth - 1,
				(float)fFrameHeight - 1));
			bigtime_t took = now - fSlideStart;
			if (took < kWorkspaceAnimation)
				running = true;
			else {
				fSlideActive = false;
				log_line("animation workspace %" B_PRId64 " ms frames %"
					B_PRId32, took / 1000, fSlideFrames);
			}
		}
	}
	fAnimationsRunning = running;
	Damage(area);
	return running;
}


//!	This frame's visibility of each opening window, the ghosts, the slide.
void
Compositor::_AnimationFrame(LayerList& layers, bigtime_t now)
{
	LayerList* ghosts = new(std::nothrow) LayerList(4);
	fFrameAnimating = false;
	fFrameSlide = false;
	fFrameSlideSnapshot = false;
	{
		BAutolock _(fAnimLock);
		for (int32 i = 0; i < layers.CountItems(); i++) {
			Layer* layer = layers.ItemAt(i);
			Entry* entry = layer->entry.Get();
			layer->visible = -1;
			if (!entry->animOpen)
				continue;
			float t = (float)(now - entry->animStart) / kOpenAnimation;
			layer->visible = entry->animFrom
				+ (1.0f - entry->animFrom) * ease_out(t);
			entry->animFrames++;
			fFrameAnimating = true;
		}
		for (int32 i = 0; ghosts != NULL && i < fGhosts.CountItems(); i++) {
			Ghost* ghost = fGhosts.ItemAt(i);
			float t = (float)(now - ghost->start) / kCloseAnimation;
			ghost->visible = ghost->from * (1.0f - ease_out(t));
			ghost->frames++;
			Layer* layer = new(std::nothrow) Layer;
			if (layer == NULL)
				continue;
			layer->entry = ghost->entry;
			layer->buffer = ghost->buffer;
			layer->left = ghost->left;
			layer->top = ghost->top;
			layer->shape = ghost->shape;
			layer->visible = ghost->visible;
			ghosts->AddItem(layer);
			fFrameAnimating = true;
		}
		if (fSlideActive) {
			float t = (float)(now - fSlideStart) / kWorkspaceAnimation;
			fFrameSlide = true;
			fFrameSlideSnapshot = fSlideSnapshot;
			fSlideSnapshot = false;
			fFrameSlideShift = (int32)(ease_out(t) * fFrameWidth);
			if (fFrameSlideShift >= (int32)fFrameWidth)
				fFrameSlideShift = fFrameWidth;
			fSlideFrames++;
			fFrameAnimating = true;
		}
	}
	delete fFrameGhosts;
	fFrameGhosts = ghosts;
}


/*!	A window scaled about its centre (95 % to 100 %) and faded (\a visible
	from 0 to 1), where the damage is. Only its footprint is drawn.
*/
void
Compositor::_RecordWindowAnimation(uint64 buffer, uint32 width,
	uint32 height, int32 left, int32 top, const BRegion& shape, float visible,
	const BRegion& damage, uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	if (visible <= 0.0f)
		return;
	BRegion part(damage);
	BRegion bounds(BRect((float)left, (float)top,
		(float)left + width - 1, (float)top + height - 1));
	part.IntersectWith(&bounds);
	if (part.CountRects() == 0)
		return;

	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, (VkBuffer)buffer, (VkBuffer)frame,
		(VkBuffer)frame);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectWindow;
	effect.targetStride = frameStride;
	effect.sourceStride = width;
	effect.sourceWidth = width;
	effect.sourceHeight = height;
	effect.offset = kOpenAnimationScale
		+ (1.0f - kOpenAnimationScale) * visible;
	effect.amount = (uint32)(visible * 255.0f + 0.5f);
	if (effect.amount > 255)
		effect.amount = 255;
	// The footprint in buffer coordinates; more than four rectangles (a
	// stack of tabs) are drawn as their bounding box.
	for (int32 i = 0; i < 4; i++) {
		effect.shape[i * 4] = 1;
		effect.shape[i * 4 + 2] = 0;
	}
	int32 count = shape.CountRects();
	for (int32 i = 0; i < count && i < 4; i++) {
		clipping_rect r = count > 4 ? shape.FrameInt() : shape.RectAtInt(i);
		effect.shape[i * 4] = r.left - left;
		effect.shape[i * 4 + 1] = r.top - top;
		effect.shape[i * 4 + 2] = r.right - left;
		effect.shape[i * 4 + 3] = r.bottom - top;
		if (count > 4)
			break;
	}
	for (int32 i = 0; i < part.CountRects(); i++) {
		clipping_rect r = part.RectAtInt(i);
		effect.width = r.right - r.left + 1;
		effect.height = r.bottom - r.top + 1;
		effect.targetX = r.left;
		effect.targetY = r.top;
		effect.auxX = r.left - left;
		effect.auxY = r.top - top;
		dispatch_effect(vk, effect);
	}
}


//!	The old and the new picture of the slide, each as large as the frame.
bool
Compositor::_EnsureSlideBuffers(uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	uint64 bytes = (uint64)frameStride * fFrameHeight * 4;
	if (vk->slideOld != VK_NULL_HANDLE && vk->slideBytes == bytes)
		return true;
	if (vk->slideOld != VK_NULL_HANDLE) {
		// between two composes the GPU is idle
		vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		vk->vkDestroyBuffer(vk->device, vk->slideNew, NULL);
		vk->vkFreeMemory(vk->device, vk->slideNewMemory, NULL);
		vk->slideOld = VK_NULL_HANDLE;
		vk->slideNew = VK_NULL_HANDLE;
		vk->slideBytes = 0;
	}
	void* bits;
	if (!allocate_host_buffer(vk, bytes, &vk->slideOld, &vk->slideOldMemory,
			&bits))
		return false;
	if (!allocate_host_buffer(vk, bytes, &vk->slideNew, &vk->slideNewMemory,
			&bits)) {
		vk->vkDestroyBuffer(vk->device, vk->slideOld, NULL);
		vk->vkFreeMemory(vk->device, vk->slideOldMemory, NULL);
		vk->slideOld = VK_NULL_HANDLE;
		return false;
	}
	vk->slideBytes = bytes;
	return true;
}


//!	The whole frame into a slide buffer, with the copy shader.
void
Compositor::_RecordSlideCopy(uint64 from, uint64 to, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->pipeline);
	VkDescriptorBufferInfo infos[2] = {};
	infos[0].buffer = (VkBuffer)from;
	infos[0].range = VK_WHOLE_SIZE;
	infos[1].buffer = (VkBuffer)to;
	infos[1].range = VK_WHOLE_SIZE;
	VkWriteDescriptorSet writes[2] = {};
	for (int i = 0; i < 2; i++) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &infos[i];
	}
	vk->vkCmdPushDescriptorSetKHR(vk->commands,
		VK_PIPELINE_BIND_POINT_COMPUTE, vk->pipelineLayout, 0, 2, writes);
	compose_rect rect;
	memset(&rect, 0, sizeof(rect));
	rect.sourceStride = frameStride;
	rect.frameStride = frameStride;
	rect.width = fFrameWidth;
	rect.height = fFrameHeight;
	vk->vkCmdPushConstants(vk->commands, vk->pipelineLayout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rect), &rect);
	vk->vkCmdDispatch(vk->commands, (rect.width + 15) / 16,
		(rect.height + 15) / 16, 1);
	effect_barrier(vk);
}


//!	The frame from the old picture and the new one, side by side.
void
Compositor::_RecordSlide(uint64 frame, uint32 frameStride)
{
	CompositorVulkan* vk = fVulkan;
	vk->vkCmdBindPipeline(vk->commands, VK_PIPELINE_BIND_POINT_COMPUTE,
		vk->effectsPipeline);
	push_effect_buffers(vk, vk->slideOld, (VkBuffer)frame, vk->slideNew);
	effect_rect effect;
	memset(&effect, 0, sizeof(effect));
	effect.mode = kEffectSlide;
	effect.width = fFrameWidth;
	effect.height = fFrameHeight;
	effect.targetStride = frameStride;
	effect.amount = fFrameSlideShift;
	effect.offset = fSlideDirection > 0 ? 1.0f : -1.0f;
	dispatch_effect(vk, effect);
	effect_barrier(vk);
}


//	#pragma mark - direct access (M11 step 5, decision 9)


/*!	A program is about to get B_DIRECT_START (a BDirectWindow) or has the
	screen (BWindowScreen). Called with fWindowLock write-locked, before the
	program learns the front buffer: waits for the compose in flight (a late
	fence is not a failure, decision 14), then every window draws to the
	screen again. The redraw the CPU-mode way follows from the compose
	thread once this write lock is released (_ApplyMode), because this can
	be called from inside a clipping rebuild.
*/
void
Compositor::DirectAccessStarting()
{
	if (!fActive || fPaused)
		return;
	{
		BAutolock slot(fComposeSlot);
		fPaused = true;
	}
	_RebindEngines(false);
	{
		BAutolock _(fLayerLock);
		fLayers->MakeEmpty();
	}
	_DropAnimations();
	log_line("compositor paused: direct window");

	fPauseRedraw = true;
	fModePending = true;
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	fWindowLock write-locked. When the last direct access stops, the resume
	is left to the compose thread: it takes the window lock after the one
	held now, so a direct window that is moved (stop, then start, under one
	write lock) never resumes and pauses the compositor.
*/
void
Compositor::DirectAccessChanged(Window* window, bool accessing)
{
	BAutolock _(fEntryLock);
	if (accessing) {
		if (!fDirectWindows.HasItem(window))
			fDirectWindows.AddItem(window);
		return;
	}
	if (fDirectWindows.RemoveItem(window) && fDirectWindows.IsEmpty()
		&& fPaused) {
		fModePending = true;
		release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
	}
}


//!	BWindowScreen: Desktop::LockDirectScreen() and UnlockDirectScreen().
void
Compositor::DirectScreenChanged(bool locked)
{
	if (!fActive)
		return;
	if (locked) {
		if (!fDesktop->LockAllWindows())
			return;
		// set before the pause, so a resume the pause schedules sees it
		fDirectScreen = true;
		DirectAccessStarting();
		fDesktop->UnlockAllWindows();
		return;
	}
	fDirectScreen = false;
	fModePending = true;
	release_sem_etc(fWakeSem, 1, B_DO_NOT_RESCHEDULE);
}


/*!	The compose thread, outside the compose slot: after a pause, the desktop
	is redrawn the CPU-mode way; once no program has direct access, the
	windows go back to their buffers, every window draws once, and the
	compose starts again.
*/
void
Compositor::_ApplyMode()
{
	if (!fDesktop->LockAllWindows())
		return;
	fModePending = false;
	fEntryLock.Lock();
	bool direct = !fDirectWindows.IsEmpty() || fDirectScreen;
	fEntryLock.Unlock();
	if (fActive && fPaused && !direct) {
		_RebindEngines(true);
		fPaused = false;
		fPauseRedraw = false;
		// the clipping is rebuilt with the GPU mode's regions, the buffers
		// are checked, and every window draws all of itself once
		fDesktop->CompositorTurnedOff();
		Damage(BRegion(BRect(0, 0, (float)fFrameWidth - 1,
			(float)fFrameHeight - 1)));
		log_line("compositor resumed");
	} else if (fPaused && fPauseRedraw) {
		fPauseRedraw = false;
		fDesktop->CompositorTurnedOff();
	}
	fDesktop->UnlockAllWindows();
}


//!	All windows locked: every window draws into its buffer, or to the screen.
void
Compositor::_RebindEngines(bool toBuffers)
{
	BAutolock _(fEntryLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++) {
		Entry* entry = fEntries.ItemAt(i);
		if (entry->window == NULL || entry->window->GetDrawingEngine() == NULL)
			continue;
		if (toBuffers)
			entry->window->GetDrawingEngine()->SetHWInterface(entry->iface);
		else
			entry->window->GetDrawingEngine()->SetHWInterface(fScreen);
	}
}


//!	No animation while paused (step 5) or after the switch to the CPU mode.
void
Compositor::_DropAnimations()
{
	BAutolock entryLock(fEntryLock);
	BAutolock _(fAnimLock);
	for (int32 i = 0; i < fEntries.CountItems(); i++)
		fEntries.ItemAt(i)->animOpen = false;
	fGhosts.MakeEmpty();
	fSlideActive = false;
	fAnimationsRunning = false;
}


/*!	The CPU mode, or the device goes: every shared image that app_server
	holds goes back to its program, so none waits in its acquire for an
	image that will never be shown. Its next present is refused and it makes
	a copy swapchain (docs/APP-SERVER-COMPOSITOR.md, "The reset path").
*/
void
Compositor::_ReturnSharedImages()
{
	BAutolock _(fShareLock);
	for (int32 i = 0; i < fShares.CountItems(); i++) {
		Share* share = fShares.ItemAt(i);
		share->broken = true;
		if (share->ended)
			continue;
		for (uint32 n = 0; n < share->count; n++) {
			Share::Image& image = share->images[n];
			if ((int32)n == share->current || (int32)n == share->pending
				|| image.retiring) {
				image.retiring = false;
				release_sem_etc(image.sem, 1, B_DO_NOT_RESCHEDULE);
			}
		}
		share->current = -1;
		share->pending = -1;
	}
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
	// M11 step 4: what the median above was measured with
	uint32 effects = fEffects;
	log_line("compose effects shadow %s frosted %s strength %s",
		(effects & kWindowEffectShadow) != 0 ? "on" : "off",
		(effects & kWindowEffectFrostedGlass) != 0 ? "on" : "off",
		window_effect_level_for(fStrength).name);
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
