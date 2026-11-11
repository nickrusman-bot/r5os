/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * R5 lock screen. The session stays; this window only covers it.
 */


#include "LockWindow.h"

#include "AccountAdmin.h"
#include "LoginApp.h"
#include "SignInBackground.h"
#include "SignInLook.h"
#include "r5_pebble.inc"

#include <AccentButton.h>
#include <Button.h>
#include <Card.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <PasswordControl.h>
#include <R5IconView.h>
#include <Screen.h>
#include <String.h>
#include <Window.h>

#include <pwd.h>
#include <string.h>
#include <time.h>


using BAppearanceKit::BAccentButton;
using BAppearanceKit::BCard;
using BAppearanceKit::BIconView;
using BAppearanceKit::BPasswordControl;


// The lock screen has the Sign in look (docs/design/login-mockup), with
// the one signed-in account and Log out on the bottom row.
class LockView : public BView {
public:
							LockView(const char* name);
	virtual					~LockView();

	virtual	void			AttachedToWindow();
	virtual	void			AllAttached();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			Pulse();

private:
			void			_Submit();
			void			_EnablePassword(bool enable);
			void			_SetFeedback(const char* text, bool error);
			void			_ApplyColors();
			void			_ApplyGlyphs();

			BStringView*	fTitleView;
			BStringView*	fSubtitleView;
			AvatarView*		fAvatar;
			BStringView*	fNameView;
			BStringView*	fRoleView;
			bool			fAdmin;
			BStringView*	fPasswordLabel;
			BPasswordControl* fPasswordControl;
			BAccentButton*	fUnlockButton;
			BStringView*	fInfoView;
			bool			fInfoIsError;
			BIconView*		fClockIcon;
			BStringView*	fClockView;
			BButton*		fLogoutButton;
			BButton*		fRebootButton;
			BButton*		fHaltButton;
			BBitmap*		fAvatarIcon;
};


LockView::LockView(const char* name)
	:
	BView("LockView", B_WILL_DRAW | B_PULSE_NEEDED | B_SUPPORTS_LAYOUT),
	fTitleView(NULL),
	fSubtitleView(NULL),
	fAvatar(NULL),
	fNameView(NULL),
	fRoleView(NULL),
	fAdmin(false),
	fPasswordLabel(NULL),
	fPasswordControl(NULL),
	fUnlockButton(NULL),
	fInfoView(NULL),
	fInfoIsError(false),
	fClockIcon(NULL),
	fClockView(NULL),
	fLogoutButton(NULL),
	fRebootButton(NULL),
	fHaltButton(NULL),
	fAvatarIcon(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	float width = px_side(440) - 2;
	SetExplicitMinSize(BSize(width, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(width, B_SIZE_UNSET));

	// Who is signed in: the full name, the login and the role.
	const char* login = name != NULL ? name : "";
	BString display(login);
	struct passwd* pwd = getpwnam(login);
	if (pwd != NULL) {
		if (pwd->pw_gecos != NULL && pwd->pw_gecos[0] != '\0')
			display = pwd->pw_gecos;
		fAdmin = is_admin_user(pwd);
	}

	const color_which document = B_DOCUMENT_BACKGROUND_COLOR;
	const color_which panel = B_PANEL_BACKGROUND_COLOR;

	// Header card: the pebble, "Locked" and the reassurance.
	BCard* header = new BCard("header", BAppearanceKit::B_CARD_DOCUMENT,
		B_HORIZONTAL);
	BBitmap* pebble = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble),
		px_side(44));
	BIconView* logo = new BIconView("logo", pebble);
	delete pebble;

	fTitleView = make_label("title", "Locked", font_px(be_bold_font, 18),
		document);
	fSubtitleView = make_label("subtitle", "Your apps stay open.",
		font_px(be_plain_font, 12.5f), document);

	BGroupLayout* headerLayout = (BGroupLayout*)header->GetLayout();
	headerLayout->SetSpacing(px(14));
	BLayoutBuilder::Group<>(headerLayout)
		.SetInsets(px(16), px(14), px(16), px(14))
		.Add(logo)
		.AddGroup(B_VERTICAL, px(2))
			.Add(fTitleView)
			.Add(fSubtitleView)
		.End()
		.AddGlue();

	// Form card: the account, the password, Unlock.
	BCard* form = new BCard("form", BAppearanceKit::B_CARD_DOCUMENT,
		B_VERTICAL);

	fAvatarIcon = person_icon(r5_avatar_icon_side(px_side(44)));
	fAvatar = new AvatarView((const BBitmap**)&fAvatarIcon, px_side(44));
	fAvatar->SetHighlighted(fAdmin);
	fNameView = make_label("name", display.String(),
		font_px(be_bold_font, 15), document);
	BString role;
	role.SetToFormat("@%s \xc2\xb7 %s", login,
		fAdmin ? kRoleAdministrator : kRoleStandard);
	fRoleView = make_label("role", role.String(),
		font_px(fAdmin ? be_bold_font : be_plain_font, 11.5f), document);

	BGroupView* banner = new BGroupView(B_HORIZONTAL, px(12));
	banner->SetViewUIColor(document);
	BLayoutBuilder::Group<>(banner)
		.SetInsets(0, px(6), 0, px(6))
		.Add(fAvatar)
		.AddGroup(B_VERTICAL, px(1))
			.Add(fNameView)
			.Add(fRoleView)
		.End()
		.AddGlue();

	fPasswordLabel = make_label("pwLabel", "Password",
		font_px(be_bold_font, 12), document);
	fPasswordControl = new BPasswordControl("password", "", "",
		new BMessage(kUnlockAction));
	fPasswordControl->SetPlaceholder("Enter password");

	fUnlockButton = new BAccentButton("unlock", "Unlock",
		new BMessage(kUnlockAction));
	fUnlockButton->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fInfoView = make_label("info", "", font_px(be_plain_font, 12.5f),
		document);
	fInfoView->SetAlignment(B_ALIGN_CENTER);
	fInfoView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	float ring = floorf(px(2) + 0.5f);
	BGroupLayout* formLayout = (BGroupLayout*)form->GetLayout();
	formLayout->SetSpacing(px(12));
	BLayoutBuilder::Group<>(formLayout)
		.SetInsets(px(16), px(16), px(16), px(16))
		.Add(banner)
		.AddGroup(B_VERTICAL, px(5) - ring)
			.Add(fPasswordLabel)
			.Add(fPasswordControl)
		.End()
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(ring, 0, ring, 0)
			.Add(fUnlockButton)
		.End()
		.Add(fInfoView);

	// The bottom row: clock, Log out, Restart, Shut down.
	fClockIcon = new BIconView("clockIcon");
	fClockView = make_label("clock", "00:00", font_px(be_bold_font, 12.5f),
		panel);

	BFont buttonFont = font_px(be_bold_font, 12.5f);
	BSize buttonHeight(B_SIZE_UNSET, px_side(32) - 1);
	fLogoutButton = new BButton("logout", "Log out",
		new BMessage(kLogoutAction));
	fRebootButton = new BButton("reboot", "Restart",
		new BMessage(kRebootAction));
	fHaltButton = new BButton("halt", "Shut down",
		new BMessage(kHaltAction));
	BButton* buttons[] = { fLogoutButton, fRebootButton, fHaltButton };
	for (BButton* button : buttons) {
		button->SetFont(&buttonFont);
		button->SetExplicitMinSize(buttonHeight);
		button->SetExplicitMaxSize(buttonHeight);
	}

	BLayoutBuilder::Group<>(this, B_VERTICAL, px(14))
		.SetInsets(px(16), px(16), px(16), px(16))
		.Add(header)
		.Add(form)
		.AddGroup(B_HORIZONTAL, px(8))
			.SetInsets(px(4), px(2), 0, 0)
			.AddGroup(B_HORIZONTAL, px(6))
				.Add(fClockIcon)
				.Add(fClockView)
			.End()
			.AddGlue()
			.Add(fLogoutButton)
			.Add(fRebootButton)
			.Add(fHaltButton)
		.End();
}


LockView::~LockView()
{
	delete fAvatarIcon;
}


void
LockView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fPasswordControl->SetTarget(this);
	fUnlockButton->SetTarget(this);
	fLogoutButton->SetTarget(be_app_messenger);
	fRebootButton->SetTarget(be_app_messenger);
	fHaltButton->SetTarget(be_app_messenger);
	Window()->SetDefaultButton(fUnlockButton);
	_ApplyGlyphs();
	_SetFeedback("Enter your password to unlock", false);
	Pulse();
}


void
LockView::AllAttached()
{
	BView::AllAttached();
	_ApplyColors();
	fPasswordControl->MakeFocus(true);
}


void
LockView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kUnlockAction:
			_Submit();
			break;
		case kLoginBad:
		{
			const char* text = NULL;
			if (message->FindString("text", &text) != B_OK || text == NULL)
				text = "That password is not right.";
			fPasswordControl->SetText("");
			_SetFeedback(text, true);
			_EnablePassword(true);
			fPasswordControl->MakeFocus(true);
			break;
		}
		case kLoginOk:
			fPasswordControl->SetText("");
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


void
LockView::Pulse()
{
	time_t now = time(NULL);
	struct tm local;
	localtime_r(&now, &local);
	char buffer[64];
	strftime(buffer, sizeof(buffer), "%H:%M", &local);
	if (strcmp(fClockView->Text(), buffer) != 0)
		fClockView->SetText(buffer);
}


void
LockView::_Submit()
{
	BMessage* message = new BMessage(kUnlockAction);
	message->AddString("password", fPasswordControl->Text());
	fPasswordControl->SetText("");
	_EnablePassword(false);
	_SetFeedback("Checking\xe2\x80\xa6", false);
	be_app->PostMessage(message, NULL, this);
}


void
LockView::_EnablePassword(bool enable)
{
	fPasswordControl->SetEnabled(enable);
	fUnlockButton->SetEnabled(enable);
}


void
LockView::_SetFeedback(const char* text, bool error)
{
	fInfoIsError = error;
	BFont font = font_px(error ? be_bold_font : be_plain_font, 12.5f);
	fInfoView->SetFont(&font);
	fInfoView->SetText(text);
	_ApplyColors();
}


void
LockView::_ApplyColors()
{
	rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color documentText = ui_color(B_DOCUMENT_TEXT_COLOR);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color soft = mix_color(documentText, document, 125);

	fTitleView->SetHighColor(documentText);
	fSubtitleView->SetHighColor(mix_color(documentText, document, 100));
	fNameView->SetHighColor(documentText);
	if (fAdmin)
		fRoleView->SetHighUIColor(B_STATUS_BAR_COLOR);
	else
		fRoleView->SetHighColor(soft);
	fPasswordLabel->SetHighColor(mix_color(documentText, document, 60));
	if (fInfoIsError)
		fInfoView->SetHighUIColor(B_FAILURE_COLOR);
	else
		fInfoView->SetHighColor(soft);
	fClockView->SetHighColor(mix_color(panelText, panel, 60));
}


void
LockView::_ApplyGlyphs()
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);
	BBitmap* clock = r5_glyph_bitmap(R5_GLYPH_CLOCK, px_side(14),
		mix_color(panelText, panel, 125));
	fClockIcon->SetIcon(clock);
	delete clock;
	r5_button_set_glyph(fLogoutButton, R5_GLYPH_LOG_OUT);
	r5_button_set_glyph(fRebootButton, R5_GLYPH_RESTART);
	r5_button_set_glyph(fHaltButton, R5_GLYPH_POWER);
}


class LockCover : public BWindow {
public:
							LockCover(BRect frame);
	virtual	bool			QuitRequested();
	virtual	void			ScreenChanged(BRect frame, color_space mode);
};


LockCover::LockCover(BRect frame)
	:
	BWindow(frame, "Lock cover", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL,
		B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES)
{
	// The same background as Sign in.
	AddChild(new SignInBackground(Bounds()));
}


bool
LockCover::QuitRequested()
{
	return false;
}


void
LockCover::ScreenChanged(BRect frame, color_space mode)
{
	BWindow::ScreenChanged(frame, mode);
	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
}


LockWindow::LockWindow(const char* name)
	:
	BWindow(BRect(0, 0, 360, 240), "Locked", B_TITLED_WINDOW_LOOK,
		B_MODAL_ALL_WINDOW_FEEL,
		B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES),
	fCover(NULL)
{
	BScreen screen;
	fCover = new LockCover(screen.Frame());
	fCover->Show();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(new LockView(name));
	SetPulseRate(1000000LL);
	ResizeToPreferred();
	CenterOnScreen();
}


LockWindow::~LockWindow()
{
	if (fCover == NULL)
		return;
	BWindow* cover = fCover;
	fCover = NULL;
	if (cover->Lock())
		cover->Quit();
}


bool
LockWindow::QuitRequested()
{
	return false;
}


void
LockWindow::ScreenChanged(BRect frame, color_space mode)
{
	BWindow::ScreenChanged(frame, mode);
	CenterOnScreen();
}
