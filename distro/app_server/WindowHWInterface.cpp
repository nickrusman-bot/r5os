/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "WindowHWInterface.h"

#include "Compositor.h"


WindowHWInterface::WindowHWInterface(Compositor* compositor)
	:
	HWInterface(),
	fCompositor(compositor),
	fLeft(0),
	fTop(0)
{
}


WindowHWInterface::~WindowHWInterface()
{
}


status_t
WindowHWInterface::Shutdown()
{
	return B_OK;
}


status_t
WindowHWInterface::SetMode(const display_mode& mode)
{
	return B_UNSUPPORTED;
}


void
WindowHWInterface::GetMode(display_mode* mode)
{
}


status_t
WindowHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	return B_ERROR;
}


status_t
WindowHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	return B_ERROR;
}


status_t
WindowHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low,
	uint32* _high)
{
	return B_ERROR;
}


status_t
WindowHWInterface::GetTimingConstraints(
	display_timing_constraints* constraints)
{
	return B_ERROR;
}


status_t
WindowHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* low, const display_mode* high)
{
	return B_ERROR;
}


sem_id
WindowHWInterface::RetraceSemaphore()
{
	return B_ERROR;
}


status_t
WindowHWInterface::WaitForRetrace(bigtime_t timeout)
{
	return B_ERROR;
}


status_t
WindowHWInterface::SetDPMSMode(uint32 state)
{
	return B_ERROR;
}


uint32
WindowHWInterface::DPMSMode()
{
	return 0;
}


uint32
WindowHWInterface::DPMSCapabilities()
{
	return 0;
}


status_t
WindowHWInterface::SetBrightness(float)
{
	return B_UNSUPPORTED;
}


status_t
WindowHWInterface::GetBrightness(float*)
{
	return B_UNSUPPORTED;
}


RenderingBuffer*
WindowHWInterface::FrontBuffer() const
{
	if (!fBuffer.IsSet())
		return NULL;
	return const_cast<WindowBufferView*>(&fView);
}


RenderingBuffer*
WindowHWInterface::BackBuffer() const
{
	return NULL;
}


bool
WindowHWInterface::IsDoubleBuffered() const
{
	return false;
}


status_t
WindowHWInterface::InvalidateRegion(const BRegion& region)
{
	fCompositor->Damage(region, this);
	return B_OK;
}


status_t
WindowHWInterface::Invalidate(const BRect& frame)
{
	fCompositor->Damage(BRegion(frame), this);
	return B_OK;
}


void
WindowHWInterface::SetBuffer(WindowBuffer* buffer, int32 left, int32 top)
{
	if (WriteLock()) {
		fBuffer.SetTo(buffer);
		fLeft = left;
		fTop = top;
		fView.SetTo(buffer, left, top);
		WriteUnlock();
	}

	// Painter attaches to the moved view; nothing is redrawn.
	if (buffer != NULL)
		_NotifyFrameBufferChanged();
}
