/*
 * R5OS Users & Accounts (read-only). Haiku #8940.
 */


#include <Application.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <Window.h>

#include <grp.h>
#include <pwd.h>
#include <string.h>


static const char* kSignature = "application/x-vnd.R5OS-Users";


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
is_admin_user(const struct passwd* pwd)
{
	if (pwd == NULL)
		return false;
	if (pwd->pw_uid == 0)
		return true;
	if (pwd->pw_gid == 0)
		return true;

	struct group* wheel = getgrnam("wheel");
	if (wheel != NULL && wheel->gr_mem != NULL) {
		for (int32 i = 0; wheel->gr_mem[i] != NULL; i++) {
			if (strcmp(wheel->gr_mem[i], pwd->pw_name) == 0)
				return true;
		}
	}
	return false;
}


class UsersWindow : public BWindow {
public:
	UsersWindow()
		:
		BWindow(BRect(80, 80, 560, 420), "Users & Accounts",
			B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
		fList(NULL)
	{
		BStringView* title = new BStringView("title",
			"Accounts on this computer");
		BFont font(be_bold_font);
		title->SetFont(&font);

		BStringView* note = new BStringView("note",
			"This list is read-only. Login, logout, and separate desktops "
			"are not in this build. Everyone still shares this session.");
		note->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		fList = new BListView("users");
		_Fill();

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(title)
			.Add(note)
			.Add(new BScrollView("scroll", fList, 0, false, true));
	}

private:
	void
	_Fill()
	{
		setpwent();
		for (;;) {
			struct passwd* pwd = getpwent();
			if (pwd == NULL)
				break;
			if (!is_listed_account(pwd))
				continue;

			const char* home = pwd->pw_dir != NULL ? pwd->pw_dir : "";
			const char* gecos = pwd->pw_gecos != NULL ? pwd->pw_gecos : "";
			BString extra;
			if (gecos[0] != '\0')
				extra.SetToFormat("  (%s)", gecos);
			BString line;
			line.SetToFormat("%s  uid %" B_PRId32 "  %s  %s%s",
				pwd->pw_name, (int32)pwd->pw_uid, home,
				is_admin_user(pwd) ? "administrator" : "standard",
				extra.String());
			fList->AddItem(new BStringItem(line.String()));
		}
		endpwent();
		if (fList->CountItems() == 0)
			fList->AddItem(new BStringItem("(no accounts in the password file)"));
	}

	BListView* fList;
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
