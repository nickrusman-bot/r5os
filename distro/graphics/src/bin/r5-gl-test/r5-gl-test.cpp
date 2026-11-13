/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * OpenGL check for the driver choice in Mesa's Haiku EGL driver.
 *
 * --pbuffer <file>  draws one fixed frame into a 256 x 256 pbuffer through
 *                   libglvnd, writes it as RGBA to <file>, prints the
 *                   renderer, six sample pixels and the time per frame.
 * --selftest        runs --pbuffer twice as child processes: run A with the
 *                   inherited environment, run B with LIBGL_ALWAYS_SOFTWARE=1,
 *                   and compares the two frames (max 2 per channel, 1%).
 * --zink-cpu        with --selftest: run A gets GALLIUM_DRIVER=zink
 *                   LIBGL_ALWAYS_SOFTWARE=1 (Zink on Lavapipe).
 */

#include <Application.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// build-vkcube.sh compiles r5_pixels.c as C++, as for VulkanTeapot.
#include "r5_pixels.h"


static const int kSize = 256;
static const int kFrames = 300;
// The VulkanTeapot sample points (384 x 384), scaled to 256 x 256.
static const int kSampleAt[][2] = {
	{16, 16}, {128, 53}, {128, 128}, {128, 89}, {60, 128}, {186, 128}
};


static double
now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}


static void
draw_scene(GLuint texture)
{
	glViewport(0, 0, kSize, kSize);
	glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Two overlapping triangles. The depth test decides the overlap.
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_TRIANGLES);
	glColor3f(0.9f, 0.1f, 0.1f);
	glVertex3f(-0.9f, -0.5f, 0.5f);
	glVertex3f(0.5f, -0.5f, 0.5f);
	glVertex3f(-0.2f, 0.9f, 0.5f);
	glColor3f(0.1f, 0.8f, 0.2f);
	glVertex3f(-0.5f, -0.2f, -0.5f);
	glVertex3f(0.9f, -0.2f, -0.5f);
	glVertex3f(0.2f, 0.7f, -0.5f);
	glEnd();

	// A quad with a 4 x 4 checker texture, GL_NEAREST.
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex2f(-0.75f, -0.95f);
	glTexCoord2f(1.0f, 0.0f);
	glVertex2f(-0.25f, -0.95f);
	glTexCoord2f(1.0f, 1.0f);
	glVertex2f(-0.25f, -0.45f);
	glTexCoord2f(0.0f, 1.0f);
	glVertex2f(-0.75f, -0.45f);
	glEnd();
	glDisable(GL_TEXTURE_2D);

	// A blended quad.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.0f, 0.9f, 0.0f, 0.5f);
	glBegin(GL_QUADS);
	glVertex2f(0.1f, 0.1f);
	glVertex2f(0.6f, 0.1f);
	glVertex2f(0.6f, 0.6f);
	glVertex2f(0.1f, 0.6f);
	glEnd();
	glDisable(GL_BLEND);
}


static int
run_pbuffer(const char* file)
{
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major = 0;
	EGLint minor = 0;
	if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
		printf("gl driver FAIL eglInitialize 0x%x\n", eglGetError());
		return 1;
	}
	if (!eglBindAPI(EGL_OPENGL_API)) {
		printf("gl driver FAIL eglBindAPI 0x%x\n", eglGetError());
		return 1;
	}
	const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	EGLConfig config;
	EGLint count = 0;
	if (!eglChooseConfig(display, configAttribs, &config, 1, &count)
		|| count < 1) {
		printf("gl driver FAIL eglChooseConfig 0x%x\n", eglGetError());
		return 1;
	}
	const EGLint surfaceAttribs[] = {
		EGL_WIDTH, kSize, EGL_HEIGHT, kSize, EGL_NONE
	};
	EGLSurface surface = eglCreatePbufferSurface(display, config,
		surfaceAttribs);
	if (surface == EGL_NO_SURFACE) {
		printf("gl driver FAIL eglCreatePbufferSurface 0x%x\n", eglGetError());
		return 1;
	}
	const EGLint contextAttribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE
	};
	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT,
		contextAttribs);
	if (context == EGL_NO_CONTEXT
		|| !eglMakeCurrent(display, surface, surface, context)) {
		printf("gl driver FAIL context 0x%x\n", eglGetError());
		return 1;
	}

	printf("gl driver %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
	printf("gl version %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

	uint8_t checker[4 * 4 * 4];
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			uint8_t* p = checker + (y * 4 + x) * 4;
			bool white = ((x + y) & 1) == 0;
			p[0] = white ? 0xff : 0x20;
			p[1] = white ? 0xff : 0x20;
			p[2] = white ? 0xff : 0xc0;
			p[3] = 0xff;
		}
	}
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, checker);

	draw_scene(texture);
	glFinish();

	static uint8_t pixels[kSize * kSize * 4];
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	GLenum error = glGetError();
	if (error != GL_NO_ERROR) {
		printf("gl FAIL glReadPixels 0x%x\n", error);
		return 1;
	}
	FILE* out = fopen(file, "wb");
	if (out == NULL || fwrite(pixels, sizeof(pixels), 1, out) != 1) {
		printf("gl FAIL write %s\n", file);
		if (out != NULL)
			fclose(out);
		return 1;
	}
	fclose(out);
	for (int i = 0; i < 6; i++) {
		int x = kSampleAt[i][0];
		int y = kSampleAt[i][1];
		const uint8_t* p = pixels + (y * kSize + x) * 4;
		printf("pixel %d %d %02x%02x%02x%02x\n", x, y, p[0], p[1], p[2], p[3]);
	}

	double start = now_ms();
	for (int frame = 0; frame < kFrames; frame++) {
		draw_scene(texture);
		glFinish();
	}
	printf("gl ms per frame %.3f\n", (now_ms() - start) / kFrames);

	glDeleteTextures(1, &texture);
	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(display, context);
	eglDestroySurface(display, surface);
	eglTerminate(display);
	return 0;
}


// Runs one child, echoes its lines, and keeps its renderer name.
static int
run_child(const char* self, const char* environment, const char* file,
	char* renderer, size_t rendererSize)
{
	char command[1024];
	snprintf(command, sizeof(command), "%s '%s' --pbuffer '%s' 2>&1",
		environment, self, file);
	unlink(file);
	FILE* pipe = popen(command, "r");
	if (pipe == NULL)
		return 1;
	renderer[0] = '\0';
	char line[512];
	while (fgets(line, sizeof(line), pipe) != NULL) {
		fputs(line, stdout);
		if (strncmp(line, "gl driver ", 10) == 0) {
			strlcpy(renderer, line + 10, rendererSize);
			renderer[strcspn(renderer, "\n")] = '\0';
		}
	}
	fflush(stdout);
	return pclose(pipe) == 0 ? 0 : 1;
}


static uint8_t*
read_frame(const char* file)
{
	size_t bytes = static_cast<size_t>(kSize) * kSize * 4;
	uint8_t* frame = static_cast<uint8_t*>(malloc(bytes));
	FILE* in = fopen(file, "rb");
	if (frame == NULL || in == NULL || fread(frame, bytes, 1, in) != 1) {
		free(frame);
		frame = NULL;
	}
	if (in != NULL)
		fclose(in);
	return frame;
}


static int
run_selftest(const char* self, bool zinkCpu)
{
	// One pair of files per process: another account may have left its own.
	char fileA[64];
	char fileB[64];
	snprintf(fileA, sizeof(fileA), "/tmp/r5-gl-test-%d-a.rgba", static_cast<int>(getpid()));
	snprintf(fileB, sizeof(fileB), "/tmp/r5-gl-test-%d-b.rgba", static_cast<int>(getpid()));
	char rendererA[256];
	char rendererB[256];
	int failA = run_child(self,
		zinkCpu ? "GALLIUM_DRIVER=zink LIBGL_ALWAYS_SOFTWARE=1" : "",
		fileA, rendererA, sizeof(rendererA));
	int failB = run_child(self, "LIBGL_ALWAYS_SOFTWARE=1", fileB, rendererB,
		sizeof(rendererB));
	uint8_t* a = failA ? NULL : read_frame(fileA);
	uint8_t* b = failB ? NULL : read_frame(fileB);
	unlink(fileA);
	unlink(fileB);
	if (a == NULL || b == NULL) {
		printf("pixels FAIL run %s\n", a == NULL ? "A" : "B");
		free(a);
		free(b);
		return 1;
	}
	printf("compared %s and %s\n", rendererA, rendererB);
	int share = 0;
	int worst = 0;
	int mismatch = r5_pixels_compare(a, b, static_cast<uint32_t>(kSize * kSize),
		2,
		&share, &worst);
	free(a);
	free(b);
	printf("pixel delta max %d\n", worst);
	if (mismatch) {
		printf("pixels MISMATCH max 2 share %d.%d%% limit 1%%\n",
			share / 10, share % 10);
		return 2;
	}
	if (strncmp(rendererA, "llvmpipe", 8) == 0
		&& strncmp(rendererB, "llvmpipe", 8) == 0) {
		printf("pixels ok llvmpipe only\n");
		return 0;
	}
	printf("pixels ok max 2 share %d.%d%% limit 1%%\n", share / 10,
		share % 10);
	return 0;
}


int
main(int argc, char** argv)
{
	bool selftest = false;
	bool zinkCpu = false;
	const char* file = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--pbuffer") == 0 && i + 1 < argc)
			file = argv[++i];
		else if (strcmp(argv[i], "--selftest") == 0)
			selftest = true;
		else if (strcmp(argv[i], "--zink-cpu") == 0)
			zinkCpu = true;
		else {
			fprintf(stderr, "usage: r5-gl-test --pbuffer <file> | --selftest"
				" [--zink-cpu]\n");
			return 1;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (file != NULL) {
		// llvmpipe's display target is a BBitmap, which needs the
		// app_server connection of a BApplication. Not run.
		BApplication application("application/x-vnd.R5OS-r5-gl-test");
		return run_pbuffer(file);
	}
	if (selftest) {
		const char* self = strchr(argv[0], '/') != NULL ? argv[0]
			: "/bin/r5-gl-test";
		return run_selftest(self, zinkCpu);
	}
	fprintf(stderr, "usage: r5-gl-test --pbuffer <file> | --selftest"
		" [--zink-cpu]\n");
	return 1;
}
