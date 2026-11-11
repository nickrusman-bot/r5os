/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "GPUCopy.h"

#include "GPUCopyBands.h"
#include "RenderingBuffer.h"

#include <r5_gpu.h>

#include <FindDirectory.h>
#include <GraphicsDefs.h>
#include <OS.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>


class GpuBackBuffer : public RenderingBuffer {
public:
								GpuBackBuffer(uint32 width, uint32 height,
									void* bits);
	virtual						~GpuBackBuffer();

	virtual	status_t			InitCheck() const;
	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

private:
			void*				fBits;
			uint32				fWidth;
			uint32				fHeight;
};


namespace {

int sFd = -1;
bool sLoaded = false;
bool sWant = false;
bool sForceFail = false;
bool sOff = false;
bool sLoggedOn = false;
void* sBits = NULL;
uint32 sBackHandle = 0;
uint64 sBackVa = 0;
uint32 sIbHandle = 0;
uint64 sIbVa = 0;
void* sIbBits = NULL;
const uint32 kIbBytes = 65536;

struct copy_times {
	bigtime_t	samples[128];
	int			count;
	bigtime_t	cpu;
	bigtime_t	window;
};

copy_times sCpuTimes;
copy_times sGpuTimes;


void
off(const char* reason)
{
	if (!sOff) {
		sOff = true;
		fprintf(stderr, "app_server: gpu copy off: %s\n", reason);
		syslog(LOG_INFO, "app_server: gpu copy off: %s\n", reason);
	}
}


void
load_settings()
{
	if (sLoaded)
		return;
	sLoaded = true;
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
		if (strncmp(line, "gpu_app_server true", 19) == 0)
			sWant = true;
		if (strncmp(line, "gpu_app_server_fail true", 24) == 0)
			sForceFail = true;
	}
	fclose(file);
}


bool
map_bo(uint32* handle, uint64 va, uint64 bytes, uint32 flags, void** cpu)
{
	r5_gpu_bo bo;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32)sizeof(bo);
	bo.version = 1;
	bo.domain = R5_GPU_DOMAIN_GTT;
	bo.bytes = bytes;
	if (ioctl(sFd, R5_GPU_BO_CREATE, &bo, sizeof(bo)) != 0)
		return false;
	*handle = bo.handle;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32)sizeof(bo);
	bo.version = 1;
	bo.handle = *handle;
	if (ioctl(sFd, R5_GPU_BO_MAP, &bo, sizeof(bo)) != 0)
		return false;
	area_info info;
	if (get_area_info(bo.area, &info) != B_OK)
		return false;
	*cpu = info.address;
	r5_gpu_va map;
	memset(&map, 0, sizeof(map));
	map.size = (uint32)sizeof(map);
	map.version = 1;
	map.context = 1;
	map.handle = *handle;
	map.gpu_va = va;
	map.map_bytes = (bytes + 4095ull) & ~4095ull;
	map.flags = flags;
	if (ioctl(sFd, R5_GPU_VA_MAP, &map, sizeof(map)) != 0)
		return false;
	return true;
}


bool
open_node()
{
	if (sFd >= 0)
		return true;
	if (sOff)
		return false;
	sFd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (sFd < 0) {
		off("no render node");
		return false;
	}
	r5_gpu_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.size = (uint32)sizeof(ctx);
	ctx.version = 1;
	if (ioctl(sFd, R5_GPU_CTX_CREATE, &ctx, sizeof(ctx)) != 0) {
		off("no render node");
		close(sFd);
		sFd = -1;
		return false;
	}
	if (!map_bo(&sIbHandle, 0x210000000ull, kIbBytes,
			R5_GPU_VA_READ | R5_GPU_VA_WRITE | R5_GPU_VA_EXEC, &sIbBits)) {
		off("no render node");
		return false;
	}
	sIbVa = 0x210000000ull;
	return true;
}


void
note(copy_times* times, bigtime_t microseconds)
{
	if (times->count < 128)
		times->samples[times->count++] = microseconds;
	times->cpu += microseconds;
	if (times->window == 0)
		times->window = system_time();
}

} // namespace


GpuBackBuffer::GpuBackBuffer(uint32 width, uint32 height, void* bits)
	:
	fBits(bits),
	fWidth(width),
	fHeight(height)
{
}


GpuBackBuffer::~GpuBackBuffer()
{
}


status_t
GpuBackBuffer::InitCheck() const
{
	return fBits != NULL ? B_OK : B_NO_INIT;
}


color_space
GpuBackBuffer::ColorSpace() const
{
	return B_RGBA32;
}


void*
GpuBackBuffer::Bits() const
{
	return fBits;
}


uint32
GpuBackBuffer::BytesPerRow() const
{
	return fWidth * 4;
}


uint32
GpuBackBuffer::Width() const
{
	return fWidth;
}


uint32
GpuBackBuffer::Height() const
{
	return fHeight;
}


RenderingBuffer*
GPUCopy::BackBuffer(uint32 width, uint32 height)
{
	load_settings();
	if (!sWant || sOff || width == 0 || height == 0)
		return NULL;
	uint64 bytes = (uint64)width * 4u * height;
	if (bytes > 64ull * 1024ull * 1024ull) {
		off("submit");
		return NULL;
	}
	if (!open_node())
		return NULL;
	void* bits = NULL;
	if (!map_bo(&sBackHandle, 0x200000000ull, bytes,
			R5_GPU_VA_READ | R5_GPU_VA_WRITE, &bits)) {
		off("submit");
		return NULL;
	}
	sBits = bits;
	sBackVa = 0x200000000ull;
	memset(bits, 255, (size_t)bytes);
	return new(std::nothrow) GpuBackBuffer(width, height, bits);
}


bool
GPUCopy::Copy(const BRegion& region, RenderingBuffer* back,
	RenderingBuffer* front)
{
	load_settings();
	if (!sWant || sOff || back == NULL || front == NULL)
		return false;
	if (back->Bits() != sBits || sFd < 0)
		return false;
	color_space space = front->ColorSpace();
	if (space != B_RGB32 && space != B_RGBA32) {
		off("format");
		return false;
	}
	if (front->BytesPerRow() != back->BytesPerRow()
		|| front->Width() != back->Width()
		|| front->Height() != back->Height()) {
		off("pitch");
		return false;
	}
	int rects = region.CountRects();
	if (rects > 256)
		return false;
	r5_copy_rows rows[256];
	for (int i = 0; i < rects; i++) {
		clipping_rect rect = region.RectAtInt(i);
		rows[i].top = rect.top;
		rows[i].bottom = rect.bottom;
	}
	r5_copy_band bands[64];
	int bandCount = r5_copy_bands(rows, rects, (int)back->Height(), bands, 64);
	if (bandCount < 0)
		return false;
	if (bandCount == 0)
		return true;
	r5_gpu_scanout scan;
	memset(&scan, 0, sizeof(scan));
	scan.size = (uint32)sizeof(scan);
	scan.version = 1;
	if (ioctl(sFd, R5_GPU_SCANOUT_BO, &scan, sizeof(scan)) != 0) {
		off("scanout refused");
		return false;
	}
	if (scan.bytes_per_row != back->BytesPerRow()
		|| scan.width != back->Width() || scan.height != back->Height()) {
		off("pitch");
		return false;
	}
	int words = r5_copy_bands_pm4(bands, bandCount, back->BytesPerRow(),
		sBackVa, scan.gpu_va, (uint32*)sIbBits, (int)(kIbBytes / 4));
	if (words < 0) {
		off("submit");
		return false;
	}
	if (sForceFail) {
		off("forced failure");
		return false;
	}
	r5_gpu_ib ib;
	memset(&ib, 0, sizeof(ib));
	ib.gpu_va = sIbVa;
	ib.dwords = (uint32)words;
	r5_gpu_cs cs;
	memset(&cs, 0, sizeof(cs));
	cs.size = (uint32)sizeof(cs);
	cs.version = 1;
	cs.context = 1;
	cs.ib_count = 1;
	cs.ibs = (uint64)(addr_t)&ib;
	bigtime_t started = system_time();
	if (ioctl(sFd, R5_GPU_CS_SUBMIT, &cs, sizeof(cs)) != 0) {
		off("submit");
		return false;
	}
	r5_gpu_fence fence;
	memset(&fence, 0, sizeof(fence));
	fence.size = (uint32)sizeof(fence);
	fence.version = 1;
	fence.sequence = cs.sequence;
	fence.timeout_us = 100000;
	if (ioctl(sFd, R5_GPU_FENCE_WAIT, &fence, sizeof(fence)) != 0) {
		off("fence timeout");
		return false;
	}
	note(&sGpuTimes, system_time() - started);
	if (!sLoggedOn) {
		sLoggedOn = true;
		syslog(LOG_INFO, "app_server: gpu copy on scanout %" B_PRIu32
			"x%" B_PRIu32 " pitch %" B_PRIu32 "\n",
			scan.width, scan.height, scan.bytes_per_row);
	}
	return true;
}


void
GPUCopy::NoteCpu(bigtime_t microseconds)
{
	load_settings();
	if (!sWant)
		return;
	note(&sCpuTimes, microseconds);
}
