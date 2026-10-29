/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginView.h"

#include "LoginApp.h"

#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <String.h>
#include <StringItem.h>
#include <Window.h>

#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <time.h>


static bool
is_listed_account(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL || pwd->pw_name[0] == '\0')
		return false;
	if (strcmp(pwd->pw_name, "sshd") == 0
		|| strcmp(pwd->pw_name, "ftp") == 0
		|| strcmp(pwd->pw_name, "nobody") == 0)
		return false;
	if (pwd->pw_shell != NULL
		&& (strcmp(pwd->pw_shell, "false") == 0
			|| strcmp(pwd->pw_shell, "true") == 0
			|| strcmp(pwd->pw_shell, "/bin/false") == 0
			|| strcmp(pwd->pw_shell, "/bin/true") == 0))
		return false;

	const char* home = pwd->pw_dir;
	if (home == NULL || home[0] == '\0')
		return false;
	if (strcmp(home, "/boot/home") == 0)
		return true;
	if (strncmp(home, "/boot/home/", 11) == 0)
		return true;
	return false;
}


static bool
is_disabled_user(const char* name)
{
	struct spwd* shadow = getspnam(name);
	if (shadow == NULL || shadow->sp_pwdp == NULL)
		return false;
	return shadow->sp_pwdp[0] == '!';
}


class UserItem : public BStringItem {
public:
	UserItem(const char* text, const char* login)
		:
		BStringItem(text),
		fLogin(login)
	{
	}

	const char*		Login() const	{ return fLogin.String(); }

private:
	BString	fLogin;
};


LoginView::LoginView()
	:
	BView("LoginView", B_WILL_DRAW | B_PULSE_NEEDED),
	fUserList(NULL),
	fNameView(NULL),
	fLoginView(NULL),
	fPasswordControl(NULL),
	fLoginButton(NULL),
	fInfoView(NULL),
	fClockView(NULL),
	fHaltButton(NULL),
	fRebootButton(NULL),
	fUserScroll(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fUserList = new BListView("users");
	fUserList->SetSelectionMessage(new BMessage(kUserSelected));
	fUserList->SetInvocationMessage(new BMessage(kUserInvoked));
	fUserList->SetExplicitMinSize(BSize(240, 48));
	fUserList->SetExplicitMaxSize(BSize(280, 120));
	fUserScroll = new BScrollView("userssv", fUserList, 0, false, true);

	fNameView = new BStringView("display", "R5OS");
	BFont nameFont(be_bold_font);
	nameFont.SetSize(be_plain_font->Size() * 1.6f);
	fNameView->SetFont(&nameFont);
	fNameView->SetAlignment(B_ALIGN_CENTER);
	fNameView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fLoginView = new BStringView("loginName", "");
	fLoginView->SetAlignment(B_ALIGN_CENTER);
	fLoginView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fPasswordControl = new BTextControl("password", "Password", "", NULL);
	fPasswordControl->TextView()->HideTyping(true);

	fLoginButton = new BButton("signin", "Sign in",
		new BMessage(kAttemptLogin));

	fInfoView = new BStringView("info", "");
	fInfoView->SetAlignment(B_ALIGN_CENTER);
	fInfoView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fClockView = new BStringView("clock", "");
	fHaltButton = new BButton("halt", "Shut down", new BMessage(kHaltAction));
	fRebootButton = new BButton("reboot", "Restart",
		new BMessage(kRebootAction));

	BGroupView* people = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	people->SetName("people");
	BLayoutBuilder::Group<>(people)
		.Add(fUserScroll)
		.Add(fNameView)
		.Add(fLoginView)
		.Add(fPasswordControl)
		.Add(fLoginButton)
		.Add(fInfoView);
	people->SetExplicitMaxSize(BSize(280, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(people)
			.AddGlue()
		.End()
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.Add(fClockView)
			.AddGlue()
			.Add(fRebootButton)
			.Add(fHaltButton)
		.End();
}


LoginView::~LoginView()
{
}


void
LoginView::AttachedToWindow()
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fUserList->SetTarget(this);
	fPasswordControl->SetTarget(this);
	fLoginButton->SetTarget(this);
	fHaltButton->SetTarget(be_app_messenger);
	fRebootButton->SetTarget(be_app_messenger);
	Window()->SetDefaultButton(fLoginButton);

	_FillUsers();
	Pulse();
	fPasswordControl->MakeFocus(true);
}


void
LoginView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kUserSelected:
		case kUserInvoked:
			_ShowSelected();
			if (message->what == kUserInvoked)
				fPasswordControl->MakeFocus(true);
			break;
		case kAttemptLogin:
			_AttemptLogin();
			break;
		case kLoginBad:
		{
			const char* text = NULL;
			if (message->FindString("text", &text) != B_OK || text == NULL)
				text = "That password is not right.";
			fPasswordControl->SetText("");
			fInfoView->SetText(text);
			_EnableControls(true);
			fPasswordControl->MakeFocus(true);
			break;
		}
		case kLoginOk:
			if (Window() != NULL)
				Window()->Hide();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
LoginView::Pulse()
{
	time_t now = time(NULL);
	struct tm local;
	localtime_r(&now, &local);
	char buffer[64];
	strftime(buffer, sizeof(buffer), "%H:%M", &local);
	fClockView->SetText(buffer);
}


void
LoginView::_FillUsers()
{
	fUserList->MakeEmpty();
	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_listed_account(pwd) || is_disabled_user(pwd->pw_name))
			continue;

		const char* gecos = pwd->pw_gecos != NULL ? pwd->pw_gecos : "";
		const char* label = gecos[0] != '\0' ? gecos : pwd->pw_name;
		fUserList->AddItem(new UserItem(label, pwd->pw_name));
	}
	endpwent();

	if (fUserList->CountItems() <= 1 && fUserScroll != NULL)
		fUserScroll->Hide();
	if (fUserList->CountItems() > 0)
		fUserList->Select(0);
	_ShowSelected();
}


void
LoginView::_ShowSelected()
{
	UserItem* item = dynamic_cast<UserItem*>(
		fUserList->ItemAt(fUserList->CurrentSelection()));
	if (item == NULL && fUserList->CountItems() > 0) {
		item = dynamic_cast<UserItem*>(fUserList->ItemAt(0));
		if (item != NULL)
			fUserList->Select(0);
	}
	if (item == NULL) {
		fNameView->SetText("R5OS");
		fLoginView->SetText("");
		return;
	}

	fNameView->SetText(item->Text());
	fLoginView->SetText(item->Login());
}


void
LoginView::_AttemptLogin()
{
	UserItem* item = dynamic_cast<UserItem*>(
		fUserList->ItemAt(fUserList->CurrentSelection()));
	if (item == NULL) {
		fInfoView->SetText("Choose a person first.");
		return;
	}

	BMessage* message = new BMessage(kAttemptLogin);
	message->AddString("login", item->Login());
	message->AddString("password", fPasswordControl->Text());
	_EnableControls(false);
	fInfoView->SetText("");
	be_app->PostMessage(message, NULL, this);
}


void
LoginView::_EnableControls(bool enable)
{
	fPasswordControl->SetEnabled(enable);
	fLoginButton->SetEnabled(enable);
	fHaltButton->SetEnabled(enable);
	fRebootButton->SetEnabled(enable);
}
