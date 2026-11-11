/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginView.h"

#include "LoginApp.h"

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccountAdmin.h"
#include "r5_pebble.inc"

#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <Font.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <PasswordControl.h>
#include <R5Icons.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <UserCard.h>
#include <UserListItem.h>
#include <Window.h>

#include <pwd.h>
#include <string.h>
#include <time.h>
#include <vector>


using BAppearanceKit::BCard;
using BAppearanceKit::BPasswordControl;
using BAppearanceKit::BUserCard;
using BAppearanceKit::BUserListItem;


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


class LogoView : public BView {
public:
								LogoView();
	virtual						~LogoView();

	virtual	void				Draw(BRect updateRect);

private:
			BBitmap*			fBitmap;
};


LogoView::LogoView()
	:
	BView("logo", B_WILL_DRAW),
	fBitmap(NULL)
{
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	float side = 44.0f * unit();
	SetExplicitMinSize(BSize(side, side));
	SetExplicitMaxSize(BSize(side, side));
	fBitmap = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble), side);
}


LogoView::~LogoView()
{
	delete fBitmap;
}


void
LogoView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fBitmap != NULL && fBitmap->IsValid()) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fBitmap, fBitmap->Bounds(), Bounds());
		SetDrawingMode(B_OP_COPY);
	}
}


class AvatarView : public BView {
public:
								AvatarView(const BBitmap** avatarRef);

	virtual	void				Draw(BRect updateRect);

private:
			const BBitmap**		fAvatarRef;
};


AvatarView::AvatarView(const BBitmap** avatarRef)
	:
	BView("avatar", B_WILL_DRAW),
	fAvatarRef(avatarRef)
{
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	float side = 38.0f * unit();
	SetExplicitMinSize(BSize(side, side));
	SetExplicitMaxSize(BSize(side, side));
}


void
AvatarView::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float u = unit();

	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color background = mix_color(accent,
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 35);
	SetHighColor(background);
	FillEllipse(bounds);

	if (fAvatarRef != NULL && *fAvatarRef != NULL
		&& (*fAvatarRef)->IsValid()) {
		BRect iconRect = bounds.InsetByCopy(3.5f * u, 3.5f * u);
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(*fAvatarRef, (*fAvatarRef)->Bounds(), iconRect);
		SetDrawingMode(B_OP_COPY);
	} else {
		SetHighColor(tint_color(background, B_DARKEN_2_TINT));
		float side = bounds.Width();
		BRect head(bounds.left + side * 0.32f, bounds.top + side * 0.18f,
			bounds.right - side * 0.32f, bounds.top + side * 0.52f);
		FillEllipse(head);

		BRect body(bounds.left + side * 0.2f, bounds.top + side * 0.58f,
			bounds.right - side * 0.2f, bounds.bottom);
		FillRoundRect(body, 3.0f * u, 3.0f * u);
	}
}


static bool
is_valid_shell(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_shell == NULL)
		return true;
	return strcmp(pwd->pw_shell, "false") != 0
		&& strcmp(pwd->pw_shell, "true") != 0
		&& strcmp(pwd->pw_shell, "/bin/false") != 0
		&& strcmp(pwd->pw_shell, "/bin/true") != 0;
}


struct AccountInfo {
	BString		login;
	BString		displayName;
	BString		role;
};


LoginView::LoginView()
	:
	BView("LoginView", B_WILL_DRAW | B_PULSE_NEEDED | B_SUPPORTS_LAYOUT),
	fHeaderCard(NULL),
	fWelcomeView(NULL),
	fSubtitleView(NULL),
	fUserSection(NULL),
	fSelectPersonLabel(NULL),
	fUserCardsGrid(NULL),
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
	SetExplicitMinSize(BSize(420.0f * u, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(520.0f * u, B_SIZE_UNSET));

	fPersonAvatar = r5_mime_icon_bitmap("application/x-person", 44.0f * u);

	// Header card
	fHeaderCard = new BCard("header", BAppearanceKit::B_CARD_DOCUMENT,
		B_HORIZONTAL);
	LogoView* logo = new LogoView();

	BGroupView* headerText = new BGroupView(B_VERTICAL, 2);
	headerText->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	fWelcomeView = new BStringView("welcome", "Welcome to R5");
	fWelcomeView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	BFont welcomeFont(be_bold_font);
	welcomeFont.SetSize(be_plain_font->Size() * 1.35f);
	fWelcomeView->SetFont(&welcomeFont);

	fSubtitleView = new BStringView("subtitle",
		"Sign in to start your session");
	fSubtitleView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	BLayoutBuilder::Group<>(headerText)
		.Add(fWelcomeView)
		.Add(fSubtitleView);

	BLayoutBuilder::Group<>((BGroupLayout*)fHeaderCard->GetLayout())
		.SetInsets(14, 12, 14, 12)
		.Add(logo)
		.Add(headerText)
		.AddGlue();

	// User selection (shown when 2+ accounts exist)
	fUserSection = new BGroupView(B_VERTICAL, 4);
	fUserSection->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fSelectPersonLabel = new BStringView("personLabel", "SELECT PERSON");
	fSelectPersonLabel->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BFont labelFont(be_bold_font);
	labelFont.SetSize(be_plain_font->Size() * 0.85f);
	fSelectPersonLabel->SetFont(&labelFont);

	fUserCardsGrid = new BGridView(8, 8);
	fUserCardsGrid->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fUserList = new BListView("users");
	fUserList->SetSelectionMessage(new BMessage(kUserSelected));
	fUserList->SetInvocationMessage(new BMessage(kUserInvoked));
	fUserScroll = new BScrollView("userssv", fUserList, 0, false, true);
	fUserScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120.0f * u));
	fUserScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 148.0f * u));

	BLayoutBuilder::Group<>(fUserSection)
		.Add(fSelectPersonLabel)
		.Add(fUserCardsGrid)
		.Add(fUserScroll);

	// Credential form card
	fFormCard = new BCard("form", BAppearanceKit::B_CARD_DOCUMENT, B_VERTICAL);

	BGroupView* userProfileRow = new BGroupView(B_HORIZONTAL, 10);
	userProfileRow->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	AvatarView* avatarView = new AvatarView((const BBitmap**)&fPersonAvatar);

	BGroupView* nameBlock = new BGroupView(B_VERTICAL, 2);
	nameBlock->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	fActiveUserLabel = new BStringView("activeUser", "User");
	fActiveUserLabel->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	BFont activeFont(be_bold_font);
	activeFont.SetSize(be_plain_font->Size() * 1.15f);
	fActiveUserLabel->SetFont(&activeFont);

	fActiveRoleLabel = new BStringView("activeRole",
		"@user \xc2\xb7 Administrator");
	fActiveRoleLabel->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	BFont roleFont(be_plain_font);
	roleFont.SetSize(be_plain_font->Size() * 0.88f);
	fActiveRoleLabel->SetFont(&roleFont);

	BLayoutBuilder::Group<>(nameBlock)
		.Add(fActiveUserLabel)
		.Add(fActiveRoleLabel);

	BLayoutBuilder::Group<>(userProfileRow)
		.Add(avatarView)
		.Add(nameBlock)
		.AddGlue();

	fPasswordLabel = new BStringView("pwLabel", "Password");
	fPasswordLabel->SetViewColor(B_TRANSPARENT_COLOR);
	fPasswordLabel->SetFlags(fPasswordLabel->Flags() | B_TRANSPARENT_BACKGROUND);
	BFont passwordFont(be_bold_font);
	passwordFont.SetSize(be_plain_font->Size() * 0.9f);
	fPasswordLabel->SetFont(&passwordFont);

	fPasswordControl = new BPasswordControl("password", "", "",
		new BMessage(kAttemptLogin));
	fPasswordControl->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	fLoginButton = new BButton("signin", "Sign in",
		new BMessage(kAttemptLogin));
	fLoginButton->MakeDefault(true);
	fLoginButton->SetExplicitMinSize(BSize(B_SIZE_UNSET, 34.0f * u));

	fInfoView = new BStringView("info", "");
	fInfoView->SetViewColor(B_TRANSPARENT_COLOR);
	fInfoView->SetFlags(fInfoView->Flags() | B_TRANSPARENT_BACKGROUND);
	fInfoView->SetAlignment(B_ALIGN_CENTER);
	fInfoView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>((BGroupLayout*)fFormCard->GetLayout())
		.SetInsets(16, 14, 16, 14)
		.Add(userProfileRow)
		.AddStrut(4)
		.Add(fPasswordLabel)
		.Add(fPasswordControl)
		.AddStrut(4)
		.Add(fLoginButton)
		.Add(fInfoView);

	// Bottom bar
	fClockView = new BStringView("clock", "00:00");
	BFont clockFont(be_bold_font);
	fClockView->SetFont(&clockFont);

	fRebootButton = new BButton("reboot", "Restart",
		new BMessage(kRebootAction));
	fHaltButton = new BButton("halt", "Shut down",
		new BMessage(kHaltAction));

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
	BView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fUserList->SetTarget(this);
	fPasswordControl->SetTarget(this);
	fLoginButton->SetTarget(this);
	fHaltButton->SetTarget(be_app_messenger);
	fRebootButton->SetTarget(be_app_messenger);
	Window()->SetDefaultButton(fLoginButton);

	_ApplyColors();
	_FillUsers();
	Pulse();
}


void
LoginView::AllAttached()
{
	BView::AllAttached();
	_ApplyColors();
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
			int32 selection = fUserList->CurrentSelection();
			BUserListItem* item = dynamic_cast<BUserListItem*>(
				fUserList->ItemAt(selection));
			if (item != NULL) {
				_ShowSelected(item->Login(), item->DisplayName(),
					item->Role());
			}
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
			fInfoView->SetHighUIColor(B_FAILURE_COLOR);
			fInfoView->SetText(text);
			_EnableControls(true);
			fPasswordControl->MakeFocus(true);
			break;
		}
		case kLoginOk:
			fPasswordControl->SetText("");
			if (Window() != NULL)
				Window()->Hide();
			break;
		case B_COLORS_UPDATED:
			_ApplyColors();
			BView::MessageReceived(message);
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}
// SPDX-SnippetEnd


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
	if (strcmp(fClockView->Text(), buffer) != 0)
		fClockView->SetText(buffer);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
void
LoginView::_FillUsers()
{
	std::vector<AccountInfo> accounts;

	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_listed_account(pwd) || !is_valid_shell(pwd)
			|| is_disabled_user(pwd->pw_name)) {
			continue;
		}

		AccountInfo info;
		info.login = pwd->pw_name;
		if (pwd->pw_gecos != NULL && pwd->pw_gecos[0] != '\0')
			info.displayName = pwd->pw_gecos;
		else
			info.displayName = pwd->pw_name;

		info.role = is_admin_user(pwd) ? "Administrator" : "Standard user";
		accounts.push_back(info);
	}
	endpwent();

	while (fUserCardsGrid->CountChildren() > 0) {
		BView* child = fUserCardsGrid->ChildAt(0);
		fUserCardsGrid->RemoveChild(child);
		delete child;
	}
	fUserList->MakeEmpty();

	if (accounts.size() <= 1) {
		_SetVisible(fUserSection, false);
		if (!accounts.empty()) {
			_ShowSelected(accounts[0].login.String(),
				accounts[0].displayName.String(), accounts[0].role.String());
		}
	} else if (accounts.size() <= 4) {
		_SetVisible(fUserSection, true);
		_SetVisible(fUserCardsGrid, true);
		_SetVisible(fUserScroll, false);

		BGridLayout* grid = fUserCardsGrid->GridLayout();
		for (size_t i = 0; i < accounts.size(); i++) {
			BMessage* message = new BMessage(kCardSelected);
			message->AddString("login", accounts[i].login);
			message->AddString("displayName", accounts[i].displayName);
			message->AddString("role", accounts[i].role);

			BUserCard* card = new BUserCard(accounts[i].login.String(),
				accounts[i].login.String(), accounts[i].displayName.String(),
				accounts[i].role.String(), fPersonAvatar, message);
			card->SetTarget(this);
			grid->AddView(card, (int32)(i % 2), (int32)(i / 2));
		}

		_ShowSelected(accounts[0].login.String(),
			accounts[0].displayName.String(), accounts[0].role.String());
	} else {
		_SetVisible(fUserSection, true);
		_SetVisible(fUserCardsGrid, false);
		_SetVisible(fUserScroll, true);

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

	for (int32 i = 0; i < fUserCardsGrid->CountChildren(); i++) {
		BUserCard* card = dynamic_cast<BUserCard*>(
			fUserCardsGrid->ChildAt(i));
		if (card != NULL)
			card->SetSelected(strcmp(card->Login(), login) == 0);
	}

	for (int32 i = 0; i < fUserList->CountItems(); i++) {
		BUserListItem* item = dynamic_cast<BUserListItem*>(
			fUserList->ItemAt(i));
		if (item != NULL && strcmp(item->Login(), login) == 0) {
			if (fUserList->CurrentSelection() != i)
				fUserList->Select(i);
			break;
		}
	}

	const char* display = (displayName != NULL && displayName[0] != '\0')
		? displayName : login;
	fActiveUserLabel->SetText(display);

	const char* effectiveRole = (role != NULL && role[0] != '\0')
		? role : "Standard user";
	BString roleText;
	roleText.SetToFormat("@%s \xc2\xb7 %s", login, effectiveRole);
	fActiveRoleLabel->SetText(roleText.String());

	if (strcmp(effectiveRole, "Administrator") == 0)
		fActiveRoleLabel->SetHighUIColor(B_STATUS_BAR_COLOR);
	else {
		fActiveRoleLabel->SetHighColor(mix_color(
			ui_color(B_DOCUMENT_TEXT_COLOR),
			ui_color(B_DOCUMENT_BACKGROUND_COLOR), 140));
	}

	fPasswordControl->SetText("");
	fInfoView->SetText("");
	if (Window() != NULL && !Window()->IsHidden())
		fPasswordControl->MakeFocus(true);
}


void
LoginView::_AttemptLogin()
{
	if (fSelectedLogin.Length() == 0) {
		fInfoView->SetHighUIColor(B_FAILURE_COLOR);
		fInfoView->SetText("Choose a person first.");
		return;
	}

	const char* password = fPasswordControl->Text();
	if (password == NULL)
		password = "";

	BMessage* message = new BMessage(kAttemptLogin);
	message->AddString("login", fSelectedLogin.String());
	message->AddString("password", password);
	fPasswordControl->SetText("");
	_EnableControls(false);
	fInfoView->SetText("");
	be_app->PostMessage(message, NULL, this);
}
// SPDX-SnippetEnd


void
LoginView::_EnableControls(bool enable)
{
	fPasswordControl->SetEnabled(enable);
	fLoginButton->SetEnabled(enable);
	fHaltButton->SetEnabled(enable);
	fRebootButton->SetEnabled(enable);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
void
LoginView::_ApplyColors()
{
	rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color documentText = ui_color(B_DOCUMENT_TEXT_COLOR);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);

	fSubtitleView->SetHighColor(mix_color(documentText, document, 140));
	fSelectPersonLabel->SetHighColor(mix_color(documentText, panel, 140));
	fActiveRoleLabel->SetHighUIColor(B_STATUS_BAR_COLOR);
	fInfoView->SetHighUIColor(B_FAILURE_COLOR);
}


void
LoginView::_SetVisible(BView* view, bool visible)
{
	if (view == NULL)
		return;

	if (visible) {
		while (view->IsHidden(view))
			view->Show();
	} else {
		if (!view->IsHidden(view))
			view->Hide();
	}
}
// SPDX-SnippetEnd
