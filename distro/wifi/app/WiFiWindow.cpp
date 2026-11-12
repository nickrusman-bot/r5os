/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "WiFiWindow.h"

#include <Catalog.h>
#include <LayoutBuilder.h>

#include <R5WiFi.h>
#include <R5WiFiView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiWindow"


WiFiWindow::WiFiWindow()
	:
	BWindow(BRect(0, 0, 440, 560), B_TRANSLATE_SYSTEM_NAME("WiFi"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE)
{
	fView = new WiFiView(false);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fView);
	CenterOnScreen();
}


void
WiFiWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgWiFiSelectNetwork:
		case kMsgWiFiJoinOther:
			// From the application: hand them to the panel
			PostMessage(message, fView);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
