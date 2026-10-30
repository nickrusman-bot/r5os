/*
 * Ctrl+Alt+L asks Login to cover the screen. The session stays.
 *
 * Ctrl+Shift is the Windows shortcut that switches keyboard layout, so a
 * VirtualBox guest on Windows never sees it. Ctrl+Alt+L does arrive.
 * Ctrl+Shift+L is still accepted for a machine that is not behind that shortcut.
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
is_l_key(BMessage* message)
{
	// Control rewrites "bytes" to a control character. raw_char stays L.
	int32 raw = 0;
	if (message->FindInt32("raw_char", &raw) == B_OK
		&& (raw == 'l' || raw == 'L'))
		return true;

	const char* bytes = NULL;
	if (message->FindString("bytes", &bytes) == B_OK && bytes != NULL
		&& (bytes[0] == 'l' || bytes[0] == 'L') && bytes[1] == '\0')
		return true;
	return false;
}


static bool
is_lock_chord(BMessage* message)
{
	if (!is_l_key(message))
		return false;

	int32 modifiers = 0;
	if (message->FindInt32("modifiers", &modifiers) != B_OK)
		return false;

	int32 held = modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY
		| B_MENU_KEY | B_SHIFT_KEY);
	// Alt is the command key on the Haiku keymap.
	return held == (B_CONTROL_KEY | B_COMMAND_KEY)
		|| held == (B_CONTROL_KEY | B_SHIFT_KEY);
}


filter_result
LockScreenFilter::Filter(BMessage* message, BList* _list)
{
	(void)_list;

	switch (message->what) {
		case B_KEY_UP:
			if (is_l_key(message))
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
