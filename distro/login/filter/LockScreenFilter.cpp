/*
 * Ctrl+Shift+L asks Login to cover the screen. The session stays.
 *
 * Ctrl+Alt is already "minimize all" in the stock filter, so this chord
 * does not use Alt.
 */


#include "LockScreenFilter.h"

#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>

#include <new>
#include <string.h>


static const uint32 kLockAction = 'lokc';
static const char* kLoginSignature = "application/x-vnd.Haiku-Login";


extern "C" BInputServerFilter*
instantiate_input_filter()
{
	return new(std::nothrow) LockScreenFilter();
}


LockScreenFilter::LockScreenFilter()
	:
	fDown(false)
{
}


static bool
is_lock_chord(BMessage* message)
{
	const char* bytes = NULL;
	if (message->FindString("bytes", &bytes) != B_OK || bytes == NULL)
		return false;
	if (bytes[0] != 'l' && bytes[0] != 'L')
		return false;
	if (bytes[1] != '\0')
		return false;

	int32 modifiers = 0;
	if (message->FindInt32("modifiers", &modifiers) != B_OK)
		return false;

	int32 held = modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY
		| B_MENU_KEY | B_SHIFT_KEY);
	return held == (B_CONTROL_KEY | B_SHIFT_KEY);
}


filter_result
LockScreenFilter::Filter(BMessage* message, BList* _list)
{
	(void)_list;

	switch (message->what) {
		case B_KEY_UP:
			if (is_lock_chord(message))
				fDown = false;
			break;
		case B_KEY_DOWN:
			if (!is_lock_chord(message))
				break;
			if (!fDown) {
				fDown = true;
				BMessenger login(kLoginSignature);
				if (login.IsValid())
					login.SendMessage(kLockAction);
			}
			return B_SKIP_MESSAGE;
		default:
			break;
	}

	return B_DISPATCH_MESSAGE;
}


status_t
LockScreenFilter::InitCheck()
{
	return B_OK;
}
