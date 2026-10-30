/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginApp.h"

#include "AccountAdmin.h"
#include "DesktopWindow.h"
#include "HomeLock.h"
#include "LockWindow.h"
#include "LoginWindow.h"
#include "SessionLog.h"

#include <Alert.h>
#include <LaunchRoster.h>
#include <List.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <Screen.h>
#include <String.h>
#include <StringList.h>

#include <errno.h>
#include <image.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

extern char** environ;

const char* kLoginAppSig = "application/x-vnd.Haiku-Login";


static bool
keep_team(const app_info& info, team_id self)
{
	if (info.team == self)
		return true;
	if ((info.flags & B_BACKGROUND_APP) != 0)
		return true;

	const char* signature = info.signature;
	if (signature == NULL || signature[0] == '\0')
		return true;
	if (strcmp(signature, kLoginAppSig) == 0)
		return true;
	if (strcmp(signature, "application/x-vnd.Be-TRAK") == 0)
		return true;
	if (strcmp(signature, "application/x-vnd.Be-TSKB") == 0)
		return true;
	if (strcmp(signature, "application/x-vnd.R5OS-airplay") == 0)
		return true;
	if (strstr(signature, "server") != NULL
		|| strstr(signature, "daemon") != NULL
		|| strstr(signature, "launch") != NULL
		|| strstr(signature, "registrar") != NULL
		|| strstr(signature, "ROST") != NULL)
		return true;

	BPath path(&info.ref);
	if (path.InitCheck() == B_OK && path.Path() != NULL
		&& strstr(path.Path(), "/servers/") != NULL)
		return true;
	return false;
}


static const char* kSessionJobs[] = {
	"x-vnd.Be-TRAK",
	"x-vnd.Be-TSKB",
	"x-vnd.R5OS-NetworkStatus",
	"x-vnd.R5OS-airplay-tray",
	"x-vnd.R5OS-vbox-clip",
	NULL
};


static bool
is_session_job(const char* name)
{
	if (name == NULL)
		return false;
	for (int32 i = 0; kSessionJobs[i] != NULL; i++) {
		if (strcmp(name, kSessionJobs[i]) == 0)
			return true;
	}
	return strstr(name, "Be-TRAK") != NULL
		|| strstr(name, "Be-TSKB") != NULL
		|| strstr(name, "NetworkStatus") != NULL
		|| strstr(name, "airplay-tray") != NULL
		|| strstr(name, "vbox-clip") != NULL;
}


LoginApp::LoginApp()
	:
	BApplication(kLoginAppSig),
	fDesktopWindow(NULL),
	fLoginWindow(NULL),
	fLockWindow(NULL),
	fEditShelfMode(false)
{
}


LoginApp::~LoginApp()
{
}


void
LoginApp::ReadyToRun()
{
	BScreen screen;

	if (fEditShelfMode) {
		BAlert* alert = new BAlert("Info",
			"Drop replicants on this desktop to customize the login "
			"background. Quit with Cmd-Q when you are finished.",
			"OK");
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		fDesktopWindow = new DesktopWindow(screen.Frame(), true);
		fDesktopWindow->Show();
		return;
	}

	lock_private_homes();
	refresh_machine_settings_access();
	fDesktopWindow = new DesktopWindow(screen.Frame(), false);
	fDesktopWindow->Show();
	_OpenLoginWindow();
}


void
LoginApp::MessageReceived(BMessage* message)
{
	bool reboot = false;

	switch (message->what) {
		case kAttemptLogin:
			TryLogin(message);
			break;
		case kMsgAdminAdd:
		case kMsgAdminDelete:
		case kMsgAdminSetDisabled:
		case kMsgAdminQuery:
		case kMsgAdminUnlock:
		case kMsgAdminRelock:
		case kMsgAdminSetClock:
		case kMsgAdminSetTimezone:
		case kMsgAdminSetClockGmt:
		case kMsgAdminGetProfile:
		case kMsgAdminSetProfile:
			// New account messages must be listed here. An unhandled
			// message still gets an empty reply, and Users treats a missing
			// "error" field as failure.
			HandleAdmin(message);
			break;
		case kLogoutAction:
			Logout();
			break;
		case kLockAction:
			Lock();
			break;
		case kUnlockAction:
			Unlock(message);
			break;
		case kRebootAction:
			reboot = true;
			// FALLTHROUGH
		case kHaltAction:
		{
			BRoster roster;
			BRoster::Private rosterPrivate(roster);
			status_t error = rosterPrivate.ShutDown(reboot, false, false);
			if (error < B_OK) {
				BString msg("Could not shut down: ");
				msg << strerror(error);
				BAlert* alert = new BAlert("Error", msg.String(), "OK");
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			}
			break;
		}
		case kSuspendAction:
		{
			BAlert* alert = new BAlert("Error", "Sleep is not available yet.",
				"OK");
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			break;
		}
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
LoginApp::ArgvReceived(int32 argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		BString arg(argv[i]);
		if (arg == "--edit")
			fEditShelfMode = true;
		else {
			fprintf(stderr, "Login [--edit]\n");
			exit(arg == "--help" ? 0 : 1);
		}
	}
}


void
LoginApp::TryLogin(BMessage* message)
{
	BMessage reply(kLoginBad);
	status_t status = B_BAD_VALUE;

	const char* login = NULL;
	if (message->FindString("login", &login) == B_OK) {
		const char* password = message->GetString("password");
		status = ValidateLogin(login, password);
		if (status == B_OK) {
			status = migrate_admin_off_root();
			if (status == B_OK)
				status = write_desktop_session(login);
			if (status == B_OK) {
				r5_session_log("sign-in %s", login);
				_EnableDesktopJobs();
				status = _WaitGone("application/x-vnd.Be-TRAK");
				if (status == B_OK)
					status = _WaitGone("application/x-vnd.Be-TSKB");
				if (status == B_OK)
					status = _LaunchSessionApp("/system/Tracker");
				if (status == B_OK)
					status = _LaunchSessionApp("/system/Deskbar");
				if (status == B_OK)
					status = _WaitSessionTeam("application/x-vnd.Be-TRAK");
				if (status == B_OK)
					status = _WaitSessionTeam("application/x-vnd.Be-TSKB");
				// Do not Target("desktop"). That asks launch_daemon user,
				// still uid 0 from autologin, to start Tracker again.
				if (status == B_OK) {
					_LaunchSessionApp("/system/apps/r5-tray");
					_LaunchSessionApp("/system/bin/desklink",
						"--add-volume");
					_LaunchSessionApp("/system/servers/airplay_deskbar");
					_LaunchSessionApp("/system/servers/r5_vbox_clip");
					_LaunchSessionHooks();
				}
				r5_session_log("sign-in done: %s", strerror(status));
			}
			if (status == B_OK)
				_HideLogin();
		}
	}

	if (status == B_OK) {
		reply.what = kLoginOk;
		message->SendReply(&reply);
	} else {
		reply.AddInt32("error", status);
		if (status == B_PERMISSION_DENIED || status == ENOENT)
			reply.AddString("text", "That password is not right.");
		else if (status == B_NAME_NOT_FOUND || status == B_TIMED_OUT)
			reply.AddString("text", "Could not start the desktop.");
		else {
			BString text("Could not start the session: ");
			text << strerror(status);
			reply.AddString("text", text.String());
		}
		message->SendReply(&reply);
	}
}


void
LoginApp::HandleAdmin(BMessage* message)
{
	BMessage reply;
	handle_admin_message(message, reply);
	message->SendReply(&reply);
}


void
LoginApp::Lock()
{
	BString name;
	if (signed_in_name(name) != B_OK)
		return;

	if (fLockWindow != NULL) {
		if (fLockWindow->Lock()) {
			fLockWindow->Activate(true);
			fLockWindow->Unlock();
		}
		return;
	}

	// Walked away: a preference grant must not stay open behind the lock.
	clear_admin_grants();
	r5_session_log("lock %s", name.String());
	BScreen screen;
	fLockWindow = new LockWindow(screen.Frame(), name.String());
	fLockWindow->Show();
}


void
LoginApp::Unlock(BMessage* message)
{
	BString name;
	const char* password = message->GetString("password");
	status_t status = signed_in_name(name);
	if (status == B_OK)
		status = ValidateLogin(name.String(), password);

	BMessage reply(status == B_OK ? kLoginOk : kLoginBad);
	if (status != B_OK)
		reply.AddString("text", "That password is not right.");
	message->SendReply(&reply);
	if (status != B_OK)
		return;

	// Same session. Do not launch Tracker again and do not clear the
	// session file.
	r5_session_log("unlock %s", name.String());
	_CloseLock();
}


void
LoginApp::_CloseLock()
{
	if (fLockWindow == NULL)
		return;
	BWindow* window = fLockWindow;
	fLockWindow = NULL;
	if (window->Lock())
		window->Quit();
}


void
LoginApp::Logout()
{
	// Do not call the launch daemon. Stop/SetEnabled/GetJobs wait for a
	// reply; launch_daemon user is the team in the crash dialog, and while
	// it sits in the debugger that reply never comes.
	_CloseLock();
	r5_session_log("logout");
	clear_admin_grants();
	clear_desktop_session();
	_QuitUserApps();
	_WaitGone("application/x-vnd.Haiku-ProcessController");
	_WaitGone("application/x-vnd.Haiku-NetworkStatus");
	_WaitGone("application/x-vnd.R5OS-airplay-tray");
	_WaitGone("application/x-vnd.R5OS-vbox-clip");
	_WaitGone("application/x-vnd.Be-TRAK");
	_WaitGone("application/x-vnd.Be-TSKB");
	_ShowLogin();
	r5_session_log("logout done");
}


status_t
LoginApp::ValidateLogin(const char* login, const char* password)
{
	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL || strcmp(pwd->pw_name, login) != 0)
		return ENOENT;

	if (account_password_matches(pwd, password))
		return B_OK;

	return B_PERMISSION_DENIED;
}


void
LoginApp::_EnableDesktopJobs()
{
	BLaunchRoster roster;
	for (int32 i = 0; kSessionJobs[i] != NULL; i++) {
		roster.SetEnabled(kSessionJobs[i], false);
		roster.Stop(kSessionJobs[i], true);
	}

	BStringList jobs;
	status_t jobsStatus = roster.GetJobs("desktop", jobs);
	if (jobsStatus != B_OK) {
		r5_session_log("GetJobs(desktop): %s", strerror(jobsStatus));
		syslog(LOG_WARNING, "Login: GetJobs(desktop) failed; "
			"session jobs stopped by name");
		return;
	}

	for (int32 i = 0; i < jobs.CountStrings(); i++) {
		const char* name = jobs.StringAt(i);
		if (is_session_job(name)) {
			roster.SetEnabled(name, false);
			roster.Stop(name, true);
		} else
			roster.SetEnabled(name, true);
	}
}


status_t
LoginApp::_LaunchSessionApp(const char* program, const char* argument,
	const char* argument2, const char* argument3, const char* argument4)
{
	const char* args[8];
	int32 count = 0;
	args[count++] = "/system/apps/r5-desktop";
	args[count++] = program;
	if (argument != NULL && argument[0] != '\0')
		args[count++] = argument;
	if (argument2 != NULL && argument2[0] != '\0')
		args[count++] = argument2;
	if (argument3 != NULL && argument3[0] != '\0')
		args[count++] = argument3;
	if (argument4 != NULL && argument4[0] != '\0')
		args[count++] = argument4;
	args[count] = NULL;

	r5_session_log("launch %s", program);
	thread_id thread = load_image(count, args, (const char**)environ);
	if (thread < B_OK) {
		r5_session_log("load_image r5-desktop %s: %s", program,
			strerror(thread));
		return thread;
	}
	resume_thread(thread);

	status_t result = B_ERROR;
	wait_for_thread(thread, &result);
	if (result != B_OK) {
		r5_session_log("r5-desktop %s: %s", program, strerror(result));
		syslog(LOG_ERR, "Login: r5-desktop %s: %s", program, strerror(result));
		return result < B_OK ? result : B_ERROR;
	}
	r5_session_log("r5-desktop %s ok", program);
	return B_OK;
}


void
LoginApp::_LaunchSessionHooks()
{
	status_t status = _LaunchSessionApp("/system/bin/dstcheck");
	if (status != B_OK)
		r5_session_log("dstcheck: %s", strerror(status));

	BPath home;
	if (_SessionHome(home) != B_OK)
		return;

	BPath script(home);
	if (script.Append("config/settings/boot/UserBootscript") == B_OK
		&& access(script.Path(), R_OK) == 0) {
		status = _LaunchSessionApp("/bin/sh", script.Path());
		if (status != B_OK)
			r5_session_log("UserBootscript: %s", strerror(status));
	}

	BPath marker(home);
	if (marker.Append("config/settings/first_login") == B_OK
		&& access(marker.Path(), F_OK) == 0) {
		status = _LaunchSessionApp("/bin/sh", "/system/boot/PostInstallScript",
			"first login", marker.Path(), "/boot/system/boot/first-login");
		if (status != B_OK)
			r5_session_log("first-login: %s", strerror(status));
	}
}


status_t
LoginApp::_SessionHome(BPath& path) const
{
	static const char* kPaths[] = {
		"/boot/system/settings/r5-desktop-session",
		"/tmp/r5-desktop-session",
		NULL
	};

	char name[64];
	name[0] = '\0';
	for (int i = 0; kPaths[i] != NULL; i++) {
		FILE* file = fopen(kPaths[i], "r");
		if (file == NULL)
			continue;
		if (fgets(name, sizeof(name), file) == NULL) {
			fclose(file);
			continue;
		}
		fclose(file);
		break;
	}

	size_t length = strlen(name);
	while (length > 0
		&& (name[length - 1] == '\n' || name[length - 1] == '\r'
			|| name[length - 1] == ' ')) {
		name[--length] = '\0';
	}
	if (length == 0)
		return B_NAME_NOT_FOUND;

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || pwd->pw_dir == NULL || pwd->pw_dir[0] == '\0')
		return ENOENT;
	return path.SetTo(pwd->pw_dir);
}


status_t
LoginApp::_WaitRunning(const char* signature)
{
	const bigtime_t deadline = system_time() + 8000000LL;
	while (system_time() < deadline) {
		if (be_roster->IsRunning(signature))
			return B_OK;
		snooze(100000);
	}
	r5_session_log("timeout waiting for %s", signature);
	return B_TIMED_OUT;
}


status_t
LoginApp::_WaitSessionTeam(const char* signature)
{
	status_t status = _WaitRunning(signature);
	if (status != B_OK)
		return status;

	team_id team = be_roster->TeamFor(signature);
	team_info info;
	if (team < 0 || get_team_info(team, &info) != B_OK) {
		r5_session_log("%s: no team info", signature);
		return B_BAD_TEAM_ID;
	}
	r5_session_log("%s team %d uid=%d gid=%d", signature, (int)team,
		(int)info.uid, (int)info.gid);
	if (info.uid == 0) {
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
		return B_NOT_ALLOWED;
	}

	char name[64];
	name[0] = '\0';
	FILE* file = fopen("/boot/system/settings/r5-desktop-session", "r");
	if (file != NULL) {
		if (fgets(name, sizeof(name), file) == NULL)
			name[0] = '\0';
		fclose(file);
	}
	size_t length = strlen(name);
	while (length > 0
		&& (name[length - 1] == '\n' || name[length - 1] == '\r'
			|| name[length - 1] == ' ')) {
		name[--length] = '\0';
	}
	struct passwd* pwd = length > 0 ? getpwnam(name) : NULL;
	if (pwd == NULL || pwd->pw_uid != info.uid) {
		r5_session_log("%s uid %d is not session %s", signature,
			(int)info.uid, length > 0 ? name : "(none)");
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
		return B_NOT_ALLOWED;
	}
	return B_OK;
}


void
LoginApp::_HideLogin()
{
	if (fLoginWindow != NULL) {
		BWindow* window = fLoginWindow;
		fLoginWindow = NULL;
		if (window->Lock())
			window->Quit();
	}
	if (fDesktopWindow != NULL) {
		BWindow* window = fDesktopWindow;
		fDesktopWindow = NULL;
		if (window->Lock())
			window->Quit();
	}
}


void
LoginApp::_ShowLogin()
{
	if (fDesktopWindow == NULL) {
		BScreen screen;
		fDesktopWindow = new DesktopWindow(screen.Frame(), false);
		fDesktopWindow->Show();
	} else if (fDesktopWindow->Lock()) {
		if (fDesktopWindow->IsHidden())
			fDesktopWindow->Show();
		fDesktopWindow->Unlock();
	}
	_OpenLoginWindow();
}


void
LoginApp::_OpenLoginWindow()
{
	if (fLoginWindow != NULL) {
		BWindow* old = fLoginWindow;
		fLoginWindow = NULL;
		if (old->Lock())
			old->Quit();
	}

	fLoginWindow = new LoginWindow(BRect(0, 0, 360, 320));
	fLoginWindow->Show();
}


void
LoginApp::_QuitUserApps()
{
	BList teams;
	be_roster->GetAppList(&teams);
	team_id self = Team();
	for (int32 i = 0; i < teams.CountItems(); i++) {
		team_id team = (team_id)(addr_t)teams.ItemAt(i);
		app_info info;
		if (be_roster->GetRunningAppInfo(team, &info) != B_OK)
			continue;
		if (keep_team(info, self))
			continue;
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
	}
}


status_t
LoginApp::_WaitGone(const char* signature)
{
	if (!be_roster->IsRunning(signature))
		return B_OK;

	team_id team = be_roster->TeamFor(signature);
	if (team >= 0)
		BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);

	const bigtime_t deadline = system_time() + 3000000LL;
	while (be_roster->IsRunning(signature) && system_time() < deadline)
		snooze(100000);
	if (be_roster->IsRunning(signature)) {
		team = be_roster->TeamFor(signature);
		r5_session_log("killing %s team %d", signature, (int)team);
		if (team >= 0)
			kill_team(team);
		const bigtime_t killDeadline = system_time() + 1000000LL;
		while (be_roster->IsRunning(signature) && system_time() < killDeadline)
			snooze(100000);
	}
	if (be_roster->IsRunning(signature)) {
		r5_session_log("still running %s", signature);
		return B_BUSY;
	}
	return B_OK;
}
