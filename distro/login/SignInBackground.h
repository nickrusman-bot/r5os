/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SIGN_IN_BACKGROUND_H
#define SIGN_IN_BACKGROUND_H


#include <View.h>


class BBitmap;


// The screen behind the Sign in window. What it shows is set at the top
// of SignInBackground.cpp: a picture file, or the drawn R5 waves.
class SignInBackground : public BView {
public:
								SignInBackground(BRect frame);
	virtual						~SignInBackground();

	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);

private:
			void				_Render();

			BBitmap*			fPicture;
			BBitmap*			fRendered;
};


#endif	// SIGN_IN_BACKGROUND_H
