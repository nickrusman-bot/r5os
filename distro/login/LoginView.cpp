/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "LoginView.h"

#include "LoginApp.h"

#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <String.h>
#include <Window.h>

#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <time.h>
#include <vector>

using namespace BAppearanceKit;


static bool
is_listed_account(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL || pwd->pw_name[0] == '\0')
		return false;
	if (strcmp(pwd->pw_name, "sshd") == 0
		|| strcmp(pwd->pw_name, "ftp") == 0
		|| strcmp(pwd->pw_name, "nobody") == 0
		|| strcmp(pwd->pw_name, "root") == 0)
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


struct AccountInfo {
	BString login;
	BString displayName;
	BString role;
};


LoginView::LoginView()
	:
	BView("LoginView", B_WILL_DRAW | B_PULSE_NEEDED | B_SUPPORTS_LAYOUT),
	fHeaderCard(NULL),
	fWelcomeView(NULL),
	fSubtitleView(NULL),
	fUserSection(NULL),
	fSelectPersonLabel(NULL),
	fUserCardsRow(NULL),
	fUserList(NULL),
	fUserScroll(NULL),
	fFormCard(NULL),
	fActiveUserLabel(NULL),
	fPasswordControl(NULL),
	fLoginButton(NULL),
	fInfoView(NULL),
	fClockView(NULL),
	fRebootButton(NULL),
	fHaltButton(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Header banner
	fHeaderCard = new BCard("header", B_CARD_DOCUMENT, B_VERTICAL);
	fWelcomeView = new BStringView("welcome", "Welcome to R5");
	BFont welcomeFont(be_bold_font);
	welcomeFont.SetSize(be_plain_font->Size() * 1.4f);
	fWelcomeView->SetFont(&welcomeFont);

	fSubtitleView = new BStringView("subtitle", "Sign in to start your session");
	fSubtitleView->SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 140));

	BLayoutBuilder::Group<>((BGroupLayout*)fHeaderCard->GetLayout())
		.SetInsets(12, 10, 12, 10)
		.Add(fWelcomeView)
		.Add(fSubtitleView);

	// User selection section
	fUserSection = new BGroupView(B_VERTICAL, 4);
	fSelectPersonLabel = new BStringView("personLabel", "SELECT PERSON");
	BFont labelFont(be_bold_font);
	labelFont.SetSize(be_plain_font->Size() * 0.85f);
	fSelectPersonLabel->SetFont(&labelFont);
	fSelectPersonLabel->SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
		ui_color(B_PANEL_BACKGROUND_COLOR), 140));

	fUserCardsRow = new BGroupView(B_HORIZONTAL, 8);

	fUserList = new BListView("users");
	fUserList->SetSelectionMessage(new BMessage(kUserSelected));
	fUserList->SetInvocationMessage(new BMessage(kUserInvoked));
	fUserScroll = new BScrollView("userssv", fUserList, 0, false, true);
	fUserScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120));
	fUserScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 140));

	BLayoutBuilder::Group<>(fUserSection)
		.Add(fSelectPersonLabel)
		.Add(fUserCardsRow)
		.Add(fUserScroll);

	// Credential form card
	fFormCard = new BCard("form", B_CARD_DOCUMENT, B_VERTICAL);
	fActiveUserLabel = new BStringView("activeUser", "Signing in as: User");
	BFont activeFont(be_bold_font);
	fActiveUserLabel->SetFont(&activeFont);

	fPasswordControl = new BPasswordControl("password", "Password", "",
		new BMessage(kAttemptLogin));

	fLoginButton = new BButton("signin", "Sign in", new BMessage(kAttemptLogin));
	fLoginButton->MakeDefault(true);

	fInfoView = new BStringView("info", "");
	fInfoView->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>((BGroupLayout*)fFormCard->GetLayout())
		.SetInsets(14, 14, 14, 14)
		.Add(fActiveUserLabel)
		.Add(fPasswordControl)
		.Add(fLoginButton)
		.Add(fInfoView);

	// Bottom power row
	fClockView = new BStringView("clock", "00:00");
	BFont clockFont(be_bold_font);
	fClockView->SetFont(&clockFont);

	fRebootButton = new BButton("reboot", "Restart",
		new BMessage(kRebootAction));
	fHaltButton = new BButton("halt", "Shut down",
		new BMessage(kHaltAction));

	// Window assembly
	BLayoutBuilder::Group<>(this, B_VERTICAL, 12)
		.SetInsets(14, 14, 14, 14)
		.Add(fHeaderCard)
		.Add(fUserSection)
		.Add(fFormCard)
		.AddGroup(B_HORIZONTAL, 8)
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
		case kCardSelected:
		{
			const char* login = NULL;
			if (message->FindString("login", &login) == B_OK)
				_ShowSelected(login);
			break;
		}
		case kUserSelected:
		case kUserInvoked:
		{
			int32 sel = fUserList->CurrentSelection();
			BUserListItem* item = dynamic_cast<BUserListItem*>(
				fUserList->ItemAt(sel));
			if (item != NULL)
				_ShowSelected(item->Login());
			if (message->what == kUserInvoked)
				fPasswordControl->MakeFocus(true);
			break;
		}
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
			fInfoView->SetHighColor(ui_color(B_FAILURE_COLOR));
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
LoginView::PrepareForSignIn()
{
	fPasswordControl->SetText("");
	fInfoView->SetText("");
	_EnableControls(true);
	fPasswordControl->MakeFocus(true);
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
	std::vector<AccountInfo> accounts;

	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_listed_account(pwd) || is_disabled_user(pwd->pw_name))
			continue;

		AccountInfo info;
		info.login = pwd->pw_name;
		if (pwd->pw_gecos != NULL && pwd->pw_gecos[0] != '\0')
			info.displayName = pwd->pw_gecos;
		else
			info.displayName = pwd->pw_name;

		if (strcmp(pwd->pw_dir, "/boot/home") == 0)
			info.role = "Administrator";
		else
			info.role = "Standard user";

		accounts.push_back(info);
	}
	endpwent();

	// Clean out any existing children
	while (fUserCardsRow->CountChildren() > 0) {
		BView* child = fUserCardsRow->ChildAt(0);
		fUserCardsRow->RemoveChild(child);
		delete child;
	}
	fUserList->MakeEmpty();

	if (accounts.size() <= 1) {
		// Single user: hide picker row completely
		fUserSection->Hide();
		if (!accounts.empty())
			_ShowSelected(accounts[0].login.String());
	} else if (accounts.size() <= 4) {
		// 2-4 users: horizontal cards
		fUserSection->Show();
		fUserCardsRow->Show();
		fUserScroll->Hide();

		for (size_t i = 0; i < accounts.size(); i++) {
			BMessage* msg = new BMessage(kCardSelected);
			msg->AddString("login", accounts[i].login);

			BUserCard* card = new BUserCard(accounts[i].login.String(),
				accounts[i].login.String(), accounts[i].displayName.String(),
				accounts[i].role.String(), NULL, msg);
			card->SetTarget(this);
			fUserCardsRow->AddChild(card);
		}

		_ShowSelected(accounts[0].login.String());
	} else {
		// > 4 users: scrollable 2-line list
		fUserSection->Show();
		fUserCardsRow->Hide();
		fUserScroll->Show();

		for (size_t i = 0; i < accounts.size(); i++) {
			fUserList->AddItem(new BUserListItem(accounts[i].login.String(),
				accounts[i].displayName.String(), accounts[i].role.String()));
		}

		fUserList->Select(0);
		_ShowSelected(accounts[0].login.String());
	}
}


void
LoginView::_ShowSelected(const char* login)
{
	if (login == NULL || login[0] == '\0')
		return;

	fSelectedLogin = login;

	// Update cards selection
	for (int32 i = 0; i < fUserCardsRow->CountChildren(); i++) {
		BUserCard* card = dynamic_cast<BUserCard*>(fUserCardsRow->ChildAt(i));
		if (card != NULL)
			card->SetSelected(strcmp(card->Login(), login) == 0);
	}

	// Update list selection
	for (int32 i = 0; i < fUserList->CountItems(); i++) {
		BUserListItem* item = dynamic_cast<BUserListItem*>(fUserList->ItemAt(i));
		if (item != NULL && strcmp(item->Login(), login) == 0) {
			if (fUserList->CurrentSelection() != i)
				fUserList->Select(i);
			break;
		}
	}

	BString headerText;
	headerText.SetToFormat("Signing in as: @%s", login);
	fActiveUserLabel->SetText(headerText.String());

	fPasswordControl->SetText("");
	fInfoView->SetText("");
	fPasswordControl->MakeFocus(true);
}


void
LoginView::_AttemptLogin()
{
	if (fSelectedLogin.Length() == 0) {
		fInfoView->SetText("Choose a person first.");
		fInfoView->SetHighColor(ui_color(B_FAILURE_COLOR));
		return;
	}

	const char* pw = fPasswordControl->Text();
	if (pw == NULL || pw[0] == '\0') {
		fInfoView->SetText("Please enter your password.");
		fInfoView->SetHighColor(ui_color(B_FAILURE_COLOR));
		fPasswordControl->MakeFocus(true);
		return;
	}

	BMessage* message = new BMessage(kAttemptLogin);
	message->AddString("login", fSelectedLogin.String());
	message->AddString("password", pw);
	_EnableControls(false);
	fInfoView->SetText("Verifying credentials...");
	fInfoView->SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
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
