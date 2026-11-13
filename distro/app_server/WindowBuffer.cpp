/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "WindowBuffer.h"

#include "Compositor.h"


WindowBuffer::WindowBuffer(Compositor* owner, void* bits, uint32 width,
	uint32 height, uint64 vulkanBuffer, uint64 vulkanMemory, uint32 generation)
	:
	fOwner(owner),
	fBits(bits),
	fWidth(width),
	fHeight(height),
	fVulkanBuffer(vulkanBuffer),
	fVulkanMemory(vulkanMemory),
	fGeneration(generation)
{
}


WindowBuffer::~WindowBuffer()
{
	fOwner->_FreeBuffer(this, fVulkanBuffer, fVulkanMemory);
}


//	#pragma mark - WindowBufferView


WindowBufferView::WindowBufferView()
	:
	fBits(0),
	fBytesPerRow(0),
	fWidth(0),
	fHeight(0)
{
}


void
WindowBufferView::SetTo(WindowBuffer* buffer, int32 left, int32 top)
{
	if (buffer == NULL) {
		fBits = 0;
		fBytesPerRow = 0;
		fWidth = 0;
		fHeight = 0;
		return;
	}

	// The address of screen pixel (0, 0). Integer arithmetic, so the
	// compiler makes no assumption about a pointer outside the buffer.
	fBytesPerRow = buffer->BytesPerRow();
	fBits = (uintptr_t)buffer->Bits() - (intptr_t)top * (intptr_t)fBytesPerRow
		- (intptr_t)left * 4;

	// Never invalid while there is a buffer: Painter ignores an invalid
	// buffer and keeps its previous one, which would then be written at the
	// new coordinates. A window entirely left of or above the screen (Deskbar
	// starts at -1000, -1000) still gets a view of at least one pixel.
	int64 right = (int64)left + buffer->Width();
	int64 bottom = (int64)top + buffer->Height();
	fWidth = right > 0 ? (uint32)right : 1;
	fHeight = bottom > 0 ? (uint32)bottom : 1;
}


status_t
WindowBufferView::InitCheck() const
{
	return fBits != 0 && fWidth > 0 && fHeight > 0 ? B_OK : B_NO_INIT;
}


color_space
WindowBufferView::ColorSpace() const
{
	return B_RGBA32;
}


void*
WindowBufferView::Bits() const
{
	return (void*)fBits;
}


uint32
WindowBufferView::BytesPerRow() const
{
	return fBytesPerRow;
}


uint32
WindowBufferView::Width() const
{
	return fWidth;
}


uint32
WindowBufferView::Height() const
{
	return fHeight;
}
