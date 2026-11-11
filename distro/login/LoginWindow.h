/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _LOGINWINDOW_H_
#define _LOGINWINDOW_H_


#include <Window.h>


class LoginWindow : public BWindow {
public:
								LoginWindow(BRect frame);
	virtual						~LoginWindow();

	virtual	void				ScreenChanged(BRect frame, color_space mode);
			void				PrepareForSignIn();
};


#endif	// _LOGINWINDOW_H_
