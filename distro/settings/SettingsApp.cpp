/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "DeskbarModule.h"
#include "DisplayModule.h"
#include "InformationModule.h"
#include "NotificationsModule.h"
#include "VirtualMemoryModule.h"
#include "LegacyPages.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "WallpaperModule.h"
#include "WiFiModule.h"
#include "SettingsStorage.h"
#include "SettingsWindow.h"

#include <Application.h>
#include <String.h>

#include <string.h>


class SettingsApp : public BApplication {
public:
								SettingsApp();
	virtual						~SettingsApp();
	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				ReadyToRun();

private:
			void				_OpenPage(const char* moduleId);

			SettingsStorage		fStorage;
			SettingsManager		fManager;
			SettingsWindow*		fWindow;
			BString				fPendingPage;
};


SettingsApp::SettingsApp()
	:
	BApplication("application/x-vnd.R5OS-Settings"),
	fWindow(NULL)
{
	register_legacy_pages(fManager);
	register_display_page(fManager);
	register_deskbar_page(fManager);
	register_notifications_page(fManager);
	register_virtual_memory_page(fManager);
	register_wallpaper_page(fManager);
	register_information_page(fManager);
	register_wifi_page(fManager);
}


SettingsApp::~SettingsApp()
{
}


/*!	"Settings --page <id>" opens that page, also when Settings already runs
	(the Deskbar network menu opens the Wi-Fi page this way).
*/
void
SettingsApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--page") == 0)
			_OpenPage(argv[i + 1]);
	}
}


void
SettingsApp::MessageReceived(BMessage* message)
{
	if (message->what == kMsgOpenPage) {
		const char* moduleId;
		if (message->FindString("module", &moduleId) == B_OK) {
			_OpenPage(moduleId);
			return;
		}
	}
	BApplication::MessageReceived(message);
}


void
SettingsApp::ReadyToRun()
{
	fWindow = new SettingsWindow(&fManager, &fStorage);
	fManager.LoadAddOns(fWindow->Context());
	fWindow->ModulesLoaded();
	fWindow->Show();
	if (fPendingPage.Length() > 0) {
		_OpenPage(fPendingPage.String());
		fPendingPage = "";
	}
}


void
SettingsApp::_OpenPage(const char* moduleId)
{
	if (fWindow == NULL) {
		fPendingPage = moduleId;
		return;
	}
	BMessage message(kMsgOpenPage);
	message.AddString("module", moduleId);
	fWindow->PostMessage(&message);
	fWindow->Activate();
}


int
main()
{
	SettingsApp app;
	app.Run();
	return 0;
}
