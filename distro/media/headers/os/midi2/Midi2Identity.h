/*
 * Copyright 2026, R5OS Contributors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Who a MIDI 2.0 program says it is. MIDI-CI Property Exchange (DeviceInfo)
 * and the UMP Device Identity both carry a manufacturer SysEx ID, a family,
 * a model and a revision. These belong to the program, not to R5: a
 * program that is published fills in its own SysEx ID from the MIDI
 * Association. B_MIDI2_NONCOMMERCIAL_ID (0x7D) is the ID for educational
 * and non-commercial use, and the default of midi2_pe_catalog_init and
 * midi2_endpoint_init.
 */

#ifndef _MIDI2_IDENTITY_H
#define _MIDI2_IDENTITY_H

#include <OS.h>


const uint8 B_MIDI2_NONCOMMERCIAL_ID = 0x7d;


struct midi2_identity {
	/* A one-byte SysEx ID is { id, 0, 0 }; a three-byte ID is
	 * { 0x00, b1, b2 }, as MIDI-CI writes it. Each byte is 7-bit. */
	uint8		manufacturer[3];
	uint8		family[2];
	uint8		model[2];
	uint8		revision[4];
	/* Names for DeviceInfo. Plain text without '"', '\' or control
	 * characters; NULL is an empty name. */
	const char*	manufacturerName;
	const char*	familyName;
	const char*	modelName;
	const char*	versionName;
};


#endif // _MIDI2_IDENTITY_H
