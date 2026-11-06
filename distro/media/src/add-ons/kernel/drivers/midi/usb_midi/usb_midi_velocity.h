/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Kernel copies of the note translation in Midi2Defs.h. A kernel driver does
 * not include that header. Keep each function the same as the one named in
 * its comment.
 */


#ifndef USB_MIDI_VELOCITY_H
#define USB_MIDI_VELOCITY_H

#include <stdint.h>


/* Must match midi2_note_on_midi1_velocity. */
static inline constexpr uint8_t
usb_midi_note_on_midi1_velocity(uint16_t velocity)
{
	uint8_t scaled = (uint8_t)(velocity >> 9);
	return scaled == 0 ? 1 : scaled;
}


/* Must match midi2_scale_up_7_to_16. */
static inline constexpr uint16_t
usb_midi_scale_up_7_to_16(uint8_t value7)
{
	uint16_t bitShifted = (uint16_t)value7 << 9;
	if (value7 <= 64)
		return bitShifted;
	uint16_t repeat = (uint16_t)value7 & 0x3f;
	return bitShifted | (uint16_t)(repeat << 3) | (uint16_t)(repeat >> 3);
}


/* Must match midi2_translate_midi1_note. */
static inline constexpr void
usb_midi_translate_midi1_note(uint8_t channel, uint8_t note, uint8_t velocity,
	int noteOn, uint32_t words[2])
{
	uint8_t status = 0x8;
	uint16_t wide = 0x8000;
	if (!(noteOn && velocity == 0)) {
		status = noteOn ? 0x9 : 0x8;
		wide = usb_midi_scale_up_7_to_16(velocity);
	}
	words[0] = (0x4u << 28)
		| ((uint32_t)status << 20)
		| ((uint32_t)(channel & 0x0f) << 16)
		| ((uint32_t)(note & 0x7f) << 8);
	words[1] = (uint32_t)wide << 16;
}


#endif
