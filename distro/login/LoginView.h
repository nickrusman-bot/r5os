/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _LOGINVIEW_H_
#define _LOGINVIEW_H_


#include <String.h>
#include <View.h>


class BBitmap;
class BButton;
class BGridView;
class BGroupView;
class BListView;
class BScrollView;
class BStringView;


namespace BAppearanceKit {
class BCard;
class BPasswordControl;
}


const uint32 kUserSelected = 'usel';
const uint32 kUserInvoked = 'uinv';
const uint32 kCardSelected = 'csel';


class LoginView : public BView {
public:
								LoginView();
	virtual						~LoginView();

	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Pulse();
			void				PrepareForSignIn();

private:
			void				_FillUsers();
			void				_ShowSelected(const char* login,
									const char* displayName,
									const char* role);
			void				_AttemptLogin();
			void				_EnableControls(bool enable);
			void				_ApplyColors();
			void				_SetVisible(BView* view, bool visible);

	BAppearanceKit::BCard*		fHeaderCard;
	BStringView*				fWelcomeView;
	BStringView*				fSubtitleView;

	BGroupView*					fUserSection;
	BStringView*				fSelectPersonLabel;
	BGridView*					fUserCardsGrid;
	BListView*					fUserList;
	BScrollView*				fUserScroll;

	BAppearanceKit::BCard*		fFormCard;
	BStringView*				fActiveUserLabel;
	BStringView*				fActiveRoleLabel;
	BStringView*				fPasswordLabel;
	BAppearanceKit::BPasswordControl* fPasswordControl;
	BButton*					fLoginButton;
	BStringView*				fInfoView;

	BStringView*				fClockView;
	BButton*					fRebootButton;
	BButton*					fHaltButton;

	BBitmap*					fPersonAvatar;
	BString						fSelectedLogin;
};


#endif	// _LOGINVIEW_H_
