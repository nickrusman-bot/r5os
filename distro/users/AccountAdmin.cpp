/*
 * R5OS account helper. Login (uid 0) migrates the first home off root
 * and applies Users & Accounts changes for an administrator sender.
 */


#include "AccountAdmin.h"

#include "HomeLock.h"

#include <Message.h>
#include <OS.h>
#include <String.h>

#include <RegistrarDefs.h>
#include <user_group.h>
#include <util/KMessage.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


const char* kR5LoginSignature = "application/x-vnd.Haiku-Login";
const char* kAdminGroupName = "admin";

static const char* kHome = "/boot/home";
static const char* kUsersRoot = "/boot/home/users";
static const char* kSessionPath = "/boot/system/settings/r5-desktop-session";
static const char* kSystemRootName = "root";


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


bool
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
	if (strcmp(home, kHome) == 0)
		return true;
	if (strncmp(home, "/boot/home/", 11) == 0)
		return true;
	return false;
}


bool
is_admin_user(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL)
		return false;

	uid_t uid = pwd->pw_uid;
	gid_t gid = pwd->pw_gid;
	BString name(pwd->pw_name);
	BString home(pwd->pw_dir != NULL ? pwd->pw_dir : "");
	if (uid == 0 || gid == 0)
		return true;
	if (home == kHome)
		return true;
	return group_has_member("wheel", name.String())
		|| group_has_member(kAdminGroupName, name.String());
}


bool
is_disabled_user(const char* name)
{
	struct spwd* shadow = getspnam(name);
	if (shadow == NULL || shadow->sp_pwdp == NULL)
		return false;
	return shadow->sp_pwdp[0] == '!';
}


bool
is_first_home_admin(const struct passwd* pwd)
{
	return pwd != NULL && pwd->pw_dir != NULL
		&& strcmp(pwd->pw_dir, kHome) == 0;
}


bool
current_user_is_admin()
{
	if (geteuid() == 0)
		return true;
	return is_admin_user(getpwuid(getuid()));
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
	if (getgrnam(kAdminGroupName) != NULL)
		return B_OK;
	char* argv[] = {
		const_cast<char*>("/bin/groupadd"),
		const_cast<char*>(kAdminGroupName),
		NULL
	};
	return run_argv(argv, errorText);
}


static status_t
add_to_group(const char* groupName, const char* user, BString& errorText)
{
	if (groupName == NULL || user == NULL)
		return B_BAD_VALUE;
	if (getgrnam(groupName) == NULL)
		return B_NAME_NOT_FOUND;
	if (group_has_member(groupName, user))
		return B_OK;
	char* argv[] = {
		const_cast<char*>("/bin/groupmod"),
		const_cast<char*>("-A"),
		const_cast<char*>(user),
		const_cast<char*>(groupName),
		NULL
	};
	return run_argv(argv, errorText);
}


static void ensure_desktop_links(const char* home);
static void ensure_private_trash(const char* home, uid_t uid, gid_t gid);


static status_t
create_home(const char* home, uid_t uid, gid_t gid)
{
	mkdir(kUsersRoot, 0755);
	if (mkdir(home, 0700) != 0 && errno != EEXIST)
		return errno;
	chmod(home, 0700);
	if (chown(home, uid, gid) != 0)
		return errno;

	static const char* kFolders[] = {
		"Desktop", "Documents", "Pictures", "Videos", "Downloads", "Music",
		"config", NULL
	};
	for (int32 i = 0; kFolders[i] != NULL; i++) {
		BString path;
		path.SetToFormat("%s/%s", home, kFolders[i]);
		mkdir(path.String(), 0700);
		chmod(path.String(), 0700);
		chown(path.String(), uid, gid);
	}

	BString settings;
	settings.SetToFormat("%s/config/settings", home);
	mkdir(settings.String(), 0700);
	chmod(settings.String(), 0700);
	chown(settings.String(), uid, gid);

	BString deskbar;
	deskbar.SetToFormat("%s/config/settings/deskbar", home);
	mkdir(deskbar.String(), 0700);
	chmod(deskbar.String(), 0700);
	chown(deskbar.String(), uid, gid);

	ensure_desktop_links(home);
	ensure_private_trash(home, uid, gid);
	lock_private_homes();
	return B_OK;
}


int32
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


static bool
sender_is_admin(BMessage* request)
{
	if (request == NULL)
		return false;
	if (geteuid() == 0 && !request->IsSourceRemote())
		return true;

	team_id team = request->ReturnAddress().Team();
	team_info info;
	if (get_team_info(team, &info) != B_OK)
		return false;
	if (info.uid == 0)
		return true;
	return is_admin_user(getpwuid(info.uid));
}


static uid_t
first_free_person_uid()
{
	uid_t uid = kMinPersonUid;
	while (getpwuid(uid) != NULL)
		uid++;
	return uid;
}


static gid_t
users_gid()
{
	struct group* group = getgrnam("users");
	if (group != NULL)
		return group->gr_gid;
	return 100;
}


static status_t
add_unix_user(const char* name, uid_t uid, gid_t gid, const char* home,
	const char* shell, const char* realName, const char* shadow)
{
	BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
	if (message.AddInt32("uid", uid) != B_OK
		|| message.AddInt32("gid", gid) != B_OK
		|| message.AddString("name", name) != B_OK
		|| message.AddString("password", "x") != B_OK
		|| message.AddString("home", home != NULL ? home : "") != B_OK
		|| message.AddString("shell",
			shell != NULL && shell[0] != '\0' ? shell : "/bin/sh") != B_OK
		|| message.AddString("real name",
			realName != NULL ? realName : "") != B_OK
		|| message.AddString("shadow password",
			shadow != NULL ? shadow : "") != B_OK
		|| message.AddInt32("last changed", (int32)time(NULL)) != B_OK
		|| message.AddInt32("min", -1) != B_OK
		|| message.AddInt32("max", -1) != B_OK
		|| message.AddInt32("warn", 7) != B_OK
		|| message.AddInt32("inactive", -1) != B_OK
		|| message.AddInt32("expiration", 99999) != B_OK
		|| message.AddInt32("flags", 0) != B_OK
		|| message.AddBool("add user", true) != B_OK)
		return B_NO_MEMORY;

	BPrivate::KMessage reply;
	return BPrivate::send_authentication_request_to_registrar(message, reply);
}


static status_t
delete_unix_user(const char* name)
{
	BPrivate::KMessage message(BPrivate::B_REG_DELETE_USER);
	if (message.AddString("name", name) != B_OK)
		return B_NO_MEMORY;
	BPrivate::KMessage reply;
	return BPrivate::send_authentication_request_to_registrar(message, reply);
}


static status_t
ensure_system_root()
{
	if (getpwuid(0) != NULL)
		return B_OK;
	if (getpwnam(kSystemRootName) != NULL)
		return B_NAME_IN_USE;
	return add_unix_user(kSystemRootName, 0, 0, "/boot/system", "/bin/sh",
		"System", "!");
}


static bool
is_other_person_home(const char* path)
{
	const char* prefix = "/boot/home/users/";
	if (strncmp(path, prefix, 17) != 0)
		return false;
	const char* rest = path + 17;
	if (rest[0] == '\0')
		return false;

	char name[64];
	size_t i = 0;
	while (rest[i] != '\0' && rest[i] != '/' && i + 1 < sizeof(name)) {
		name[i] = rest[i];
		i++;
	}
	name[i] = '\0';
	if (name[0] == '\0')
		return false;

	BString home;
	home.SetToFormat("%s/%s", kUsersRoot, name);
	struct passwd* pwd = getpwnam(name);
	return pwd != NULL && pwd->pw_dir != NULL
		&& strcmp(pwd->pw_dir, home.String()) == 0;
}


static void
chown_tree(const char* path, uid_t uid, gid_t gid, dev_t homeDevice)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return;
	if (st.st_dev != homeDevice)
		return;
	if (S_ISLNK(st.st_mode))
		return;
	if (is_other_person_home(path))
		return;

	lchown(path, uid, gid);
	if (!S_ISDIR(st.st_mode))
		return;

	DIR* dir = opendir(path);
	if (dir == NULL)
		return;
	while (struct dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0)
			continue;
		char child[PATH_MAX];
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		if (is_other_person_home(child))
			continue;
		chown_tree(child, uid, gid, homeDevice);
	}
	closedir(dir);
}


static void
ensure_desktop_link(const char* home, const char* name, const char* target)
{
	BString path;
	path.SetToFormat("%s/Desktop/%s", home, name);
	struct stat st;
	if (lstat(path.String(), &st) == 0)
		return;
	symlink(target, path.String());
}


static void
ensure_desktop_links(const char* home)
{
	if (home == NULL)
		return;
	ensure_desktop_link(home, "Home", home);
	ensure_desktop_link(home, "Quick Tour", "/boot/system/bin/quicktour");
	ensure_desktop_link(home, "User Guide", "/boot/system/bin/userguide");
}


// Trash follows the account home ($h/trash). The image only had /boot/trash,
// which was mode 0700 for the administrator, so a standard account got no icon.
static void
ensure_private_trash(const char* home, uid_t uid, gid_t gid)
{
	if (home == NULL)
		return;

	BString dest;
	dest.SetToFormat("%s/trash", home);
	struct stat st;
	if (lstat(dest.String(), &st) != 0) {
		if (strcmp(home, kHome) == 0 && lstat("/boot/trash", &st) == 0
			&& S_ISDIR(st.st_mode)) {
			if (rename("/boot/trash", dest.String()) != 0)
				mkdir(dest.String(), 0700);
		} else
			mkdir(dest.String(), 0700);
	}
	chmod(dest.String(), 0700);
	lchown(dest.String(), uid, gid);
}


static void
adopt_trash(uid_t uid, gid_t gid)
{
	struct stat st;
	if (lstat("/boot/trash", &st) != 0 || !S_ISDIR(st.st_mode))
		return;
	lchown("/boot/trash", uid, gid);
	chmod("/boot/trash", 0700);
}


static void
ensure_person_dirs(uid_t uid, gid_t gid)
{
	static const char* kRelative[] = {
		"Desktop",
		"Documents",
		"Downloads",
		"Music",
		"Pictures",
		"Videos",
		"mail",
		"config",
		"config/settings",
		"config/settings/deskbar",
		NULL
	};
	for (int32 i = 0; kRelative[i] != NULL; i++) {
		BString path;
		path.SetToFormat("%s/%s", kHome, kRelative[i]);
		mkdir(path.String(), 0700);
		chmod(path.String(), 0700);
		lchown(path.String(), uid, gid);
	}
}


static status_t
find_first_home_admin(BString& name, uid_t& uid, gid_t& gid)
{
	struct passwd* named = getpwnam("user");
	if (named != NULL && is_first_home_admin(named)) {
		name = named->pw_name;
		uid = named->pw_uid;
		gid = named->pw_gid;
		return B_OK;
	}

	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_first_home_admin(pwd) || !is_listed_account(pwd))
			continue;
		name = pwd->pw_name;
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
		endpwent();
		return B_OK;
	}
	endpwent();
	return B_NAME_NOT_FOUND;
}


status_t
migrate_admin_off_root()
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	BString name;
	uid_t uid = 0;
	gid_t gid = 0;
	status_t status = find_first_home_admin(name, uid, gid);
	if (status != B_OK)
		return status;
	if (uid != 0) {
		ensure_system_root();
		ensure_person_dirs(uid, gid);
		adopt_trash(uid, gid);
		return B_OK;
	}

	struct stat homeStat;
	if (lstat(kHome, &homeStat) != 0 || !S_ISDIR(homeStat.st_mode))
		return B_NOT_A_DIRECTORY;

	struct passwd* pwd = getpwnam(name.String());
	if (pwd == NULL)
		return ENOENT;

	BString home(pwd->pw_dir != NULL ? pwd->pw_dir : kHome);
	BString shell(pwd->pw_shell != NULL ? pwd->pw_shell : "/bin/sh");
	BString realName(pwd->pw_gecos != NULL ? pwd->pw_gecos : "");
	BString shadow;
	struct spwd* spwd = getspnam(name.String());
	if (spwd != NULL && spwd->sp_pwdp != NULL)
		shadow = spwd->sp_pwdp;

	uid_t newUid = first_free_person_uid();
	gid_t newGid = users_gid();

	status = delete_unix_user(name.String());
	if (status != B_OK)
		return status;

	status = ensure_system_root();
	if (status != B_OK) {
		add_unix_user(name.String(), 0, 0, home.String(), shell.String(),
			realName.String(), shadow.String());
		return status;
	}

	status = add_unix_user(name.String(), newUid, newGid, home.String(),
		shell.String(), realName.String(), shadow.String());
	if (status != B_OK) {
		add_unix_user(name.String(), 0, 0, home.String(), shell.String(),
			realName.String(), shadow.String());
		return status;
	}

	endpwent();
	pwd = getpwnam(name.String());
	if (pwd == NULL || pwd->pw_uid != newUid)
		return B_ERROR;

	BString error;
	ensure_admin_group(error);
	add_to_group(kAdminGroupName, name.String(), error);
	add_to_group("users", name.String(), error);
	add_to_group("wheel", name.String(), error);

	chown_tree(kHome, newUid, newGid, homeStat.st_dev);
	ensure_person_dirs(newUid, newGid);
	adopt_trash(newUid, newGid);
	lock_private_homes();
	return B_OK;
}


static bool
is_session_home(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_dir == NULL || pwd->pw_name == NULL)
		return false;
	if (strcmp(pwd->pw_dir, kHome) == 0)
		return true;

	BString expected;
	expected.SetToFormat("%s/%s", kUsersRoot, pwd->pw_name);
	return expected == pwd->pw_dir;
}


status_t
write_desktop_session(const char* login)
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;
	if (login == NULL || login[0] == '\0')
		return B_BAD_VALUE;
	if (is_disabled_user(login))
		return B_NOT_ALLOWED;

	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL || strcmp(pwd->pw_name, login) != 0)
		return ENOENT;
	if (!is_listed_account(pwd) || !is_session_home(pwd))
		return B_NOT_ALLOWED;
	if (pwd->pw_uid == 0)
		return B_NOT_ALLOWED;

	// getpwnam() uses one static buffer. Copy before create_home().
	char name[64];
	if (snprintf(name, sizeof(name), "%s", pwd->pw_name) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;
	if (strchr(name, '\n') != NULL || strchr(name, '\r') != NULL)
		return B_BAD_VALUE;
	uid_t uid = pwd->pw_uid;
	gid_t gid = pwd->pw_gid;
	BString home(pwd->pw_dir);
	bool firstHome = home == kHome;

	// Do not run ensure_person_dirs() here: that always rewrites /boot/home
	// and would give a standard account the administrator's folders.
	if (firstHome)
		ensure_person_dirs(uid, gid);
	else {
		status_t status = create_home(home.String(), uid, gid);
		if (status != B_OK)
			return status;
	}
	ensure_desktop_links(home.String());
	ensure_private_trash(home.String(), uid, gid);

	FILE* file = fopen(kSessionPath, "w");
	if (file == NULL)
		return errno;
	if (fprintf(file, "%s\n", name) < 0) {
		fclose(file);
		unlink(kSessionPath);
		return EIO;
	}
	if (fclose(file) != 0)
		return errno;
	if (chmod(kSessionPath, 0600) != 0 || chown(kSessionPath, 0, 0) != 0)
		return errno;

	lock_private_homes();
	return B_OK;
}


status_t
clear_desktop_session()
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	unlink(kSessionPath);
	unlink("/tmp/r5-desktop-session");
	return B_OK;
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
		const_cast<char*>(real != NULL ? real : ""),
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
	if (pwd->pw_uid < kMinPersonUid) {
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

	if (admin && add_to_group(kAdminGroupName, name, error) != B_OK) {
		ensure_admin_group(error);
		if (add_to_group(kAdminGroupName, name, error) != B_OK) {
			BString text;
			text.SetToFormat("The account exists, but it is not an "
				"administrator: %s", error.String());
			error = text;
			return B_ERROR;
		}
	}

	return B_OK;
}


static status_t
set_disabled(const char* name, bool disable, BString& error)
{
	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || !is_listed_account(pwd)) {
		error = "That account was not found.";
		return ENOENT;
	}
	if (is_first_home_admin(pwd)) {
		error = "The original administrator account cannot be disabled.";
		return B_NOT_ALLOWED;
	}

	if (!disable) {
		struct spwd* shadow = getspnam(name);
		const char* current = "";
		if (shadow != NULL && shadow->sp_pwdp != NULL)
			current = shadow->sp_pwdp;
		if (current[0] == '!')
			current++;
		if (set_shadow_password(name, current) != B_OK) {
			error = "Could not enable the account.";
			return B_ERROR;
		}
		return B_OK;
	}

	if (is_admin_user(pwd) && admin_count() <= 1) {
		error = "There must be at least one administrator.";
		return B_NOT_ALLOWED;
	}

	BString locked("!");
	struct spwd* shadow = getspnam(name);
	if (shadow != NULL && shadow->sp_pwdp != NULL
		&& shadow->sp_pwdp[0] != '!')
		locked << shadow->sp_pwdp;
	if (set_shadow_password(name, locked.String()) != B_OK) {
		error = "Could not disable the account.";
		return B_ERROR;
	}
	return B_OK;
}


static status_t
delete_account(const char* name, bool deleteFiles, BString& error)
{
	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || !is_listed_account(pwd)) {
		error = "That account was not found.";
		return ENOENT;
	}
	if (is_first_home_admin(pwd)) {
		error = "The original administrator account cannot be deleted.";
		return B_NOT_ALLOWED;
	}
	if (pwd->pw_uid == getuid()) {
		error = "You cannot delete the account that is signed in.";
		return B_NOT_ALLOWED;
	}
	if (is_admin_user(pwd) && !is_disabled_user(name) && admin_count() <= 1) {
		error = "There must be at least one administrator.";
		return B_NOT_ALLOWED;
	}

	BString home;
	if (pwd->pw_dir != NULL)
		home = pwd->pw_dir;

	char* argv[] = {
		const_cast<char*>("/bin/userdel"),
		const_cast<char*>(name),
		NULL
	};
	if (run_argv(argv, error) != B_OK)
		return B_ERROR;

	if (deleteFiles && home.FindFirst("/boot/home/users/") == 0) {
		char* rmArgv[] = {
			const_cast<char*>("/bin/rm"),
			const_cast<char*>("-rf"),
			const_cast<char*>(home.String()),
			NULL
		};
		run_argv(rmArgv, error);
	}
	return B_OK;
}


status_t
handle_admin_message(BMessage* request, BMessage& reply)
{
	reply.MakeEmpty();
	if (request == NULL)
		return B_BAD_VALUE;
	if (geteuid() != 0) {
		reply.AddString("text", "Account changes require the sign-in helper.");
		return B_PERMISSION_DENIED;
	}
	if (!sender_is_admin(request)) {
		reply.AddString("text", "Only an administrator can change accounts.");
		return B_PERMISSION_DENIED;
	}

	BString error;
	status_t status = B_BAD_VALUE;
	switch (request->what) {
		case kMsgAdminAdd:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = create_account(name, request->GetString("real"),
				request->GetString("password"),
				request->GetBool("admin", false), error);
			break;
		}
		case kMsgAdminDelete:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = delete_account(name, request->GetBool("delete_files",
				false), error);
			break;
		}
		case kMsgAdminSetDisabled:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = set_disabled(name, request->GetBool("disable", true),
				error);
			break;
		}
		default:
			error = "Unknown account request.";
			status = B_BAD_VALUE;
			break;
	}

	if (status != B_OK) {
		if (error.Length() > 0)
			reply.AddString("text", error.String());
		reply.AddInt32("error", status);
	}
	return status;
}
