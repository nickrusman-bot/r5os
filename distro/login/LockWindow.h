/*
 * R5OS lock screen. The session stays; this window only covers it.
 */
#ifndef R5_LOCK_WINDOW_H
#define R5_LOCK_WINDOW_H


#include <String.h>
#include <Window.h>


class LockWindow : public BWindow {
public:
							LockWindow(BRect frame, const char* name);
	virtual					~LockWindow();

	virtual	bool			QuitRequested();
	virtual	void			ScreenChanged(BRect frame, color_space mode);
};


#endif
