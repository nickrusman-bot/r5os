/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "GPUCopy.h"

#include "GPUCopyBands.h"
#include "RenderingBuffer.h"

#include <r5_gpu.h>

#include <FindDirectory.h>
#include <GraphicsDefs.h>
#include <OS.h>

#include <new>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/ioctl.h>


class GpuBackBuffer : public RenderingBuffer {
public:
								GpuBackBuffer(uint32 width, uint32 height,
									uint32 bytesPerRow, void* bits);
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
			uint32				fBytesPerRow;
};


namespace {

int sFd = -1;
bool sLoaded = false;
bool sWant = false;
bool sForceFail = false;
bool sDebug = false;
bool sOff = false;
bool sTookOff = false;
const char* sOffReason = NULL;
char sCompareDetail[192];
bool sLoggedOn = false;
bool sCompared = false;
uint32 sFrame = 0;
void* sBits = NULL;
uint32 sBackHandle = 0;
uint64 sBackVa = 0;
uint32 sIbHandle = 0;
uint64 sIbVa = 0;
void* sIbBits = NULL;
uint32 sScanHandle = 0;
uint64 sScanVa = 0;
uint32 sScanWidth = 0;
uint32 sScanHeight = 0;
uint32 sScanPitch = 0;
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
		sTookOff = true;
		sOffReason = reason;
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
		if (strncmp(line, "gpu_copy_debug true", 19) == 0)
			sDebug = true;
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


bigtime_t
median_of(const copy_times* times)
{
	if (times->count <= 0)
		return 0;
	bigtime_t sorted[128];
	for (int i = 0; i < times->count; i++)
		sorted[i] = times->samples[i];
	for (int i = 1; i < times->count; i++) {
		bigtime_t value = sorted[i];
		int j = i;
		while (j > 0 && sorted[j - 1] > value) {
			sorted[j] = sorted[j - 1];
			j--;
		}
		sorted[j] = value;
	}
	return sorted[times->count / 2];
}


void
report()
{
	if (!sDebug)
		return;
	bigtime_t start = sCpuTimes.window;
	if (start == 0 || (sGpuTimes.window != 0 && sGpuTimes.window < start))
		start = sGpuTimes.window;
	if (start == 0 || system_time() - start < 10000000)
		return;
	if (sCpuTimes.count > 0) {
		if (sOff) {
			syslog(LOG_INFO, "app_server: copy cpu median %" B_PRId64
				" us cpu %" B_PRId64
				" ms per 10 s, gpu copy off: %s%s%s\n",
				(int64)median_of(&sCpuTimes),
				(int64)(sCpuTimes.cpu / 1000),
				sOffReason != NULL ? sOffReason : "unknown",
				sCompareDetail[0] != '\0' ? " " : "",
				sCompareDetail);
		} else {
			syslog(LOG_INFO, "app_server: copy cpu median %" B_PRId64
				" us cpu %" B_PRId64 " ms per 10 s\n",
				(int64)median_of(&sCpuTimes), (int64)(sCpuTimes.cpu / 1000));
		}
	}
	if (sGpuTimes.count > 0) {
		syslog(LOG_INFO, "app_server: copy gpu median %" B_PRId64
			" us cpu %" B_PRId64 " ms per 10 s\n",
			(int64)median_of(&sGpuTimes), (int64)(sGpuTimes.cpu / 1000));
	}
	if (sCompared)
		syslog(LOG_INFO, "app_server: copy compare ok\n");
	sCpuTimes.count = 0;
	sCpuTimes.cpu = 0;
	sCpuTimes.window = 0;
	sGpuTimes.count = 0;
	sGpuTimes.cpu = 0;
	sGpuTimes.window = 0;
	sCompared = false;
}


void
flush_front(const void* address, size_t bytes)
{
#if defined(__x86_64__)
	uintptr_t start = (uintptr_t)address & ~(uintptr_t)63;
	uintptr_t end = ((uintptr_t)address + bytes + 63u) & ~(uintptr_t)63;
	for (uintptr_t line = start; line < end; line += 64)
		asm volatile("clflush (%0)" :: "r"((void*)line) : "memory");
	asm volatile("sfence" ::: "memory");
#else
	(void)address;
	(void)bytes;
#endif
}


void
note_mismatch(uint32 y, uint32 byte, const uint8* back, const uint8* front,
	uint32 rowBytes, int32 left, int32 top, int32 right, int32 bottom)
{
	uint32 n = 0;
	if (byte < rowBytes)
		n = rowBytes - byte;
	if (n > 8u)
		n = 8u;
	int used = snprintf(sCompareDetail, sizeof(sCompareDetail),
		"at %" B_PRIu32 " byte %" B_PRIu32 " back", y, byte);
	if (used < 0 || (size_t)used >= sizeof(sCompareDetail)) {
		sCompareDetail[0] = '\0';
		return;
	}
	for (uint32 i = 0; i < n && (size_t)used + 3 < sizeof(sCompareDetail); i++)
		used += snprintf(sCompareDetail + used, sizeof(sCompareDetail) - used,
			" %02x", back[byte + i]);
	if ((size_t)used + 7 < sizeof(sCompareDetail))
		used += snprintf(sCompareDetail + used, sizeof(sCompareDetail) - used,
			" front");
	for (uint32 i = 0; i < n && (size_t)used + 3 < sizeof(sCompareDetail); i++)
		used += snprintf(sCompareDetail + used, sizeof(sCompareDetail) - used,
			" %02x", front[byte + i]);
	if ((size_t)used + 16 < sizeof(sCompareDetail)) {
		snprintf(sCompareDetail + used, sizeof(sCompareDetail) - used,
			" in rect %" B_PRId32 ",%" B_PRId32 "-%" B_PRId32 ",%" B_PRId32,
			left, top, right, bottom);
	}
}


bool
same_picture(RenderingBuffer* back, RenderingBuffer* front,
	const BRegion& region)
{
	const uint8* src = (const uint8*)back->Bits();
	const uint8* dst = (const uint8*)front->Bits();
	uint32 pitch = back->BytesPerRow();
	uint32 rowBytes = back->Width() * 4u;
	if (src == NULL || dst == NULL || front->BytesPerRow() != pitch
		|| rowBytes == 0 || rowBytes > pitch)
		return false;
	int rects = region.CountRects();
	for (int i = 0; i < rects; i++) {
		clipping_rect rect = region.RectAtInt(i);
		int32 left = rect.left;
		int32 right = rect.right;
		int32 top = rect.top;
		int32 bottom = rect.bottom;
		if (left < 0)
			left = 0;
		if (top < 0)
			top = 0;
		if (right >= (int32)back->Width())
			right = (int32)back->Width() - 1;
		if (bottom >= (int32)back->Height())
			bottom = (int32)back->Height() - 1;
		if (left > right || top > bottom)
			continue;
		uint32 byte0 = (uint32)left * 4u;
		uint32 span = (uint32)(right - left + 1) * 4u;
		if (byte0 >= rowBytes || span > rowBytes - byte0)
			continue;
		for (int32 y = top; y <= bottom; y++) {
			size_t offset = (size_t)y * pitch + byte0;
			const uint8* srcRow = src + offset;
			const uint8* dstRow = dst + offset;
			flush_front(dstRow, span);
			if (memcmp(srcRow, dstRow, span) != 0) {
				uint32 byte = 0;
				while (byte < span && srcRow[byte] == dstRow[byte])
					byte++;
				note_mismatch((uint32)y, byte0 + byte, src + (size_t)y * pitch,
					dst + (size_t)y * pitch, rowBytes, rect.left, rect.top,
					rect.right, rect.bottom);
				return false;
			}
		}
	}
	return true;
}

} // namespace


GpuBackBuffer::GpuBackBuffer(uint32 width, uint32 height,
	uint32 bytesPerRow, void* bits)
	:
	fBits(bits),
	fWidth(width),
	fHeight(height),
	fBytesPerRow(bytesPerRow)
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
	return fBytesPerRow;
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
GPUCopy::BackBuffer(uint32 width, uint32 height, uint32 bytesPerRow)
{
	load_settings();
	if (!sWant || sOff || width == 0 || height == 0)
		return NULL;
	if (bytesPerRow < width * 4u || (bytesPerRow & 3u) != 0) {
		off("pitch");
		return NULL;
	}
	uint64 bytes = (uint64)bytesPerRow * height;
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
	return new(std::nothrow) GpuBackBuffer(width, height, bytesPerRow, bits);
}


bool
GPUCopy::TookOff()
{
	if (!sTookOff)
		return false;
	sTookOff = false;
	return true;
}


bool
GPUCopy::Copy(const BRegion& region, RenderingBuffer* back,
	RenderingBuffer* front, clipping_rect* bandBounds)
{
	if (bandBounds != NULL) {
		bandBounds->left = 0;
		bandBounds->top = 0;
		bandBounds->right = -1;
		bandBounds->bottom = -1;
	}
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
	if (sScanHandle != 0 && (sScanWidth != back->Width()
			|| sScanHeight != back->Height()
			|| sScanPitch != back->BytesPerRow())) {
		r5_gpu_bo gone;
		memset(&gone, 0, sizeof(gone));
		gone.size = (uint32)sizeof(gone);
		gone.version = 1;
		gone.handle = sScanHandle;
		ioctl(sFd, R5_GPU_BO_FREE, &gone, sizeof(gone));
		sScanHandle = 0;
	}
	r5_gpu_scanout scan;
	memset(&scan, 0, sizeof(scan));
	if (sScanHandle == 0) {
		scan.size = (uint32)sizeof(scan);
		scan.version = 1;
		if (ioctl(sFd, R5_GPU_SCANOUT_BO, &scan, sizeof(scan)) != 0) {
			off("scanout refused");
			return false;
		}
		sScanHandle = scan.handle;
		sScanVa = scan.gpu_va;
		sScanWidth = scan.width;
		sScanHeight = scan.height;
		sScanPitch = scan.bytes_per_row;
	} else {
		scan.width = sScanWidth;
		scan.height = sScanHeight;
		scan.bytes_per_row = sScanPitch;
		scan.gpu_va = sScanVa;
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
	if (sDebug) {
		sFrame++;
		if (sFrame == 1 || sFrame % 100u == 0) {
			if (!same_picture(back, front, region)) {
				syslog(LOG_INFO, "app_server: copy compare MISMATCH %s\n",
					sCompareDetail);
				off("compare");
				return false;
			}
			sCompared = true;
		}
	}
	report();
	if (!sLoggedOn) {
		sLoggedOn = true;
		syslog(LOG_INFO, "app_server: gpu copy on scanout %" B_PRIu32
			"x%" B_PRIu32 " pitch %" B_PRIu32 "\n",
			scan.width, scan.height, scan.bytes_per_row);
	}
	if (bandBounds != NULL && back->Width() > 0) {
		int32 top = (int32)bands[0].y;
		int32 bottom = top + (int32)bands[0].rows - 1;
		for (int i = 1; i < bandCount; i++) {
			int32 bandTop = (int32)bands[i].y;
			int32 bandBottom = bandTop + (int32)bands[i].rows - 1;
			if (bandTop < top)
				top = bandTop;
			if (bandBottom > bottom)
				bottom = bandBottom;
		}
		bandBounds->left = 0;
		bandBounds->top = top;
		bandBounds->right = (int32)back->Width() - 1;
		bandBounds->bottom = bottom;
	}
	return true;
}


void
GPUCopy::NoteCpu(bigtime_t microseconds)
{
	load_settings();
	if (!sDebug)
		return;
	note(&sCpuTimes, microseconds);
	report();
}
