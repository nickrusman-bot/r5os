/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Draws the freeglut Newell teapot with Vulkan. --selftest renders one
 * offscreen frame and prints the device and a few pixels.
 */

#include "teapot_spv.h"

typedef float GLfloat;
#include "fg_teapot_data.h"

#include <vulkan/vulkan.h>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <OS.h>
#include <View.h>
#include <Window.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const int kSize = 128;
static const int kGrid = 4;


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


static void
die(const char* text, VkResult result)
{
	fprintf(stderr, "%s (%d)\n", text, (int)result);
	char message[256];
	snprintf(message, sizeof(message), "%s (%d)", text, (int)result);
	if (be_app == NULL)
		new BApplication("application/x-vnd.R5OS-VulkanTeapot");
	BAlert* alert = new BAlert("VulkanTeapot", message, "OK",
		NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
	exit(1);
}


static void
check(const char* text, VkResult result)
{
	if (result != VK_SUCCESS)
		die(text, result);
}


static float
bernstein(int index, float t)
{
	float u = 1.0f - t;
	if (index == 0)
		return u * u * u;
	if (index == 1)
		return 3.0f * u * u * t;
	if (index == 2)
		return 3.0f * u * t * t;
	return t * t * t;
}


static void
eval_point(const int patch[16], float u, float v, int flip, float angle,
	float out[3])
{
	out[0] = out[1] = out[2] = 0;
	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			const GLfloat* cp = cpdata_teapot[patch[j * 4 + i]];
			float bu = bernstein(i, u);
			float bv = bernstein(j, v);
			float x = cp[0];
			float y = cp[1];
			float z = flip ? -cp[2] : cp[2];
			float c = cosf(angle);
			float s = sinf(angle);
			out[0] += bu * bv * (c * x + s * z);
			out[1] += bu * bv * y;
			out[2] += bu * bv * (-s * x + c * z);
		}
	}
}


static void
eval_patch(const int patch[16], float u, float v, int flip, float angle,
	float out[3], float normal[3])
{
	float point[3];
	float du[3];
	float dv[3];
	eval_point(patch, u, v, flip, angle, point);
	float step = 1.0f / kGrid;
	float u2 = u + step > 1.0f ? u - step : u + step;
	float v2 = v + step > 1.0f ? v - step : v + step;
	float other[3];
	eval_point(patch, u2, v, flip, angle, other);
	for (int n = 0; n < 3; n++)
		du[n] = other[n] - point[n];
	eval_point(patch, u, v2, flip, angle, other);
	for (int n = 0; n < 3; n++)
		dv[n] = other[n] - point[n];
	normal[0] = du[1] * dv[2] - du[2] * dv[1];
	normal[1] = du[2] * dv[0] - du[0] * dv[2];
	normal[2] = du[0] * dv[1] - du[1] * dv[0];
	float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1]
		+ normal[2] * normal[2]);
	if (length > 0.0001f) {
		normal[0] /= length;
		normal[1] /= length;
		normal[2] /= length;
	}
	memcpy(out, point, sizeof(point));
}


static void
add_vertex(Mesh* mesh, const float point[3], const float normal[3])
{
	mesh->vertices = (Vertex*)realloc(mesh->vertices,
		(mesh->vertexCount + 1) * sizeof(Vertex));
	Vertex* vertex = &mesh->vertices[mesh->vertexCount++];
	memcpy(vertex->position, point, sizeof(vertex->position));
	memcpy(vertex->normal, normal, sizeof(vertex->normal));
}


static void
add_patch(Mesh* mesh, const int patch[16], int flip, float angle)
{
	uint32_t base = mesh->vertexCount;
	for (int y = 0; y <= kGrid; y++) {
		for (int x = 0; x <= kGrid; x++) {
			float point[3];
			float normal[3];
			eval_patch(patch, (float)x / kGrid, (float)y / kGrid, flip, angle,
				point, normal);
			add_vertex(mesh, point, normal);
		}
	}
	int stride = kGrid + 1;
	for (int y = 0; y < kGrid; y++) {
		for (int x = 0; x < kGrid; x++) {
			uint32_t i0 = base + y * stride + x;
			uint32_t i1 = i0 + 1;
			uint32_t i2 = i0 + stride;
			uint32_t i3 = i2 + 1;
			uint32_t extra = 6;
			mesh->indices = (uint32_t*)realloc(mesh->indices,
				(mesh->indexCount + extra) * sizeof(uint32_t));
			uint32_t* out = mesh->indices + mesh->indexCount;
			out[0] = i0;
			out[1] = i2;
			out[2] = i1;
			out[3] = i1;
			out[4] = i2;
			out[5] = i3;
			mesh->indexCount += extra;
		}
	}
}


static Mesh
build_teapot(void)
{
	Mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	for (int patch = 0; patch < GLUT_TEAPOT_N_INPUT_PATCHES; patch++) {
		if (patch < 6) {
			for (int turn = 0; turn < 4; turn++)
				add_patch(&mesh, patchdata_teapot[patch], 0, turn * 1.5707963f);
		} else {
			add_patch(&mesh, patchdata_teapot[patch], 0, 0);
			add_patch(&mesh, patchdata_teapot[patch], 1, 0);
		}
	}
	return mesh;
}


static void
mvp(float out[16])
{
	float eye[3] = {0, 2.2f, 6.0f};
	float center[3] = {0, 0.8f, 0};
	float up[3] = {0, 1, 0};
	float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
	float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	f[0] /= fl;
	f[1] /= fl;
	f[2] /= fl;
	float s[3] = {
		f[1] * up[2] - f[2] * up[1],
		f[2] * up[0] - f[0] * up[2],
		f[0] * up[1] - f[1] * up[0]
	};
	float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	s[0] /= sl;
	s[1] /= sl;
	s[2] /= sl;
	float u[3] = {
		s[1] * f[2] - s[2] * f[1],
		s[2] * f[0] - s[0] * f[2],
		s[0] * f[1] - s[1] * f[0]
	};
	float view[16] = {
		s[0], u[0], -f[0], 0,
		s[1], u[1], -f[1], 0,
		s[2], u[2], -f[2], 0,
		-(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]),
		-(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
		(f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]),
		1
	};
	float near = 0.1f;
	float far = 20.0f;
	float fov = 0.7f;
	float yScale = 1.0f / tanf(fov * 0.5f);
	float proj[16] = {
		yScale, 0, 0, 0,
		0, -yScale, 0, 0,
		0, 0, far / (near - far), -1,
		0, 0, (far * near) / (near - far), 0
	};
	memset(out, 0, 16 * sizeof(float));
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			for (int k = 0; k < 4; k++)
				out[col * 4 + row] += proj[k * 4 + row] * view[col * 4 + k];
		}
	}
}


static void
mvp_turned(float out[16], float angle)
{
	float base[16];
	mvp(base);
	float c = cosf(angle);
	float s = sinf(angle);
	float rot[16] = {
		c, 0, -s, 0,
		0, 1, 0, 0,
		s, 0, c, 0,
		0, 0, 0, 1
	};
	memset(out, 0, 16 * sizeof(float));
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			for (int k = 0; k < 4; k++)
				out[col * 4 + row] += base[k * 4 + row] * rot[col * 4 + k];
		}
	}
}


static uint32_t
memory_type(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags)
{
	VkPhysicalDeviceMemoryProperties properties;
	vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
	for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
		if ((bits & (1u << i))
			&& (properties.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}
	die("memory type", VK_ERROR_FEATURE_NOT_PRESENT);
	return 0;
}


struct DrawState {
	VkDevice		device;
	VkQueue			queue;
	VkCommandBuffer	cmd;
	VkRenderPass	pass;
	VkFramebuffer	framebuffer;
	VkPipeline		pipeline;
	VkPipelineLayout layout;
	VkBuffer		vertexBuffer;
	VkBuffer		indexBuffer;
	VkImage			color;
	VkBuffer		readback;
	uint32_t		indexCount;
	uint8_t*		pixels;
};


static void
draw_frame(DrawState* draw, const float matrix[16])
{
	check("vkResetCommandBuffer", vkResetCommandBuffer(draw->cmd, 0));
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	check("vkBeginCommandBuffer", vkBeginCommandBuffer(draw->cmd, &begin));
	VkClearValue clears[2] = {};
	clears[0].color.float32[0] = 0.15f;
	clears[0].color.float32[1] = 0.18f;
	clears[0].color.float32[2] = 0.22f;
	clears[0].color.float32[3] = 1;
	clears[1].depthStencil.depth = 1;
	VkRenderPassBeginInfo beginPass = {};
	beginPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginPass.renderPass = draw->pass;
	beginPass.framebuffer = draw->framebuffer;
	beginPass.renderArea.extent.width = kSize;
	beginPass.renderArea.extent.height = kSize;
	beginPass.clearValueCount = 2;
	beginPass.pClearValues = clears;
	vkCmdBeginRenderPass(draw->cmd, &beginPass, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(draw->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw->pipeline);
	vkCmdPushConstants(draw->cmd, draw->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(float) * 16, matrix);
	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(draw->cmd, 0, 1, &draw->vertexBuffer, &offset);
	vkCmdBindIndexBuffer(draw->cmd, draw->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(draw->cmd, draw->indexCount, 1, 0, 0, 0);
	vkCmdEndRenderPass(draw->cmd);
	VkBufferImageCopy copy = {};
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = kSize;
	copy.imageExtent.height = kSize;
	copy.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer(draw->cmd, draw->color,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, draw->readback, 1, &copy);
	check("vkEndCommandBuffer", vkEndCommandBuffer(draw->cmd));
	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &draw->cmd;
	check("vkQueueSubmit", vkQueueSubmit(draw->queue, 1, &submit, VK_NULL_HANDLE));
	check("vkQueueWaitIdle", vkQueueWaitIdle(draw->queue));
}


class TeapotView : public BView {
public:
	TeapotView(BRect frame)
		:
		BView(frame, "teapot", B_FOLLOW_ALL, B_WILL_DRAW),
		fBitmap(new BBitmap(BRect(0, 0, kSize - 1, kSize - 1), B_RGBA32))
	{
	}

	~TeapotView()
	{
		delete fBitmap;
	}

	void Draw(BRect updateRect)
	{
		(void)updateRect;
		DrawBitmap(fBitmap, Bounds());
	}

	void SetPixels(const uint8_t* pixels)
	{
		uint8_t* bits = (uint8_t*)fBitmap->Bits();
		for (int i = 0; i < kSize * kSize; i++) {
			bits[i * 4 + 0] = pixels[i * 4 + 2];
			bits[i * 4 + 1] = pixels[i * 4 + 1];
			bits[i * 4 + 2] = pixels[i * 4 + 0];
			bits[i * 4 + 3] = pixels[i * 4 + 3];
		}
	}

private:
	BBitmap* fBitmap;
};


class TeapotWindow : public BWindow {
public:
	TeapotWindow(DrawState* draw, const char* name, const char* driver)
		:
		BWindow(BRect(80, 80, 80 + kSize * 3, 80 + kSize * 3),
			"VulkanTeapot", B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE),
		fView(new TeapotView(Bounds())),
		fDraw(draw),
		fStop(false),
		fThread(-1),
		fFrames(0),
		fMark(0)
	{
		snprintf(fName, sizeof(fName), "%s", name);
		snprintf(fDriver, sizeof(fDriver), "%s", driver);
		AddChild(fView);
	}

	void Start()
	{
		fThread = spawn_thread(render_entry, "teapot", B_NORMAL_PRIORITY, this);
		if (fThread >= 0)
			resume_thread(fThread);
	}

	bool QuitRequested()
	{
		if (fThread >= 0) {
			fStop = true;
			thread_id thread = fThread;
			fThread = -1;
			Unlock();
			status_t status;
			wait_for_thread(thread, &status);
			Lock();
		}
		return BWindow::QuitRequested();
	}

private:
	static int32 render_entry(void* data)
	{
		((TeapotWindow*)data)->Render();
		return 0;
	}

	void Render()
	{
		fMark = system_time();
		while (!fStop) {
			float turns = (float)(system_time() % 4000000) / 4000000.0f;
			float matrix[16];
			mvp_turned(matrix, turns * 6.2831853f);
			draw_frame(fDraw, matrix);
			if (fStop || !Lock())
				return;
			fView->SetPixels(fDraw->pixels);
			fView->Invalidate();
			fFrames++;
			bigtime_t now = system_time();
			if (now - fMark >= 1000000) {
				char title[160];
				snprintf(title, sizeof(title), "%s  %s  %d fps",
					fName, fDriver, fFrames);
				SetTitle(title);
				fFrames = 0;
				fMark = now;
			}
			Unlock();
		}
	}

	TeapotView*		fView;
	DrawState*		fDraw;
	volatile bool	fStop;
	thread_id		fThread;
	int				fFrames;
	bigtime_t		fMark;
	char			fName[256];
	char			fDriver[32];
};


int
main(int argc, char** argv)
{
	int selftest = argc > 1 && strcmp(argv[1], "--selftest") == 0;
	Mesh mesh = build_teapot();
	float matrix[16];
	mvp(matrix);

	uint32_t instanceVersion = 0;
	uint32_t apiVersion = VK_API_VERSION_1_0;
	if (vkEnumerateInstanceVersion(&instanceVersion) == VK_SUCCESS
		&& instanceVersion >= VK_API_VERSION_1_2)
		apiVersion = VK_API_VERSION_1_2;
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VulkanTeapot";
	appInfo.apiVersion = apiVersion;
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	VkInstance instance = VK_NULL_HANDLE;
	VkResult result = vkCreateInstance(&instanceInfo, NULL, &instance);
	if (result != VK_SUCCESS)
		die("vkCreateInstance", result);

	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, NULL);
	if (count == 0)
		die("no device", VK_ERROR_INITIALIZATION_FAILED);
	VkPhysicalDevice* gpus = (VkPhysicalDevice*)calloc(count, sizeof(*gpus));
	vkEnumeratePhysicalDevices(instance, &count, gpus);
	VkPhysicalDevice gpu = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties;
	uint32_t driverID = 0;
	int driverKnown = 0;
	memset(&properties, 0, sizeof(properties));
	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceDriverProperties driver = {};
		driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		VkPhysicalDeviceProperties2 props2 = {};
		props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props2.pNext = &driver;
		vkGetPhysicalDeviceProperties2(gpus[i], &props2);
		bool cpu = props2.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
		if (gpu != VK_NULL_HANDLE && cpu)
			continue;
		gpu = gpus[i];
		properties = props2.properties;
		driverKnown = 0;
		driverID = 0;
		if (props2.properties.apiVersion >= VK_API_VERSION_1_2)
			driverKnown = 1;
		else {
			uint32_t extensions = 0;
			vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &extensions, NULL);
			if (extensions > 0) {
				VkExtensionProperties* names = (VkExtensionProperties*)calloc(
					extensions, sizeof(*names));
				vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &extensions, names);
				for (uint32_t n = 0; n < extensions; n++) {
					if (strcmp(names[n].extensionName, "VK_KHR_driver_properties") == 0)
						driverKnown = 1;
				}
				free(names);
			}
		}
		if (driverKnown)
			driverID = driver.driverID;
		if (!cpu)
			break;
	}

	uint32_t family = 0;
	uint32_t families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, NULL);
	VkQueueFamilyProperties* familyInfo =
		(VkQueueFamilyProperties*)calloc(families, sizeof(*familyInfo));
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, familyInfo);
	for (uint32_t i = 0; i < families; i++) {
		if (familyInfo[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			family = i;
			break;
		}
	}
	float priority = 1;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	VkDevice device = VK_NULL_HANDLE;
	result = vkCreateDevice(gpu, &deviceInfo, NULL, &device);
	if (result != VK_SUCCESS)
		die("vkCreateDevice", result);
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, family, 0, &queue);

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.extent.width = kSize;
	imageInfo.extent.height = kSize;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	VkImage color = VK_NULL_HANDLE;
	check("vkCreateImage", vkCreateImage(device, &imageInfo, NULL, &color));
	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(device, color, &requirements);
	VkMemoryAllocateInfo alloc = {};
	alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory colorMemory = VK_NULL_HANDLE;
	check("vkAllocateMemory", vkAllocateMemory(device, &alloc, NULL, &colorMemory));
	vkBindImageMemory(device, color, colorMemory, 0);

	imageInfo.format = VK_FORMAT_D32_SFLOAT;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VkImage depth = VK_NULL_HANDLE;
	check("vkCreateImage", vkCreateImage(device, &imageInfo, NULL, &depth));
	vkGetImageMemoryRequirements(device, depth, &requirements);
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VkDeviceMemory depthMemory = VK_NULL_HANDLE;
	check("vkAllocateMemory", vkAllocateMemory(device, &alloc, NULL, &depthMemory));
	vkBindImageMemory(device, depth, depthMemory, 0);

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = color;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	VkImageView colorView = VK_NULL_HANDLE;
	check("vkCreateImageView", vkCreateImageView(device, &viewInfo, NULL, &colorView));
	viewInfo.image = depth;
	viewInfo.format = VK_FORMAT_D32_SFLOAT;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	VkImageView depthView = VK_NULL_HANDLE;
	check("vkCreateImageView", vkCreateImageView(device, &viewInfo, NULL, &depthView));

	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;
	VkRenderPassCreateInfo passInfo = {};
	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	passInfo.attachmentCount = 2;
	passInfo.pAttachments = attachments;
	passInfo.subpassCount = 1;
	passInfo.pSubpasses = &subpass;
	VkRenderPass pass = VK_NULL_HANDLE;
	check("vkCreateRenderPass", vkCreateRenderPass(device, &passInfo, NULL, &pass));

	VkImageView views[2] = {colorView, depthView};
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.renderPass = pass;
	fbInfo.attachmentCount = 2;
	fbInfo.pAttachments = views;
	fbInfo.width = kSize;
	fbInfo.height = kSize;
	fbInfo.layers = 1;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	check("vkCreateFramebuffer", vkCreateFramebuffer(device, &fbInfo, NULL, &framebuffer));

	VkShaderModuleCreateInfo shaderInfo = {};
	shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderInfo.codeSize = kTeapotVertSpvWords * 4;
	shaderInfo.pCode = kTeapotVertSpv;
	VkShaderModule vert = VK_NULL_HANDLE;
	check("vkCreateShaderModule", vkCreateShaderModule(device, &shaderInfo, NULL, &vert));
	shaderInfo.codeSize = kTeapotFragSpvWords * 4;
	shaderInfo.pCode = kTeapotFragSpv;
	VkShaderModule frag = VK_NULL_HANDLE;
	check("vkCreateShaderModule", vkCreateShaderModule(device, &shaderInfo, NULL, &frag));

	VkPushConstantRange range = {};
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = sizeof(matrix);
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	check("vkCreatePipelineLayout",
		vkCreatePipelineLayout(device, &layoutInfo, NULL, &layout));

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
	VkViewport viewport = {0, 0, (float)kSize, (float)kSize, 0, 1};
	VkRect2D scissor = {{0, 0}, {kSize, kSize}};
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;
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
	pipeInfo.layout = layout;
	pipeInfo.renderPass = pass;
	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &pipeline);
	if (result != VK_SUCCESS)
		die("pipeline", result);

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = mesh.vertexCount * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	check("vkCreateBuffer", vkCreateBuffer(device, &bufferInfo, NULL, &vertexBuffer));
	vkGetBufferMemoryRequirements(device, vertexBuffer, &requirements);
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
	check("vkAllocateMemory", vkAllocateMemory(device, &alloc, NULL, &vertexMemory));
	vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);
	void* mapped = NULL;
	vkMapMemory(device, vertexMemory, 0, bufferInfo.size, 0, &mapped);
	memcpy(mapped, mesh.vertices, bufferInfo.size);
	vkUnmapMemory(device, vertexMemory);

	bufferInfo.size = mesh.indexCount * sizeof(uint32_t);
	bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	VkBuffer indexBuffer = VK_NULL_HANDLE;
	check("vkCreateBuffer", vkCreateBuffer(device, &bufferInfo, NULL, &indexBuffer));
	vkGetBufferMemoryRequirements(device, indexBuffer, &requirements);
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory indexMemory = VK_NULL_HANDLE;
	check("vkAllocateMemory", vkAllocateMemory(device, &alloc, NULL, &indexMemory));
	vkBindBufferMemory(device, indexBuffer, indexMemory, 0);
	vkMapMemory(device, indexMemory, 0, bufferInfo.size, 0, &mapped);
	memcpy(mapped, mesh.indices, bufferInfo.size);
	vkUnmapMemory(device, indexMemory);

	VkBuffer readback = VK_NULL_HANDLE;
	bufferInfo.size = kSize * kSize * 4;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	check("vkCreateBuffer", vkCreateBuffer(device, &bufferInfo, NULL, &readback));
	vkGetBufferMemoryRequirements(device, readback, &requirements);
	alloc.allocationSize = requirements.size;
	alloc.memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkDeviceMemory readMemory = VK_NULL_HANDLE;
	check("vkAllocateMemory", vkAllocateMemory(device, &alloc, NULL, &readMemory));
	vkBindBufferMemory(device, readback, readMemory, 0);

	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = family;
	VkCommandPool pool = VK_NULL_HANDLE;
	check("vkCreateCommandPool", vkCreateCommandPool(device, &poolInfo, NULL, &pool));
	VkCommandBufferAllocateInfo cmdInfo = {};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdInfo.commandPool = pool;
	cmdInfo.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	check("vkAllocateCommandBuffers",
		vkAllocateCommandBuffers(device, &cmdInfo, &cmd));

	DrawState draw = {};
	draw.device = device;
	draw.queue = queue;
	draw.cmd = cmd;
	draw.pass = pass;
	draw.framebuffer = framebuffer;
	draw.pipeline = pipeline;
	draw.layout = layout;
	draw.vertexBuffer = vertexBuffer;
	draw.indexBuffer = indexBuffer;
	draw.color = color;
	draw.readback = readback;
	draw.indexCount = mesh.indexCount;
	check("vkMapMemory", vkMapMemory(device, readMemory, 0, kSize * kSize * 4, 0,
		(void**)&draw.pixels));

	if (selftest) {
		draw_frame(&draw, matrix);
		const int sampleAt[][2] = {{64, 40}, {64, 64}, {64, 90}, {40, 70}, {90, 70}, {64, 20}};
		printf("device %s\n", properties.deviceName);
		printf("deviceType %u\n", (unsigned)properties.deviceType);
		if (driverKnown)
			printf("driverID %u\n", driverID);
		else
			printf("driverID unknown\n");
		printf("frames 1\n");
		printf("fps 0\n");
		int mismatch = 0;
		for (int i = 0; i < 6; i++) {
			int x = sampleAt[i][0];
			int y = sampleAt[i][1];
			const uint8_t* p = draw.pixels + (y * kSize + x) * 4;
			printf("pixel %d %d %02x%02x%02x%02x\n", x, y, p[0], p[1], p[2], p[3]);
			(void)mismatch;
		}
		printf("pixels recorded\n");
	} else {
		if (be_app == NULL)
			new BApplication("application/x-vnd.R5OS-VulkanTeapot");
		char driverText[32];
		if (driverKnown)
			snprintf(driverText, sizeof(driverText), "%u", driverID);
		else
			snprintf(driverText, sizeof(driverText), "unknown");
		TeapotWindow* window = new TeapotWindow(&draw,
			properties.deviceName, driverText);
		window->Start();
		window->Show();
		be_app->Run();
	}

	vkDeviceWaitIdle(device);
	vkUnmapMemory(device, readMemory);
	vkDestroyCommandPool(device, pool, NULL);
	vkDestroyBuffer(device, readback, NULL);
	vkFreeMemory(device, readMemory, NULL);
	vkDestroyBuffer(device, indexBuffer, NULL);
	vkFreeMemory(device, indexMemory, NULL);
	vkDestroyBuffer(device, vertexBuffer, NULL);
	vkFreeMemory(device, vertexMemory, NULL);
	vkDestroyPipeline(device, pipeline, NULL);
	vkDestroyShaderModule(device, frag, NULL);
	vkDestroyShaderModule(device, vert, NULL);
	vkDestroyPipelineLayout(device, layout, NULL);
	vkDestroyFramebuffer(device, framebuffer, NULL);
	vkDestroyImageView(device, depthView, NULL);
	vkDestroyImageView(device, colorView, NULL);
	vkDestroyRenderPass(device, pass, NULL);
	vkDestroyImage(device, depth, NULL);
	vkFreeMemory(device, depthMemory, NULL);
	vkDestroyImage(device, color, NULL);
	vkFreeMemory(device, colorMemory, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroyInstance(instance, NULL);
	free(mesh.vertices);
	free(mesh.indices);
	free(gpus);
	free(familyInfo);
	return 0;
}
