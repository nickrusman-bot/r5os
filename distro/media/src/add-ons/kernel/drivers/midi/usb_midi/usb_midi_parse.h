/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: MIT
 *
 * UMP byte walk and the MIDI 1.0 expansion. The driver and the host fuzz
 * harness include this file, so both run the same code.
 */


#ifndef USB_MIDI_PARSE_H
#define USB_MIDI_PARSE_H

#include <stddef.h>
#include <stdint.h>

#include "../../../../../../headers/os/midi2/Midi2Translate.h"


inline uint32_t
usb_midi_load_le32(const uint8_t* in)
{
	return (uint32_t)in[0]
		| ((uint32_t)in[1] << 8)
		| ((uint32_t)in[2] << 16)
		| ((uint32_t)in[3] << 24);
}


/* The next USB-MIDI 1.0 event packet of a SysEx message, from the bytes of
 * that message still to send (USB-MIDI 1.0 § 4, Table 4-1). More than three
 * bytes left: CIN 0x4 with three bytes. Three, two or one left: the end
 * packets CIN 0x7, 0x6 or 0x5 with that many bytes. Nothing left: count 0. */
inline void
usb_midi_sysex_packet(size_t left, uint8_t* cin, uint8_t* count)
{
	if (left > 3) {
		*cin = 0x4;
		*count = 3;
		return;
	}
	*cin = (uint8_t)(0x4 + left);
	*count = (uint8_t)left;
}


inline uint32_t
usb_midi_ump_word_count(uint32_t first)
{
	switch (first >> 28) {
		case 0x0:
		case 0x1:
		case 0x2:
			return 1;
		case 0x3:
		case 0x4:
			return 2;
		case 0x5:
		case 0xd:
		case 0xf:
			return 4;
		default:
			return 0;
	}
}


/* The size of any UMP, also of the reserved message types, so a receiver
 * can step over a packet it does not know (UMP 1.1 § 2.1.4). The walk used
 * usb_midi_ump_word_count and dropped the rest of a transfer at the first
 * reserved type. Writing still accepts only the known types. */
inline uint32_t
usb_midi_ump_size(uint32_t first)
{
	switch (first >> 28) {
		case 0x0: case 0x1: case 0x2: case 0x6: case 0x7:
			return 1;
		case 0x3: case 0x4: case 0x8: case 0x9: case 0xa:
			return 2;
		case 0xb: case 0xc:
			return 3;
		default:
			return 4;
	}
}


inline int
usb_midi_sysex7_bytes(const uint32_t* words, uint8_t* status, uint8_t data[6])
{
	if ((words[0] >> 28) != 0x3)
		return -1;
	*status = (words[0] >> 20) & 0x0f;
	int count = (int)((words[0] >> 16) & 0x0f);
	if (*status > 3 || count > 6)
		return -1;
	data[0] = (words[0] >> 8) & 0xff;
	data[1] = words[0] & 0xff;
	data[2] = (words[1] >> 24) & 0xff;
	data[3] = (words[1] >> 16) & 0xff;
	data[4] = (words[1] >> 8) & 0xff;
	data[5] = words[1] & 0xff;
	return count;
}


/* out holds 16 bytes. */
inline int
usb_midi_expand_ump(const uint32_t* words, uint32_t wordCount, uint8_t* out)
{
	uint32_t messageType = words[0] >> 28;
	if (messageType == 0x1 || messageType == 0x2) {
		out[0] = (words[0] >> 16) & 0xff;
		out[1] = (words[0] >> 8) & 0xff;
		out[2] = words[0] & 0xff;
		if (messageType == 0x1) {
			if (out[0] >= 0xf8 || out[0] == 0xf6)
				return 1;
			if (out[0] == 0xf1 || out[0] == 0xf3)
				return 2;
			if (out[0] == 0xf2)
				return 3;
			return 0;
		}
		uint8_t kind = out[0] & 0xf0;
		if (kind == 0xc0 || kind == 0xd0)
			return 2;
		if (kind >= 0x80 && kind <= 0xe0)
			return 3;
		return 0;
	}
	if (messageType == 0x4 && wordCount == 2)
		return midi2_channel_voice_to_midi1(words, wordCount, out);
	if (messageType == 0x3 && wordCount == 2) {
		uint8_t status = 0;
		uint8_t data[6];
		int count = usb_midi_sysex7_bytes(words, &status, data);
		if (count < 0)
			return 0;
		int length = 0;
		if (status == 0 || status == 1)
			out[length++] = 0xf0;
		for (int i = 0; i < count; i++)
			out[length++] = data[i] & 0x7f;
		if (status == 0 || status == 3)
			out[length++] = 0xf7;
		return length;
	}
	return 0;
}


inline void
usb_midi_walk_ump(const uint8_t* bytes, size_t left,
	void (*deliver)(void* cookie, uint32_t group, const uint32_t* words,
		uint32_t count),
	void* cookie)
{
	while (left >= 4) {
		uint32_t word = usb_midi_load_le32(bytes);
		uint32_t count = usb_midi_ump_size(word);
		if (left < count * 4)
			break;

		uint32_t words[4];
		for (uint32_t i = 0; i < count; i++)
			words[i] = usb_midi_load_le32(bytes + i * 4);

		/* A reserved type is stepped over, not delivered. */
		uint32_t group = (word >> 24) & 0x0f;
		if (deliver != NULL && usb_midi_ump_word_count(word) == count)
			deliver(cookie, group, words, count);

		bytes += count * 4;
		left -= count * 4;
	}
}


#endif
