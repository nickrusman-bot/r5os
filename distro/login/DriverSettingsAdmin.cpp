/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Login side of kMsgAdminSetDriverSetting. Media runs as the signed-in
 * person. The drivers read their settings from the administrator's
 * kernel/drivers directory, which a standard account cannot write. Login
 * checks the administrator grant and then writes only the files, keys and
 * values that Media offers. Anything else is refused, so this is not a way
 * to write an arbitrary file as uid 0.
 */


#include "DriverSettingsAdmin.h"

#include "AccountAdmin.h"

#include <AdminLock.h>
#include <String.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>


enum value_kind {
	kValueFrames,		// 128 or 256
	kValueChannels,		// 1 to 16, or auto
	kValueOnOff			// on or off
};

struct allowed_key {
	const char*	file;
	const char*	key;
	value_kind	kind;
};

// The Latency, USB layout and Virtual surround menus of Media.
static const allowed_key kAllowedKeys[] = {
	{ "hda.settings", "play_buffer_frames", kValueFrames },
	{ "hda.settings", "record_buffer_frames", kValueFrames },
	{ "usb_audio", "play_buffer_frames", kValueFrames },
	{ "usb_audio", "record_buffer_frames", kValueFrames },
	{ "usb_audio", "channel_layout", kValueChannels },
	{ "virtual_surround", "virtual_surround", kValueOnOff },
};

static const int32 kMaxKeys = 8;


static status_t
fail(BMessage& reply, status_t status, const char* text)
{
	reply.AddString("text", text);
	reply.AddInt32("error", status);
	return status;
}


static const allowed_key*
find_allowed(const char* file, const char* key)
{
	for (size_t i = 0; i < sizeof(kAllowedKeys) / sizeof(kAllowedKeys[0]);
			i++) {
		if (strcmp(kAllowedKeys[i].file, file) == 0
			&& strcmp(kAllowedKeys[i].key, key) == 0)
			return &kAllowedKeys[i];
	}
	return NULL;
}


static bool
valid_value(value_kind kind, const BString& value)
{
	if (value.Length() == 0 || value.Length() > 4)
		return false;
	for (int32 i = 0; i < value.Length(); i++) {
		char c = value.ByteAt(i);
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')))
			return false;
	}

	switch (kind) {
		case kValueFrames:
			return value == "128" || value == "256";
		case kValueChannels:
		{
			if (value == "auto")
				return true;
			char* end = NULL;
			long channels = strtol(value.String(), &end, 10);
			return end != NULL && *end == '\0' && channels >= 1
				&& channels <= 16;
		}
		case kValueOnOff:
			return value == "on" || value == "off";
	}
	return false;
}


status_t
handle_driver_settings_admin_message(BMessage* request, BMessage& reply)
{
	reply.MakeEmpty();
	if (request == NULL)
		return fail(reply, B_BAD_VALUE, "The request is empty.");
	if (geteuid() != 0) {
		return fail(reply, B_PERMISSION_DENIED,
			"Driver settings require the sign-in helper.");
	}
	if (!admin_sender_may_change(request)) {
		return fail(reply, B_PERMISSION_DENIED,
			"Unlock this with an administrator password.");
	}

	const char* file = NULL;
	const char* lines = NULL;
	bool replaceFile = false;
	if (request->FindString("file", &file) != B_OK
		|| request->FindString("lines", &lines) != B_OK
		|| request->FindBool("replace", &replaceFile) != B_OK)
		return fail(reply, B_BAD_VALUE, "The request is incomplete.");

	const char* keys[kMaxKeys];
	int32 keyCount = 0;
	const char* key = NULL;
	while (request->FindString("key", keyCount, &key) == B_OK) {
		if (keyCount >= kMaxKeys)
			return fail(reply, B_BAD_VALUE, "Too many settings.");
		if (find_allowed(file, key) == NULL)
			return fail(reply, B_NOT_ALLOWED, "This setting is not allowed.");
		keys[keyCount++] = key;
	}
	if (keyCount == 0)
		return fail(reply, B_BAD_VALUE, "The request names no setting.");

	// Every line is "key value" for one of the keys of this request.
	BString text(lines);
	int32 start = 0;
	while (start < text.Length()) {
		int32 end = text.FindFirst('\n', start);
		if (end < 0)
			return fail(reply, B_BAD_VALUE, "A line has no end.");
		BString line;
		text.CopyInto(line, start, end - start);
		int32 space = line.FindFirst(' ');
		if (space <= 0)
			return fail(reply, B_BAD_VALUE, "A line is not a setting.");
		BString lineKey;
		BString value;
		line.CopyInto(lineKey, 0, space);
		line.CopyInto(value, space + 1, line.Length() - space - 1);

		const allowed_key* allowed = NULL;
		for (int32 i = 0; i < keyCount; i++) {
			if (lineKey == keys[i]) {
				allowed = find_allowed(file, keys[i]);
				break;
			}
		}
		if (allowed == NULL || !valid_value(allowed->kind, value))
			return fail(reply, B_NOT_ALLOWED, "This value is not allowed.");
		start = end + 1;
	}

	// virtual_surround is read by its first line only, so it is always the
	// whole file. The other files keep their other lines.
	if (strcmp(file, "virtual_surround") != 0)
		replaceFile = false;

	status_t status = r5_driver_settings_write(file, keys, keyCount, lines,
		replaceFile);
	if (status != B_OK)
		return fail(reply, status, strerror(status));

	// The file is now owned by uid 0. The administrator owns the directory,
	// so Media as the administrator still replaces it (temporary file and
	// rename()).
	reply.AddInt32("error", B_OK);
	return B_OK;
}
