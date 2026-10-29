/*
 * R5OS Users & Accounts. Haiku #8940.
 */


#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <SupportDefs.h>
#include <TextControl.h>
#include <Window.h>

#include <RegistrarDefs.h>
#include <user_group.h>
#include <util/KMessage.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


static const char* kSignature = "application/x-vnd.R5OS-Users";
static const char* kUsersRoot = "/boot/home/users";
static const char* kAdminGroup = "admin";
static const uid_t kMinNewUid = 1000;

static const uint32 kMsgAdd = 'uadd';
static const uint32 kMsgDelete = 'udel';
static const uint32 kMsgDisable = 'udis';
static const uint32 kMsgRefresh = 'urfr';
static const uint32 kMsgSelect = 'usel';


static bool
is_listed_account(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL || pwd->pw_name[0] == '\0')
		return false;
	if (strcmp(pwd->pw_name, "sshd") == 0
		|| strcmp(pwd->pw_name, "ftp") == 0
		|| strcmp(pwd->pw_name, "nobody") == 0)
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
group_has_member(const char* groupName, const char* user)
{
	struct group* group = getgrnam(groupName);
	if (group == NULL || group->gr_mem == NULL || user == NULL)
		return false;
	for (int32 i = 0; group->gr_mem[i] != NULL; i++) {
		if (strcmp(group->gr_mem[i], user) == 0)
			return true;
	}
	return false;
}


static bool
is_admin_user(const struct passwd* pwd)
{
	if (pwd == NULL)
		return false;
	if (pwd->pw_uid == 0 || pwd->pw_gid == 0)
		return true;
	return group_has_member("wheel", pwd->pw_name)
		|| group_has_member(kAdminGroup, pwd->pw_name);
}


static bool
is_disabled_user(const char* name)
{
	struct spwd* shadow = getspnam(name);
	if (shadow == NULL || shadow->sp_pwdp == NULL)
		return false;
	return shadow->sp_pwdp[0] == '!';
}


static bool
valid_username(const char* name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) > 31)
		return false;
	if (!islower((unsigned char)name[0]) && name[0] != '_')
		return false;
	for (const char* p = name; *p != '\0'; p++) {
		if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p)
			&& *p != '_' && *p != '-')
			return false;
	}
	return true;
}


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


static status_t
run_argv(char** argv, BString& errorText)
{
	pid_t pid = fork();
	if (pid < 0) {
		errorText = strerror(errno);
		return errno;
	}
	if (pid == 0) {
		int nullFd = open("/dev/null", O_RDWR);
		if (nullFd >= 0) {
			dup2(nullFd, STDIN_FILENO);
			dup2(nullFd, STDOUT_FILENO);
			dup2(nullFd, STDERR_FILENO);
			if (nullFd > 2)
				close(nullFd);
		}
		execv(argv[0], argv);
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		errorText = strerror(errno);
		return errno;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		errorText.SetToFormat("%s failed", argv[0]);
		return B_ERROR;
	}
	return B_OK;
}


static void
show_error(const char* text)
{
	BAlert* alert = new BAlert("Users", text, "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


static status_t
set_shadow_password(const char* user, const char* encrypted)
{
	struct passwd* pwd = getpwnam(user);
	if (pwd == NULL)
		return ENOENT;

	BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
	if (message.AddInt32("uid", pwd->pw_uid) != B_OK
		|| message.AddInt32("last changed", (int32)time(NULL)) != B_OK
		|| message.AddString("password", "x") != B_OK
		|| message.AddString("shadow password", encrypted) != B_OK)
		return B_NO_MEMORY;

	BPrivate::KMessage reply;
	return BPrivate::send_authentication_request_to_registrar(message, reply);
}


static status_t
ensure_admin_group(BString& errorText)
{
	if (getgrnam(kAdminGroup) != NULL)
		return B_OK;
	char* argv[] = {
		const_cast<char*>("/bin/groupadd"),
		const_cast<char*>(kAdminGroup),
		NULL
	};
	return run_argv(argv, errorText);
}


static status_t
add_to_admin_group(const char* user, BString& errorText)
{
	status_t status = ensure_admin_group(errorText);
	if (status != B_OK)
		return status;
	char* argv[] = {
		const_cast<char*>("/bin/groupmod"),
		const_cast<char*>("-A"),
		const_cast<char*>(user),
		const_cast<char*>(kAdminGroup),
		NULL
	};
	return run_argv(argv, errorText);
}


static status_t
create_home(const char* home, uid_t uid, gid_t gid)
{
	mkdir(kUsersRoot, 0755);
	if (mkdir(home, 0700) != 0 && errno != EEXIST)
		return errno;
	if (chown(home, uid, gid) != 0)
		return errno;

	static const char* kFolders[] = {
		"Desktop", "Documents", "Pictures", "Videos", "Downloads", "Music",
		"config", NULL
	};
	for (int32 i = 0; kFolders[i] != NULL; i++) {
		BString path;
		path.SetToFormat("%s/%s", home, kFolders[i]);
		mkdir(path.String(), 0755);
		chown(path.String(), uid, gid);
	}

	BString settings;
	settings.SetToFormat("%s/config/settings", home);
	mkdir(settings.String(), 0755);
	chown(settings.String(), uid, gid);
	return B_OK;
}


static int32
admin_count()
{
	int32 count = 0;
	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_listed_account(pwd) || !is_admin_user(pwd))
			continue;
		if (!is_disabled_user(pwd->pw_name))
			count++;
	}
	endpwent();
	return count;
}


static status_t
create_account(const char* name, const char* real, const char* password,
	bool admin, BString& error)
{
	if (!valid_username(name)) {
		error = "Use a lowercase username of letters, digits, underscore "
			"or hyphen.";
		return B_BAD_VALUE;
	}
	if (getpwnam(name) != NULL) {
		error = "That username already exists.";
		return B_NAME_IN_USE;
	}

	BString home;
	home.SetToFormat("%s/%s", kUsersRoot, name);
	char* argv[] = {
		const_cast<char*>("/bin/useradd"),
		const_cast<char*>("-d"),
		const_cast<char*>(home.String()),
		const_cast<char*>("-n"),
		const_cast<char*>(real),
		const_cast<char*>("-s"),
		const_cast<char*>("/bin/sh"),
		const_cast<char*>(name),
		NULL
	};
	status_t status = run_argv(argv, error);
	if (status != B_OK)
		return status;

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL) {
		error = "The account was not created.";
		return B_ERROR;
	}
	if (pwd->pw_uid < kMinNewUid) {
		char* delArgv[] = {
			const_cast<char*>("/bin/userdel"),
			const_cast<char*>(name),
			NULL
		};
		run_argv(delArgv, error);
		error = "The new account did not get a user id of 1000 or higher.";
		return B_ERROR;
	}

	create_home(home.String(), pwd->pw_uid, pwd->pw_gid);

	if (password == NULL || password[0] == '\0') {
		char* delArgv[] = {
			const_cast<char*>("/bin/passwd"),
			const_cast<char*>("-d"),
			const_cast<char*>(name),
			NULL
		};
		if (run_argv(delArgv, error) != B_OK)
			return B_ERROR;
	} else {
		char* encrypted = crypt(password, NULL);
		if (encrypted == NULL) {
			error = "Could not encrypt the password.";
			return B_ERROR;
		}
		if (set_shadow_password(name, encrypted) != B_OK) {
			error = "The account exists, but the password was not set.";
			return B_ERROR;
		}
	}

	if (admin && add_to_admin_group(name, error) != B_OK) {
		BString text;
		text.SetToFormat("The account exists, but it is not an administrator: "
			"%s", error.String());
		error = text;
		return B_ERROR;
	}

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

		BString error;
		status_t status = create_account(name, fReal->Text(),
			password.String(), fAdmin->Value() == B_CONTROL_ON, error);
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


class UsersWindow : public BWindow {
public:
	UsersWindow()
		:
		BWindow(BRect(80, 80, 620, 460), "Users & Accounts",
			B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
		fList(NULL)
	{
		BStringView* title = new BStringView("title",
			"Accounts on this computer");
		BFont font(be_bold_font);
		title->SetFont(&font);

		BStringView* note = new BStringView("note",
			"New accounts get user id 1000 or higher (often 1001, because "
			"sshd already uses 1000) and a folder under /boot/home/users. "
			"They still share this desktop until login exists. An empty "
			"password means anyone at the keyboard can use the account.");
		note->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		fList = new BListView("users");
		fList->SetSelectionMessage(new BMessage(kMsgSelect));
		fAdd = new BButton("add", "Add...", new BMessage(kMsgAdd));
		fDisable = new BButton("disable", "Disable", new BMessage(kMsgDisable));
		fDelete = new BButton("delete", "Delete...", new BMessage(kMsgDelete));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(title)
			.Add(note)
			.Add(new BScrollView("scroll", fList, 0, false, true))
			.AddGroup(B_HORIZONTAL)
				.Add(fAdd)
				.Add(fDisable)
				.Add(fDelete)
				.AddGlue()
			.End();

		_Fill();
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

	void
	_UpdateButtons()
	{
		bool root = geteuid() == 0;
		fAdd->SetEnabled(root);
		UserItem* item = _Selected();
		if (!root || item == NULL) {
			fDisable->SetEnabled(false);
			fDelete->SetEnabled(false);
			fDisable->SetLabel("Disable");
			return;
		}

		bool originalAdmin = item->Uid() == 0;
		fDelete->SetEnabled(!originalAdmin);
		fDisable->SetEnabled(!originalAdmin);
		fDisable->SetLabel(item->IsDisabled() ? "Enable" : "Disable");
	}

	void
	_Add()
	{
		if (geteuid() != 0) {
			show_error("Only the administrator session can add accounts.");
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
		if (item->Uid() == 0) {
			show_error("The original administrator account cannot be "
				"disabled.");
			return;
		}

		if (item->IsDisabled()) {
			struct spwd* shadow = getspnam(item->Name());
			const char* current = "";
			if (shadow != NULL && shadow->sp_pwdp != NULL)
				current = shadow->sp_pwdp;
			if (current[0] == '!')
				current++;
			if (set_shadow_password(item->Name(), current) != B_OK)
				show_error("Could not enable the account.");
			_Fill();
			return;
		}

		if (item->IsAdmin() && admin_count() <= 1) {
			show_error("There must be at least one administrator.");
			return;
		}

		BString locked("!");
		struct spwd* shadow = getspnam(item->Name());
		if (shadow != NULL && shadow->sp_pwdp != NULL
			&& shadow->sp_pwdp[0] != '!')
			locked << shadow->sp_pwdp;
		if (set_shadow_password(item->Name(), locked.String()) != B_OK)
			show_error("Could not disable the account.");
		_Fill();
	}

	void
	_Delete()
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return;
		if (item->Uid() == 0) {
			show_error("The original administrator account cannot be deleted.");
			return;
		}
		if (item->Uid() == getuid()) {
			show_error("You cannot delete the account that is signed in.");
			return;
		}
		if (item->IsAdmin() && !item->IsDisabled() && admin_count() <= 1) {
			show_error("There must be at least one administrator.");
			return;
		}

		BString text;
		text.SetToFormat("Delete account \"%s\"?", item->Name());
		BAlert* alert = new BAlert("Delete", text.String(), "Cancel",
			"Keep files", "Delete files", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return;

		BString home;
		struct passwd* pwd = getpwnam(item->Name());
		if (pwd != NULL && pwd->pw_dir != NULL)
			home = pwd->pw_dir;

		BString error;
		BString userName(item->Name());
		char* argv[] = {
			const_cast<char*>("/bin/userdel"),
			const_cast<char*>(userName.String()),
			NULL
		};
		if (run_argv(argv, error) != B_OK) {
			show_error(error.String());
			return;
		}

		if (choice == 2 && home.FindFirst("/boot/home/users/") == 0) {
			char* rmArgv[] = {
				const_cast<char*>("/bin/rm"),
				const_cast<char*>("-rf"),
				const_cast<char*>(home.String()),
				NULL
			};
			run_argv(rmArgv, error);
		}

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
