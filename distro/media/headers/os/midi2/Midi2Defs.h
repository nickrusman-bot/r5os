
#ifndef _MIDI2_DEFS_H
#define _MIDI2_DEFS_H

#include <OS.h>
#include <Errors.h>
#include <string.h>

#ifndef _MIDI_CONSTANTS_
#define _MIDI_CONSTANTS_

/* Channel Message Masks */
const uchar B_NOTE_OFF          = 0x80;
const uchar B_NOTE_ON           = 0x90;
const uchar B_KEY_PRESSURE      = 0xa0;
const uchar B_CONTROL_CHANGE    = 0xb0;
const uchar B_PROGRAM_CHANGE    = 0xc0;
const uchar B_CHANNEL_PRESSURE  = 0xd0;
const uchar B_PITCH_BEND        = 0xe0;

/* System Messages */
const uchar B_SYS_EX_START      = 0xf0;
const uchar B_MIDI_TIME_CODE    = 0xf1;
const uchar B_SONG_POSITION     = 0xf2;
const uchar B_SONG_SELECT       = 0xf3;
const uchar B_CABLE_MESSAGE     = 0xf5;
const uchar B_TUNE_REQUEST      = 0xf6;
const uchar B_SYS_EX_END        = 0xf7;
const uchar B_TIMING_CLOCK      = 0xf8;
const uchar B_START             = 0xfa;
const uchar B_CONTINUE          = 0xfb;
const uchar B_STOP              = 0xfc;
const uchar B_ACTIVE_SENSING    = 0xfe;
const uchar B_SYSTEM_RESET      = 0xff;

/* Controller Numbers */
const uchar B_MODULATION            = 0x01;
const uchar B_BREATH_CONTROLLER     = 0x02;
const uchar B_FOOT_CONTROLLER       = 0x04;
const uchar B_PORTAMENTO_TIME       = 0x05;
const uchar B_DATA_ENTRY            = 0x06;
const uchar B_MAIN_VOLUME           = 0x07;
const uchar B_MIDI_BALANCE          = 0x08;
const uchar B_PAN                   = 0x0a;
const uchar B_EXPRESSION_CTRL       = 0x0b;
const uchar B_GENERAL_CTRL_1        = 0x10;
const uchar B_GENERAL_CTRL_2        = 0x11;
const uchar B_GENERAL_CTRL_3        = 0x12;
const uchar B_GENERAL_CTRL_4        = 0x13;
const uchar B_SUSTAIN_PEDAL         = 0x40;
const uchar B_PORTAMENTO            = 0x41;
const uchar B_SOSTENUTO             = 0x42;
const uchar B_SOFT_PEDAL            = 0x43;
const uchar B_HOLD_2                = 0x45;
const uchar B_GENERAL_CTRL_5        = 0x50;
const uchar B_GENERAL_CTRL_6        = 0x51;
const uchar B_TEMPO_CHANGE          = 0x51;
const uchar B_GENERAL_CTRL_7        = 0x52;
const uchar B_GENERAL_CTRL_8        = 0x53;
const uchar B_EFFECTS_DEPTH         = 0x5b;
const uchar B_TREMOLO_DEPTH         = 0x5c;
const uchar B_CHORUS_DEPTH          = 0x5d;
const uchar B_CELESTE_DEPTH         = 0x5e;
const uchar B_PHASER_DEPTH          = 0x5f;
const uchar B_DATA_INCREMENT        = 0x60;
const uchar B_DATA_DECREMENT        = 0x61;
const uchar B_RESET_ALL_CONTROLLERS = 0x79;
const uchar B_LOCAL_CONTROL         = 0x7a;
const uchar B_ALL_NOTES_OFF         = 0x7b;
const uchar B_OMNI_MODE_OFF         = 0x7c;
const uchar B_OMNI_MODE_ON          = 0x7d;
const uchar B_MONO_MODE_ON          = 0x7e;
const uchar B_POLY_MODE_ON          = 0x7f;

#endif // _MIDI_CONSTANTS_

/* Universal MIDI Packet. MIDI 1.0 channel messages stay the bytes above.
 * A sprayed UMP is not a MIDI 1.0 byte string. The bytes after the roster
 * header are: uint32 magic, uint32 word count, then that many native words.
 */
const uint32 B_MIDI2_UMP_MAGIC = 0x31504d55; /* 'UMP1' on a little-endian CPU */


inline uint32
midi2_ump_word_count(uint32 firstWord)
{
	switch (firstWord >> 28) {
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


inline void
midi2_encode_note_on(uchar channel, uchar note, uint16 velocity,
	uint32 words[2])
{
	words[0] = (0x4u << 28)
		| (0x9u << 20)
		| ((uint32)(channel & 0x0f) << 16)
		| ((uint32)note << 8);
	words[1] = (uint32)velocity << 16;
}


inline void
midi2_encode_control_change(uchar channel, uchar controller, uint32 value,
	uint32 words[2])
{
	words[0] = (0x4u << 28)
		| (0xbu << 20)
		| ((uint32)(channel & 0x0f) << 16)
		| ((uint32)controller << 8);
	words[1] = value;
}


inline status_t
midi2_ump_from_payload(const void* data, size_t length, uint32* words,
	size_t capacity, size_t* _wordCount)
{
	if (data == NULL || words == NULL || _wordCount == NULL || length < 8)
		return B_BAD_VALUE;

	const uint8* bytes = (const uint8*)data;
	uint32 magic = 0;
	uint32 wordCount = 0;
	memcpy(&magic, bytes, sizeof(magic));
	memcpy(&wordCount, bytes + 4, sizeof(wordCount));
	if (magic != B_MIDI2_UMP_MAGIC || wordCount == 0 || wordCount > capacity)
		return B_BAD_VALUE;
	if (length < 8 + (size_t)wordCount * 4)
		return B_BAD_VALUE;

	memcpy(words, bytes + 8, (size_t)wordCount * 4);
	if (midi2_ump_word_count(words[0]) != wordCount)
		return B_BAD_VALUE;

	*_wordCount = wordCount;
	return B_OK;
}


/* Type 0x4 channel voice. The 16-bit velocity is the top half of word 1.
 * A 32-bit controller value is all of word 1. These do not change the
 * 7-bit NoteOn / ControlChange hooks.
 */
inline bool
midi2_is_note(const uint32* words, size_t wordCount)
{
	if (words == NULL || wordCount != 2 || (words[0] >> 28) != 0x4)
		return false;
	uint32 status = (words[0] >> 20) & 0x0f;
	return status == 0x8 || status == 0x9;
}


inline uint16
midi2_note_velocity(const uint32* words, size_t wordCount)
{
	if (!midi2_is_note(words, wordCount))
		return 0;
	return (uint16)(words[1] >> 16);
}


/* Appendix D, MIDI 2.0 Note On Velocity (D.2.1). Every note-on is at least 1.
 * The same formula is usb_midi_note_on_midi1_velocity. Keep the two copies
 * the same.
 */
inline constexpr uint8
midi2_note_on_midi1_velocity(uint16 velocity)
{
	uint8 scaled = (uint8)(velocity >> 9);
	return scaled == 0 ? 1 : scaled;
}


/* Appendix D, Min-Center-Max (D.1.3), 7 bits to 16. The same formula is
 * usb_midi_scale_up_7_to_16. Keep the two copies the same.
 */
inline constexpr uint16
midi2_scale_up_7_to_16(uint8 value7)
{
	uint16 bitShifted = (uint16)value7 << 9;
	if (value7 <= 64)
		return bitShifted;
	uint16 repeat = (uint16)value7 & 0x3f;
	return bitShifted | (uint16)(repeat << 3) | (uint16)(repeat >> 3);
}


/* Appendix D, Note On/Off (D.3.1). A MIDI 1.0 note-on of velocity 0 is a
 * MIDI 2.0 note-off at 0x8000. Attribute type and attribute value stay 0.
 * The same formula is usb_midi_translate_midi1_note. Keep the two copies
 * the same.
 */
inline constexpr void
midi2_translate_midi1_note(uint8 channel, uint8 note, uint8 velocity,
	bool noteOn, uint32 words[2])
{
	uint8 status = 0x8;
	uint16 wide = 0x8000;
	if (!(noteOn && velocity == 0)) {
		status = noteOn ? 0x9 : 0x8;
		wide = midi2_scale_up_7_to_16(velocity);
	}
	words[0] = (0x4u << 28)
		| ((uint32)status << 20)
		| ((uint32)(channel & 0x0f) << 16)
		| ((uint32)(note & 0x7f) << 8);
	words[1] = (uint32)wide << 16;
}


inline bool
midi2_is_control(const uint32* words, size_t wordCount)
{
	return words != NULL && wordCount == 2 && (words[0] >> 28) == 0x4
		&& ((words[0] >> 20) & 0x0f) == 0x0b;
}


inline uint32
midi2_control_value(const uint32* words, size_t wordCount)
{
	if (!midi2_is_control(words, wordCount))
		return 0;
	return words[1];
}


/* MIDI 2.0 per-note controller, message type 0x4. Status 0 is registered,
 * status 1 is assignable. The note is 7 bits. The index is one byte.
 * The controller value is all of word 1. MIDI 1.0 hooks do not see it.
 */
inline void
midi2_encode_per_note(uchar channel, uchar note, uint8 index, uint32 value,
	bool registered, uint32 words[2])
{
	uint32 status = registered ? 0 : 1;
	words[0] = (0x4u << 28) | (status << 20) | ((uint32)(channel & 0x0f) << 16)
		| ((uint32)(note & 0x7f) << 8) | index;
	words[1] = value;
}


inline bool
midi2_is_per_note(const uint32* words, size_t wordCount)
{
	if (words == NULL || wordCount != 2 || (words[0] >> 28) != 0x4)
		return false;
	uint32 status = (words[0] >> 20) & 0x0f;
	return status == 0 || status == 1;
}


inline uint8
midi2_per_note_note(const uint32* words, size_t wordCount)
{
	if (!midi2_is_per_note(words, wordCount))
		return 0;
	return (words[0] >> 8) & 0x7f;
}


inline uint8
midi2_per_note_index(const uint32* words, size_t wordCount)
{
	if (!midi2_is_per_note(words, wordCount))
		return 0;
	return words[0] & 0xff;
}


inline uint32
midi2_per_note_value(const uint32* words, size_t wordCount)
{
	if (!midi2_is_per_note(words, wordCount))
		return 0;
	return words[1];
}


inline bool
midi2_per_note_registered(const uint32* words, size_t wordCount)
{
	return midi2_is_per_note(words, wordCount) && ((words[0] >> 20) & 0x0f) == 0;
}


/* SysEx7, message type 0x3. Status 0 is a complete message, 1 starts,
 * 2 continues, 3 ends. The payload is the bytes between F0 and F7.
 * Each packet holds at most 6 of those bytes.
 */
inline size_t
midi2_sysex_packet_count(size_t length)
{
	if (length == 0)
		return 1;
	return (length + 5) / 6;
}


inline status_t
midi2_encode_sysex(const uint8* data, size_t length, uint32* words,
	size_t wordCapacity, size_t* _wordCount)
{
	if (words == NULL || _wordCount == NULL || (length > 0 && data == NULL))
		return B_BAD_VALUE;

	size_t packets = midi2_sysex_packet_count(length);
	if (wordCapacity < packets * 2)
		return B_BAD_VALUE;

	size_t offset = 0;
	for (size_t packet = 0; packet < packets; packet++) {
		size_t count = length - offset;
		if (count > 6)
			count = 6;
		bool first = packet == 0;
		bool last = offset + count == length;
		uint32 status = 0;
		if (!first && last)
			status = 3;
		else if (!first)
			status = 2;
		else if (!last)
			status = 1;

		uint32 word0 = (0x3u << 28) | (status << 20) | ((uint32)count << 16);
		uint32 word1 = 0;
		for (size_t i = 0; i < count; i++) {
			uint32 byte = data[offset + i] & 0x7f;
			if (i == 0)
				word0 |= byte << 8;
			else if (i == 1)
				word0 |= byte;
			else
				word1 |= byte << (8 * (5 - i));
		}
		words[packet * 2] = word0;
		words[packet * 2 + 1] = word1;
		offset += count;
	}

	*_wordCount = packets * 2;
	return B_OK;
}


inline status_t
midi2_sysex_payload(const uint32* words, size_t wordCount, uint8* _status,
	uint8* out, size_t outSize, size_t* _length)
{
	if (words == NULL || wordCount != 2 || (words[0] >> 28) != 0x3)
		return B_BAD_VALUE;

	uint32 count = (words[0] >> 16) & 0x0f;
	if (count > 6 || (out == NULL && count > 0) || outSize < count)
		return B_BAD_VALUE;

	uint8 raw[6];
	raw[0] = (words[0] >> 8) & 0xff;
	raw[1] = words[0] & 0xff;
	raw[2] = (words[1] >> 24) & 0xff;
	raw[3] = (words[1] >> 16) & 0xff;
	raw[4] = (words[1] >> 8) & 0xff;
	raw[5] = words[1] & 0xff;
	if (count > 0)
		memcpy(out, raw, count);
	if (_status != NULL)
		*_status = (words[0] >> 20) & 0x0f;
	if (_length != NULL)
		*_length = count;
	return B_OK;
}


#include "Midi2Translate.h"
#include <Midi2Property.h>
#include <Midi2Profile.h>
#include <Midi2Stream.h>
#include <Midi2Usb.h>


#endif // _MIDI2_DEFS_H
