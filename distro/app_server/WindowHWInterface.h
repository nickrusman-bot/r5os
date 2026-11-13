/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The HWInterface of one window in the GPU mode. Like BitmapHWInterface,
 * but its drawing buffer is the window buffer, and an invalidate reports
 * damage to the compositor instead of copying to the screen.
 */
#ifndef WINDOW_HW_INTERFACE_H
#define WINDOW_HW_INTERFACE_H


#include <Referenceable.h>

#include <Region.h>

#include "HWInterface.h"
#include "WindowBuffer.h"


class Compositor;


class WindowHWInterface : public HWInterface {
public:
								WindowHWInterface(Compositor* compositor);
	virtual						~WindowHWInterface();

	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);
	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);
	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);
	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();
	virtual status_t			SetBrightness(float);
	virtual status_t			GetBrightness(float*);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			InvalidateRegion(const BRegion& region);
	virtual	status_t			Invalidate(const BRect& frame);

	// Desktop thread, with all windows locked.
			void				SetBuffer(WindowBuffer* buffer, int32 left,
									int32 top);
			WindowBuffer*		Buffer() const { return fBuffer.Get(); }
			int32				Left() const { return fLeft; }
			int32				Top() const { return fTop; }

	// M11 step 4: what this window changed since the last compose, for
	// the glass of the windows above it. Under the compositor's damage
	// lock.
			BRegion&			ContentDamage() { return fContentDamage; }

private:
			Compositor*			fCompositor;
			BReference<WindowBuffer> fBuffer;
			int32				fLeft;
			int32				fTop;
			WindowBufferView	fView;
			BRegion				fContentDamage;
};


#endif	// WINDOW_HW_INTERFACE_H
