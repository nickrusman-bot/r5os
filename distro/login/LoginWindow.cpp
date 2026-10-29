/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginWindow.h"

#include "LoginView.h"

#include <WindowPrivate.h>


LoginWindow::LoginWindow(BRect frame)
	:
	BWindow(frame, "R5OS", kDesktopWindowLook, kDesktopWindowFeel,
		B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE,
		B_ALL_WORKSPACES)
{
	AddChild(new LoginView());
	SetPulseRate(1000000LL);
}


LoginWindow::~LoginWindow()
{
}
