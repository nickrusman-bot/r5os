/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * MIDI-CI Profile messages. The bytes are the System Exclusive payload
 * between F0 and F7, the same path as Property Exchange.
 */

#ifndef _MIDI2_PROFILE_H
#define _MIDI2_PROFILE_H

#include <string.h>

#include <Midi2Property.h>


const uint8 B_MIDI2_PROFILE_INQUIRY = 0x20;
const uint8 B_MIDI2_PROFILE_INQUIRY_REPLY = 0x21;
const uint8 B_MIDI2_PROFILE_SET_ON = 0x22;
const uint8 B_MIDI2_PROFILE_SET_OFF = 0x23;
const uint8 B_MIDI2_PROFILE_ENABLED = 0x24;
const uint8 B_MIDI2_PROFILE_DISABLED = 0x25;
const uint8 B_MIDI2_PROFILE_SPECIFIC = 0x2f;

const size_t B_MIDI2_PROFILE_ID_LENGTH = 5;
const size_t B_MIDI2_PROFILE_HEADER = 13;


struct midi2_profile_id {
	uint8		bytes[B_MIDI2_PROFILE_ID_LENGTH];
};


struct midi2_profile_message {
	uint8		deviceId;
	uint8		subId;
	uint8		version;
	uint32		sourceMuid;
	uint32		destinationMuid;
	bool		hasProfile;
	midi2_profile_id profile;
	uint8		enabledCount;
	midi2_profile_id enabled[4];
	uint8		disabledCount;
	midi2_profile_id disabled[4];
	const uint8* data;
	size_t		dataLength;
};


struct midi2_profile_catalog {
	uint32		muid;
	midi2_profile_id ids[4];
	bool		enabled[4];
	uint8		count;
	midi2_profile_id specificProfile;
	uint8		specific[32];
	size_t		specificLength;
	bool		hasSpecific;
};


inline bool
midi2_profile_same(const midi2_profile_id* left, const midi2_profile_id* right)
{
	if (left == NULL || right == NULL)
		return false;
	return memcmp(left->bytes, right->bytes, B_MIDI2_PROFILE_ID_LENGTH) == 0;
}


inline int
midi2_profile_find(const midi2_profile_catalog* catalog,
	const midi2_profile_id* id)
{
	if (catalog == NULL || id == NULL)
		return -1;
	for (uint8 i = 0; i < catalog->count && i < 4; i++) {
		if (midi2_profile_same(&catalog->ids[i], id))
			return i;
	}
	return -1;
}


/* Defined in libmidi2.so. The name and the arguments are unchanged. */
void
midi2_profile_catalog_init(midi2_profile_catalog* catalog);



status_t
midi2_profile_encode(const midi2_profile_message* message, uint8* out,
	size_t capacity, size_t* _length);



status_t
midi2_profile_decode(const uint8* payload, size_t length,
	midi2_profile_message* message);



status_t
midi2_profile_respond(midi2_profile_catalog* catalog,
	const midi2_profile_message* inquiry, midi2_profile_message* reply);



#endif // _MIDI2_PROFILE_H
