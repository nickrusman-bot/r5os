/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "LoginView.h"

#include "LoginApp.h"
#include "r5_pebble.inc"

#include <Bitmap.h>
#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <R5Icons.h>
#include <String.h>
#include <Window.h>

#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <time.h>
#include <vector>

using namespace BAppearanceKit;


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


// View for drawing the R5 pebble logo
class LogoView : public BView {
public:
	LogoView()
		:
		BView("logo", B_WILL_DRAW),
		fBitmap(NULL)
	{
		float side = 44.0f * unit();
		SetExplicitMinSize(BSize(side, side));
		SetExplicitMaxSize(BSize(side, side));
		fBitmap = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble), side);
	}

	virtual ~LogoView()
	{
		delete fBitmap;
	}

	virtual void AttachedToWindow()
	{
		if (Parent() != NULL)
			SetViewColor(Parent()->ViewColor());
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		if (fBitmap != NULL && fBitmap->IsValid()) {
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmap(fBitmap, Bounds());
			SetDrawingMode(B_OP_COPY);
		}
	}

private:
	BBitmap* fBitmap;
};


// View for drawing a circular user avatar
class AvatarView : public BView {
public:
	AvatarView(const BBitmap** avatarRef)
		:
		BView("avatar", B_WILL_DRAW),
		fAvatarRef(avatarRef)
	{
		float side = 38.0f * unit();
		SetExplicitMinSize(BSize(side, side));
		SetExplicitMaxSize(BSize(side, side));
	}

	virtual void AttachedToWindow()
	{
		if (Parent() != NULL)
			SetViewColor(Parent()->ViewColor());
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		BRect bounds = Bounds();
		float u = unit();

		rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
		rgb_color avBg = mix_color(accent, ui_color(B_DOCUMENT_BACKGROUND_COLOR), 35);
		SetHighColor(avBg);
		FillEllipse(bounds);

		if (fAvatarRef != NULL && *fAvatarRef != NULL && (*fAvatarRef)->IsValid()) {
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmap(*fAvatarRef, (*fAvatarRef)->Bounds(), bounds);
			SetDrawingMode(B_OP_COPY);
		} else {
			// Silhouette fallback
			SetHighColor(tint_color(avBg, B_DARKEN_2_TINT));
			float side = bounds.Width();
			BRect head(bounds.left + side * 0.32f, bounds.top + side * 0.18f,
				bounds.right - side * 0.32f, bounds.top + side * 0.52f);
			FillEllipse(head);
			BRect body(bounds.left + side * 0.2f, bounds.top + side * 0.58f,
				bounds.right - side * 0.2f, bounds.bottom);
			FillRoundRect(body, 3.0f * u, 3.0f * u);
		}
	}

private:
	const BBitmap** fAvatarRef;
};


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
	fActiveRoleLabel(NULL),
	fPasswordLabel(NULL),
	fPasswordControl(NULL),
	fLoginButton(NULL),
	fInfoView(NULL),
	fClockView(NULL),
	fRebootButton(NULL),
	fHaltButton(NULL),
	fPersonAvatar(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	float u = unit();
	SetExplicitMinSize(BSize(420.0f * u, 380.0f * u));

	// Load default user avatar icon
	fPersonAvatar = r5_mime_icon_bitmap("application/x-person", 44.0f * u);

	// Header banner with logo
	fHeaderCard = new BCard("header", B_CARD_DOCUMENT, B_HORIZONTAL);
	LogoView* logo = new LogoView();

	BGroupView* headerText = new BGroupView(B_VERTICAL, 2);
	fWelcomeView = new BStringView("welcome", "Welcome to R5");
	BFont welcomeFont(be_bold_font);
	welcomeFont.SetSize(be_plain_font->Size() * 1.35f);
	fWelcomeView->SetFont(&welcomeFont);

	fSubtitleView = new BStringView("subtitle", "Sign in to start your session");
	fSubtitleView->SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 140));

	BLayoutBuilder::Group<>(headerText)
		.Add(fWelcomeView)
		.Add(fSubtitleView);

	BLayoutBuilder::Group<>((BGroupLayout*)fHeaderCard->GetLayout())
		.SetInsets(14, 12, 14, 12)
		.Add(logo)
		.Add(headerText)
		.AddGlue();

	// User selection section (for multiple users)
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
	fUserScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120.0f * u));
	fUserScroll->SetExplicitMaxSize(BSize(B_SIZE_UNSET, 140.0f * u));

	BLayoutBuilder::Group<>(fUserSection)
		.Add(fSelectPersonLabel)
		.Add(fUserCardsRow)
		.Add(fUserScroll);

	// Credential form card
	fFormCard = new BCard("form", B_CARD_DOCUMENT, B_VERTICAL);

	// Active user display row (avatar + name + role)
	BGroupView* userProfileRow = new BGroupView(B_HORIZONTAL, 10);
	AvatarView* avView = new AvatarView((const BBitmap**)&fPersonAvatar);

	BGroupView* nameBlock = new BGroupView(B_VERTICAL, 2);
	fActiveUserLabel = new BStringView("activeUser", "User");
	BFont activeFont(be_bold_font);
	activeFont.SetSize(be_plain_font->Size() * 1.15f);
	fActiveUserLabel->SetFont(&activeFont);

	fActiveRoleLabel = new BStringView("activeRole", "@user · Administrator");
	fActiveRoleLabel->SetHighColor(ui_color(B_STATUS_BAR_COLOR));
	BFont roleFont(be_plain_font);
	roleFont.SetSize(be_plain_font->Size() * 0.88f);
	fActiveRoleLabel->SetFont(&roleFont);

	BLayoutBuilder::Group<>(nameBlock)
		.Add(fActiveUserLabel)
		.Add(fActiveRoleLabel);

	BLayoutBuilder::Group<>(userProfileRow)
		.Add(avView)
		.Add(nameBlock)
		.AddGlue();

	fPasswordLabel = new BStringView("pwLabel", "Password");
	BFont pwFont(be_bold_font);
	pwFont.SetSize(be_plain_font->Size() * 0.9f);
	fPasswordLabel->SetFont(&pwFont);

	fPasswordControl = new BPasswordControl("password", "", "",
		new BMessage(kAttemptLogin));

	fLoginButton = new BButton("signin", "Sign in", new BMessage(kAttemptLogin));
	fLoginButton->MakeDefault(true);
	fLoginButton->SetExplicitMinSize(BSize(B_SIZE_UNSET, 34.0f * u));

	fInfoView = new BStringView("info", "");
	fInfoView->SetAlignment(B_ALIGN_CENTER);

	BLayoutBuilder::Group<>((BGroupLayout*)fFormCard->GetLayout())
		.SetInsets(16, 14, 16, 14)
		.Add(userProfileRow)
		.AddStrut(4)
		.Add(fPasswordLabel)
		.Add(fPasswordControl)
		.AddStrut(4)
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
	delete fPersonAvatar;
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
			const char* display = NULL;
			const char* role = NULL;
			if (message->FindString("login", &login) == B_OK) {
				message->FindString("displayName", &display);
				message->FindString("role", &role);
				_ShowSelected(login, display, role);
			}
			break;
		}
		case kUserSelected:
		case kUserInvoked:
		{
			int32 sel = fUserList->CurrentSelection();
			BUserListItem* item = dynamic_cast<BUserListItem*>(
				fUserList->ItemAt(sel));
			if (item != NULL)
				_ShowSelected(item->Login(), item->DisplayName(), item->Role());
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

	while (fUserCardsRow->CountChildren() > 0) {
		BView* child = fUserCardsRow->ChildAt(0);
		fUserCardsRow->RemoveChild(child);
		delete child;
	}
	fUserList->MakeEmpty();

	if (accounts.size() <= 1) {
		fUserSection->Hide();
		if (!accounts.empty()) {
			_ShowSelected(accounts[0].login.String(),
				accounts[0].displayName.String(), accounts[0].role.String());
		}
	} else if (accounts.size() <= 4) {
		fUserSection->Show();
		fUserCardsRow->Show();
		fUserScroll->Hide();

		for (size_t i = 0; i < accounts.size(); i++) {
			BMessage* msg = new BMessage(kCardSelected);
			msg->AddString("login", accounts[i].login);
			msg->AddString("displayName", accounts[i].displayName);
			msg->AddString("role", accounts[i].role);

			BUserCard* card = new BUserCard(accounts[i].login.String(),
				accounts[i].login.String(), accounts[i].displayName.String(),
				accounts[i].role.String(), fPersonAvatar, msg);
			card->SetTarget(this);
			fUserCardsRow->AddChild(card);
		}

		_ShowSelected(accounts[0].login.String(),
			accounts[0].displayName.String(), accounts[0].role.String());
	} else {
		fUserSection->Show();
		fUserCardsRow->Hide();
		fUserScroll->Show();

		for (size_t i = 0; i < accounts.size(); i++) {
			fUserList->AddItem(new BUserListItem(accounts[i].login.String(),
				accounts[i].displayName.String(), accounts[i].role.String(),
				fPersonAvatar));
		}

		fUserList->Select(0);
		_ShowSelected(accounts[0].login.String(),
			accounts[0].displayName.String(), accounts[0].role.String());
	}
}


void
LoginView::_ShowSelected(const char* login, const char* displayName,
	const char* role)
{
	if (login == NULL || login[0] == '\0')
		return;

	fSelectedLogin = login;

	for (int32 i = 0; i < fUserCardsRow->CountChildren(); i++) {
		BUserCard* card = dynamic_cast<BUserCard*>(fUserCardsRow->ChildAt(i));
		if (card != NULL)
			card->SetSelected(strcmp(card->Login(), login) == 0);
	}

	for (int32 i = 0; i < fUserList->CountItems(); i++) {
		BUserListItem* item = dynamic_cast<BUserListItem*>(fUserList->ItemAt(i));
		if (item != NULL && strcmp(item->Login(), login) == 0) {
			if (fUserList->CurrentSelection() != i)
				fUserList->Select(i);
			break;
		}
	}

	const char* disp = (displayName != NULL && displayName[0] != '\0') ?
		displayName : login;
	fActiveUserLabel->SetText(disp);

	BString roleText;
	const char* r = (role != NULL && role[0] != '\0') ? role : "Standard user";
	roleText.SetToFormat("@%s · %s", login, r);
	fActiveRoleLabel->SetText(roleText.String());

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
	if (pw == NULL)
		pw = "";

	BMessage* message = new BMessage(kAttemptLogin);
	message->AddString("login", fSelectedLogin.String());
	message->AddString("password", pw);
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
