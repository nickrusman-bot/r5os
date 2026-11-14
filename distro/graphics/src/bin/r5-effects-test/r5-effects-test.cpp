/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 step 4 test tool: sets the window effects from a shell, through the
 * same <WindowEffects.h> calls the Settings page uses, and opens a window
 * with or without glass for the measurement.
 *
 *   r5-effects-test get
 *   r5-effects-test shadow|frosted|animations on|off
 *   r5-effects-test strength quiet|balanced|glassy
 *   r5-effects-test workspace <index>   (step 4b: the slide)
 *   r5-effects-test glass|plain|full|tick|slide [seconds]
 *
 * glass and plain open the same window with and without set_window_glass(),
 * full a glass window over the whole screen, tick a window below them that
 * draws a moving bar every 16 ms, so there is damage under the glass.
 * slide is a plain window that moves itself across the others every 16 ms,
 * the way a drag moves it (Desktop::MoveWindowBy).
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <InterfaceDefs.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <WindowEffects.h>


using namespace BAppearanceKit;


static const uint32 kMsgTick = 'tick';
static const uint32 kMsgSlide = 'slid';


class SlideWindow : public BWindow {
public:
	SlideWindow(BRect frame)
		:
		BWindow(frame, "Slide", B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE),
		fStep(6),
		fRunner(NULL)
	{
		BMessage slide(kMsgSlide);
		fRunner = new BMessageRunner(BMessenger(this), &slide, 16000);
	}

	virtual ~SlideWindow()
	{
		delete fRunner;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgSlide) {
			BWindow::MessageReceived(message);
			return;
		}
		BRect frame = Frame();
		if (frame.left + fStep < 40 || frame.right + fStep > 900)
			fStep = -fStep;
		MoveBy(fStep, 0);
	}

private:
	float			fStep;
	BMessageRunner*	fRunner;
};


class TextView : public BView {
public:
	TextView(BRect frame, bool composite, bool tick)
		:
		BView(frame, "text", B_FOLLOW_ALL, B_WILL_DRAW),
		fComposite(composite),
		fTick(tick),
		fPosition(0),
		fRunner(NULL)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual ~TextView()
	{
		delete fRunner;
	}

	virtual void AttachedToWindow()
	{
		if (fTick) {
			BMessage tick(kMsgTick);
			fRunner = new BMessageRunner(BMessenger(this), &tick, 16000);
		}
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgTick) {
			fPosition = (fPosition + 4) % (int32)Bounds().Width();
			Invalidate();
			return;
		}
		BView::MessageReceived(message);
	}

	virtual void Draw(BRect)
	{
		// Text with alpha over glass: B_OP_ALPHA with B_ALPHA_COMPOSITE
		// writes correct alpha; B_OP_OVER would be opaque.
		if (fComposite) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		}
		SetHighColor(0, 0, 0);
		BFont font;
		GetFont(&font);
		font.SetSize(24);
		SetFont(&font);
		DrawString(fTick ? "R5 tick" : "R5 glass", BPoint(20, 40));
		FillRect(BRect(20, 60, 120, 90));
		if (fTick) {
			SetHighColor(0, 90, 200);
			FillRect(BRect(fPosition, 100, fPosition + 40, Bounds().bottom));
		}
	}

private:
	bool			fComposite;
	bool			fTick;
	int32			fPosition;
	BMessageRunner*	fRunner;
};


class TestApp : public BApplication {
public:
	TestApp(int argc, char** argv)
		:
		BApplication("application/x-vnd.R5OS-effects-test"),
		fArgc(argc),
		fArgv(argv),
		fResult(0)
	{
	}

	virtual void ReadyToRun()
	{
		fResult = _Run();
		if (fWindowSeconds <= 0)
			PostMessage(B_QUIT_REQUESTED);
	}

	int Result() const { return fResult; }

private:
	int _Run()
	{
		fWindowSeconds = 0;
		if (fArgc < 2)
			return _Usage();
		const char* what = fArgv[1];
		const char* value = fArgc > 2 ? fArgv[2] : NULL;

		if (strcmp(what, "get") == 0) {
			uint32 effects = window_effects();
			const char* names[] = { "", "quiet", "balanced", "glassy" };
			printf("shadow %s frosted %s animations %s strength %s\n",
				(effects & B_WINDOW_SHADOW) != 0 ? "on" : "off",
				(effects & B_WINDOW_FROSTED_GLASS) != 0 ? "on" : "off",
				(effects & B_WINDOW_ANIMATIONS) != 0 ? "on" : "off",
				names[window_effect_strength()]);
			return 0;
		}
		if (strcmp(what, "workspace") == 0 && value != NULL) {
			int32 index = atoi(value);
			if (count_workspaces() <= index)
				set_workspace_count(index + 1);
			activate_workspace(index);
			return 0;
		}
		if (strcmp(what, "strength") == 0 && value != NULL) {
			enum window_effect_strength strength = B_WINDOW_EFFECTS_BALANCED;
			if (strcmp(value, "quiet") == 0)
				strength = B_WINDOW_EFFECTS_QUIET;
			else if (strcmp(value, "glassy") == 0)
				strength = B_WINDOW_EFFECTS_GLASSY;
			else if (strcmp(value, "balanced") != 0)
				return _Usage();
			return _Check(set_window_effect_strength(strength));
		}
		uint32 flag = 0;
		if (strcmp(what, "shadow") == 0)
			flag = B_WINDOW_SHADOW;
		else if (strcmp(what, "frosted") == 0)
			flag = B_WINDOW_FROSTED_GLASS;
		else if (strcmp(what, "animations") == 0)
			flag = B_WINDOW_ANIMATIONS;
		if (flag != 0 && value != NULL) {
			uint32 effects = window_effects() & ~flag;
			if (strcmp(value, "on") == 0)
				effects |= flag;
			else if (strcmp(value, "off") != 0)
				return _Usage();
			return _Check(set_window_effects(effects));
		}

		bool glass = strcmp(what, "glass") == 0;
		bool full = strcmp(what, "full") == 0;
		bool tick = strcmp(what, "tick") == 0;
		bool slide = strcmp(what, "slide") == 0;
		if (!glass && !full && !tick && !slide && strcmp(what, "plain") != 0)
			return _Usage();
		fWindowSeconds = value != NULL ? atoi(value) : 30;
		if (fWindowSeconds <= 0)
			fWindowSeconds = 30;
		BRect frame(120, 120, 520, 400);
		// the glass window lies over the tick window and the desktop
		if (glass)
			frame.Set(600, 140, 1000, 420);
		if (tick)
			frame.Set(260, 220, 760, 560);
		if (slide) {
			BWindow* window = new SlideWindow(BRect(60, 180, 300, 360));
			window->AddChild(new TextView(window->Bounds(), false, false));
			window->Show();
			SetPulseRate(fWindowSeconds * 1000000LL);
			return 0;
		}
		window_look look = B_TITLED_WINDOW_LOOK;
		if (full) {
			frame = BScreen().Frame();
			look = B_NO_BORDER_WINDOW_LOOK;
		}
		BWindow* window = new BWindow(frame, full ? "Full glass"
				: glass ? "Glass" : tick ? "Tick" : "Plain", look,
			B_NORMAL_WINDOW_FEEL,
			B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS);
		TextView* view = new TextView(window->Bounds(), glass || full, tick);
		window->AddChild(view);
		window->Show();
		if (glass || full) {
			// The window's top view, and the view that fills it: a child
			// with its own colour stays opaque otherwise.
			_Check(set_window_glass(window, true));
			if (window->Lock()) {
				_Check(set_view_glass(view, true));
				window->Unlock();
			}
		}
		SetPulseRate(fWindowSeconds * 1000000LL);
		return 0;
	}

	virtual void Pulse()
	{
		PostMessage(B_QUIT_REQUESTED);
	}

	int _Check(status_t status)
	{
		if (status != B_OK) {
			fprintf(stderr, "r5-effects-test: %s\n", strerror(status));
			return 1;
		}
		return 0;
	}

	int _Usage()
	{
		fprintf(stderr, "usage: r5-effects-test get\n"
			"       r5-effects-test shadow|frosted|animations on|off\n"
			"       r5-effects-test strength quiet|balanced|glassy\n"
			"       r5-effects-test workspace <index>\n"
			"       r5-effects-test glass|plain|full|tick|slide [seconds]\n");
		return 2;
	}

	int		fArgc;
	char**	fArgv;
	int		fResult;
	int32	fWindowSeconds;
};


int
main(int argc, char** argv)
{
	TestApp app(argc, argv);
	app.Run();
	return app.Result();
}
