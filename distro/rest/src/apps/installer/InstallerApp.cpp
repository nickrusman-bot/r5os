/*
 * Copyright 2020, Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 * Copyright 2015, Axel Dörfler <axeld@pinc-software.de>
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005, Jérôme DUVAL.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The copyright and license notice above still cover
 * the original work. A build that ships both follows GPL-3.0-or-later, and
 * that notice stays on the original portions.
 */


#include "InstallerApp.h"

#include <unistd.h>

#include <Alert.h>
#include <Roster.h>
#include <TextView.h>

#include <syscalls.h>

#include "tracker_private.h"
#include "Utility.h"


static const uint32 kMsgAgree = 'agre';
static const uint32 kMsgNext = 'next';

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallerApp"


int main(int, char **)
{
	InstallerApp installer;
	installer.Run();
	return 0;
}


InstallerApp::InstallerApp()
	:
	BApplication("application/x-vnd.Haiku-Installer")
{
}


void
InstallerApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAgree:
			fEULAWindow->PostMessage(B_QUIT_REQUESTED);
			break;
		case kMsgNext:
			new InstallerWindow();
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


void
InstallerApp::ReadyToRun()
{
#if 1
	// Show the EULA first.
	fEULAWindow = new EULAWindow();
#else
	// Show the installer window without EULA.
	new InstallerWindow();
#endif
}


void
InstallerApp::Quit()
{
	BApplication::Quit();

	if (!be_roster->IsRunning(kDeskbarSignature)) {
		if (CurrentMessage()->GetBool("install_complete")) {
			// Synchronize disks
			sync();

			if (Utility::IsReadOnlyVolume("/boot")) {
				// Unblock CD tray, and eject the CD
				Utility::BlockMedia("/boot", false);
				Utility::EjectMedia("/boot");
			}

			// Quickly reboot without touching anything
			// on disk (which we might just have ejected)
			_kern_shutdown(true);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		} else if (!be_roster->IsRunning(
				"application/x-vnd.Haiku-Login")) {
			// Live Try/Install: Installer cancelled and Deskbar is
			// not running. Log out also stops Deskbar; if Sign in is
			// up, do not reopen FirstBootPrompt.
// SPDX-SnippetEnd
			be_roster->Launch("application/x-vnd.Haiku-FirstBootPrompt");
		}
	}
}
