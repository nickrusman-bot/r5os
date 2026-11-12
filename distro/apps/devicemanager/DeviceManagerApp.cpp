/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include <Application.h>
#include <Catalog.h>

extern "C" {
#include "dm_wrapper.h"
}

#include "AboutWindow.h"
#include "MainWindow.h"
#include "Messages.h"


class DeviceManagerApp : public BApplication {
public:
	DeviceManagerApp()
		:
		BApplication(kAppSignature)
	{
	}

	virtual void ReadyToRun()
	{
		init_dm_wrapper();
		(new MainWindow())->Show();
	}

	virtual void AboutRequested()
	{
		(new AboutWindow())->Show();
	}

	virtual bool QuitRequested()
	{
		uninit_dm_wrapper();
		return true;
	}
};


int
main()
{
	DeviceManagerApp app;
	app.Run();
	return 0;
}
