/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGINVIEW_H_
#define _LOGINVIEW_H_


#include <Button.h>
#include <ListView.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>


const uint32 kUserSelected = 'usel';
const uint32 kUserInvoked = 'uinv';


class LoginView : public BView {
public:
					LoginView();
	virtual			~LoginView();
	virtual void	AttachedToWindow();
	virtual void	MessageReceived(BMessage* message);
	virtual void	Pulse();

private:
	void			_FillUsers();
	void			_ShowSelected();
	void			_AttemptLogin();
	void			_EnableControls(bool enable);

	BListView*		fUserList;
	BStringView*	fWelcomeView;
	BStringView*	fHintView;
	BTextControl*	fPasswordControl;
	BButton*		fLoginButton;
	BStringView*	fInfoView;
	BStringView*	fClockView;
	BButton*		fHaltButton;
	BButton*		fRebootButton;
	BScrollView*	fUserScroll;
	BString			fSelectedLogin;
};


#endif	// _LOGINVIEW_H_
