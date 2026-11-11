/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Opens the window vkcube draws into. cube.c keeps its own device choice.
 */

#include <Application.h>
#include <View.h>
#include <Window.h>

#include <pthread.h>
#include <stdlib.h>

extern "C" {
struct demo;
struct demo* demo_r5_create(void);
void demo_r5_start(struct demo* demo, void* view, int argc, char** argv);
int demo_r5_step(struct demo* demo);
void demo_r5_request_quit(struct demo* demo);
void demo_r5_cleanup(struct demo* demo);
void demo_r5_free(struct demo* demo);
}

class CubeWindow : public BWindow {
public:
	CubeWindow(struct demo* demo)
		: BWindow(BRect(80, 80, 80 + 499, 80 + 499), "vkcube",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS),
		fDemo(demo),
		fStop(false),
		fFinished(false)
	{
		AddChild(new BView(Bounds(), "vkcube", B_FOLLOW_ALL, B_WILL_DRAW));
	}

	BView* View()
	{
		return ChildAt(0);
	}

	bool Stopping()
	{
		return fStop;
	}

	void Finish()
	{
		fFinished = true;
	}

	bool QuitRequested()
	{
		fStop = true;
		demo_r5_request_quit(fDemo);
		return fFinished;
	}

private:
	struct demo* fDemo;
	volatile bool fStop;
	volatile bool fFinished;
};

struct Draw {
	CubeWindow* window;
	struct demo* demo;
	int argc;
	char** argv;
};

static void*
draw_thread(void* data)
{
	Draw* draw = (Draw*)data;
	draw->window->Lock();
	BView* view = draw->window->View();
	draw->window->Unlock();
	demo_r5_start(draw->demo, view, draw->argc, draw->argv);
	while (!draw->window->Stopping()) {
		if (demo_r5_step(draw->demo))
			break;
	}
	demo_r5_cleanup(draw->demo);
	draw->window->Finish();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return NULL;
}

int
main(int argc, char** argv)
{
	struct demo* demo = demo_r5_create();
	if (demo == NULL)
		return 1;
	BApplication app("application/x-vnd.R5OS-vkcube");
	CubeWindow* window = new CubeWindow(demo);
	window->Show();
	Draw draw;
	draw.window = window;
	draw.demo = demo;
	draw.argc = argc;
	draw.argv = argv;
	pthread_t thread;
	if (pthread_create(&thread, NULL, draw_thread, &draw) != 0)
		return 1;
	app.Run();
	pthread_join(thread, NULL);
	demo_r5_free(demo);
	return 0;
}
