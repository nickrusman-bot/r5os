/*
 * R5OS lock screen. The session stays; this window only covers it.
 */


#include "LockWindow.h"

#include "LoginApp.h"

#include <Button.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Screen.h>
#include <StringView.h>
#include <TextControl.h>

#include <time.h>


class LockView : public BView {
public:
							LockView(const char* name);
	virtual					~LockView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);
	virtual	void			Pulse();

private:
			void			_Submit();
			void			_EnablePassword(bool enable);

			BStringView*	fNameView;
			BTextControl*	fPasswordControl;
			BButton*		fUnlockButton;
			BStringView*	fInfoView;
			BStringView*	fClockView;
			BButton*		fLogoutButton;
			BButton*		fRebootButton;
			BButton*		fHaltButton;
};


LockView::LockView(const char* name)
	:
	BView("LockView", B_WILL_DRAW | B_PULSE_NEEDED | B_SUPPORTS_LAYOUT),
	fNameView(NULL),
	fPasswordControl(NULL),
	fUnlockButton(NULL),
	fInfoView(NULL),
	fClockView(NULL),
	fLogoutButton(NULL),
	fRebootButton(NULL),
	fHaltButton(NULL)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fNameView = new BStringView("name", name != NULL ? name : "");
	BFont nameFont(be_bold_font);
	fNameView->SetFont(&nameFont);
	fNameView->SetAlignment(B_ALIGN_CENTER);
	fNameView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BStringView* hint = new BStringView("hint",
		"Ctrl+Shift+L locks. Your apps stay open.");
	hint->SetAlignment(B_ALIGN_CENTER);
	hint->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fPasswordControl = new BTextControl("password", "Password", "", NULL);
	fPasswordControl->TextView()->HideTyping(true);

	fUnlockButton = new BButton("unlock", "Unlock",
		new BMessage(kUnlockAction));

	fInfoView = new BStringView("info", "");
	fInfoView->SetAlignment(B_ALIGN_CENTER);
	fInfoView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fClockView = new BStringView("clock", "");
	fLogoutButton = new BButton("logout", "Log out",
		new BMessage(kLogoutAction));
	fRebootButton = new BButton("reboot", "Restart",
		new BMessage(kRebootAction));
	fHaltButton = new BButton("halt", "Shut down",
		new BMessage(kHaltAction));

	SetExplicitMinSize(BSize(280, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(320, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fNameView)
		.Add(hint)
		.Add(fPasswordControl)
		.Add(fUnlockButton)
		.Add(fInfoView)
		.AddGroup(B_HORIZONTAL)
			.Add(fClockView)
			.AddGlue()
			.Add(fLogoutButton)
			.Add(fRebootButton)
			.Add(fHaltButton)
		.End();
}


LockView::~LockView()
{
}


void
LockView::AttachedToWindow()
{
	fPasswordControl->SetTarget(this);
	fUnlockButton->SetTarget(this);
	fLogoutButton->SetTarget(be_app_messenger);
	fRebootButton->SetTarget(be_app_messenger);
	fHaltButton->SetTarget(be_app_messenger);
	Window()->SetDefaultButton(fUnlockButton);
	Pulse();
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
			fInfoView->SetText(text);
			_EnablePassword(true);
			fPasswordControl->MakeFocus(true);
			break;
		}
		case kLoginOk:
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
	fClockView->SetText(buffer);
}


void
LockView::_Submit()
{
	BMessage* message = new BMessage(kUnlockAction);
	message->AddString("password", fPasswordControl->Text());
	_EnablePassword(false);
	fInfoView->SetText("");
	be_app->PostMessage(message, NULL, this);
}


void
LockView::_EnablePassword(bool enable)
{
	fPasswordControl->SetEnabled(enable);
	fUnlockButton->SetEnabled(enable);
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
	BView* view = new BView(Bounds(), "cover", B_FOLLOW_ALL, 0);
	BScreen screen;
	view->SetViewColor(screen.DesktopColor());
	AddChild(view);
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
