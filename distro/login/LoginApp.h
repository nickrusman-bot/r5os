/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGINAPP_H_
#define _LOGINAPP_H_


#include <Application.h>


const uint32 kAttemptLogin = 'logi';
const uint32 kHaltAction = 'halt';
const uint32 kRebootAction = 'rebo';
const uint32 kSuspendAction = 'susp';
const uint32 kLogoutAction = 'lout';
const uint32 kLoginBad = 'lgba';
const uint32 kLoginOk = 'lgok';


class LoginWindow;
class DesktopWindow;


class LoginApp : public BApplication {
public:
					LoginApp();
	virtual			~LoginApp();
	void			ReadyToRun();
	void			MessageReceived(BMessage* message);
	void			ArgvReceived(int32 argc, char** argv);

private:
	void			TryLogin(BMessage* message);
	void			Logout();
	status_t		ValidateLogin(const char* login, const char* password);
	void			_EnableDesktopJobs();
	void			_HideLogin();
	void			_ShowLogin();
	void			_QuitUserApps();
	void			_WaitGone(const char* signature);

	DesktopWindow*	fDesktopWindow;
	LoginWindow*	fLoginWindow;
	bool			fEditShelfMode;
};


#endif	// _LOGINAPP_H_
