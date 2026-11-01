/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "LegacyPages.h"
#include "SettingsManager.h"
#include "SettingsStorage.h"
#include "SettingsWindow.h"

#include <Application.h>


class SettingsApp : public BApplication {
public:
								SettingsApp();
	virtual						~SettingsApp();
	virtual	void				ReadyToRun();

private:
			SettingsStorage		fStorage;
			SettingsManager		fManager;
			SettingsWindow*		fWindow;
};


SettingsApp::SettingsApp()
	:
	BApplication("application/x-vnd.R5OS-Settings"),
	fWindow(NULL)
{
	register_legacy_pages(fManager);
}


SettingsApp::~SettingsApp()
{
}


void
SettingsApp::ReadyToRun()
{
	fWindow = new SettingsWindow(&fManager, &fStorage);
	fManager.LoadAddOns(fWindow->Context());
	fWindow->ModulesLoaded();
	fWindow->Show();
}


int
main()
{
	SettingsApp app;
	app.Run();
	return 0;
}
