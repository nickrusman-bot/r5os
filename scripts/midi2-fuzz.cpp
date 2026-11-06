/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host fuzz harness for the MIDI 2.0 parsers. Build with AddressSanitizer.
 * Usage: midi2-fuzz <target> <seconds>
 * Targets: kernel, payload, property, profile, stream, usb.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Midi2Defs.h>
#include "usb_midi_parse.h"


static uint64_t gState = 0x123456789abcdefULL;


static uint32_t
next_word()
{
	gState ^= gState << 7;
	gState ^= gState >> 9;
	gState ^= gState << 8;
	return (uint32_t)gState;
}


static void
fill(uint8_t* bytes, size_t length)
{
	for (size_t i = 0; i < length; i++)
		bytes[i] = (uint8_t)next_word();
}


static int
seconds_left(time_t start, int seconds)
{
	return (int)(time(NULL) - start) < seconds;
}


static void
keep(uint8_t byte)
{
	static volatile uint8_t sink;
	sink = (uint8_t)(sink + byte);
}


static void
fuzz_kernel(time_t start, int seconds)
{
	while (seconds_left(start, seconds)) {
		size_t length = next_word() % 64;
		uint8_t* bytes = (uint8_t*)malloc(length == 0 ? 1 : length);
		fill(bytes, length);
		usb_midi_walk_ump(bytes, length, NULL, NULL);

		uint32_t words[2];
		words[0] = next_word();
		words[1] = next_word();
		uint8_t status = 0;
		uint8_t data[6];
		int count = usb_midi_sysex7_bytes(words, &status, data);
		if (count > 0)
			keep(data[0]);
		uint8_t expanded[16];
		int expandedLength = usb_midi_expand_ump(words, next_word() % 5, expanded);
		if (expandedLength > 0)
			keep(expanded[0]);
		free(bytes);
	}
}


static void
fuzz_payload(time_t start, int seconds)
{
	while (seconds_left(start, seconds)) {
		size_t length = next_word() % 48;
		uint8_t* bytes = (uint8_t*)malloc(length == 0 ? 1 : length);
		fill(bytes, length);
		uint32_t words[8];
		size_t count = 0;
		if (midi2_ump_from_payload(bytes, length, words, 8, &count) == B_OK && count > 0)
			keep((uint8_t)words[0]);
		uint32_t sysex[8];
		size_t sysexCount = 0;
		midi2_encode_sysex(bytes, length, sysex, 8, &sysexCount);
		if (sysexCount >= 2) {
			uint8_t out[6];
			uint8_t sysexStatus = 0;
			size_t outCount = 0;
			midi2_sysex_payload(sysex, 2, &sysexStatus, out, 6, &outCount);
		}
		free(bytes);
	}
}


static void
fuzz_property(time_t start, int seconds)
{
	midi2_pe_catalog catalog;
	midi2_pe_catalog_init(&catalog);
	while (seconds_left(start, seconds)) {
		size_t length = next_word() % 128;
		uint8_t* bytes = (uint8_t*)malloc(length == 0 ? 1 : length);
		fill(bytes, length);
		midi2_pe_message message;
		memset(&message, 0, sizeof(message));
		midi2_pe_decode(bytes, length, &message);
		uint8_t encoded[256];
		size_t encodedLength = 0;
		message.header = (const char*)bytes;
		message.headerLength = length;
		message.body = bytes;
		message.bodyLength = length;
		message.subId = (uint8_t)(0x30 + (next_word() % 16));
		midi2_pe_encode(&message, encoded, sizeof(encoded), &encodedLength);
		midi2_pe_message decoded;
		memset(&decoded, 0, sizeof(decoded));
		if (midi2_pe_decode(bytes, length, &decoded) == B_OK) {
			midi2_pe_assembly assembly;
			midi2_pe_assembly_init(&assembly);
			midi2_pe_assembly_add(&assembly, &decoded);
		}
		char header[128];
		size_t headerLength = 0;
		uint8_t body[128];
		size_t bodyLength = 0;
		midi2_pe_message reply;
		memset(&reply, 0, sizeof(reply));
		midi2_pe_respond(&catalog, &message, header, sizeof(header), &headerLength,
			body, sizeof(body), &bodyLength, &reply);
		if (headerLength > 0)
			keep(header[0]);
		free(bytes);
	}
}


static void
fuzz_profile(time_t start, int seconds)
{
	midi2_profile_catalog catalog;
	midi2_profile_catalog_init(&catalog);
	while (seconds_left(start, seconds)) {
		size_t length = next_word() % 80;
		uint8_t* bytes = (uint8_t*)malloc(length == 0 ? 1 : length);
		fill(bytes, length);
		midi2_profile_message message;
		memset(&message, 0, sizeof(message));
		midi2_profile_decode(bytes, length, &message);
		message.data = bytes;
		message.dataLength = length;
		message.subId = (uint8_t)next_word();
		uint8_t encoded[128];
		size_t encodedLength = 0;
		midi2_profile_encode(&message, encoded, sizeof(encoded), &encodedLength);
		midi2_profile_message reply;
		memset(&reply, 0, sizeof(reply));
		midi2_profile_respond(&catalog, &message, &reply);
		if (encodedLength > 0)
			keep(encoded[0]);
		free(bytes);
	}
}


static void
fuzz_stream(time_t start, int seconds)
{
	midi2_endpoint endpoint;
	midi2_endpoint_init(&endpoint);
	while (seconds_left(start, seconds)) {
		uint32_t request[4];
		for (int i = 0; i < 4; i++)
			request[i] = next_word();
		uint32_t replies[32];
		size_t count = 0;
		if (midi2_stream_reply(&endpoint, request, replies, 8, &count) == B_OK
			&& count > 0)
			keep((uint8_t)replies[0]);
		char text[16];
		fill((uint8_t*)text, sizeof(text) - 1);
		text[sizeof(text) - 1] = '\0';
		uint32_t words[4];
		midi2_stream_text((uint8_t)next_word(), (uint8_t)next_word(), text, words);
		keep((uint8_t)words[0]);
	}
}


static void
fuzz_usb(time_t start, int seconds)
{
	while (seconds_left(start, seconds)) {
		size_t length = next_word() % 32;
		uint8_t* bytes = (uint8_t*)malloc(length == 0 ? 1 : length);
		fill(bytes, length);
		midi2_usb_interface interface;
		midi2_usb_header header;
		midi2_usb_group_terminal block;
		memset(&interface, 0, sizeof(interface));
		memset(&header, 0, sizeof(header));
		memset(&block, 0, sizeof(block));
		midi2_usb_parse_interface(bytes, length, &interface);
		midi2_usb_parse_header(bytes, length, &header);
		midi2_usb_parse_group_terminal(bytes, length, &block);
		uint8_t out[16];
		size_t outLength = 0;
		midi2_usb_write_interface(&interface, out, sizeof(out), &outLength);
		midi2_usb_write_header(&header, out, sizeof(out), &outLength);
		midi2_usb_write_group_terminal(&block, out, sizeof(out), &outLength);
		if (outLength > 0)
			keep(out[0]);
		free(bytes);
	}
}


int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: midi2-fuzz <target> <seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[2]);
	if (seconds < 1)
		return 2;
	const char* target = argv[1];
	time_t start = time(NULL);
	if (strcmp(target, "kernel") == 0)
		fuzz_kernel(start, seconds);
	else if (strcmp(target, "payload") == 0)
		fuzz_payload(start, seconds);
	else if (strcmp(target, "property") == 0)
		fuzz_property(start, seconds);
	else if (strcmp(target, "profile") == 0)
		fuzz_profile(start, seconds);
	else if (strcmp(target, "stream") == 0)
		fuzz_stream(start, seconds);
	else if (strcmp(target, "usb") == 0)
		fuzz_usb(start, seconds);
	else {
		fprintf(stderr, "unknown target\n");
		return 2;
	}
	printf("ok %s %d\n", target, seconds);
	return 0;
}
