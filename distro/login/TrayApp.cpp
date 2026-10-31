/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Install Deskbar replicants as the signed-in user.
 * ProcessController -deskbar also opens a window; quitting that window
 * tries to save settings and shows "Damned!" when the folder is missing.
 */


#include <Application.h>
#include <Deskbar.h>
#include <Entry.h>


static const char* kSignature = "application/x-vnd.R5OS-tray";


struct TrayItem {
	const char*	path;
	const char*	name;
};


static const TrayItem kItems[] = {
	{ "/system/apps/ProcessController", "ProcessController" },
	{ "/system/apps/NetworkStatus", "NetworkStatus" },
	{ NULL, NULL }
};


class TrayApp : public BApplication {
public:
							TrayApp();
	virtual void			ReadyToRun();
};


TrayApp::TrayApp()
	:
	BApplication(kSignature)
{
}


void
TrayApp::ReadyToRun()
{
	BDeskbar deskbar;
	for (int32 i = 0; kItems[i].path != NULL; i++) {
		if (deskbar.HasItem(kItems[i].name))
			continue;
		entry_ref ref;
		if (get_ref_for_path(kItems[i].path, &ref) != B_OK)
			continue;
		deskbar.AddItem(&ref);
	}
	Quit();
}


int
main()
{
	TrayApp app;
	app.Run();
	return 0;
}
