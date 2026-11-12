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
#include "SignInLook.h"
#include "r5_pebble.inc"

#include <AccentButton.h>
#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <Font.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupView.h>
#include <R5IconView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <PasswordControl.h>
#include <R5Icons.h>
#include <R5UI.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <UserCard.h>
#include <UserListItem.h>
#include <Window.h>

#include <math.h>
#include <pwd.h>
#include <string.h>
#include <time.h>
#include <vector>


using BAppearanceKit::BAccentButton;
using BAppearanceKit::BCard;
using BAppearanceKit::BIconView;
using BAppearanceKit::BPasswordControl;
using BAppearanceKit::BUserCard;
using BAppearanceKit::BUserListItem;


// The "@login" pill after "Signing in as".
class BadgeView : public BView {
public:
	BadgeView()
		:
		BView("badge", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		BFont font = font_px(be_bold_font, 10.5f);
		SetFont(&font);
	}

	void SetText(const char* text)
	{
		fText = text;
		InvalidateLayout();
		Invalidate();
	}

	virtual BSize MinSize()
	{
		BFont font;
		GetFont(&font);
		font_height height;
		font.GetHeight(&height);
		return BSize(ceilf(font.StringWidth(fText.String()) + px(14)),
			ceilf(height.ascent + height.descent + px(2)));
	}

	virtual BSize MaxSize()
	{
		return MinSize();
	}

	virtual BSize PreferredSize()
	{
		return MinSize();
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		BRect bounds = Bounds();
		rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
		rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		float radius = bounds.Height() / 2;
		SetHighColor(mix_color(document, accent, 40));
		FillRoundRect(bounds, radius, radius);

		BFont font;
		GetFont(&font);
		font_height height;
		font.GetHeight(&height);
		SetHighColor(tint_color(accent, B_DARKEN_2_TINT));
		SetDrawingMode(B_OP_OVER);
		DrawString(fText.String(), BPoint(floorf(px(7)),
			floorf((bounds.Height() + 1 - height.ascent - height.descent) / 2
				+ height.ascent)));
		SetDrawingMode(B_OP_COPY);
	}

private:
	BString			fText;
};


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
	fAccountCountLabel(NULL),
	fUserCardsGrid(NULL),
	fUserList(NULL),
	fUserScroll(NULL),
	fFormCard(NULL),
	fSingleBanner(NULL),
	fBannerAvatar(NULL),
	fBannerName(NULL),
	fBannerRole(NULL),
	fActiveLine(NULL),
	fActiveCaption(NULL),
	fActiveName(NULL),
	fActiveBadge(NULL),
	fPasswordLabel(NULL),
	fPasswordControl(NULL),
	fLoginButton(NULL),
	fInfoView(NULL),
	fInfoIsError(false),
	fClockIcon(NULL),
	fClockView(NULL),
	fRebootButton(NULL),
	fHaltButton(NULL),
	fCardAvatar(NULL),
	fListAvatar(NULL),
	fBannerIcon(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// The window is 440 mockup pixels wide.
	float width = px_side(440) - 2;
	SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));

	// Each picture is rendered at the size it is shown, so none of them
	// is scaled.
	fCardAvatar = person_icon(r5_avatar_icon_side(px_side(38)));
	fListAvatar = person_icon(r5_avatar_icon_side(px_side(32)));
	fBannerIcon = person_icon(r5_avatar_icon_side(px_side(44)));

	const color_which document = B_DOCUMENT_BACKGROUND_COLOR;
	const color_which panel = B_PANEL_BACKGROUND_COLOR;

	// Header card: the pebble, "Welcome to R5" and the subtitle.
	fHeaderCard = new BCard("header", BAppearanceKit::B_CARD_DOCUMENT,
		B_HORIZONTAL);
	BBitmap* pebble = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble),
		px_side(44));
	BIconView* logo = new BIconView("logo", pebble);
	delete pebble;

	fWelcomeView = make_label("welcome", "Welcome to R5",
		font_px(be_bold_font, 18), document);
	fSubtitleView = make_label("subtitle", "Sign in to start your session",
		font_px(be_plain_font, 12.5f), document);

	BGroupLayout* headerLayout = (BGroupLayout*)fHeaderCard->GetLayout();
	headerLayout->SetSpacing(px(14));
	BLayoutBuilder::Group<>(headerLayout)
		.SetInsets(px(16), px(14), px(16), px(14))
		.Add(logo)
		.AddGroup(B_VERTICAL, px(2))
			.Add(fWelcomeView)
			.Add(fSubtitleView)
		.End()
		.AddGlue();

	// The account picker, shown when there are two accounts or more.
	fUserSection = new BGroupView(B_VERTICAL, px(6));
	fUserSection->SetViewUIColor(panel);

	fSelectPersonLabel = make_label("personLabel", "SELECT PERSON",
		font_px(be_bold_font, 11.5f), panel);
	fAccountCountLabel = make_label("accountCount", "",
		font_px(be_plain_font, 11), panel);

	fUserCardsGrid = new BGridView(px(8), px(8));
	fUserCardsGrid->SetViewUIColor(panel);

	fUserList = new BListView("users");
	fUserList->SetSelectionMessage(new BMessage(kUserSelected));
	fUserList->SetInvocationMessage(new BMessage(kUserInvoked));
	fUserScroll = new BScrollView("userssv", fUserList, 0, false, true);
	fUserScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, px(120)));
	fUserScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, px(148)));

	BLayoutBuilder::Group<>(fUserSection)
		.AddGroup(B_HORIZONTAL, px(8))
			.Add(fSelectPersonLabel)
			.AddGlue()
			.Add(fAccountCountLabel)
		.End()
		.Add(fUserCardsGrid)
		.Add(fUserScroll);

	// The form card.
	fFormCard = new BCard("form", BAppearanceKit::B_CARD_DOCUMENT, B_VERTICAL);

	// One account: its picture, name and role.
	fSingleBanner = new BGroupView(B_HORIZONTAL, px(12));
	fSingleBanner->SetViewUIColor(document);
	fBannerAvatar = new AvatarView((const BBitmap**)&fBannerIcon,
		px_side(44));
	fBannerName = make_label("bannerName", "",
		font_px(be_bold_font, 15), document);
	fBannerRole = make_label("bannerRole", "",
		font_px(be_bold_font, 11.5f), document);
	BLayoutBuilder::Group<>(fSingleBanner)
		.SetInsets(0, px(6), 0, px(6))
		.Add(fBannerAvatar)
		.AddGroup(B_VERTICAL, px(1))
			.Add(fBannerName)
			.Add(fBannerRole)
		.End()
		.AddGlue();

	// Several accounts: "Signing in as: Name @login".
	fActiveLine = new BGroupView(B_HORIZONTAL, px(8));
	fActiveLine->SetViewUIColor(document);
	fActiveCaption = make_label("activeCaption", "Signing in as:",
		font_px(be_plain_font, 12), document);
	fActiveName = make_label("activeName", "",
		font_px(be_bold_font, 13), document);
	fActiveBadge = new BadgeView();
	BLayoutBuilder::Group<>(fActiveLine)
		.Add(fActiveCaption)
		.Add(fActiveName)
		.Add(fActiveBadge)
		.AddGlue();

	fPasswordLabel = make_label("pwLabel", "Password",
		font_px(be_bold_font, 12), document);

	fPasswordControl = new BPasswordControl("password", "", "",
		new BMessage(kAttemptLogin));
	fPasswordControl->SetPlaceholder("Enter password");

	fLoginButton = new BAccentButton("signin", "Sign in",
		new BMessage(kAttemptLogin));
	fLoginButton->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fInfoView = make_label("info", "", font_px(be_plain_font, 12.5f),
		document);
	fInfoView->SetAlignment(B_ALIGN_CENTER);
	fInfoView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	// The password field keeps room for its focus ring around the field.
	// The button is inset by the same ring so both line up.
	float ring = floorf(px(2) + 0.5f);
	BGroupLayout* formLayout = (BGroupLayout*)fFormCard->GetLayout();
	formLayout->SetSpacing(px(12));
	BLayoutBuilder::Group<>(formLayout)
		.SetInsets(px(16), px(16), px(16), px(16))
		.Add(fSingleBanner)
		.Add(fActiveLine)
		.AddGroup(B_VERTICAL, px(5) - ring)
			.Add(fPasswordLabel)
			.Add(fPasswordControl)
		.End()
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(ring, 0, ring, 0)
			.Add(fLoginButton)
		.End()
		.Add(fInfoView);

	// The bottom row: clock, Restart, Shut down.
	fClockIcon = new BIconView("clockIcon");
	fClockView = make_label("clock", "00:00", font_px(be_bold_font, 12.5f),
		panel);

	BFont buttonFont = font_px(be_bold_font, 12.5f);
	BSize buttonHeight(B_SIZE_UNSET, px_side(32) - 1);
	fRebootButton = new BButton("reboot", "Restart",
		new BMessage(kRebootAction));
	fHaltButton = new BButton("halt", "Shut down",
		new BMessage(kHaltAction));
	BButton* buttons[] = { fRebootButton, fHaltButton };
	for (BButton* button : buttons) {
		button->SetFont(&buttonFont);
		button->SetExplicitMinSize(buttonHeight);
		button->SetExplicitMaxSize(buttonHeight);
	}

	BLayoutBuilder::Group<>(this, B_VERTICAL, px(14))
		.SetInsets(px(16), px(16), px(16), px(16))
		.Add(fHeaderCard)
		.Add(fUserSection)
		.Add(fFormCard)
		.AddGroup(B_HORIZONTAL, px(8))
			.SetInsets(px(4), px(2), 0, 0)
			.AddGroup(B_HORIZONTAL, px(6))
				.Add(fClockIcon)
				.Add(fClockView)
			.End()
			.AddGlue()
			.Add(fRebootButton)
			.Add(fHaltButton)
		.End();
}


LoginView::~LoginView()
{
	delete fCardAvatar;
	delete fListAvatar;
	delete fBannerIcon;
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
	_ApplyGlyphs();
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
			_SetFeedback(text, true);
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
			_ApplyGlyphs();
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
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	BString ready;
	ready.SetToFormat("Ready to sign in as %s", fActiveName->Text());
	_SetFeedback(ready.String(), false);
// SPDX-SnippetEnd
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

		info.role = is_admin_user(pwd) ? kRoleAdministrator : kRoleStandard;
		accounts.push_back(info);
	}
	endpwent();

	while (fUserCardsGrid->CountChildren() > 0) {
		BView* child = fUserCardsGrid->ChildAt(0);
		fUserCardsGrid->RemoveChild(child);
		delete child;
	}
	for (int32 i = fUserList->CountItems() - 1; i >= 0; i--)
		delete fUserList->RemoveItem(i);

	bool several = accounts.size() > 1;
	_SetVisible(fUserSection, several);
	_SetVisible(fSingleBanner, !several);
	_SetVisible(fActiveLine, several);

	if (accounts.size() > 1 && accounts.size() <= 4) {
		// Two to four accounts: cards, two on a row.
		_SetVisible(fUserCardsGrid, true);
		_SetVisible(fUserScroll, false);
		_SetVisible(fAccountCountLabel, false);

		BGridLayout* grid = fUserCardsGrid->GridLayout();
		for (size_t i = 0; i < accounts.size(); i++) {
			BMessage* message = new BMessage(kCardSelected);
			message->AddString("login", accounts[i].login);
			message->AddString("displayName", accounts[i].displayName);
			message->AddString("role", accounts[i].role);

			BUserCard* card = new BUserCard(accounts[i].login.String(),
				accounts[i].login.String(), accounts[i].displayName.String(),
				accounts[i].role.String(), fCardAvatar, message);
			card->SetTarget(this);
			grid->AddView(card, (int32)(i % 2), (int32)(i / 2));
		}
	} else if (accounts.size() > 4) {
		// Five or more: a two-line list that scrolls.
		_SetVisible(fUserCardsGrid, false);
		_SetVisible(fUserScroll, true);
		_SetVisible(fAccountCountLabel, true);

		BString count;
		count.SetToFormat("%d accounts available", (int)accounts.size());
		fAccountCountLabel->SetText(count.String());

		for (size_t i = 0; i < accounts.size(); i++) {
			fUserList->AddItem(new BUserListItem(accounts[i].login.String(),
				accounts[i].displayName.String(), accounts[i].role.String(),
				fListAvatar));
		}
		fUserList->Select(0);
	}

	if (!accounts.empty()) {
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
	const char* effectiveRole = (role != NULL && role[0] != '\0')
		? role : kRoleStandard;
	bool admin = strcmp(effectiveRole, kRoleAdministrator) == 0;

	fBannerName->SetText(display);
	BString roleText;
	roleText.SetToFormat("@%s \xc2\xb7 %s", login, effectiveRole);
	fBannerRole->SetText(roleText.String());
	fBannerAvatar->SetHighlighted(admin);

	fActiveName->SetText(display);
	BString badge;
	badge.SetToFormat("@%s", login);
	fActiveBadge->SetText(badge.String());

	_ApplyColors();

	fPasswordControl->SetText("");
	BString ready;
	ready.SetToFormat("Ready to sign in as %s", display);
	_SetFeedback(ready.String(), false);
	if (Window() != NULL && !Window()->IsHidden())
		fPasswordControl->MakeFocus(true);
}


void
LoginView::_SetFeedback(const char* text, bool error)
{
	fInfoIsError = error;
	BFont font = font_px(error ? be_bold_font : be_plain_font, 12.5f);
	fInfoView->SetFont(&font);
	fInfoView->SetText(text);
	_ApplyColors();
}


void
LoginView::_AttemptLogin()
{
	if (fSelectedLogin.Length() == 0) {
		_SetFeedback("Choose a person first.", true);
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
	_SetFeedback("Checking\xe2\x80\xa6", false);
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
	rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);

	// The mockup's three grays: strong, muted, and soft.
	rgb_color muted = mix_color(documentText, document, 100);
	rgb_color soft = mix_color(documentText, document, 125);

	fWelcomeView->SetHighColor(documentText);
	fSubtitleView->SetHighColor(muted);
	fSelectPersonLabel->SetHighColor(mix_color(panelText, panel, 100));
	fAccountCountLabel->SetHighColor(mix_color(panelText, panel, 125));
	fBannerName->SetHighColor(documentText);
	bool admin = strstr(fBannerRole->Text(), kRoleAdministrator) != NULL;
	fBannerRole->SetHighColor(admin ? accent : soft);
	fActiveCaption->SetHighColor(soft);
	fActiveName->SetHighColor(documentText);
	fPasswordLabel->SetHighColor(mix_color(documentText, document, 60));
	if (fInfoIsError)
		fInfoView->SetHighUIColor(B_FAILURE_COLOR);
	else
		fInfoView->SetHighColor(soft);
	fClockView->SetHighColor(mix_color(panelText, panel, 60));
}


void
LoginView::_ApplyGlyphs()
{
	// The glyphs take the current text color.
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);
	BBitmap* clock = r5_glyph_bitmap(R5_GLYPH_CLOCK, px_side(14),
		mix_color(panelText, panel, 125));
	fClockIcon->SetIcon(clock);
	delete clock;
	r5_button_set_glyph(fRebootButton, R5_GLYPH_RESTART);
	r5_button_set_glyph(fHaltButton, R5_GLYPH_POWER);
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
