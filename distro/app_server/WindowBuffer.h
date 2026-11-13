/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * One window's buffer in the GPU mode (M11, decision 5), and the view of it
 * that Painter draws into.
 */
#ifndef WINDOW_BUFFER_H
#define WINDOW_BUFFER_H


#include <Referenceable.h>

#include "RenderingBuffer.h"


class Compositor;


/*!	The real buffer: memory the compositor reads, with its real size. Its
	pixel (0, 0) is the screen pixel (Left(), Top()) of the window it belongs
	to. Freed by the compositor when the last reference goes, which is after
	the fence of the last compose that read it.
*/
class WindowBuffer : public BReferenceable {
public:
								WindowBuffer(Compositor* owner, void* bits,
									uint32 width, uint32 height,
									uint64 vulkanBuffer, uint64 vulkanMemory,
									uint32 generation);
	virtual						~WindowBuffer();

			void*				Bits() const { return fBits; }
			uint32				Width() const { return fWidth; }
			uint32				Height() const { return fHeight; }
			uint32				BytesPerRow() const { return fWidth * 4; }
			uint64				Bytes() const
									{ return (uint64)fWidth * fHeight * 4; }
			uint64				VulkanBuffer() const { return fVulkanBuffer; }
			uint32				Generation() const { return fGeneration; }

private:
			Compositor*			fOwner;
			void*				fBits;
			uint32				fWidth;
			uint32				fHeight;
			uint64				fVulkanBuffer;
			uint64				fVulkanMemory;
			uint32				fGeneration;
};


/*!	What Painter attaches to. Bits() is where screen pixel (0, 0) would lie
	if the buffer were as large as the screen, so Painter draws in screen
	coordinates without an offset. Width() and Height() reach to the right
	and bottom edge of the window. Painter clips to its region without
	clamping it to these bounds (clip_box_naked), so the window's drawable
	regions are limited to its buffer and to x >= 0, y >= 0
	(Window::_ClipToBuffer): Painter multiplies a row by an unsigned stride
	and cannot draw at a negative coordinate. Such a part is drawn when it
	comes on screen. Nothing may clear this view over
	Height() * BytesPerRow().
*/
class WindowBufferView : public RenderingBuffer {
public:
								WindowBufferView();

			void				SetTo(WindowBuffer* buffer, int32 left,
									int32 top);

	virtual	status_t			InitCheck() const;
	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

private:
			uintptr_t			fBits;
			uint32				fBytesPerRow;
			uint32				fWidth;
			uint32				fHeight;
};


#endif	// WINDOW_BUFFER_H
