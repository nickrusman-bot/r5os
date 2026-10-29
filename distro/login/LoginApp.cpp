/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginApp.h"

#include "DesktopWindow.h"
#include "LoginWindow.h"

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
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

	fDesktopWindow = new DesktopWindow(screen.Frame(), false);
	fDesktopWindow->Show();
	fLoginWindow = new LoginWindow(BRect(0, 0, 360, 320));
	fLoginWindow->Show();
}


void
LoginApp::MessageReceived(BMessage* message)
{
	bool reboot = false;

	switch (message->what) {
		case kAttemptLogin:
			TryLogin(message);
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
			_EnableDesktopJobs();
			status = BLaunchRoster().Target("desktop");
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
		else if (status == B_NAME_NOT_FOUND)
			reply.AddString("text", "Could not start the desktop.");
		else
			reply.AddString("text", strerror(status));
		message->SendReply(&reply);
	}
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
	BStringList jobs;
	BLaunchRoster roster;
	if (roster.GetJobs("desktop", jobs) != B_OK)
		return;

	for (int32 i = 0; i < jobs.CountStrings(); i++)
		roster.SetEnabled(jobs.StringAt(i), true);
}


void
LoginApp::_HideLogin()
{
	if (fLoginWindow != NULL && fLoginWindow->Lock()) {
		if (!fLoginWindow->IsHidden())
			fLoginWindow->Hide();
		fLoginWindow->Unlock();
	}
	if (fDesktopWindow != NULL && fDesktopWindow->Lock()) {
		if (!fDesktopWindow->IsHidden())
			fDesktopWindow->Hide();
		fDesktopWindow->Unlock();
	}
}


void
LoginApp::_ShowLogin()
{
	if (fDesktopWindow != NULL && fDesktopWindow->Lock()) {
		if (fDesktopWindow->IsHidden())
			fDesktopWindow->Show();
		fDesktopWindow->Activate();
		fDesktopWindow->Unlock();
	}
	if (fLoginWindow != NULL && fLoginWindow->Lock()) {
		if (fLoginWindow->IsHidden())
			fLoginWindow->Show();
		fLoginWindow->PrepareForSignIn();
		fLoginWindow->Activate();
		fLoginWindow->Unlock();
	}
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


void
LoginApp::_WaitGone(const char* signature)
{
	const bigtime_t deadline = system_time() + 8000000LL;
	while (be_roster->IsRunning(signature) && system_time() < deadline)
		snooze(100000);
}
