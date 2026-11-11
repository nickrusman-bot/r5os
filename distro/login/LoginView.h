/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _LOGINVIEW_H_
#define _LOGINVIEW_H_


#include <Button.h>
#include <GroupView.h>
#include <ListView.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <View.h>

#include <Card.h>
#include <PasswordControl.h>
#include <UserCard.h>
#include <UserListItem.h>


const uint32 kUserSelected = 'usel';
const uint32 kUserInvoked = 'uinv';
const uint32 kCardSelected = 'csel';


class BBitmap;


class LoginView : public BView {
public:
								LoginView();
	virtual						~LoginView();

	virtual void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);
	virtual void				Pulse();
	void						PrepareForSignIn();

private:
	void						_FillUsers();
	void						_ShowSelected(const char* login,
									const char* displayName,
									const char* role);
	void						_AttemptLogin();
	void						_EnableControls(bool enable);

	BAppearanceKit::BCard*		fHeaderCard;
	BStringView*				fWelcomeView;
	BStringView*				fSubtitleView;

	BGroupView*					fUserSection;
	BStringView*				fSelectPersonLabel;
	BGroupView*					fUserCardsRow;
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
