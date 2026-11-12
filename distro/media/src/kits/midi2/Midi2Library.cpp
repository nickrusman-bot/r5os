/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Parsers and catalogs for the MIDI 2.0 kit. The headers keep the names.
 * A program that includes them calls these functions in libmidi2.so.
 * The public kit, headers and this library, is MIT.
 */


#include <Midi2Property.h>
#include <Midi2Profile.h>
#include <Midi2Stream.h>
#include <Midi2Usb.h>


/* MIDI-CI wants a random 28-bit MUID per instance, so two programs or two
 * computers can tell each other apart. 0 and 0x0FFFFF00-0x0FFFFFFF are
 * reserved. This was a fixed 1. */
static uint32
midi2_new_muid(const void* instance)
{
	static int32 sCounter = 0;
	uint64 mix = (uint64)system_time() ^ ((uint64)find_thread(NULL) << 20)
		^ (uint64)(addr_t)instance
		^ ((uint64)atomic_add(&sCounter, 1) * 0x9e3779b97f4a7c15ULL);
	mix ^= mix >> 29;
	mix *= 0xbf58476d1ce4e5b9ULL;
	mix ^= mix >> 32;
	uint32 muid = (uint32)mix & 0x0fffffff;
	if (muid == 0 || muid >= 0x0fffff00)
		muid = 0x00123456;
	return muid;
}


status_t
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

status_t
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

status_t
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

void
midi2_pe_assembly_init(midi2_pe_assembly* assembly)
{
	if (assembly == NULL)
		return;
	memset(assembly, 0, sizeof(midi2_pe_assembly));
}

status_t
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

bool
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

int32
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
		/* The header comes from the other device. A long run of digits used
		 * to overflow int32; anything above 2^31 - 1 is refused. */
		int64 number = 0;
		while (value < length && header[value] >= '0' && header[value] <= '9') {
			number = number * 10 + (header[value] - '0');
			if (number > 0x7fffffff)
				return -1;
			value++;
		}
		return (int32)number;
	}
	return -1;
}

void
midi2_pe_catalog_init(midi2_pe_catalog* catalog)
{
	if (catalog == NULL)
		return;
	memset(catalog, 0, sizeof(midi2_pe_catalog));
	catalog->muid = midi2_new_muid(catalog);
	strlcpy(catalog->deviceInfo,
		"{\"manufacturerId\":[125,0,0],\"manufacturer\":\"Educational Use\","
		"\"familyId\":[0,0],\"family\":\"R5\",\"modelId\":[1,0],"
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

const char*
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

char*
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

bool
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

bool
midi2_pe_catalog_subscribable(const char* resource)
{
	return resource != NULL && (strcmp(resource, "ChannelList") == 0
		|| strcmp(resource, "CtrlMapList") == 0
		|| strcmp(resource, "ControllerMap") == 0);
}

status_t
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

void
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

status_t
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

status_t
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

void
midi2_profile_catalog_init(midi2_profile_catalog* catalog)
{
	if (catalog == NULL)
		return;
	memset(catalog, 0, sizeof(midi2_profile_catalog));
	catalog->muid = midi2_new_muid(catalog);
	catalog->count = 1;
	catalog->ids[0].bytes[0] = 0x7e;
	catalog->ids[0].bytes[4] = 0x01;
	catalog->enabled[0] = false;
}

status_t
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

status_t
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

status_t
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

status_t
midi2_stream_configure(midi2_endpoint* endpoint, const uint32* request)
{
	uint8 protocol;
	if (endpoint == NULL || request == NULL)
		return B_BAD_VALUE;
	if ((request[0] >> 28) != 0xf)
		return B_BAD_VALUE;
	if (midi2_stream_status(request[0]) != B_MIDI2_STREAM_CONFIG_REQUEST)
		return B_BAD_VALUE;
	protocol = (uint8)((request[0] >> 8) & 0xff);
	if (protocol != B_MIDI2_STREAM_PROTOCOL_MIDI1
		&& protocol != B_MIDI2_STREAM_PROTOCOL_MIDI2)
		return B_BAD_VALUE;
	endpoint->protocol = protocol;
	return B_OK;
}


void
midi2_endpoint_init(midi2_endpoint* endpoint)
{
	if (endpoint == NULL)
		return;
	memset(endpoint, 0, sizeof(midi2_endpoint));
	endpoint->versionMajor = 1;
	endpoint->versionMinor = 1;
	endpoint->staticBlocks = true;
	endpoint->midi1 = true;
	endpoint->midi2 = true;
	/* No Stream Configuration has been seen. MIDI 1.0 protocol. */
	endpoint->protocol = B_MIDI2_STREAM_PROTOCOL_MIDI1;
	endpoint->manufacturer[0] = 125;
	endpoint->blockCount = 1;
	endpoint->blocks[0].number = 0;
	endpoint->blocks[0].active = true;
	endpoint->blocks[0].direction = B_MIDI2_STREAM_DIRECTION_BOTH;
	endpoint->blocks[0].uiHint = B_MIDI2_STREAM_DIRECTION_BOTH;
	endpoint->blocks[0].firstGroup = 0;
	endpoint->blocks[0].groupCount = 1;
	endpoint->blocks[0].midiCiVersion = 1;
	strlcpy(endpoint->name, "R5", sizeof(endpoint->name));
	strlcpy(endpoint->productId, "r5os", sizeof(endpoint->productId));
	strlcpy(endpoint->blocks[0].name, "midi2", sizeof(endpoint->blocks[0].name));
}

status_t
midi2_stream_endpoint_discovery(uint8 filter, uint32 words[4])
{
	if (words == NULL || (filter & ~0x1f) != 0)
		return B_BAD_VALUE;
	memset(words, 0, 16);
	words[0] = midi2_stream_pack(0xf0, 0x00, 1, 1);
	words[1] = filter;
	return B_OK;
}

status_t
midi2_stream_block_discovery(uint8 block, uint8 filter, uint32 words[4])
{
	if (words == NULL || (filter & ~0x03) != 0)
		return B_BAD_VALUE;
	if (block > 31 && block != 0xff)
		return B_BAD_VALUE;
	memset(words, 0, 16);
	words[0] = midi2_stream_pack(0xf0, 0x10, block, filter);
	return B_OK;
}

void
midi2_stream_identity(const midi2_endpoint* endpoint, uint32 words[4])
{
	memset(words, 0, 16);
	words[0] = midi2_stream_pack(0xf0, 0x02, 0, 0);
	words[1] = ((uint32)(endpoint->manufacturer[0] & 0x7f) << 16)
		| ((uint32)(endpoint->manufacturer[1] & 0x7f) << 8)
		| (endpoint->manufacturer[2] & 0x7f);
	words[2] = ((uint32)(endpoint->family[0] & 0x7f) << 24)
		| ((uint32)(endpoint->family[1] & 0x7f) << 16)
		| ((uint32)(endpoint->model[0] & 0x7f) << 8)
		| (endpoint->model[1] & 0x7f);
	words[3] = ((uint32)(endpoint->revision[0] & 0x7f) << 24)
		| ((uint32)(endpoint->revision[1] & 0x7f) << 16)
		| ((uint32)(endpoint->revision[2] & 0x7f) << 8)
		| (endpoint->revision[3] & 0x7f);
}

void
midi2_stream_text(uint8 status, uint8 first, const char* text, uint32 words[4])
{
	uint8 bytes[16];
	memset(bytes, 0, sizeof(bytes));
	bytes[0] = 0xf0;
	bytes[1] = status;
	if (first != 0xff)
		bytes[2] = first;
	size_t offset = first == 0xff ? 2 : 3;
	if (text != NULL) {
		size_t length = strlen(text);
		if (length > 16 - offset)
			length = 16 - offset;
		memcpy(bytes + offset, text, length);
	}
	for (int i = 0; i < 4; i++) {
		words[i] = midi2_stream_pack(bytes[i * 4], bytes[i * 4 + 1],
			bytes[i * 4 + 2], bytes[i * 4 + 3]);
	}
}

status_t
midi2_stream_reply(const midi2_endpoint* endpoint, const uint32* request,
	uint32* replies, size_t capacity, size_t* _count)
{
	if (endpoint == NULL || request == NULL || replies == NULL || _count == NULL)
		return B_BAD_VALUE;
	if ((request[0] >> 28) != 0xf)
		return B_BAD_VALUE;

	uint16 status = midi2_stream_status(request[0]);
	size_t count = 0;
	if (status == B_MIDI2_STREAM_ENDPOINT_DISCOVERY) {
		uint8 filter = request[1] & 0x1f;
		if ((filter & B_MIDI2_STREAM_FILTER_INFO) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			uint32* words = replies + count * 4;
			memset(words, 0, 16);
			uint8 flags = (endpoint->staticBlocks ? 0x80 : 0)
				| (endpoint->blockCount & 0x7f);
			uint8 protocols = (endpoint->midi2 ? 0x02 : 0)
				| (endpoint->midi1 ? 0x01 : 0);
			uint8 jr = (endpoint->receivesJr ? 0x02 : 0)
				| (endpoint->sendsJr ? 0x01 : 0);
			words[0] = midi2_stream_pack(0xf0, 0x01, endpoint->versionMajor,
				endpoint->versionMinor);
			words[1] = midi2_stream_pack(flags, 0, protocols, jr);
			count++;
		}
		if ((filter & B_MIDI2_STREAM_FILTER_IDENTITY) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			midi2_stream_identity(endpoint, replies + count * 4);
			count++;
		}
		if ((filter & B_MIDI2_STREAM_FILTER_NAME) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			midi2_stream_text(0x03, 0xff, endpoint->name, replies + count * 4);
			count++;
		}
		if ((filter & B_MIDI2_STREAM_FILTER_PRODUCT) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			midi2_stream_text(0x04, 0xff, endpoint->productId,
				replies + count * 4);
			count++;
		}
		if ((filter & B_MIDI2_STREAM_FILTER_CONFIG) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			uint32* words = replies + count * 4;
			memset(words, 0, 16);
			uint8 jr = (endpoint->receivesJr ? 0x02 : 0)
				| (endpoint->sendsJr ? 0x01 : 0);
			words[0] = midi2_stream_pack(0xf0, 0x06, endpoint->protocol, jr);
			count++;
		}
		*_count = count;
		return B_OK;
	}

	if (status != B_MIDI2_STREAM_BLOCK_DISCOVERY)
		return B_BAD_VALUE;

	uint8 block = (request[0] >> 8) & 0xff;
	uint8 filter = request[0] & 0x03;
	for (uint8 i = 0; i < endpoint->blockCount && i < 2; i++) {
		const midi2_function_block* item = &endpoint->blocks[i];
		if (block != 0xff && item->number != block)
			continue;
		if ((filter & B_MIDI2_STREAM_BLOCK_FILTER_INFO) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			uint32* words = replies + count * 4;
			memset(words, 0, 16);
			uint8 active = (item->active ? 0x80 : 0) | (item->number & 0x7f);
			uint8 detail = ((item->uiHint & 0x03) << 4)
				| ((item->midi1 & 0x03) << 2) | (item->direction & 0x03);
			words[0] = midi2_stream_pack(0xf0, 0x11, active, detail);
			words[1] = midi2_stream_pack(item->firstGroup, item->groupCount,
				item->midiCiVersion, item->maxSysex8);
			count++;
		}
		if ((filter & B_MIDI2_STREAM_BLOCK_FILTER_NAME) != 0) {
			if (count >= capacity)
				return B_BAD_VALUE;
			midi2_stream_text(0x12, item->number, item->name,
				replies + count * 4);
			count++;
		}
	}
	if (count == 0 && block != 0xff)
		return B_NAME_NOT_FOUND;
	*_count = count;
	return B_OK;
}

status_t
midi2_usb_write_interface(const midi2_usb_interface* interface, uint8* out,
	size_t capacity, size_t* _length)
{
	if (interface == NULL || out == NULL || _length == NULL)
		return B_BAD_VALUE;
	if (capacity < B_MIDI2_USB_INTERFACE_LENGTH)
		return B_BAD_VALUE;
	out[0] = B_MIDI2_USB_INTERFACE_LENGTH;
	out[1] = B_MIDI2_USB_INTERFACE;
	out[2] = interface->interfaceNumber;
	out[3] = interface->alternateSetting;
	out[4] = interface->numEndpoints;
	out[5] = B_MIDI2_USB_AUDIO_CLASS;
	out[6] = B_MIDI2_USB_MIDISTREAMING;
	out[7] = interface->protocol;
	out[8] = interface->stringIndex;
	*_length = B_MIDI2_USB_INTERFACE_LENGTH;
	return B_OK;
}

status_t
midi2_usb_parse_interface(const uint8* raw, size_t length,
	midi2_usb_interface* interface)
{
	if (raw == NULL || interface == NULL
		|| length < B_MIDI2_USB_INTERFACE_LENGTH) {
		return B_BAD_DATA;
	}
	if (raw[0] < B_MIDI2_USB_INTERFACE_LENGTH || raw[1] != B_MIDI2_USB_INTERFACE)
		return B_BAD_VALUE;
	memset(interface, 0, sizeof(midi2_usb_interface));
	interface->length = raw[0];
	interface->type = raw[1];
	interface->interfaceNumber = raw[2];
	interface->alternateSetting = raw[3];
	interface->numEndpoints = raw[4];
	interface->deviceClass = raw[5];
	interface->subclass = raw[6];
	interface->protocol = raw[7];
	interface->stringIndex = raw[8];
	return B_OK;
}

status_t
midi2_usb_write_header(const midi2_usb_header* header, uint8* out,
	size_t capacity, size_t* _length)
{
	if (header == NULL || out == NULL || _length == NULL)
		return B_BAD_VALUE;
	if (capacity < B_MIDI2_USB_HEADER_LENGTH)
		return B_BAD_VALUE;
	out[0] = B_MIDI2_USB_HEADER_LENGTH;
	out[1] = B_MIDI2_USB_CS_INTERFACE;
	out[2] = B_MIDI2_USB_MS_HEADER;
	out[3] = header->version & 0xff;
	out[4] = (header->version >> 8) & 0xff;
	out[5] = header->totalLength & 0xff;
	out[6] = (header->totalLength >> 8) & 0xff;
	*_length = B_MIDI2_USB_HEADER_LENGTH;
	return B_OK;
}

status_t
midi2_usb_parse_header(const uint8* raw, size_t length, midi2_usb_header* header)
{
	if (raw == NULL || header == NULL || length < B_MIDI2_USB_HEADER_LENGTH)
		return B_BAD_DATA;
	if (raw[0] < B_MIDI2_USB_HEADER_LENGTH || raw[1] != B_MIDI2_USB_CS_INTERFACE
		|| raw[2] != B_MIDI2_USB_MS_HEADER) {
		return B_BAD_VALUE;
	}
	memset(header, 0, sizeof(midi2_usb_header));
	header->length = raw[0];
	header->type = raw[1];
	header->subtype = raw[2];
	header->version = raw[3] | ((uint16)raw[4] << 8);
	header->totalLength = raw[5] | ((uint16)raw[6] << 8);
	return B_OK;
}

status_t
midi2_usb_write_group_terminal(const midi2_usb_group_terminal* block,
	uint8* out, size_t capacity, size_t* _length)
{
	if (block == NULL || out == NULL || _length == NULL)
		return B_BAD_VALUE;
	if (capacity < B_MIDI2_USB_GROUP_TERMINAL_LENGTH)
		return B_BAD_VALUE;
	if (block->direction > B_MIDI2_USB_BLOCK_OUTPUT || block->firstGroup > 15
		|| block->groupCount == 0 || block->groupCount > 16) {
		return B_BAD_VALUE;
	}
	out[0] = B_MIDI2_USB_GROUP_TERMINAL_LENGTH;
	out[1] = B_MIDI2_USB_GROUP_TERMINAL_TYPE;
	out[2] = B_MIDI2_USB_GROUP_TERMINAL_SUBTYPE;
	out[3] = block->blockId;
	out[4] = block->direction;
	out[5] = block->firstGroup;
	out[6] = block->groupCount;
	out[7] = block->stringIndex;
	out[8] = block->protocol;
	out[9] = block->maxInputBandwidth & 0xff;
	out[10] = (block->maxInputBandwidth >> 8) & 0xff;
	out[11] = block->maxOutputBandwidth & 0xff;
	out[12] = (block->maxOutputBandwidth >> 8) & 0xff;
	*_length = B_MIDI2_USB_GROUP_TERMINAL_LENGTH;
	return B_OK;
}

status_t
midi2_usb_parse_group_terminal(const uint8* raw, size_t length,
	midi2_usb_group_terminal* block)
{
	if (raw == NULL || block == NULL
		|| length < B_MIDI2_USB_GROUP_TERMINAL_LENGTH) {
		return B_BAD_DATA;
	}
	if (raw[0] < B_MIDI2_USB_GROUP_TERMINAL_LENGTH
		|| raw[1] != B_MIDI2_USB_GROUP_TERMINAL_TYPE
		|| raw[2] != B_MIDI2_USB_GROUP_TERMINAL_SUBTYPE) {
		return B_BAD_VALUE;
	}
	memset(block, 0, sizeof(midi2_usb_group_terminal));
	block->length = raw[0];
	block->type = raw[1];
	block->subtype = raw[2];
	block->blockId = raw[3];
	block->direction = raw[4];
	block->firstGroup = raw[5];
	block->groupCount = raw[6];
	block->stringIndex = raw[7];
	block->protocol = raw[8];
	block->maxInputBandwidth = raw[9] | ((uint16)raw[10] << 8);
	block->maxOutputBandwidth = raw[11] | ((uint16)raw[12] << 8);
	return B_OK;
}

