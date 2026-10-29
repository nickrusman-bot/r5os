/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginWindow.h"

#include "LoginView.h"


LoginWindow::LoginWindow(BRect frame)
	:
	BWindow(frame, "Sign in", B_TITLED_WINDOW_LOOK, B_MODAL_ALL_WINDOW_FEEL,
		B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES)
{
	AddChild(new LoginView());
	SetPulseRate(1000000LL);
	CenterOnScreen();
}


LoginWindow::~LoginWindow()
{
}
