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


inline void
midi2_profile_catalog_init(midi2_profile_catalog* catalog)
{
	if (catalog == NULL)
		return;
	memset(catalog, 0, sizeof(midi2_profile_catalog));
	catalog->muid = 1;
	catalog->count = 1;
	catalog->ids[0].bytes[0] = 0x7e;
	catalog->ids[0].bytes[4] = 0x01;
	catalog->enabled[0] = false;
}


inline status_t
midi2_profile_encode(const midi2_profile_message* message, uint8* out,
	size_t capacity, size_t* _length)
{
	if (message == NULL || out == NULL || _length == NULL)
		return B_BAD_VALUE;
	if (message->sourceMuid > 0x0fffffff
		|| message->destinationMuid > 0x0fffffff) {
		return B_BAD_VALUE;
	}

	uint8 subId = message->subId;
	bool withProfile = subId == B_MIDI2_PROFILE_SET_ON
		|| subId == B_MIDI2_PROFILE_SET_OFF
		|| subId == B_MIDI2_PROFILE_ENABLED
		|| subId == B_MIDI2_PROFILE_DISABLED
		|| subId == B_MIDI2_PROFILE_SPECIFIC;
	bool inquiry = subId == B_MIDI2_PROFILE_INQUIRY;
	bool reply = subId == B_MIDI2_PROFILE_INQUIRY_REPLY;
	if (!withProfile && !inquiry && !reply)
		return B_BAD_VALUE;
	if (withProfile && !message->hasProfile)
		return B_BAD_VALUE;
	if (subId == B_MIDI2_PROFILE_SPECIFIC
		&& (message->dataLength > 16383
			|| (message->dataLength > 0 && message->data == NULL)
			|| !midi2_pe_bytes_fit(message->data, message->dataLength))) {
		return B_BAD_VALUE;
	}
	if (reply && (message->enabledCount > 4 || message->disabledCount > 4))
		return B_BAD_VALUE;

	size_t total = B_MIDI2_PROFILE_HEADER;
	if (withProfile)
		total += B_MIDI2_PROFILE_ID_LENGTH;
	if (subId == B_MIDI2_PROFILE_SPECIFIC)
		total += 2 + message->dataLength;
	if (reply) {
		total += 4 + (size_t)(message->enabledCount + message->disabledCount)
			* B_MIDI2_PROFILE_ID_LENGTH;
	}
	if (capacity < total)
		return B_BAD_VALUE;

	uint8 version = message->version == 0 ? B_MIDI2_CI_VERSION : message->version;
	uint8 deviceId = message->deviceId == 0
		? B_MIDI2_PE_WHOLE_BLOCK : message->deviceId;
	out[0] = B_MIDI2_CI_UNIVERSAL;
	out[1] = deviceId;
	out[2] = B_MIDI2_CI_SUB_ID;
	out[3] = subId;
	out[4] = version;
	midi2_pe_put_muid(out + 5, message->sourceMuid);
	midi2_pe_put_muid(out + 9, message->destinationMuid);
	size_t at = B_MIDI2_PROFILE_HEADER;
	if (withProfile) {
		memcpy(out + at, message->profile.bytes, B_MIDI2_PROFILE_ID_LENGTH);
		at += B_MIDI2_PROFILE_ID_LENGTH;
	}
	if (subId == B_MIDI2_PROFILE_SPECIFIC) {
		midi2_pe_put14(out + at, (uint16)message->dataLength);
		at += 2;
		if (message->dataLength > 0)
			memcpy(out + at, message->data, message->dataLength);
	}
	if (reply) {
		midi2_pe_put14(out + at, message->enabledCount);
		at += 2;
		for (uint8 i = 0; i < message->enabledCount; i++) {
			memcpy(out + at, message->enabled[i].bytes,
				B_MIDI2_PROFILE_ID_LENGTH);
			at += B_MIDI2_PROFILE_ID_LENGTH;
		}
		midi2_pe_put14(out + at, message->disabledCount);
		at += 2;
		for (uint8 i = 0; i < message->disabledCount; i++) {
			memcpy(out + at, message->disabled[i].bytes,
				B_MIDI2_PROFILE_ID_LENGTH);
			at += B_MIDI2_PROFILE_ID_LENGTH;
		}
	}
	*_length = total;
	return B_OK;
}


inline status_t
midi2_profile_decode(const uint8* payload, size_t length,
	midi2_profile_message* message)
{
	if (payload == NULL || message == NULL
		|| length < B_MIDI2_PROFILE_HEADER) {
		return B_BAD_DATA;
	}
	if (payload[0] != B_MIDI2_CI_UNIVERSAL || payload[2] != B_MIDI2_CI_SUB_ID)
		return B_BAD_VALUE;

	memset(message, 0, sizeof(midi2_profile_message));
	message->deviceId = payload[1];
	message->subId = payload[3];
	message->version = payload[4];
	message->sourceMuid = midi2_pe_get_muid(payload + 5);
	message->destinationMuid = midi2_pe_get_muid(payload + 9);

	uint8 subId = message->subId;
	bool withProfile = subId == B_MIDI2_PROFILE_SET_ON
		|| subId == B_MIDI2_PROFILE_SET_OFF
		|| subId == B_MIDI2_PROFILE_ENABLED
		|| subId == B_MIDI2_PROFILE_DISABLED
		|| subId == B_MIDI2_PROFILE_SPECIFIC;
	if (subId == B_MIDI2_PROFILE_INQUIRY) {
		if (length != B_MIDI2_PROFILE_HEADER)
			return B_BAD_DATA;
		return B_OK;
	}
	if (withProfile) {
		if (length < B_MIDI2_PROFILE_HEADER + B_MIDI2_PROFILE_ID_LENGTH)
			return B_BAD_DATA;
		memcpy(message->profile.bytes, payload + B_MIDI2_PROFILE_HEADER,
			B_MIDI2_PROFILE_ID_LENGTH);
		message->hasProfile = true;
		if (subId != B_MIDI2_PROFILE_SPECIFIC) {
			if (length != B_MIDI2_PROFILE_HEADER + B_MIDI2_PROFILE_ID_LENGTH)
				return B_BAD_DATA;
			return B_OK;
		}
		size_t at = B_MIDI2_PROFILE_HEADER + B_MIDI2_PROFILE_ID_LENGTH;
		if (length < at + 2)
			return B_BAD_DATA;
		uint16 dataLength = midi2_pe_get14(payload + at);
		if (at + 2 + dataLength != length)
			return B_BAD_DATA;
		message->dataLength = dataLength;
		message->data = dataLength == 0 ? NULL : payload + at + 2;
		return B_OK;
	}
	if (subId != B_MIDI2_PROFILE_INQUIRY_REPLY)
		return B_BAD_VALUE;

	size_t at = B_MIDI2_PROFILE_HEADER;
	if (length < at + 2)
		return B_BAD_DATA;
	uint16 enabled = midi2_pe_get14(payload + at);
	at += 2;
	if (enabled > 4 || length < at + enabled * B_MIDI2_PROFILE_ID_LENGTH)
		return B_BAD_DATA;
	message->enabledCount = (uint8)enabled;
	for (uint16 i = 0; i < enabled; i++) {
		memcpy(message->enabled[i].bytes, payload + at,
			B_MIDI2_PROFILE_ID_LENGTH);
		at += B_MIDI2_PROFILE_ID_LENGTH;
	}
	if (length < at + 2)
		return B_BAD_DATA;
	uint16 disabled = midi2_pe_get14(payload + at);
	at += 2;
	if (disabled > 4
		|| at + disabled * B_MIDI2_PROFILE_ID_LENGTH != length) {
		return B_BAD_DATA;
	}
	message->disabledCount = (uint8)disabled;
	for (uint16 i = 0; i < disabled; i++) {
		memcpy(message->disabled[i].bytes, payload + at,
			B_MIDI2_PROFILE_ID_LENGTH);
		at += B_MIDI2_PROFILE_ID_LENGTH;
	}
	return B_OK;
}


inline status_t
midi2_profile_respond(midi2_profile_catalog* catalog,
	const midi2_profile_message* inquiry, midi2_profile_message* reply)
{
	if (catalog == NULL || inquiry == NULL || reply == NULL)
		return B_BAD_VALUE;
	memset(reply, 0, sizeof(midi2_profile_message));
	reply->deviceId = B_MIDI2_PE_WHOLE_BLOCK;
	reply->version = inquiry->version == 0
		? B_MIDI2_CI_VERSION : inquiry->version;
	reply->sourceMuid = catalog->muid;
	reply->destinationMuid = inquiry->sourceMuid;

	if (inquiry->subId == B_MIDI2_PROFILE_INQUIRY) {
		reply->subId = B_MIDI2_PROFILE_INQUIRY_REPLY;
		for (uint8 i = 0; i < catalog->count && i < 4; i++) {
			if (catalog->enabled[i]) {
				reply->enabled[reply->enabledCount] = catalog->ids[i];
				reply->enabledCount++;
			} else {
				reply->disabled[reply->disabledCount] = catalog->ids[i];
				reply->disabledCount++;
			}
		}
		return B_OK;
	}

	if (inquiry->subId != B_MIDI2_PROFILE_SET_ON
		&& inquiry->subId != B_MIDI2_PROFILE_SET_OFF
		&& inquiry->subId != B_MIDI2_PROFILE_SPECIFIC) {
		return B_BAD_VALUE;
	}
	if (!inquiry->hasProfile)
		return B_BAD_VALUE;
	int index = midi2_profile_find(catalog, &inquiry->profile);
	if (index < 0)
		return B_NAME_NOT_FOUND;

	if (inquiry->subId == B_MIDI2_PROFILE_SPECIFIC) {
		if (inquiry->dataLength > sizeof(catalog->specific))
			return B_BAD_VALUE;
		if (inquiry->dataLength > 0)
			memcpy(catalog->specific, inquiry->data, inquiry->dataLength);
		catalog->specificLength = inquiry->dataLength;
		catalog->specificProfile = inquiry->profile;
		catalog->hasSpecific = true;
		reply->subId = B_MIDI2_PROFILE_SPECIFIC;
		reply->hasProfile = true;
		reply->profile = inquiry->profile;
		reply->data = catalog->specific;
		reply->dataLength = catalog->specificLength;
		return B_OK;
	}

	catalog->enabled[index] = inquiry->subId == B_MIDI2_PROFILE_SET_ON;
	reply->hasProfile = true;
	reply->profile = inquiry->profile;
	reply->subId = catalog->enabled[index]
		? B_MIDI2_PROFILE_ENABLED : B_MIDI2_PROFILE_DISABLED;
	return B_OK;
}


#endif // _MIDI2_PROFILE_H
