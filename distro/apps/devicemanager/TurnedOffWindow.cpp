/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "TurnedOffWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>

#include "DriverControl.h"
#include "Messages.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TurnedOffWindow"


TurnedOffWindow::TurnedOffWindow(BMessenger mainWindow)
	:
	BWindow(BRect(0, 0, 459, 299), B_TRANSLATE("Turned-off drivers"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fMainWindow(mainWindow)
{
	fList = new BListView("drivers", B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	fList->SetInvocationMessage(new BMessage(kMsgTurnOnSelected));
	BScrollView* scroll = new BScrollView("drivers-scroll", fList, 0, false,
		true);
	fTurnOnButton = new BButton("turn-on", B_TRANSLATE("Turn on"),
		new BMessage(kMsgTurnOnSelected));
	BButton* close = new BButton("close", B_TRANSLATE("Close"),
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(new BStringView(NULL, B_TRANSLATE(
			"A driver you turn on loads after a restart.")))
		.Add(scroll, 1.0f)
		.AddGroup(B_HORIZONTAL)
			.Add(fTurnOnButton)
			.AddGlue()
			.Add(close)
		.End();

	SetSizeLimits(360, B_SIZE_UNLIMITED, 220, B_SIZE_UNLIMITED);
	ResizeTo(459, 299);
	CenterOnScreen();
	_Refresh();
}


void
TurnedOffWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelectionChanged:
			fTurnOnButton->SetEnabled(fList->CurrentSelection() >= 0);
			break;

		case kMsgTurnOnSelected:
		{
			int32 index = fList->CurrentSelection();
			if (index < 0 || index >= (int32)fDrivers.size())
				break;
			BMessage turnOn(kMsgTurnOnPath);
			turnOn.AddString("path", fDrivers[index].path);
			fMainWindow.SendMessage(&turnOn);
			break;
		}

		case kMsgTurnedOffRefresh:
			_Refresh();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
TurnedOffWindow::_Refresh()
{
	while (BListItem* item = fList->RemoveItem((int32)0))
		delete item;

	dm_turned_off_drivers(fDrivers);
	for (size_t i = 0; i < fDrivers.size(); i++) {
		BPath path(fDrivers[i].path.String());
		BString label;
		label << path.Leaf() << "  \xE2\x80\x94  " << fDrivers[i].package;
		if (!dm_is_system_driver(fDrivers[i].path))
			label << " " << B_TRANSLATE("(your packages)");
		fList->AddItem(new BStringItem(label));
	}
	if (fDrivers.empty())
		fList->AddItem(new BStringItem(B_TRANSLATE("No drivers are turned off.")));
	fList->SetEnabled(!fDrivers.empty());
	fTurnOnButton->SetEnabled(false);
}
