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


inline status_t
midi2_pe_encode(const midi2_pe_message* message, uint8* out, size_t capacity,
	size_t* _length)
{
	if (message == NULL || out == NULL || _length == NULL)
		return B_BAD_VALUE;
	if (message->sourceMuid > 0x0fffffff
		|| message->destinationMuid > 0x0fffffff) {
		return B_BAD_VALUE;
	}

	uint8 version = message->version == 0 ? B_MIDI2_CI_VERSION : message->version;
	uint8 deviceId = message->deviceId == 0
		? B_MIDI2_PE_WHOLE_BLOCK : message->deviceId;

	if (message->subId == B_MIDI2_PE_CAPABILITIES
		|| message->subId == B_MIDI2_PE_CAPABILITIES_REPLY) {
		if (capacity < 14 || message->simultaneous > 127)
			return B_BAD_VALUE;
		out[0] = B_MIDI2_CI_UNIVERSAL;
		out[1] = deviceId;
		out[2] = B_MIDI2_CI_SUB_ID;
		out[3] = message->subId;
		out[4] = version;
		midi2_pe_put_muid(out + 5, message->sourceMuid);
		midi2_pe_put_muid(out + 9, message->destinationMuid);
		out[13] = message->simultaneous;
		*_length = 14;
		return B_OK;
	}

	if (!midi2_pe_is_data(message->subId) || message->requestId > 127)
		return B_BAD_VALUE;
	if (message->headerLength > 16383 || message->bodyLength > 16383)
		return B_BAD_VALUE;
	if ((message->headerLength > 0 && message->header == NULL)
		|| (message->bodyLength > 0 && message->body == NULL)) {
		return B_BAD_VALUE;
	}
	if (!midi2_pe_bytes_fit(message->header, message->headerLength)
		|| !midi2_pe_bytes_fit(message->body, message->bodyLength)) {
		return B_BAD_VALUE;
	}
	if (message->chunkNumber == 0)
		return B_BAD_VALUE;

	size_t total = B_MIDI2_PE_CHUNK_OVERHEAD + message->headerLength
		+ message->bodyLength;
	if (capacity < total)
		return B_BAD_VALUE;

	out[0] = B_MIDI2_CI_UNIVERSAL;
	out[1] = deviceId;
	out[2] = B_MIDI2_CI_SUB_ID;
	out[3] = message->subId;
	out[4] = version;
	midi2_pe_put_muid(out + 5, message->sourceMuid);
	midi2_pe_put_muid(out + 9, message->destinationMuid);
	out[13] = message->requestId;
	midi2_pe_put14(out + 14, (uint16)message->headerLength);
	if (message->headerLength > 0)
		memcpy(out + 16, message->header, message->headerLength);
	size_t at = 16 + message->headerLength;
	midi2_pe_put14(out + at, message->chunkCount);
	midi2_pe_put14(out + at + 2, message->chunkNumber);
	midi2_pe_put14(out + at + 4, (uint16)message->bodyLength);
	if (message->bodyLength > 0)
		memcpy(out + at + 6, message->body, message->bodyLength);
	*_length = total;
	return B_OK;
}


inline status_t
midi2_pe_decode(const uint8* payload, size_t length, midi2_pe_message* message)
{
	if (payload == NULL || message == NULL || length < 14)
		return B_BAD_DATA;
	if (payload[0] != B_MIDI2_CI_UNIVERSAL || payload[2] != B_MIDI2_CI_SUB_ID)
		return B_BAD_VALUE;

	memset(message, 0, sizeof(midi2_pe_message));
	message->deviceId = payload[1];
	message->subId = payload[3];
	message->version = payload[4];
	message->sourceMuid = midi2_pe_get_muid(payload + 5);
	message->destinationMuid = midi2_pe_get_muid(payload + 9);

	if (message->subId == B_MIDI2_PE_CAPABILITIES
		|| message->subId == B_MIDI2_PE_CAPABILITIES_REPLY) {
		message->simultaneous = payload[13] & 0x7f;
		return B_OK;
	}
	if (!midi2_pe_is_data(message->subId))
		return B_BAD_VALUE;
	if (length < B_MIDI2_PE_CHUNK_OVERHEAD)
		return B_BAD_DATA;

	message->requestId = payload[13] & 0x7f;
	uint16 headerLength = midi2_pe_get14(payload + 14);
	if ((size_t)16 + headerLength + 6 > length)
		return B_BAD_DATA;
	size_t at = 16 + headerLength;
	uint16 bodyLength = midi2_pe_get14(payload + at + 4);
	if (at + 6 + bodyLength != length)
		return B_BAD_DATA;

	message->headerLength = headerLength;
	message->header = headerLength == 0 ? NULL : (const char*)(payload + 16);
	message->chunkCount = midi2_pe_get14(payload + at);
	message->chunkNumber = midi2_pe_get14(payload + at + 2);
	message->bodyLength = bodyLength;
	message->body = bodyLength == 0 ? NULL : payload + at + 6;
	if (message->chunkNumber == 0 && message->chunkCount != 0)
		return B_BAD_DATA;
	return B_OK;
}


inline status_t
midi2_pe_split(const midi2_pe_message* message, size_t maxPayload,
	midi2_pe_message* chunks, size_t capacity, size_t* _count)
{
	if (message == NULL || chunks == NULL || _count == NULL || capacity == 0)
		return B_BAD_VALUE;
	if (message->headerLength > 0 && message->header == NULL)
		return B_BAD_VALUE;
	if (message->bodyLength > 0 && message->body == NULL)
		return B_BAD_VALUE;

	if (maxPayload == 0) {
		chunks[0] = *message;
		chunks[0].chunkCount = 1;
		chunks[0].chunkNumber = 1;
		*_count = 1;
		return B_OK;
	}

	if (maxPayload < B_MIDI2_PE_CHUNK_OVERHEAD + message->headerLength)
		return B_BAD_VALUE;
	size_t firstRoom = maxPayload - B_MIDI2_PE_CHUNK_OVERHEAD
		- message->headerLength;
	size_t nextRoom = maxPayload - B_MIDI2_PE_CHUNK_OVERHEAD;
	if (message->bodyLength > firstRoom && nextRoom == 0)
		return B_BAD_VALUE;

	size_t total = 1;
	if (message->bodyLength > firstRoom) {
		size_t remaining = message->bodyLength - firstRoom;
		total += (remaining + nextRoom - 1) / nextRoom;
	}
	if (total > capacity || total > 16383)
		return B_BAD_VALUE;

	size_t offset = 0;
	for (size_t i = 0; i < total; i++) {
		midi2_pe_message* chunk = &chunks[i];
		*chunk = *message;
		chunk->chunkCount = (uint16)total;
		chunk->chunkNumber = (uint16)(i + 1);
		if (i == 0) {
			size_t take = message->bodyLength;
			if (take > firstRoom)
				take = firstRoom;
			chunk->body = take == 0 ? NULL : message->body;
			chunk->bodyLength = take;
			offset = take;
		} else {
			size_t take = message->bodyLength - offset;
			if (take > nextRoom)
				take = nextRoom;
			chunk->header = NULL;
			chunk->headerLength = 0;
			chunk->body = message->body + offset;
			chunk->bodyLength = take;
			offset += take;
		}
	}
	*_count = total;
	return B_OK;
}


inline void
midi2_pe_assembly_init(midi2_pe_assembly* assembly)
{
	if (assembly == NULL)
		return;
	memset(assembly, 0, sizeof(midi2_pe_assembly));
}


inline status_t
midi2_pe_assembly_add(midi2_pe_assembly* assembly,
	const midi2_pe_message* chunk)
{
	if (assembly == NULL || chunk == NULL)
		return B_BAD_VALUE;
	if (chunk->chunkNumber == 0)
		return B_BAD_DATA;

	bool start = !assembly->active || chunk->chunkNumber == 1;
	if (!start && (chunk->requestId != assembly->requestId
		|| chunk->subId != assembly->subId
		|| chunk->chunkNumber != assembly->nextChunk)) {
		midi2_pe_assembly_init(assembly);
		return B_BAD_DATA;
	}

	if (start) {
		if (chunk->chunkNumber != 1) {
			midi2_pe_assembly_init(assembly);
			return B_BAD_DATA;
		}
		midi2_pe_assembly_init(assembly);
		assembly->active = true;
		assembly->subId = chunk->subId;
		assembly->requestId = chunk->requestId;
		assembly->sourceMuid = chunk->sourceMuid;
		assembly->destinationMuid = chunk->destinationMuid;
		assembly->chunkCount = chunk->chunkCount;
		assembly->nextChunk = 2;
		if (chunk->headerLength >= sizeof(assembly->header)) {
			midi2_pe_assembly_init(assembly);
			return B_BAD_VALUE;
		}
		if (chunk->headerLength > 0)
			memcpy(assembly->header, chunk->header, chunk->headerLength);
		assembly->header[chunk->headerLength] = '\0';
		assembly->headerLength = chunk->headerLength;
	}

	if (assembly->bodyLength + chunk->bodyLength > sizeof(assembly->body)) {
		midi2_pe_assembly_init(assembly);
		return B_BAD_VALUE;
	}
	if (chunk->bodyLength > 0) {
		memcpy(assembly->body + assembly->bodyLength, chunk->body,
			chunk->bodyLength);
		assembly->bodyLength += chunk->bodyLength;
	}

	if (chunk->chunkCount != 0 && chunk->chunkNumber == chunk->chunkCount)
		assembly->complete = true;
	else
		assembly->nextChunk = (uint16)(chunk->chunkNumber + 1);
	return B_OK;
}


inline bool
midi2_pe_header_string(const char* header, size_t length, const char* key,
	char* out, size_t outSize)
{
	if (header == NULL || key == NULL || out == NULL || outSize == 0)
		return false;
	size_t keyLength = strlen(key);
	if (keyLength == 0 || keyLength > 20)
		return false;

	for (size_t i = 0; i + keyLength + 3 < length; i++) {
		if (header[i] != '"')
			continue;
		if (memcmp(header + i + 1, key, keyLength) != 0)
			continue;
		if (header[i + 1 + keyLength] != '"'
			|| header[i + 2 + keyLength] != ':') {
			continue;
		}
		size_t value = i + 3 + keyLength;
		if (value >= length || header[value] != '"')
			continue;
		value++;
		size_t used = 0;
		while (value < length && header[value] != '"') {
			if (used + 1 >= outSize)
				return false;
			out[used++] = header[value++];
		}
		if (value >= length || header[value] != '"')
			return false;
		out[used] = '\0';
		return true;
	}
	return false;
}


inline int32
midi2_pe_header_number(const char* header, size_t length, const char* key)
{
	if (header == NULL || key == NULL)
		return -1;
	size_t keyLength = strlen(key);
	if (keyLength == 0 || keyLength > 20)
		return -1;

	for (size_t i = 0; i + keyLength + 3 < length; i++) {
		if (header[i] != '"')
			continue;
		if (memcmp(header + i + 1, key, keyLength) != 0)
			continue;
		if (header[i + 1 + keyLength] != '"'
			|| header[i + 2 + keyLength] != ':') {
			continue;
		}
		size_t value = i + 3 + keyLength;
		if (value >= length || header[value] < '0' || header[value] > '9')
			return -1;
		int32 number = 0;
		while (value < length && header[value] >= '0' && header[value] <= '9') {
			number = number * 10 + (header[value] - '0');
			value++;
		}
		return number;
	}
	return -1;
}


inline void
midi2_pe_catalog_init(midi2_pe_catalog* catalog)
{
	if (catalog == NULL)
		return;
	memset(catalog, 0, sizeof(midi2_pe_catalog));
	catalog->muid = 1;
	strlcpy(catalog->deviceInfo,
		"{\"manufacturerId\":[125,0,0],\"manufacturer\":\"Educational Use\","
		"\"familyId\":[0,0],\"family\":\"R5OS\",\"modelId\":[1,0],"
		"\"model\":\"midi2\",\"versionId\":[1,0,0,0],\"version\":\"1\"}",
		sizeof(catalog->deviceInfo));
	strlcpy(catalog->channelList,
		"[{\"title\":\"Channel 1\",\"channel\":1,\"programTitle\":\"Piano\","
		"\"bankPC\":[0,0,0]}]",
		sizeof(catalog->channelList));
	strlcpy(catalog->programList,
		"[{\"title\":\"Piano\",\"bankPC\":[0,0,0]},"
		"{\"title\":\"Strings\",\"bankPC\":[0,0,48]}]",
		sizeof(catalog->programList));
	strlcpy(catalog->controllerMap,
		"[{\"value\":0,\"title\":\"Off\"},"
		"{\"value\":4294967295,\"title\":\"Full\"}]",
		sizeof(catalog->controllerMap));
	strlcpy(catalog->subscribeId, "sub1", sizeof(catalog->subscribeId));
}


inline const char*
midi2_pe_catalog_body(const midi2_pe_catalog* catalog, const char* resource)
{
	if (catalog == NULL || resource == NULL)
		return NULL;
	if (strcmp(resource, "DeviceInfo") == 0)
		return catalog->deviceInfo;
	if (strcmp(resource, "ChannelList") == 0)
		return catalog->channelList;
	if (strcmp(resource, "ProgramList") == 0)
		return catalog->programList;
	if (strcmp(resource, "CtrlMapList") == 0
		|| strcmp(resource, "ControllerMap") == 0) {
		return catalog->controllerMap;
	}
	return NULL;
}


inline char*
midi2_pe_catalog_store(midi2_pe_catalog* catalog, const char* resource,
	size_t* _capacity)
{
	if (catalog == NULL || resource == NULL)
		return NULL;
	if (strcmp(resource, "DeviceInfo") == 0) {
		*_capacity = sizeof(catalog->deviceInfo);
		return catalog->deviceInfo;
	}
	if (strcmp(resource, "ChannelList") == 0) {
		*_capacity = sizeof(catalog->channelList);
		return catalog->channelList;
	}
	if (strcmp(resource, "ProgramList") == 0) {
		*_capacity = sizeof(catalog->programList);
		return catalog->programList;
	}
	if (strcmp(resource, "CtrlMapList") == 0
		|| strcmp(resource, "ControllerMap") == 0) {
		*_capacity = sizeof(catalog->controllerMap);
		return catalog->controllerMap;
	}
	return NULL;
}


inline bool
midi2_pe_header_contains(const char* header, size_t length, const char* text)
{
	if (header == NULL || text == NULL)
		return false;
	size_t textLength = strlen(text);
	if (textLength == 0 || length < textLength)
		return false;
	for (size_t i = 0; i + textLength <= length; i++) {
		if (memcmp(header + i, text, textLength) == 0)
			return true;
	}
	return false;
}


inline bool
midi2_pe_catalog_subscribable(const char* resource)
{
	return resource != NULL && (strcmp(resource, "ChannelList") == 0
		|| strcmp(resource, "CtrlMapList") == 0
		|| strcmp(resource, "ControllerMap") == 0);
}


inline status_t
midi2_pe_write_status(char* header, size_t capacity, size_t* _length,
	int32 status, const char* message, const char* subscribeId)
{
	int written;
	if (message != NULL && subscribeId != NULL) {
		written = snprintf(header, capacity,
			"{\"status\":%ld,\"message\":\"%s\",\"subscribeId\":\"%s\"}",
			(long)status, message, subscribeId);
	} else if (message != NULL) {
		written = snprintf(header, capacity,
			"{\"status\":%ld,\"message\":\"%s\"}", (long)status, message);
	} else if (subscribeId != NULL) {
		written = snprintf(header, capacity,
			"{\"status\":%ld,\"subscribeId\":\"%s\"}", (long)status,
			subscribeId);
	} else {
		written = snprintf(header, capacity, "{\"status\":%ld}", (long)status);
	}
	if (written < 0 || (size_t)written >= capacity)
		return B_BAD_VALUE;
	*_length = (size_t)written;
	return B_OK;
}


inline void
midi2_pe_fill_reply(midi2_pe_message* reply, const midi2_pe_message* inquiry,
	const midi2_pe_catalog* catalog, uint8 subId, const char* header,
	size_t headerLength, const uint8* body, size_t bodyLength)
{
	memset(reply, 0, sizeof(midi2_pe_message));
	reply->deviceId = B_MIDI2_PE_WHOLE_BLOCK;
	reply->subId = subId;
	reply->version = inquiry->version == 0 ? B_MIDI2_CI_VERSION : inquiry->version;
	reply->sourceMuid = catalog->muid;
	reply->destinationMuid = inquiry->sourceMuid;
	reply->requestId = inquiry->requestId;
	reply->chunkCount = 1;
	reply->chunkNumber = 1;
	reply->header = header;
	reply->headerLength = headerLength;
	reply->body = bodyLength == 0 ? NULL : body;
	reply->bodyLength = bodyLength;
}


inline status_t
midi2_pe_respond(midi2_pe_catalog* catalog, const midi2_pe_message* inquiry,
	char* header, size_t headerCapacity, size_t* headerLength, uint8* body,
	size_t bodyCapacity, size_t* bodyLength, midi2_pe_message* reply)
{
	if (catalog == NULL || inquiry == NULL || header == NULL
		|| headerLength == NULL || bodyLength == NULL || reply == NULL) {
		return B_BAD_VALUE;
	}
	*headerLength = 0;
	*bodyLength = 0;

	if (inquiry->subId == B_MIDI2_PE_NOTIFY) {
		memset(reply, 0, sizeof(midi2_pe_message));
		return B_OK;
	}
	if (inquiry->subId == B_MIDI2_PE_CAPABILITIES) {
		memset(reply, 0, sizeof(midi2_pe_message));
		reply->deviceId = B_MIDI2_PE_WHOLE_BLOCK;
		reply->subId = B_MIDI2_PE_CAPABILITIES_REPLY;
		reply->version = B_MIDI2_CI_VERSION;
		reply->sourceMuid = catalog->muid;
		reply->destinationMuid = inquiry->sourceMuid;
		reply->simultaneous = 1;
		return B_OK;
	}

	uint8 replySubId = 0;
	if (inquiry->subId == B_MIDI2_PE_GET)
		replySubId = B_MIDI2_PE_GET_REPLY;
	else if (inquiry->subId == B_MIDI2_PE_SET)
		replySubId = B_MIDI2_PE_SET_REPLY;
	else if (inquiry->subId == B_MIDI2_PE_SUBSCRIBE)
		replySubId = B_MIDI2_PE_SUBSCRIBE_REPLY;
	else
		return B_BAD_VALUE;

	char resource[40];
	char command[16];
	char resId[40];
	bool hasResource = midi2_pe_header_string(inquiry->header,
		inquiry->headerLength, "resource", resource, sizeof(resource));
	bool hasCommand = midi2_pe_header_string(inquiry->header,
		inquiry->headerLength, "command", command, sizeof(command));
	bool hasResId = midi2_pe_header_string(inquiry->header,
		inquiry->headerLength, "resId", resId, sizeof(resId));
	status_t written = B_OK;

	if (inquiry->subId == B_MIDI2_PE_GET && !hasResource) {
		written = midi2_pe_write_status(header, headerCapacity, headerLength,
			B_MIDI2_PE_STATUS_BAD_REQUEST,
			"Inquiry Header does not specify the Resource.", NULL);
	} else if (inquiry->subId == B_MIDI2_PE_GET
		&& strcmp(resource, "ResourceList") == 0) {
		const char* list =
			"[{\"resource\":\"DeviceInfo\",\"canGet\":true,\"canSet\":\"none\","
			"\"canSubscribe\":false},{\"resource\":\"ChannelList\","
			"\"canGet\":true,\"canSet\":\"none\",\"canSubscribe\":true},"
			"{\"resource\":\"ProgramList\",\"canGet\":true,\"canSet\":\"none\","
			"\"canSubscribe\":false},{\"resource\":\"CtrlMapList\","
			"\"canGet\":true,\"canSet\":\"none\",\"canSubscribe\":true}]";
		if (strlen(list) > bodyCapacity)
			return B_BAD_VALUE;
		memcpy(body, list, strlen(list));
		*bodyLength = strlen(list);
		written = midi2_pe_write_status(header, headerCapacity, headerLength,
			B_MIDI2_PE_STATUS_OK, NULL, NULL);
	} else if (inquiry->subId == B_MIDI2_PE_GET) {
		if (hasResId && strcmp(resource, "ProgramList") == 0
			&& strcmp(resId, "programs") != 0) {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_NOT_FOUND, NULL, NULL);
		} else if (hasResId && (strcmp(resource, "CtrlMapList") == 0
			|| strcmp(resource, "ControllerMap") == 0)
			&& strcmp(resId, "volume") != 0) {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_NOT_FOUND, NULL, NULL);
		} else {
			const char* found = midi2_pe_catalog_body(catalog, resource);
			if (found == NULL) {
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_NOT_FOUND, NULL, NULL);
			} else {
				size_t foundLength = strlen(found);
				if (foundLength > bodyCapacity)
					return B_BAD_VALUE;
				memcpy(body, found, foundLength);
				*bodyLength = foundLength;
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_OK, NULL, NULL);
			}
		}
	} else if (inquiry->subId == B_MIDI2_PE_SET) {
		if (!hasResource) {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_BAD_REQUEST,
				"Inquiry Header does not specify the Resource.", NULL);
		} else if (midi2_pe_header_contains(inquiry->header,
			inquiry->headerLength, "\"setPartial\":true")) {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_BAD_REQUEST, NULL, NULL);
		} else {
			size_t capacity = 0;
			char* store = midi2_pe_catalog_store(catalog, resource, &capacity);
			if (store == NULL) {
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_NOT_FOUND, NULL, NULL);
			} else if (inquiry->bodyLength + 1 > capacity
				|| !midi2_pe_bytes_fit(inquiry->body, inquiry->bodyLength)) {
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_TOO_LARGE, NULL, NULL);
			} else {
				if (inquiry->bodyLength > 0)
					memcpy(store, inquiry->body, inquiry->bodyLength);
				store[inquiry->bodyLength] = '\0';
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_OK, NULL, NULL);
			}
		}
	} else if (inquiry->subId == B_MIDI2_PE_SUBSCRIBE) {
		if (!hasCommand) {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_BAD_REQUEST, NULL, NULL);
		} else if (strcmp(command, "start") == 0) {
			if (!hasResource) {
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_BAD_REQUEST, NULL, NULL);
			} else if (midi2_pe_catalog_body(catalog, resource) == NULL) {
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_NOT_FOUND, NULL, NULL);
			} else if (!midi2_pe_catalog_subscribable(resource)) {
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_NOT_ALLOWED, NULL, NULL);
			} else {
				catalog->subscribed = true;
				strlcpy(catalog->subscribedResource, resource,
					sizeof(catalog->subscribedResource));
				written = midi2_pe_write_status(header, headerCapacity,
					headerLength, B_MIDI2_PE_STATUS_OK, NULL,
					catalog->subscribeId);
			}
		} else if (strcmp(command, "end") == 0) {
			catalog->subscribed = false;
			catalog->subscribedResource[0] = '\0';
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_OK, NULL, NULL);
		} else if (strcmp(command, "full") == 0 || strcmp(command, "partial") == 0
			|| strcmp(command, "notify") == 0) {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_OK, NULL, NULL);
		} else {
			written = midi2_pe_write_status(header, headerCapacity,
				headerLength, B_MIDI2_PE_STATUS_BAD_REQUEST, NULL, NULL);
		}
	}

	if (written != B_OK)
		return written;
	midi2_pe_fill_reply(reply, inquiry, catalog, replySubId, header,
		*headerLength, body, *bodyLength);
	return B_OK;
}


inline status_t
midi2_pe_subscription_full(const midi2_pe_catalog* catalog, char* header,
	size_t headerCapacity, size_t* headerLength, const uint8** body,
	size_t* bodyLength)
{
	if (catalog == NULL || header == NULL || headerLength == NULL
		|| body == NULL || bodyLength == NULL) {
		return B_BAD_VALUE;
	}
	if (!catalog->subscribed)
		return B_BAD_VALUE;
	const char* found = midi2_pe_catalog_body(catalog,
		catalog->subscribedResource);
	if (found == NULL)
		return B_BAD_VALUE;
	int written = snprintf(header, headerCapacity,
		"{\"command\":\"full\",\"subscribeId\":\"%s\"}", catalog->subscribeId);
	if (written < 0 || (size_t)written >= headerCapacity)
		return B_BAD_VALUE;
	*headerLength = (size_t)written;
	*body = (const uint8*)found;
	*bodyLength = strlen(found);
	return B_OK;
}


#endif // _MIDI2_PROPERTY_H
