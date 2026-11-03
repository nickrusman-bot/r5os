/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * UMP Stream messages, message type 0xF. Endpoint Discovery and Function
 * Block Discovery, and the notifications those filters ask for.
 */

#ifndef _MIDI2_STREAM_H
#define _MIDI2_STREAM_H

#include <string.h>

#include <OS.h>


const uint16 B_MIDI2_STREAM_ENDPOINT_DISCOVERY = 0x000;
const uint16 B_MIDI2_STREAM_ENDPOINT_INFO = 0x001;
const uint16 B_MIDI2_STREAM_DEVICE_IDENTITY = 0x002;
const uint16 B_MIDI2_STREAM_ENDPOINT_NAME = 0x003;
const uint16 B_MIDI2_STREAM_PRODUCT_INSTANCE = 0x004;
const uint16 B_MIDI2_STREAM_CONFIG_REQUEST = 0x005;
const uint16 B_MIDI2_STREAM_CONFIG_NOTIFY = 0x006;
const uint16 B_MIDI2_STREAM_BLOCK_DISCOVERY = 0x010;
const uint16 B_MIDI2_STREAM_BLOCK_INFO = 0x011;
const uint16 B_MIDI2_STREAM_BLOCK_NAME = 0x012;

const uint8 B_MIDI2_STREAM_FILTER_INFO = 0x01;
const uint8 B_MIDI2_STREAM_FILTER_IDENTITY = 0x02;
const uint8 B_MIDI2_STREAM_FILTER_NAME = 0x04;
const uint8 B_MIDI2_STREAM_FILTER_PRODUCT = 0x08;
const uint8 B_MIDI2_STREAM_FILTER_CONFIG = 0x10;

const uint8 B_MIDI2_STREAM_BLOCK_FILTER_INFO = 0x01;
const uint8 B_MIDI2_STREAM_BLOCK_FILTER_NAME = 0x02;

const uint8 B_MIDI2_STREAM_PROTOCOL_MIDI1 = 0x01;
const uint8 B_MIDI2_STREAM_PROTOCOL_MIDI2 = 0x02;

const uint8 B_MIDI2_STREAM_DIRECTION_INPUT = 0x01;
const uint8 B_MIDI2_STREAM_DIRECTION_OUTPUT = 0x02;
const uint8 B_MIDI2_STREAM_DIRECTION_BOTH = 0x03;


struct midi2_function_block {
	uint8		number;
	bool		active;
	uint8		direction;
	uint8		midi1;
	uint8		uiHint;
	uint8		firstGroup;
	uint8		groupCount;
	uint8		midiCiVersion;
	uint8		maxSysex8;
	char		name[14];
};


struct midi2_endpoint {
	uint8		versionMajor;
	uint8		versionMinor;
	bool		staticBlocks;
	bool		midi1;
	bool		midi2;
	bool		receivesJr;
	bool		sendsJr;
	uint8		protocol;
	uint8		manufacturer[3];
	uint8		family[2];
	uint8		model[2];
	uint8		revision[4];
	char		name[15];
	char		productId[15];
	midi2_function_block blocks[2];
	uint8		blockCount;
};


inline uint32
midi2_stream_pack(uint8 b0, uint8 b1, uint8 b2, uint8 b3)
{
	return ((uint32)b0 << 24) | ((uint32)b1 << 16) | ((uint32)b2 << 8) | b3;
}


inline uint16
midi2_stream_status(uint32 firstWord)
{
	return (firstWord >> 16) & 0x03ff;
}


inline void
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
	endpoint->protocol = B_MIDI2_STREAM_PROTOCOL_MIDI2;
	endpoint->manufacturer[0] = 125;
	endpoint->blockCount = 1;
	endpoint->blocks[0].number = 0;
	endpoint->blocks[0].active = true;
	endpoint->blocks[0].direction = B_MIDI2_STREAM_DIRECTION_BOTH;
	endpoint->blocks[0].uiHint = B_MIDI2_STREAM_DIRECTION_BOTH;
	endpoint->blocks[0].firstGroup = 0;
	endpoint->blocks[0].groupCount = 1;
	endpoint->blocks[0].midiCiVersion = 1;
	strlcpy(endpoint->name, "R5OS", sizeof(endpoint->name));
	strlcpy(endpoint->productId, "r5os", sizeof(endpoint->productId));
	strlcpy(endpoint->blocks[0].name, "midi2", sizeof(endpoint->blocks[0].name));
}


inline status_t
midi2_stream_endpoint_discovery(uint8 filter, uint32 words[4])
{
	if (words == NULL || (filter & ~0x1f) != 0)
		return B_BAD_VALUE;
	memset(words, 0, 16);
	words[0] = midi2_stream_pack(0xf0, 0x00, 1, 1);
	words[1] = filter;
	return B_OK;
}


inline status_t
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


inline void
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


inline void
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


inline status_t
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


#endif // _MIDI2_STREAM_H
