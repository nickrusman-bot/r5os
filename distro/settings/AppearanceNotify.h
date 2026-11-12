/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * The copied Appearance views post this instead of a message to their window.
 */
#ifndef APPEARANCE_NOTIFY_H
#define APPEARANCE_NOTIFY_H


#include <Message.h>
#include <Messenger.h>
#include <View.h>

#include <string.h>


static const int32 kMsgUpdate = 'updt';


inline void
appearance_notify(BView* view)
{
	for (BView* current = view; current != NULL;
		current = current->Parent()) {
		const char* name = current->Name();
		if (name != NULL && (strcmp(name, "fonts page") == 0
			|| strcmp(name, "look page") == 0)) {
			BMessenger(current).SendMessage(kMsgUpdate);
			return;
		}
	}
}


#endif
