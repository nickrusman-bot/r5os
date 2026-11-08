/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * MIDI-CI Property Exchange on top of a System Exclusive payload.
 * The bytes are the MIDI-CI message between F0 and F7. A Universal MIDI
 * Packet carries those bytes with midi2_encode_sysex (message type 0x3).
 * This stays in libmidi2 and keeps that kit's MIT license.
 */

#ifndef _MIDI2_PROPERTY_H
#define _MIDI2_PROPERTY_H

#include <stdio.h>
#include <string.h>

#include <Errors.h>
#include <OS.h>


/* Universal SysEx, sub-id 0x0D, MIDI-CI version 1.
 * Property Exchange sub-ids are from the MIDI-CI specification.
 * 0x30 capabilities, 0x34 get, 0x36 set, 0x38 subscription, 0x3F notify.
 * The matching reply is the inquiry sub-id plus one.
 */
const uint8 B_MIDI2_CI_UNIVERSAL = 0x7e;
const uint8 B_MIDI2_CI_SUB_ID = 0x0d;
const uint8 B_MIDI2_CI_VERSION = 0x01;
const uint8 B_MIDI2_PE_WHOLE_BLOCK = 0x7f;

const uint8 B_MIDI2_PE_CAPABILITIES = 0x30;
const uint8 B_MIDI2_PE_CAPABILITIES_REPLY = 0x31;
const uint8 B_MIDI2_PE_GET = 0x34;
const uint8 B_MIDI2_PE_GET_REPLY = 0x35;
const uint8 B_MIDI2_PE_SET = 0x36;
const uint8 B_MIDI2_PE_SET_REPLY = 0x37;
const uint8 B_MIDI2_PE_SUBSCRIBE = 0x38;
const uint8 B_MIDI2_PE_SUBSCRIBE_REPLY = 0x39;
const uint8 B_MIDI2_PE_NOTIFY = 0x3f;

const int32 B_MIDI2_PE_STATUS_OK = 200;
const int32 B_MIDI2_PE_STATUS_BAD_REQUEST = 400;
const int32 B_MIDI2_PE_STATUS_NOT_ALLOWED = 405;
const int32 B_MIDI2_PE_STATUS_NOT_FOUND = 404;
const int32 B_MIDI2_PE_STATUS_TOO_LARGE = 413;
const int32 B_MIDI2_PE_STATUS_WAIT = 100;
const int32 B_MIDI2_PE_STATUS_TERMINATE = 144;
const int32 B_MIDI2_PE_STATUS_TIMEOUT = 408;

const size_t B_MIDI2_PE_CHUNK_OVERHEAD = 22;


struct midi2_pe_message {
	uint8		deviceId;
	uint8		subId;
	uint8		version;
	uint32		sourceMuid;
	uint32		destinationMuid;
	uint8		requestId;
	uint8		simultaneous;
	uint16		chunkCount;
	uint16		chunkNumber;
	const char*	header;
	size_t		headerLength;
	const uint8* body;
	size_t		bodyLength;
};


struct midi2_pe_assembly {
	bool		active;
	bool		complete;
	uint8		subId;
	uint8		requestId;
	uint32		sourceMuid;
	uint32		destinationMuid;
	uint16		chunkCount;
	uint16		nextChunk;
	char		header[512];
	size_t		headerLength;
	uint8		body[2048];
	size_t		bodyLength;
};


/* One local property set. Strings are the JSON property data, with no
 * whitespace. ChannelList and CtrlMapList accept a subscription. The catalog
 * tracks one subscription. ControllerMap is the same body as CtrlMapList.
 */
struct midi2_pe_catalog {
	uint32		muid;
	char		deviceInfo[384];
	char		channelList[384];
	char		programList[384];
	char		controllerMap[384];
	bool		subscribed;
	char		subscribedResource[40];
	char		subscribeId[16];
};


inline void
midi2_pe_put14(uint8* out, uint16 value)
{
	out[0] = value & 0x7f;
	out[1] = (value >> 7) & 0x7f;
}


inline uint16
midi2_pe_get14(const uint8* in)
{
	return (uint16)(in[0] & 0x7f) | ((uint16)(in[1] & 0x7f) << 7);
}


inline void
midi2_pe_put_muid(uint8* out, uint32 muid)
{
	out[0] = muid & 0x7f;
	out[1] = (muid >> 7) & 0x7f;
	out[2] = (muid >> 14) & 0x7f;
	out[3] = (muid >> 21) & 0x7f;
}


inline uint32
midi2_pe_get_muid(const uint8* in)
{
	return (uint32)(in[0] & 0x7f)
		| ((uint32)(in[1] & 0x7f) << 7)
		| ((uint32)(in[2] & 0x7f) << 14)
		| ((uint32)(in[3] & 0x7f) << 21);
}


inline bool
midi2_pe_is_data(uint8 subId)
{
	return subId == B_MIDI2_PE_GET || subId == B_MIDI2_PE_GET_REPLY
		|| subId == B_MIDI2_PE_SET || subId == B_MIDI2_PE_SET_REPLY
		|| subId == B_MIDI2_PE_SUBSCRIBE
		|| subId == B_MIDI2_PE_SUBSCRIBE_REPLY
		|| subId == B_MIDI2_PE_NOTIFY;
}


inline bool
midi2_pe_bytes_fit(const void* data, size_t length)
{
	const uint8* bytes = (const uint8*)data;
	for (size_t i = 0; i < length; i++) {
		if (bytes[i] > 0x7f)
			return false;
	}
	return true;
}


/* Defined in libmidi2.so. The name and the arguments are unchanged. */
status_t
midi2_pe_encode(const midi2_pe_message* message, uint8* out, size_t capacity,
	size_t* _length);



status_t
midi2_pe_decode(const uint8* payload, size_t length, midi2_pe_message* message);



status_t
midi2_pe_split(const midi2_pe_message* message, size_t maxPayload,
	midi2_pe_message* chunks, size_t capacity, size_t* _count);



void
midi2_pe_assembly_init(midi2_pe_assembly* assembly);



status_t
midi2_pe_assembly_add(midi2_pe_assembly* assembly,
	const midi2_pe_message* chunk);



bool
midi2_pe_header_string(const char* header, size_t length, const char* key,
	char* out, size_t outSize);



int32
midi2_pe_header_number(const char* header, size_t length, const char* key);



void
midi2_pe_catalog_init(midi2_pe_catalog* catalog);



const char*
midi2_pe_catalog_body(const midi2_pe_catalog* catalog, const char* resource);



char*
midi2_pe_catalog_store(midi2_pe_catalog* catalog, const char* resource,
	size_t* _capacity);



bool
midi2_pe_header_contains(const char* header, size_t length, const char* text);



bool
midi2_pe_catalog_subscribable(const char* resource);



status_t
midi2_pe_write_status(char* header, size_t capacity, size_t* _length,
	int32 status, const char* message, const char* subscribeId);



void
midi2_pe_fill_reply(midi2_pe_message* reply, const midi2_pe_message* inquiry,
	const midi2_pe_catalog* catalog, uint8 subId, const char* header,
	size_t headerLength, const uint8* body, size_t bodyLength);



status_t
midi2_pe_respond(midi2_pe_catalog* catalog, const midi2_pe_message* inquiry,
	char* header, size_t headerCapacity, size_t* headerLength, uint8* body,
	size_t bodyCapacity, size_t* bodyLength, midi2_pe_message* reply);



status_t
midi2_pe_subscription_full(const midi2_pe_catalog* catalog, char* header,
	size_t headerCapacity, size_t* headerLength, const uint8** body,
	size_t* bodyLength);



#endif // _MIDI2_PROPERTY_H
