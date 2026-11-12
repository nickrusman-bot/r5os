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

#include <Midi2Identity.h>


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


/* Defined in libmidi2.so. The name and the arguments are unchanged. */
void
midi2_endpoint_init(midi2_endpoint* endpoint);


/* Stream Configuration Request, status 0x005. Protocol 1 or 2. */
status_t
midi2_stream_configure(midi2_endpoint* endpoint, const uint32* request);



status_t
midi2_stream_endpoint_discovery(uint8 filter, uint32 words[4]);



status_t
midi2_stream_block_discovery(uint8 block, uint8 filter, uint32 words[4]);



void
midi2_stream_identity(const midi2_endpoint* endpoint, uint32 words[4]);



void
midi2_stream_text(uint8 status, uint8 first, const char* text, uint32 words[4]);



status_t
midi2_stream_reply(const midi2_endpoint* endpoint, const uint32* request,
	uint32* replies, size_t capacity, size_t* _count);



/* Sets the Device Identity (manufacturer, family, model, revision), the
 * endpoint name and the product instance ID from the program's own
 * identity. name and productId may be NULL to keep the current ones; they
 * are cut to 14 characters. B_BAD_VALUE for a byte above 0x7F. */
status_t
midi2_endpoint_set_identity(midi2_endpoint* endpoint,
	const midi2_identity* identity, const char* name, const char* productId);



#endif // _MIDI2_STREAM_H
