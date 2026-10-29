/*
 * R5OS Users & Accounts. Haiku #8940.
 */


#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <SupportDefs.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include "AccountAdmin.h"
#include "AdminLock.h"
#include "HomeLock.h"

#include <pwd.h>
#include <string.h>
#include <unistd.h>


static const char* kSignature = "application/x-vnd.R5OS-Users";

static const uint32 kMsgAdd = 'uadd';
static const uint32 kMsgDelete = 'udel';
static const uint32 kMsgDisable = 'udis';
static const uint32 kMsgRefresh = 'urfr';
static const uint32 kMsgSelect = 'usel';


static void
wipe_string(BString& text)
{
	int32 length = text.Length();
	if (length <= 0)
		return;
	char* buffer = text.LockBuffer(length);
	if (buffer != NULL)
		memset(buffer, 0, length);
	text.UnlockBuffer(length);
}


static void
show_error(const char* text)
{
	BAlert* alert = new BAlert("Users", text, "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


static status_t
ask_login(BMessage& request, BString& error)
{
	BMessenger login(kR5LoginSignature);
	if (!login.IsValid()) {
		error = "Sign in must be running to change accounts.";
		return B_ERROR;
	}

	BMessage reply;
	status_t status = login.SendMessage(&request, &reply);
	if (status != B_OK) {
		error = strerror(status);
		return status;
	}

	int32 code = B_OK;
	reply.FindInt32("error", &code);
	const char* text = NULL;
	reply.FindString("text", &text);
	if (code != B_OK) {
		error = text != NULL ? text : strerror(code);
		return code;
	}
	if (text != NULL && text[0] != '\0' && status != B_OK)
		error = text;
	return B_OK;
}


class UserItem : public BStringItem {
public:
	UserItem(const char* text, const char* name, uid_t uid, bool admin,
		bool disabled)
		:
		BStringItem(text),
		fName(name),
		fUid(uid),
		fAdmin(admin),
		fDisabled(disabled)
	{
	}

	const char*		Name() const		{ return fName.String(); }
	uid_t			Uid() const			{ return fUid; }
	bool			IsAdmin() const		{ return fAdmin; }
	bool			IsDisabled() const	{ return fDisabled; }

private:
	BString	fName;
	uid_t	fUid;
	bool	fAdmin;
	bool	fDisabled;
};


class CreateWindow : public BWindow {
public:
	CreateWindow(BWindow* parent)
		:
		BWindow(BRect(0, 0, 380, 240), "Add account", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fParent(parent)
	{
		fName = new BTextControl("name", "Username", "", NULL);
		fReal = new BTextControl("real", "Display name", "", NULL);
		fPassword = new BTextControl("password", "Password", "", NULL);
		fPassword->TextView()->HideTyping(true);
		fRepeat = new BTextControl("repeat", "Repeat password", "", NULL);
		fRepeat->TextView()->HideTyping(true);
		fAdmin = new BCheckBox("admin", "Administrator", NULL);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fName)
			.Add(fReal)
			.Add(fPassword)
			.Add(fRepeat)
			.Add(fAdmin)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", "Cancel",
					new BMessage(B_QUIT_REQUESTED)))
				.Add(new BButton("create", "Create", new BMessage('ucrt')))
			.End();

		fName->MakeFocus(true);
		AddShortcut('W', 0, new BMessage(B_QUIT_REQUESTED));
	}

	virtual void
	MessageReceived(BMessage* message)
	{
		if (message->what != 'ucrt') {
			BWindow::MessageReceived(message);
			return;
		}

		const char* name = fName->Text();
		BString password(fPassword->Text());
		BString repeat(fRepeat->Text());
		if (password != repeat) {
			wipe_string(password);
			wipe_string(repeat);
			show_error("The passwords do not match.");
			return;
		}

		BMessage request(kMsgAdminAdd);
		request.AddString("name", name);
		request.AddString("real", fReal->Text());
		request.AddString("password", password.String());
		request.AddBool("admin", fAdmin->Value() == B_CONTROL_ON);

		BString error;
		status_t status = ask_login(request, error);
		wipe_string(password);
		wipe_string(repeat);
		if (status != B_OK) {
			show_error(error.String());
			return;
		}

		fParent->PostMessage(kMsgRefresh);
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BWindow*		fParent;
	BTextControl*	fName;
	BTextControl*	fReal;
	BTextControl*	fPassword;
	BTextControl*	fRepeat;
	BCheckBox*		fAdmin;
};


class NoteView : public BTextView {
public:
	NoteView()
		:
		BTextView("note", B_WILL_DRAW)
	{
		MakeEditable(false);
		MakeSelectable(false);
		SetStylable(false);
		SetWordWrap(true);
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
		SetInsets(0, 0, 0, 0);
		SetText("New people get user id 1000 or higher and a folder under "
			"/boot/home/users. Those folders are private. Sign in starts "
			"the desktop as that person. One screen. An empty password is "
			"allowed. Add, disable, and delete need an administrator.");
		SetExplicitMinSize(BSize(280, 72));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 72));
		SetExplicitPreferredSize(BSize(420, 56));
	}

	virtual void
	FrameResized(float width, float height)
	{
		BTextView::FrameResized(width, height);
		SetTextRect(Bounds());
	}
};


class UsersWindow : public BWindow {
public:
	UsersWindow()
		:
		BWindow(BRect(80, 80, 520, 420), "Users & Accounts",
			B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
		fList(NULL)
	{
		BStringView* title = new BStringView("title",
			"Accounts on this computer");
		BFont font(be_bold_font);
		title->SetFont(&font);

		fList = new BListView("users");
		fList->SetSelectionMessage(new BMessage(kMsgSelect));
		fList->SetExplicitMinSize(BSize(280, 140));
		fAdd = new BButton("add", "Add...", new BMessage(kMsgAdd));
		fDisable = new BButton("disable", "Disable", new BMessage(kMsgDisable));
		fDelete = new BButton("delete", "Delete...", new BMessage(kMsgDelete));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(title)
			.Add(new NoteView())
			.Add(new BScrollView("scroll", fList, 0, false, true))
			.AddGroup(B_HORIZONTAL)
				.Add(fAdd)
				.Add(fDisable)
				.Add(fDelete)
				.AddGlue()
				.Add(new AdminLockBar())
			.End();

		_Fill();
		CenterOnScreen();
	}

	virtual void
	MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgAdd:
				_Add();
				break;
			case kMsgRefresh:
			case kMsgSelect:
				if (message->what == kMsgRefresh)
					_Fill();
				else
					_UpdateButtons();
				break;
			case kMsgDisable:
				_DisableOrEnable();
				break;
			case kMsgDelete:
				_Delete();
				break;
			case kMsgAdminLockChanged:
				_UpdateButtons();
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	void
	_Fill()
	{
		fList->MakeEmpty();
		setpwent();
		for (;;) {
			struct passwd* pwd = getpwent();
			if (pwd == NULL)
				break;
			if (!is_listed_account(pwd))
				continue;

			const char* home = pwd->pw_dir != NULL ? pwd->pw_dir : "";
			const char* gecos = pwd->pw_gecos != NULL ? pwd->pw_gecos : "";
			bool admin = is_admin_user(pwd);
			bool disabled = is_disabled_user(pwd->pw_name);
			BString extra;
			if (gecos[0] != '\0')
				extra.SetToFormat("  (%s)", gecos);
			BString line;
			line.SetToFormat("%s  uid %" B_PRId32 "  %s  %s%s%s",
				pwd->pw_name, (int32)pwd->pw_uid, home,
				admin ? "administrator" : "standard",
				disabled ? "  disabled" : "", extra.String());
			fList->AddItem(new UserItem(line.String(), pwd->pw_name,
				pwd->pw_uid, admin, disabled));
		}
		endpwent();
		if (fList->CountItems() == 0) {
			fList->AddItem(new BStringItem(
				"(no accounts in the password file)"));
		}
		_UpdateButtons();
	}

	UserItem*
	_Selected() const
	{
		return dynamic_cast<UserItem*>(
			fList->ItemAt(fList->CurrentSelection()));
	}

	bool
	_SelectedIsFirstHome() const
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return false;
		struct passwd* pwd = getpwnam(item->Name());
		return is_first_home_admin(pwd);
	}

	void
	_UpdateButtons()
	{
		bool admin = r5_admin_change_allowed();
		fAdd->SetEnabled(admin);
		UserItem* item = _Selected();
		if (!admin || item == NULL) {
			fDisable->SetEnabled(false);
			fDelete->SetEnabled(false);
			fDisable->SetLabel("Disable");
			return;
		}

		bool firstHome = _SelectedIsFirstHome();
		fDelete->SetEnabled(!firstHome);
		fDisable->SetEnabled(!firstHome);
		fDisable->SetLabel(item->IsDisabled() ? "Enable" : "Disable");
	}

	void
	_Add()
	{
		if (!r5_admin_change_allowed()) {
			show_error("Unlock this with an administrator password.");
			return;
		}
		CreateWindow* window = new CreateWindow(this);
		window->CenterOnScreen();
		window->Show();
	}

	void
	_DisableOrEnable()
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return;
		if (_SelectedIsFirstHome()) {
			show_error("The original administrator account cannot be "
				"disabled.");
			return;
		}

		BMessage request(kMsgAdminSetDisabled);
		request.AddString("name", item->Name());
		request.AddBool("disable", !item->IsDisabled());
		BString error;
		if (ask_login(request, error) != B_OK)
			show_error(error.String());
		_Fill();
	}

	void
	_Delete()
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return;
		if (_SelectedIsFirstHome()) {
			show_error("The original administrator account cannot be deleted.");
			return;
		}
		if (item->Uid() == getuid()) {
			show_error("You cannot delete the account that is signed in.");
			return;
		}

		BString text;
		text.SetToFormat("Delete account \"%s\"?", item->Name());
		BAlert* alert = new BAlert("Delete", text.String(), "Cancel",
			"Keep files", "Delete files", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return;

		BMessage request(kMsgAdminDelete);
		request.AddString("name", item->Name());
		request.AddBool("delete_files", choice == 2);
		BString error;
		if (ask_login(request, error) != B_OK)
			show_error(error.String());
		_Fill();
	}

	BListView*	fList;
	BButton*	fAdd;
	BButton*	fDisable;
	BButton*	fDelete;
};


class UsersApp : public BApplication {
public:
	UsersApp()
		:
		BApplication(kSignature)
	{
	}

	virtual void
	ReadyToRun()
	{
		lock_private_homes();
		(new UsersWindow())->Show();
	}
};


int
main()
{
	UsersApp app;
	app.Run();
	return 0;
}
