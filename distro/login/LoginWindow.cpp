/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "LoginWindow.h"

#include "LoginView.h"

#include <Font.h>
#include <LayoutBuilder.h>


LoginWindow::LoginWindow(BRect frame)
	:
	BWindow(frame, "Sign in", B_TITLED_WINDOW_LOOK, B_MODAL_ALL_WINDOW_FEEL,
		B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES)
{
	float u = be_plain_font->Size() / 13.0f;
	SetSizeLimits(440.0f * u, 560.0f * u, 380.0f * u, 540.0f * u);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(new LoginView());

	ResizeTo(440.0f * u, 400.0f * u);
	CenterOnScreen();
	SetPulseRate(1000000LL);
}


LoginWindow::~LoginWindow()
{
}


void
LoginWindow::PrepareForSignIn()
{
	LoginView* view = dynamic_cast<LoginView*>(FindView("LoginView"));
	if (view != NULL)
		view->PrepareForSignIn();
}
