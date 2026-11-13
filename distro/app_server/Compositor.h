/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The compositor in app_server (M11 A1, docs/APP-SERVER-COMPOSITOR.md).
 * Each window draws into its own buffer; a compose thread puts them on
 * the screen with Vulkan (RADV, or Lavapipe without a GPU). Off unless
 * gpu_compositor is set in the radeon_hd settings.
 */
#ifndef COMPOSITOR_H
#define COMPOSITOR_H


#include <GraphicsDefs.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <Referenceable.h>
#include <Region.h>


class Desktop;
class DrawingEngine;
class HWInterface;
class Window;
class WindowBuffer;
class WindowHWInterface;
struct CompositorVulkan;


class Compositor {
public:
	// NULL when gpu_compositor is off, or when no Vulkan device starts:
	// the desktop then runs in the CPU mode.
	static	Compositor*			Create(Desktop* desktop, HWInterface* screen);
								~Compositor();

			bool				IsActive() const { return fActive; }

	// ServerWindow::_CreateWindow: the engine of a new window.
			DrawingEngine*		CreateWindowEngine(WindowHWInterface** _iface);
			void				AddWindow(Window* window,
									WindowHWInterface* iface);
	// Window::~Window, after its engine is gone.
			void				RemoveWindow(Window* window);

	// Desktop thread, all windows locked.
			void				ClippingChanged();
			void				SetBackgroundColor(rgb_color color);

	// Any thread: a part of the screen needs a new compose.
			void				Damage(const BRegion& region);

	// The screen rectangle the window's buffer covers; false without a
	// buffer. Drawing is limited to it, whatever the window's regions say.
			bool				BufferBounds(Window* window,
									clipping_rect* _bounds);

private:
	friend class WindowBuffer;

			struct Entry;
			struct Layer;
			typedef BObjectList<Layer, true> LayerList;

								Compositor(Desktop* desktop,
									HWInterface* screen);

			status_t			_Init();
			status_t			_InitVulkan(bool radv, const char** reason);
			void				_ExitVulkan();
			WindowBuffer*		_AllocateBuffer(uint32 width, uint32 height);
			void				_FreeBuffer(WindowBuffer* buffer,
									uint64 vulkanBuffer, uint64 vulkanMemory);
			bool				_EnsureFrame(uint32 width, uint32 height);
			WindowBuffer*		_AllocateOrMakeRoom(uint32 width,
									uint32 height, const Entry* keep);
			Entry*				_FindEntry(Window* window) const;

	static	status_t			_ComposeThread(void* data);
			void				_ComposeLoop();
			bool				_Compose(const BRegion& damage,
									const LayerList& layers);
			void				_Present(const BRegion& damage);
			bool				_Compare(const BRegion& damage,
									const LayerList& layers,
									uint32 background);
			void				_TurnOff(const char* reason);
			bool				_RestartAfterReset();
			void				_Report();
			void				_UpdatePeriod();

private:
			Desktop*			fDesktop;
			HWInterface*		fScreen;
			CompositorVulkan*	fVulkan;
			bool				fActive;
			bool				fOnLavapipe;
			bool				fDebug;
			bool				fForceFail;
			bool				fRestarted;
			uint32				fGeneration;

			BLocker				fEntryLock;
			BObjectList<Entry, false> fEntries;

			BLocker				fLayerLock;
			LayerList*			fLayers;
			uint32				fBackground;

			BLocker				fDamageLock;
			BRegion				fDamage;
			bool				fWakePending;
			sem_id				fWakeSem;
			thread_id			fThread;
			bool				fQuit;
			bool				fOutOfMemory;

			bigtime_t			fPeriod;
			bigtime_t			fLastCompose;
			uint32				fFrameWidth;
			uint32				fFrameHeight;
			uint32*				fScratch;

			uint64				fWindowBytes;
			uint64				fBudget;
			uint32				fFrames;
			bigtime_t			fSamples[128];
			int32				fSampleCount;
			bigtime_t			fReportStart;
			bigtime_t			fCpuAtReport;
			bigtime_t			fLateLogged;
};


#endif	// COMPOSITOR_H
