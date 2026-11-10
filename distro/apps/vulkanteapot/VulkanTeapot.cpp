/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Draws the freeglut Newell teapot, or a cube with --cube, through Vulkan.
 * --selftest draws the same frame on two explicitly chosen devices and
 * compares those pictures. RADV is driver id 3, Lavapipe is driver id 13.
 * With only one of them, that device is rendered twice.
 */

#include "r5_pixels.h"
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


static const int kSize = 384;

int run_window(int cube);
static const int kSubDiv = 8;


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


static void
bernstein3(int index, float x, float* value, float* deriv)
{
	float inv = 1.0f - x;
	float temp;
	if (index == 0) {
		temp = inv * inv;
		*value = inv * temp;
		*deriv = -3.0f * temp;
	} else if (index == 1) {
		temp = inv * inv;
		*value = 3.0f * x * temp;
		*deriv = 3.0f * temp - 6.0f * x * inv;
	} else if (index == 2) {
		temp = x * x;
		*value = 3.0f * temp * inv;
		*deriv = 6.0f * x * inv - 3.0f * temp;
	} else {
		temp = x * x;
		*value = x * temp;
		*deriv = 3.0f * temp;
	}
}


static void
load_patch(const int patch[16], float cp[4][4][3])
{
	const float scale = 0.5f;
	for (int i = 0; i < 16; i++) {
		const GLfloat* src = cpdata_teapot[patch[i]];
		cp[i / 4][i % 4][0] = src[0] * scale;
		cp[i / 4][i % 4][1] = (src[2] - 1.575f) * scale;
		cp[i / 4][i % 4][2] = -src[1] * scale;
	}
}


static void
rotate_or_mirror(float* values, int count, int copies)
{
	if (copies == 4) {
		for (int o = 0; o < count; o += 3) {
			float x = values[o];
			float y = values[o + 1];
			float z = values[o + 2];
			float* turn90 = values + count + o;
			float* turn180 = values + count * 2 + o;
			float* turn270 = values + count * 3 + o;
			turn90[0] = z;
			turn90[1] = y;
			turn90[2] = -x;
			turn180[0] = -x;
			turn180[1] = y;
			turn180[2] = -z;
			turn270[0] = -z;
			turn270[1] = y;
			turn270[2] = x;
		}
		return;
	}
	for (int row = 0; row < kSubDiv; row++) {
		int source = (kSubDiv - row - 1) * kSubDiv * 3;
		int dest = count + row * kSubDiv * 3;
		for (int col = 0; col < kSubDiv; col++) {
			values[dest] = values[source];
			values[dest + 1] = values[source + 1];
			values[dest + 2] = -values[source + 2];
			source += 3;
			dest += 3;
		}
	}
}


static void
eval_bezier(const float cp[4][4][3], const float bern[8][4],
	const float deriv[8][4], int normalFix, float* verts, float* norms)
{
	int offset = 0;
	for (int u = 0; u < kSubDiv; u++) {
		for (int v = 0; v < kSubDiv; v++, offset += 3) {
			float tangentV[3] = {0, 0, 0};
			float tangentU[3] = {0, 0, 0};
			verts[offset] = verts[offset + 1] = verts[offset + 2] = 0;
			for (int i = 0; i < 4; i++) {
				float point[3] = {0, 0, 0};
				float alongV[3] = {0, 0, 0};
				for (int j = 0; j < 4; j++) {
					point[0] += bern[v][j] * cp[i][j][0];
					point[1] += bern[v][j] * cp[i][j][1];
					point[2] += bern[v][j] * cp[i][j][2];
					alongV[0] += deriv[v][j] * cp[i][j][0];
					alongV[1] += deriv[v][j] * cp[i][j][1];
					alongV[2] += deriv[v][j] * cp[i][j][2];
				}
				verts[offset] += bern[u][i] * point[0];
				verts[offset + 1] += bern[u][i] * point[1];
				verts[offset + 2] += bern[u][i] * point[2];
				tangentV[0] += bern[u][i] * alongV[0];
				tangentV[1] += bern[u][i] * alongV[1];
				tangentV[2] += bern[u][i] * alongV[2];
				tangentU[0] += deriv[u][i] * point[0];
				tangentU[1] += deriv[u][i] * point[1];
				tangentU[2] += deriv[u][i] * point[2];
			}
			norms[offset] = tangentV[1] * tangentU[2] - tangentV[2] * tangentU[1];
			norms[offset + 1] = tangentV[2] * tangentU[0] - tangentV[0] * tangentU[2];
			norms[offset + 2] = tangentV[0] * tangentU[1] - tangentV[1] * tangentU[0];
			float length = sqrtf(norms[offset] * norms[offset]
				+ norms[offset + 1] * norms[offset + 1]
				+ norms[offset + 2] * norms[offset + 2]);
			if (length > 0.000001f) {
				norms[offset] /= length;
				norms[offset + 1] /= length;
				norms[offset + 2] /= length;
			} else {
				norms[offset] = norms[offset + 1] = norms[offset + 2] = 0;
			}
		}
	}
	if (normalFix != 0) {
		float up = normalFix == 1 ? 1.0f : -1.0f;
		for (int n = 0; n < kSubDiv; n++) {
			norms[n * 3] = 0;
			norms[n * 3 + 1] = up;
			norms[n * 3 + 2] = 0;
		}
	}
}


static void
add_patch_indices(Mesh* mesh, uint32_t base)
{
	for (int row = 0; row < kSubDiv - 1; row++) {
		for (int col = 0; col < kSubDiv - 1; col++) {
			uint32_t row1 = base + row * kSubDiv + col;
			uint32_t row2 = row1 + kSubDiv;
			mesh->indices = (uint32_t*)realloc(mesh->indices,
				(mesh->indexCount + 6) * sizeof(uint32_t));
			uint32_t* out = mesh->indices + mesh->indexCount;
			out[0] = row1;
			out[1] = row2;
			out[2] = row2 + 1;
			out[3] = row1;
			out[4] = row2 + 1;
			out[5] = row1 + 1;
			mesh->indexCount += 6;
		}
	}
}


Mesh
build_teapot(void)
{
	Mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	float bern[kSubDiv][4];
	float deriv[kSubDiv][4];
	for (int s = 0; s < kSubDiv; s++) {
		float x = s / (float)(kSubDiv - 1);
		for (int i = 0; i < 4; i++)
			bernstein3(i, x, &bern[s][i], &deriv[s][i]);
	}
	const int points = kSubDiv * kSubDiv;
	const int values = points * 3;
	float verts[4 * 8 * 8 * 3];
	float norms[4 * 8 * 8 * 3];
	for (int patch = 0; patch < GLUT_TEAPOT_N_INPUT_PATCHES; patch++) {
		float cp[4][4][3];
		load_patch(patchdata_teapot[patch], cp);
		int copies = patch < 6 ? 4 : 2;
		int normalFix = 0;
		if (patch == 3)
			normalFix = 1;
		else if (patch == 5)
			normalFix = 2;
		eval_bezier(cp, bern, deriv, normalFix, verts, norms);
		rotate_or_mirror(verts, values, copies);
		rotate_or_mirror(norms, values, copies);
		uint32_t base = mesh.vertexCount;
		for (int copy = 0; copy < copies; copy++) {
			for (int n = 0; n < points; n++) {
				mesh.vertices = (Vertex*)realloc(mesh.vertices,
					(mesh.vertexCount + 1) * sizeof(Vertex));
				Vertex* vertex = &mesh.vertices[mesh.vertexCount++];
				memcpy(vertex->position, verts + (copy * points + n) * 3,
					sizeof(vertex->position));
				memcpy(vertex->normal, norms + (copy * points + n) * 3,
					sizeof(vertex->normal));
			}
		}
		for (int copy = 0; copy < copies; copy++)
			add_patch_indices(&mesh, base + copy * points);
	}
	return mesh;
}


Mesh
build_cube(void)
{
	Mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	static const float faces[6][4][3] = {
		{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
		{{-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {1, -1, -1}},
		{{-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {1, 1, -1}},
		{{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
		{{1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}},
		{{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}
	};
	static const float normals[6][3] = {
		{0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}
	};
	mesh.vertices = (Vertex*)calloc(24, sizeof(Vertex));
	mesh.indices = (uint32_t*)calloc(36, sizeof(uint32_t));
	mesh.vertexCount = 24;
	mesh.indexCount = 36;
	for (int face = 0; face < 6; face++) {
		for (int corner = 0; corner < 4; corner++) {
			Vertex* vertex = &mesh.vertices[face * 4 + corner];
			memcpy(vertex->position, faces[face][corner], sizeof(vertex->position));
			memcpy(vertex->normal, normals[face], sizeof(vertex->normal));
		}
		uint32_t base = (uint32_t)face * 4u;
		uint32_t* out = mesh.indices + face * 6;
		out[0] = base;
		out[1] = base + 1;
		out[2] = base + 2;
		out[3] = base;
		out[4] = base + 2;
		out[5] = base + 3;
	}
	return mesh;
}


static void
mvp(float out[16])
{
	float eye[3] = {0, 0.15f, 7.2f};
	float center[3] = {0, 0, 0};
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


void
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


uint32_t
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


static int
memory_type_or(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags)
{
	VkPhysicalDeviceMemoryProperties properties;
	vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
	for (uint32_t i = 0; i < properties.memoryTypeCount; i++) {
		if ((bits & (1u << i))
			&& (properties.memoryTypes[i].propertyFlags & flags) == flags)
			return (int)i;
	}
	return -1;
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
	VkBuffer		readbackVram;
	uint32_t		indexCount;
	uint8_t*		pixels;
	uint8_t*		pixelsVram;
	VkQueryPool		queries;
	uint32_t		queryBase;
	uint32_t		queryCount;
};


static void
draw_frame(DrawState* draw, const float matrix[16])
{
	check("vkResetCommandBuffer", vkResetCommandBuffer(draw->cmd, 0));
	VkCommandBufferBeginInfo begin = {};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	check("vkBeginCommandBuffer", vkBeginCommandBuffer(draw->cmd, &begin));
	if (draw->queries != VK_NULL_HANDLE) {
		if (draw->queryBase == 0) {
			vkCmdResetQueryPool(draw->cmd, draw->queries, 0, draw->queryCount);
		}
		vkCmdWriteTimestamp(draw->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			draw->queries, draw->queryBase);
	}
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
	if (draw->readbackVram != VK_NULL_HANDLE) {
		vkCmdCopyImageToBuffer(draw->cmd, draw->color,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, draw->readbackVram, 1, &copy);
	}
	if (draw->queries != VK_NULL_HANDLE) {
		vkCmdWriteTimestamp(draw->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			draw->queries, draw->queryBase + 1);
	}
	check("vkEndCommandBuffer", vkEndCommandBuffer(draw->cmd));
	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &draw->cmd;
	check("vkQueueSubmit", vkQueueSubmit(draw->queue, 1, &submit, VK_NULL_HANDLE));
	VkResult waited = vkQueueWaitIdle(draw->queue);
	printf("vkQueueWaitIdle %d\n", (int)waited);
	check("vkQueueWaitIdle", waited);
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
		BWindow(BRect(80, 80, 80 + kSize - 1, 80 + kSize - 1),
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


static int
cmp_double(const void* left, const void* right)
{
	double a = *(const double*)left;
	double b = *(const double*)right;
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}


static double
median_ms(const uint64_t* stamps, int frames, float period)
{
	double samples[100];
	if (frames > 100)
		frames = 100;
	for (int i = 0; i < frames; i++) {
		uint64_t start = stamps[i * 2];
		uint64_t end = stamps[i * 2 + 1];
		double ticks = end >= start ? (double)(end - start) : 0;
		samples[i] = ticks * (double)period / 1000000.0;
	}
	qsort(samples, (size_t)frames, sizeof(samples[0]), cmp_double);
	return samples[frames / 2];
}


int
main(int argc, char** argv)
{
	int selftest = 0;
	int cube = 0;
	int windowMode = 0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--selftest") == 0)
			selftest = 1;
		else if (strcmp(argv[i], "--cube") == 0)
			cube = 1;
		else if (strcmp(argv[i], "--window") == 0)
			windowMode = 1;
	}
	if (windowMode && !selftest)
		return run_window(cube);
	Mesh mesh = cube ? build_cube() : build_teapot();
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
	struct DevicePick {
		VkPhysicalDevice gpu;
		VkPhysicalDeviceProperties properties;
		uint32_t driverID;
		int driverKnown;
	};
	DevicePick radvPick = {};
	DevicePick lvpPick = {};
	DevicePick anyPick = {};
	int haveRadv = 0;
	int haveLvp = 0;
	int haveAny = 0;
	for (uint32_t i = 0; i < count; i++) {
		VkPhysicalDeviceDriverProperties driver = {};
		driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		VkPhysicalDeviceProperties2 props2 = {};
		props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		props2.pNext = &driver;
		vkGetPhysicalDeviceProperties2(gpus[i], &props2);
		DevicePick pick = {};
		pick.gpu = gpus[i];
		pick.properties = props2.properties;
		if (props2.properties.apiVersion >= VK_API_VERSION_1_2)
			pick.driverKnown = 1;
		else {
			uint32_t extensions = 0;
			vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &extensions, NULL);
			if (extensions > 0) {
				VkExtensionProperties* names = (VkExtensionProperties*)calloc(
					extensions, sizeof(*names));
				vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &extensions, names);
				for (uint32_t n = 0; n < extensions; n++) {
					if (strcmp(names[n].extensionName, "VK_KHR_driver_properties") == 0)
						pick.driverKnown = 1;
				}
				free(names);
			}
		}
		if (pick.driverKnown)
			pick.driverID = driver.driverID;
		if (!haveAny) {
			anyPick = pick;
			haveAny = 1;
		}
		if (pick.driverKnown && pick.driverID == VK_DRIVER_ID_MESA_RADV && !haveRadv) {
			radvPick = pick;
			haveRadv = 1;
		}
		if (pick.driverKnown && pick.driverID == VK_DRIVER_ID_MESA_LLVMPIPE && !haveLvp) {
			lvpPick = pick;
			haveLvp = 1;
		}
	}

	DevicePick picks[2];
	int pickCount = 0;
	if (selftest) {
		if (!haveRadv && !haveLvp)
			die("no RADV or Lavapipe", VK_ERROR_INITIALIZATION_FAILED);
		/* Same selection either way: RADV then Lavapipe when both exist.
		 * One of them is used for both renders when the other is absent. */
		picks[0] = haveRadv ? radvPick : lvpPick;
		picks[1] = haveLvp ? lvpPick : radvPick;
		pickCount = 2;
	} else {
		picks[0] = haveRadv ? radvPick : (haveLvp ? lvpPick : anyPick);
		pickCount = 1;
	}

	const int kFrames = 100;
	const int kBytes = kSize * kSize * 4;
	uint8_t* shot[2] = {};
	double passMs[2] = {};
	double passSpanMs[2] = {};
	double passCpuMs[2] = {};
	int passTimed[2] = {};
	int pixelStatus = 0;
	for (int devicePass = 0; devicePass < pickCount; devicePass++) {
	VkPhysicalDevice gpu = picks[devicePass].gpu;
	VkPhysicalDeviceProperties properties = picks[devicePass].properties;
	uint32_t driverID = picks[devicePass].driverID;
	int driverKnown = picks[devicePass].driverKnown;

	uint32_t family = 0;
	uint32_t families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, NULL);
	VkQueueFamilyProperties* familyInfo =
		(VkQueueFamilyProperties*)calloc(families, sizeof(*familyInfo));
	vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, familyInfo);
	uint32_t timestampBits = 0;
	for (uint32_t i = 0; i < families; i++) {
		if (familyInfo[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			family = i;
			timestampBits = familyInfo[i].timestampValidBits;
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

	VkBuffer readbackVram = VK_NULL_HANDLE;
	VkDeviceMemory readVramMemory = VK_NULL_HANDLE;
	int vramType = -1;
	check("vkCreateBuffer", vkCreateBuffer(device, &bufferInfo, NULL, &readbackVram));
	vkGetBufferMemoryRequirements(device, readbackVram, &requirements);
	vramType = memory_type_or(gpu, requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	if (vramType < 0) {
		vkDestroyBuffer(device, readbackVram, NULL);
		readbackVram = VK_NULL_HANDLE;
	} else {
		alloc.allocationSize = requirements.size;
		alloc.memoryTypeIndex = (uint32_t)vramType;
		check("vkAllocateMemory",
			vkAllocateMemory(device, &alloc, NULL, &readVramMemory));
		vkBindBufferMemory(device, readbackVram, readVramMemory, 0);
	}

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
	draw.readbackVram = readbackVram;
	draw.indexCount = mesh.indexCount;
	draw.pixelsVram = NULL;
	check("vkMapMemory", vkMapMemory(device, readMemory, 0, kSize * kSize * 4, 0,
		(void**)&draw.pixels));
	if (readVramMemory != VK_NULL_HANDLE) {
		check("vkMapMemory", vkMapMemory(device, readVramMemory, 0,
			kSize * kSize * 4, 0, (void**)&draw.pixelsVram));
	}

	VkQueryPool queryPool = VK_NULL_HANDLE;
	int timestamps = properties.limits.timestampComputeAndGraphics;
	if (selftest && timestamps) {
		VkQueryPoolCreateInfo queryInfo = {};
		queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryInfo.queryCount = (uint32_t)kFrames * 2u;
		check("vkCreateQueryPool",
			vkCreateQueryPool(device, &queryInfo, NULL, &queryPool));
		draw.queries = queryPool;
		draw.queryCount = (uint32_t)kFrames * 2u;
	}
	if (selftest) {
		shot[devicePass] = (uint8_t*)malloc((size_t)kBytes);
		bigtime_t cpuStart = system_time();
		for (int frame = 0; frame < kFrames; frame++) {
			draw.queryBase = (uint32_t)frame * 2u;
			draw_frame(&draw, matrix);
			if (frame == 0)
				memcpy(shot[devicePass], draw.pixels, (size_t)kBytes);
		}
		passCpuMs[devicePass] = (double)(system_time() - cpuStart) / 1000.0;
		if (queryPool != VK_NULL_HANDLE) {
			uint64_t* stamps = (uint64_t*)calloc((size_t)kFrames * 2u, sizeof(uint64_t));
			VkResult read = vkGetQueryPoolResults(device, queryPool, 0,
				(uint32_t)kFrames * 2u, (size_t)kFrames * 2u * sizeof(uint64_t),
				stamps, sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
			if (read == VK_SUCCESS && timestampBits > 0) {
				uint64_t mask = timestampBits >= 64
					? ~0ull : (1ull << timestampBits) - 1ull;
				uint64_t first = stamps[0] & mask;
				uint64_t last = stamps[(kFrames - 1) * 2 + 1] & mask;
				uint64_t ticks = last >= first
					? last - first : (mask - first) + 1ull + last;
				passMs[devicePass] = median_ms(stamps, kFrames,
					properties.limits.timestampPeriod);
				passSpanMs[devicePass] = (double)ticks
					* (double)properties.limits.timestampPeriod / 1.0e6;
				passTimed[devicePass] = 1;
			}
			free(stamps);
		}
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

	{
		VkResult waited = vkDeviceWaitIdle(device);
		printf("vkDeviceWaitIdle %d\n", (int)waited);
		check("vkDeviceWaitIdle", waited);
	}
	vkUnmapMemory(device, readMemory);
	if (draw.pixelsVram != NULL)
		vkUnmapMemory(device, readVramMemory);
	vkDestroyCommandPool(device, pool, NULL);
	if (readbackVram != VK_NULL_HANDLE)
		vkDestroyBuffer(device, readbackVram, NULL);
	if (readVramMemory != VK_NULL_HANDLE)
		vkFreeMemory(device, readVramMemory, NULL);
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
	if (queryPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(device, queryPool, NULL);
	vkDestroyDevice(device, NULL);
	free(familyInfo);
	}
	if (selftest) {
		printf("scene %s\n", cube ? "cube" : "teapot");
		printf("compared driverID %u %s and driverID %u %s\n",
			picks[0].driverKnown ? picks[0].driverID : 0,
			picks[0].properties.deviceName,
			picks[1].driverKnown ? picks[1].driverID : 0,
			picks[1].properties.deviceName);
		printf("frames %d\n", kFrames);
		int printedGpu = 0;
		int printedLvp = 0;
		for (int devicePass = 0; devicePass < pickCount; devicePass++) {
			int radv = picks[devicePass].driverKnown
				&& picks[devicePass].driverID == VK_DRIVER_ID_MESA_RADV;
			int lvp = picks[devicePass].driverKnown
				&& picks[devicePass].driverID == VK_DRIVER_ID_MESA_LLVMPIPE;
			if (radv && !printedGpu) {
				if (passTimed[devicePass]) {
					printf("radeon ms per frame %.3f\n", passMs[devicePass]);
					printf("gpu span ms %.3f cpu span ms %.3f\n",
						passSpanMs[devicePass], passCpuMs[devicePass]);
				} else
					printf("radeon ms per frame none\n");
				printedGpu = 1;
			}
			if (lvp && !printedLvp) {
				if (passTimed[devicePass]) {
					printf("lavapipe ms per frame %.3f\n", passMs[devicePass]);
					printf("gpu span ms %.3f cpu span ms %.3f\n",
						passSpanMs[devicePass], passCpuMs[devicePass]);
				} else
					printf("lavapipe ms per frame none\n");
				printedLvp = 1;
			}
		}
		if (!printedGpu)
			printf("radeon ms per frame none\n");
		if (!printedLvp)
			printf("lavapipe ms per frame none\n");
		const int sampleAt[][2] = {
			{24, 24}, {192, 80}, {192, 192}, {192, 134}, {90, 192}, {280, 192}
		};
		for (int i = 0; i < 6; i++) {
			int x = sampleAt[i][0];
			int y = sampleAt[i][1];
			const uint8_t* p = shot[0] + (y * kSize + x) * 4;
			printf("pixel %d %d %02x%02x%02x%02x\n", x, y, p[0], p[1], p[2], p[3]);
		}
		int share = 0;
		int worst = 0;
		int mismatch = r5_pixels_compare(shot[0], shot[1], (uint32_t)(kSize * kSize),
			2, &share, &worst);
		int lavapipeOnly = picks[0].driverKnown && picks[1].driverKnown
			&& picks[0].driverID == VK_DRIVER_ID_MESA_LLVMPIPE
			&& picks[1].driverID == VK_DRIVER_ID_MESA_LLVMPIPE;
		printf("pixel delta max %d\n", worst);
		if (mismatch) {
			printf("pixels MISMATCH max 2 share %d.%d%% limit 1%%\n",
				share / 10, share % 10);
			pixelStatus = 2;
		} else if (lavapipeOnly) {
			printf("pixels ok lavapipe only\n");
			printf("pixels ok max 2 share %d.%d%% limit 1%%\n",
				share / 10, share % 10);
		} else {
			printf("pixels ok max 2 share %d.%d%% limit 1%%\n",
				share / 10, share % 10);
		}
		free(shot[0]);
		free(shot[1]);
	}
	vkDestroyInstance(instance, NULL);
	free(mesh.vertices);
	free(mesh.indices);
	free(gpus);
	return pixelStatus;
}
