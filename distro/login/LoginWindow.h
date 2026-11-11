/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGINWINDOW_H_
#define _LOGINWINDOW_H_


#include <Window.h>


class LoginWindow : public BWindow {
public:
								LoginWindow(BRect frame);
	virtual						~LoginWindow();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	virtual	void				ScreenChanged(BRect frame, color_space mode);
// SPDX-SnippetEnd
			void				PrepareForSignIn();
};


#endif	// _LOGINWINDOW_H_
