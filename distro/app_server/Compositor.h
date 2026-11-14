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


// M11 step 3b: one image of a Vulkan view's swapchain, from the program's
// AS_R5_SHARE_IMAGES message (docs/APP-SERVER-COMPOSITOR.md).
struct compositor_share_image {
	uint8		token[16];
	sem_id		sem;
	uint32		width;
	uint32		height;
	uint32		pitch;
	uint32		format;
};

static const uint32 kMaxShareImages = 4;


class Desktop;
class DrawingEngine;
class HWInterface;
class RenderingBuffer;
class View;
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

	// Any thread: a part of the screen needs a new compose. With a source,
	// that window changed it (its own drawing or its move): the glass of
	// the windows above it is blurred again there (M11 step 4).
			void				Damage(const BRegion& region);
			void				Damage(const BRegion& region,
									WindowHWInterface* source);

	// M11 step 4: the user's window effects (<WindowEffects.h>). The first
	// at start, the second with all windows locked.
			void				SetInitialEffects(uint32 effects,
									int32 strength);
			void				SetEffects(uint32 effects, int32 strength);
	// Whether a glass view's background is drawn with alpha 0 now.
			bool				GlassActive() const;
	// All windows locked: a view of this window was marked or unmarked.
			void				GlassViewChanged(Window* window, View* view);

	// The screen rectangle the window's buffer covers; false without a
	// buffer. Drawing is limited to it, whatever the window's regions say.
			bool				BufferBounds(Window* window,
									clipping_rect* _bounds);

	// M11 step 3b, from a ServerApp thread. ShareImages and ShareDeclined
	// with all windows locked; window is the view's window, team the
	// connection's ServerApp::ClientTeam().
			status_t			ShareImages(Window* window, int32 viewToken,
									team_id team, uint32 count,
									const compositor_share_image* images,
									int32* _shareId);
			void				ShareDeclined(Window* window);
			status_t			SharePresent(team_id team, int32 viewToken,
									int32 shareId, uint32 index);
			void				ShareEnd(team_id team, int32 viewToken,
									int32 shareId);

private:
	friend class WindowBuffer;

			struct Entry;
			struct Ghost;
			struct Layer;
			struct Share;
			struct ShareDraw;
			typedef BObjectList<Layer, true> LayerList;
			typedef BObjectList<ShareDraw, true> ShareDrawList;

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
									const LayerList& layers,
									const ShareDrawList& shares,
									bool effects);
			// M11 step 4
			bool				_PrepareEffects(BRegion& damage,
									const LayerList& layers,
									const BRegion& generic);
			bool				_EnsureShadowMask(Layer* layer);
			bool				_EnsureGlassCache(Layer* layer,
									const BRegion& glass);
			bool				_EnsureScratch();
			void				_RecordBlur(Layer* layer, uint64 frame,
									uint32 frameStride);
			void				_RecordShadow(Layer* layer,
									const BRegion& damage, uint64 frame,
									uint32 frameStride);
			void				_RecordGlass(Layer* layer,
									const BRegion& damage, uint64 frame,
									uint32 frameStride);
			void				_DropEffects();
			// M11 step 4b
			bool				_AnimationDamage(bigtime_t now);
			void				_AnimationFrame(LayerList& layers,
									bigtime_t now);
			void				_RecordWindowAnimation(uint64 buffer,
									uint32 width, uint32 height, int32 left,
									int32 top, const BRegion& shape,
									float visible, const BRegion& damage,
									uint64 frame, uint32 frameStride);
			bool				_EnsureSlideBuffers(uint32 frameStride);
			void				_RecordSlideCopy(uint64 from, uint64 to,
									uint32 frameStride);
			void				_RecordSlide(uint64 frame,
									uint32 frameStride);
			void				_Present(const BRegion& damage);
			bool				_Compare(const BRegion& damage,
									const LayerList& layers,
									uint32 background,
									const BRegion& shared);
			void				_ShareLog(Window* window, const char* reason);
			void				_ShareShape(Share* share);
			void				_ShareFrame(ShareDrawList& draws,
									BRegion& retry);
			void				_ShareFree(Share* share);
			void				_ShareRetire();
			bool				_ShareReady(uint64 sequence);
			void				_ShareRetireDone();
			void				_ScanoutFree(CompositorVulkan* vk);
			bool				_ScanoutPrepare(RenderingBuffer* back);
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
			// M11 step 3b, decision 4: gpu_compositor_scanout.
			bool				fWantScanout;
			bool				fScanoutLogged;
			// This frame's front was written by the Vulkan copy.
			bool				fFrontByVulkan;
			bigtime_t			fPresentSamples[128];
			int32				fPresentCount;
			bool				fRestarted;
			uint32				fGeneration;

			BLocker				fEntryLock;
			BObjectList<Entry, false> fEntries;

			// M11 step 3b: shared views. fShareLock is taken after the
			// window lock and fEntryLock, never before.
			BLocker				fShareLock;
			BObjectList<Share, false> fShares;
			int32				fShareCounter;
			int					fFenceFd;
			// An overtaken image still waits for its mark (written under
			// fShareLock, read by the compose thread without it).
	volatile bool				fShareRetiring;

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

			// M11 step 4: written with all windows locked, read by the
			// compose thread once per frame.
	volatile uint32				fEffects;
	volatile int32				fStrength;
			BRegion				fGenericDamage;
			bool				fEffectsLogged;

			// M11 step 4b: the animations. fAnimLock is taken after the
			// window lock, fEntryLock and fLayerLock, never before.
			BLocker				fAnimLock;
			BObjectList<Ghost, true> fGhosts;
			int32				fLastWorkspace;
			bool				fSlideActive;
			bool				fSlideSnapshot;
			bigtime_t			fSlideStart;
			int32				fSlideDirection;
			int32				fSlideFrames;
	volatile bool				fAnimationsRunning;
			// This frame's, the compose thread's own
			LayerList*			fFrameGhosts;
			bool				fFrameAnimating;
			bool				fFrameSlide;
			bool				fFrameSlideSnapshot;
			int32				fFrameSlideShift;
};


#endif	// COMPOSITOR_H
