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


/* Defined in libmidi2.so. The name and the arguments are unchanged. */
status_t
midi2_usb_write_interface(const midi2_usb_interface* interface, uint8* out,
	size_t capacity, size_t* _length);




status_t
midi2_usb_parse_interface(const uint8* raw, size_t length,
	midi2_usb_interface* interface);



status_t
midi2_usb_write_header(const midi2_usb_header* header, uint8* out,
	size_t capacity, size_t* _length);



status_t
midi2_usb_parse_header(const uint8* raw, size_t length, midi2_usb_header* header);



status_t
midi2_usb_write_group_terminal(const midi2_usb_group_terminal* block,
	uint8* out, size_t capacity, size_t* _length);



status_t
midi2_usb_parse_group_terminal(const uint8* raw, size_t length,
	midi2_usb_group_terminal* block);



#endif // _MIDI2_USB_H
