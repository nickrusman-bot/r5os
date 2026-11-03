/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * USB MIDI 2.0 descriptors: the interface alternate setting, the
 * class-specific header, and a Group Terminal Block. The bytes can be
 * built and parsed without a device.
 */

#ifndef _MIDI2_USB_H
#define _MIDI2_USB_H

#include <string.h>

#include <OS.h>


const uint8 B_MIDI2_USB_INTERFACE = 0x04;
const uint8 B_MIDI2_USB_AUDIO_CLASS = 0x01;
const uint8 B_MIDI2_USB_MIDISTREAMING = 0x03;
const uint8 B_MIDI2_USB_CS_INTERFACE = 0x24;
const uint8 B_MIDI2_USB_MS_HEADER = 0x01;
const uint8 B_MIDI2_USB_GROUP_TERMINAL_TYPE = 0x26;
const uint8 B_MIDI2_USB_GROUP_TERMINAL_SUBTYPE = 0x01;
const uint16 B_MIDI2_USB_VERSION = 0x0200;

const uint8 B_MIDI2_USB_BLOCK_BIDIRECTIONAL = 0x00;
const uint8 B_MIDI2_USB_BLOCK_INPUT = 0x01;
const uint8 B_MIDI2_USB_BLOCK_OUTPUT = 0x02;
const uint8 B_MIDI2_USB_PROTOCOL_MIDI1_64 = 0x01;
const uint8 B_MIDI2_USB_PROTOCOL_MIDI1_128 = 0x02;
const uint8 B_MIDI2_USB_PROTOCOL_MIDI2 = 0x11;

const size_t B_MIDI2_USB_INTERFACE_LENGTH = 9;
const size_t B_MIDI2_USB_HEADER_LENGTH = 7;
const size_t B_MIDI2_USB_GROUP_TERMINAL_LENGTH = 13;


struct midi2_usb_interface {
	uint8		length;
	uint8		type;
	uint8		interfaceNumber;
	uint8		alternateSetting;
	uint8		numEndpoints;
	uint8		deviceClass;
	uint8		subclass;
	uint8		protocol;
	uint8		stringIndex;
};


struct midi2_usb_header {
	uint8		length;
	uint8		type;
	uint8		subtype;
	uint16		version;
	uint16		totalLength;
};


struct midi2_usb_group_terminal {
	uint8		length;
	uint8		type;
	uint8		subtype;
	uint8		blockId;
	uint8		direction;
	uint8		firstGroup;
	uint8		groupCount;
	uint8		stringIndex;
	uint8		protocol;
	uint16		maxInputBandwidth;
	uint16		maxOutputBandwidth;
};


inline status_t
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



inline status_t
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


inline status_t
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


inline status_t
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


inline status_t
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


inline status_t
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


#endif // _MIDI2_USB_H
