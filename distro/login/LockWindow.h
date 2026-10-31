/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * R5OS lock screen. The session stays; this window only covers it.
 */
#ifndef R5_LOCK_WINDOW_H
#define R5_LOCK_WINDOW_H


#include <String.h>
#include <Window.h>


class LockWindow : public BWindow {
public:
							LockWindow(const char* name);
	virtual					~LockWindow();

	virtual	bool			QuitRequested();
	virtual	void			ScreenChanged(BRect frame, color_space mode);

private:
			BWindow*		fCover;
};


#endif
