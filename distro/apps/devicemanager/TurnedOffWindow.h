/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_TURNED_OFF_WINDOW_H
#define DM_TURNED_OFF_WINDOW_H


#include <Messenger.h>
#include <Window.h>

#include <vector>

#include "BlockedDrivers.h"


class BButton;
class BListView;


// The drivers that are turned off. A turned-off driver no longer loads, so
// its device may not appear in the main list; this is where it is turned on.
class TurnedOffWindow : public BWindow {
public:
								TurnedOffWindow(BMessenger mainWindow);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Refresh();

			BMessenger			fMainWindow;
			BListView*			fList;
			BButton*			fTurnOnButton;
			std::vector<BlockedDriver> fDrivers;
};


#endif
