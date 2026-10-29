/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginApp.h"

#include "AccountAdmin.h"
#include "DesktopWindow.h"
#include "HomeLock.h"
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
password_matches(const struct passwd* pwd, const char* plain)
{
	if (pwd == NULL)
		return false;

	const char* required = pwd->pw_passwd;
	if (required != NULL && strcmp(required, "x") == 0) {
		struct spwd* shadow = getspnam(pwd->pw_name);
		if (shadow == NULL)
			return false;
		required = shadow->sp_pwdp;
	}

	if (required == NULL || required[0] == '\0')
		return plain == NULL || plain[0] == '\0';
	if (required[0] == '!')
		return false;

	if (plain == NULL)
		plain = "";
	char* encrypted = crypt(plain, required);
	return encrypted != NULL && strcmp(encrypted, required) == 0;
}


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
			HandleAdmin(message);
			break;
		case kLogoutAction:
			Logout();
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
				status = write_desktop_session();
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
				if (status == B_OK)
					status = BLaunchRoster().Target("desktop");
				if (status == B_OK)
					_EnableDesktopJobs();
				if (status == B_OK) {
					_LaunchSessionApp("/system/apps/NetworkStatus",
						"--deskbar");
					_LaunchSessionApp("/system/servers/airplay_deskbar");
					_LaunchSessionApp("/system/servers/r5_vbox_clip");
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
LoginApp::Logout()
{
	_QuitUserApps();
	BLaunchRoster().StopTarget("desktop");
	_WaitGone("application/x-vnd.Be-TRAK");
	_WaitGone("application/x-vnd.Be-TSKB");
	_ShowLogin();
}


status_t
LoginApp::ValidateLogin(const char* login, const char* password)
{
	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL || strcmp(pwd->pw_name, login) != 0)
		return ENOENT;

	if (password_matches(pwd, password))
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
LoginApp::_LaunchSessionApp(const char* program, const char* argument)
{
	const char* args[4];
	int32 count = 0;
	args[count++] = "/system/apps/r5-desktop";
	args[count++] = program;
	if (argument != NULL && argument[0] != '\0')
		args[count++] = argument;
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
	const bigtime_t deadline = system_time() + 8000000LL;
	while (be_roster->IsRunning(signature) && system_time() < deadline) {
		team_id team = be_roster->TeamFor(signature);
		if (team >= 0)
			BMessenger(NULL, team).SendMessage(B_QUIT_REQUESTED);
		snooze(100000);
	}
	return be_roster->IsRunning(signature) ? B_BUSY : B_OK;
}
