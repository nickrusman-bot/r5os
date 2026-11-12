/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The last copy from the back buffer to the screen. Off unless
 * gpu_app_server is set. A failure stays on the CPU copy.
 */

#ifndef GPU_COPY_H
#define GPU_COPY_H

#include <Region.h>
#include <SupportDefs.h>

class RenderingBuffer;


class GPUCopy {
public:
	static	RenderingBuffer*	BackBuffer(uint32 width, uint32 height,
									uint32 bytesPerRow);
	static	bool				Copy(const BRegion& region,
									RenderingBuffer* back,
									RenderingBuffer* front,
									clipping_rect* bandBounds = NULL);
	static	bool				TookOff();
	static	void				NoteCpu(bigtime_t microseconds);
};

#endif
