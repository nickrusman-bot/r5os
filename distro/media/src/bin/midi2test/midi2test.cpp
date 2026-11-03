/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks the MIDI 2.0 helpers and, when midi_server is running, one local
 * producer sprayed into one local consumer.
 */


#include <stdio.h>
#include <string.h>

#include <MidiConsumer.h>
#include <MidiProducer.h>
#include <OS.h>


static int sFailures = 0;


static void
expect(bool ok, const char* name)
{
	if (ok) {
		printf("ok   %s\n", name);
		return;
	}

	printf("FAIL %s\n", name);
	sFailures++;
}


static void
test_helpers()
{
	expect(midi2_ump_word_count(0x00000000) == 1, "type 0 is one word");
	expect(midi2_ump_word_count(0x20000000) == 1, "type 2 is one word");
	expect(midi2_ump_word_count(0x30000000) == 2, "type 3 is two words");
	expect(midi2_ump_word_count(0x40000000) == 2, "type 4 is two words");
	expect(midi2_ump_word_count(0x50000000) == 4, "type 5 is four words");
	expect(midi2_ump_word_count(0x60000000) == 0, "type 6 is rejected");

	uint32 note[2];
	midi2_encode_note_on(3, 60, 0x8000, note);
	expect(note[0] == ((0x4u << 28) | (0x9u << 20) | (3u << 16) | (60u << 8)),
		"note-on word 0");
	expect(note[1] == (0x8000u << 16), "note-on velocity sits in word 1");

	uint32 control[2];
	midi2_encode_control_change(1, 7, 0x80000000, control);
	expect(control[0] == ((0x4u << 28) | (0xbu << 20) | (1u << 16) | (7u << 8)),
		"control word 0");
	expect(control[1] == 0x80000000, "control value is word 1");

	uint8 payload[16];
	uint32 magic = B_MIDI2_UMP_MAGIC;
	uint32 count = 2;
	memcpy(payload, &magic, 4);
	memcpy(payload + 4, &count, 4);
	memcpy(payload + 8, note, 8);

	uint32 out[4];
	size_t outCount = 0;
	expect(midi2_ump_from_payload(payload, 16, out, 4, &outCount) == B_OK
		&& outCount == 2 && out[0] == note[0] && out[1] == note[1],
		"payload unpacks the note");
	expect(midi2_ump_from_payload(payload, 12, out, 4, &outCount) == B_BAD_VALUE,
		"short payload is rejected");
	expect(midi2_ump_from_payload(NULL, 16, out, 4, &outCount) == B_BAD_VALUE,
		"null payload is rejected");

	payload[0] ^= 0xff;
	expect(midi2_ump_from_payload(payload, 16, out, 4, &outCount) == B_BAD_VALUE,
		"wrong magic is rejected");

	expect(midi2_note_velocity(note, 2) == 0x8000,
		"helper keeps the 16-bit velocity");
	expect(midi2_control_value(control, 2) == 0x80000000,
		"helper keeps the 32-bit controller");
	expect(midi2_note_velocity(control, 2) == 0,
		"a controller is not a note velocity");

	uint8 sysex[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint32 sysexWords[4];
	size_t sysexCount = 0;
	expect(midi2_encode_sysex(sysex, 8, sysexWords, 2, &sysexCount) == B_BAD_VALUE,
		"short SysEx buffer is rejected");
	expect(midi2_encode_sysex(sysex, 8, sysexWords, 4, &sysexCount) == B_OK
		&& sysexCount == 4, "eight SysEx bytes are two packets");
	expect((sysexWords[0] >> 28) == 0x3 && ((sysexWords[0] >> 20) & 0xf) == 1
		&& ((sysexWords[0] >> 16) & 0xf) == 6, "first SysEx packet starts");
	expect(((sysexWords[2] >> 20) & 0xf) == 3
		&& ((sysexWords[2] >> 16) & 0xf) == 2, "second SysEx packet ends");

	uint8 unpacked[6];
	uint8 sysexStatus = 0xff;
	size_t unpackedCount = 0;
	expect(midi2_sysex_payload(sysexWords, 2, &sysexStatus, unpacked, 6,
			&unpackedCount) == B_OK && sysexStatus == 1 && unpackedCount == 6
		&& memcmp(unpacked, sysex, 6) == 0, "start packet returns six bytes");
	expect(midi2_sysex_payload(sysexWords + 2, 2, &sysexStatus, unpacked, 6,
			&unpackedCount) == B_OK && sysexStatus == 3 && unpackedCount == 2
		&& unpacked[0] == 7 && unpacked[1] == 8, "end packet returns the rest");

	uint32 one[2];
	expect(midi2_encode_sysex(sysex, 4, one, 2, &sysexCount) == B_OK
		&& sysexCount == 2 && ((one[0] >> 20) & 0xf) == 0,
		"four SysEx bytes are one complete packet");
}


class Probe : public BMidiLocalConsumer {
public:
	Probe()
		:
		BMidiLocalConsumer("midi2test"),
		fDepth(0),
		fUmp(false),
		fWordCount(0),
		fMidi(false),
		fMidiLength(0),
		fNote(false),
		fNoteOff(false),
		fChannel(0),
		fKey(0),
		fVelocity(0),
		fControl(false),
		fController(0),
		fControlValue(0),
		fSysex(false),
		fSysexLength(0)
	{
		fDone = create_sem(0, "midi2test done");
	}

	void Reset()
	{
		fUmp = false;
		fWordCount = 0;
		fMidi = false;
		fMidiLength = 0;
		fNote = false;
		fNoteOff = false;
		fControl = false;
		fSysex = false;
		fSysexLength = 0;
		memset(fWords, 0, sizeof(fWords));
		memset(fBytes, 0, sizeof(fBytes));
		while (acquire_sem_etc(fDone, 1, B_RELATIVE_TIMEOUT, 0) == B_OK)
			;
	}

	status_t Wait()
	{
		return acquire_sem_etc(fDone, 1, B_RELATIVE_TIMEOUT, 2000000);
	}

	bool HasUmp(const uint32* words, size_t count) const
	{
		if (!fUmp || fWordCount != count)
			return false;
		return memcmp(fWords, words, count * 4) == 0;
	}

	bool HasMidi(const uchar* bytes, size_t length) const
	{
		if (!fMidi || fMidiLength != length)
			return false;
		return memcmp(fBytes, bytes, length) == 0;
	}

	uchar Channel() const
	{
		return fChannel;
	}

	uchar Key() const
	{
		return fKey;
	}

	uchar NoteVelocity() const
	{
		return fVelocity;
	}

	uchar Controller() const
	{
		return fController;
	}

	bool SawNote() const
	{
		return fNote;
	}

	bool SawNoteOff() const
	{
		return fNoteOff;
	}

	uchar ControlValue() const
	{
		return fControlValue;
	}

	bool SawControl() const
	{
		return fControl;
	}

	uint16 FullVelocity() const
	{
		return midi2_note_velocity(fWords, fWordCount);
	}

	uint32 FullControl() const
	{
		return midi2_control_value(fWords, fWordCount);
	}

	bool SawSysex(const uint8* bytes, size_t length) const
	{
		if (!fSysex || fSysexLength != length)
			return false;
		return memcmp(fSysexBytes, bytes, length) == 0;
	}

	bool SawAnySysex() const
	{
		return fSysex;
	}

	virtual void Data(uchar* data, size_t length, bool atomic, bigtime_t time)
	{
		uint32 words[4];
		size_t wordCount = 0;
		bool ump = midi2_ump_from_payload(data, length, words, 4, &wordCount)
			== B_OK;
		if (ump) {
			fUmp = true;
			fWordCount = wordCount;
			memcpy(fWords, words, wordCount * 4);
		} else if (length <= sizeof(fBytes)) {
			fMidi = true;
			fMidiLength = length;
			memcpy(fBytes, data, length);
		}

		fDepth++;
		BMidiLocalConsumer::Data(data, length, atomic, time);
		fDepth--;
		if (fDepth == 0)
			release_sem(fDone);
	}

	virtual void NoteOn(uchar channel, uchar note, uchar velocity,
		bigtime_t)
	{
		fNote = true;
		fChannel = channel;
		fKey = note;
		fVelocity = velocity;
	}

	virtual void NoteOff(uchar channel, uchar note, uchar velocity,
		bigtime_t)
	{
		fNoteOff = true;
		fChannel = channel;
		fKey = note;
		fVelocity = velocity;
	}

	virtual void ControlChange(uchar channel, uchar controller, uchar value,
		bigtime_t)
	{
		fControl = true;
		fChannel = channel;
		fController = controller;
		fControlValue = value;
	}

	virtual void SystemExclusive(void* data, size_t length, bigtime_t)
	{
		fSysex = true;
		fSysexLength = length < sizeof(fSysexBytes) ? length : sizeof(fSysexBytes);
		if (data != NULL && fSysexLength > 0)
			memcpy(fSysexBytes, data, fSysexLength);
	}

private:
	sem_id		fDone;
	int32		fDepth;
	bool		fUmp;
	uint32		fWords[4];
	size_t		fWordCount;
	bool		fMidi;
	uchar		fBytes[8];
	size_t		fMidiLength;
	bool		fNote;
	bool		fNoteOff;
	uchar		fChannel;
	uchar		fKey;
	uchar		fVelocity;
	bool		fControl;
	uchar		fController;
	uchar		fControlValue;
	bool		fSysex;
	uint8		fSysexBytes[16];
	size_t		fSysexLength;
};


static bool
wait_valid(BMidiEndpoint* endpoint)
{
	for (int i = 0; i < 50; i++) {
		if (endpoint->IsValid())
			return true;
		snooze(100000);
	}
	return false;
}


static void
test_spray()
{
	BMidiLocalProducer* producer = new BMidiLocalProducer("midi2test");
	Probe* probe = new Probe();
	if (!wait_valid(producer) || !wait_valid(probe)) {
		expect(false, "midi_server registered the endpoints");
		producer->Release();
		probe->Release();
		return;
	}
	expect(true, "midi_server registered the endpoints");

	uint32 bad[2] = { 0x40000000, 0 };
	expect(producer->SprayUMP(NULL, 2) == B_BAD_VALUE, "null UMP is rejected");
	expect(producer->SprayUMP(bad, 1) == B_BAD_VALUE,
		"word count must match the message type");

	status_t connected = producer->Connect(probe);
	expect(connected == B_OK, "Connect returns B_OK");
	bool linked = false;
	for (int i = 0; i < 50 && connected == B_OK; i++) {
		if (producer->IsConnected(probe)) {
			linked = true;
			break;
		}
		snooze(100000);
	}
	expect(linked, "producer is connected");
	if (!linked) {
		producer->Release();
		probe->Release();
		return;
	}

	probe->Reset();
	uchar midiVelocity = 100;
	producer->SprayNoteOn(2, 64, midiVelocity);
	expect(probe->Wait() == B_OK, "MIDI 1.0 note arrives");
	uchar midi1[3] = { (uchar)(B_NOTE_ON + 2), 64, 100 };
	expect(probe->HasMidi(midi1, 3), "MIDI 1.0 note is three bytes");
	expect(probe->SawNote() && probe->Channel() == 2 && probe->Key() == 64
		&& probe->NoteVelocity() == 100,
		"MIDI 1.0 NoteOn keeps channel, note, and velocity");

	probe->Reset();
	uint16 wide = 0x8000;
	producer->SprayNoteOn(2, 64, wide);
	expect(probe->Wait() == B_OK, "16-bit note arrives");
	uint32 note[2];
	midi2_encode_note_on(2, 64, wide, note);
	expect(probe->HasUmp(note, 2), "16-bit note is a type 4 packet");
	expect(probe->SawNote() && probe->Channel() == 2 && probe->Key() == 64
		&& probe->NoteVelocity() == 0x40,
		"NoteOn receives the top 7 velocity bits");
	expect(probe->FullVelocity() == wide,
		"Data keeps the 16-bit velocity");

	probe->Reset();
	uint32 noteOff[2];
	noteOff[0] = (0x4u << 28) | (0x8u << 20) | (2u << 16) | (70u << 8);
	noteOff[1] = 0xffffu << 16;
	expect(producer->SprayUMP(noteOff, 2) == B_OK, "note-off UMP is accepted");
	expect(probe->Wait() == B_OK, "note-off UMP arrives");
	expect(probe->SawNoteOff() && probe->Channel() == 2 && probe->Key() == 70
		&& probe->NoteVelocity() == 0x7f,
		"NoteOff receives the top 7 velocity bits");

	probe->Reset();
	uchar midiControl = 90;
	producer->SprayControlChange(4, B_MAIN_VOLUME, midiControl);
	expect(probe->Wait() == B_OK, "MIDI 1.0 control arrives");
	uchar ccMidi[3] = { (uchar)(B_CONTROL_CHANGE + 4), B_MAIN_VOLUME, 90 };
	expect(probe->HasMidi(ccMidi, 3), "MIDI 1.0 control is three bytes");
	expect(probe->SawControl() && probe->Channel() == 4
		&& probe->Controller() == B_MAIN_VOLUME && probe->ControlValue() == 90,
		"ControlChange keeps controller 7 and value 90");

	probe->Reset();
	uint32 wideControl = 0x80000000;
	producer->SprayControlChange(4, B_MAIN_VOLUME, wideControl);
	expect(probe->Wait() == B_OK, "32-bit control arrives");
	uint32 control[2];
	midi2_encode_control_change(4, B_MAIN_VOLUME, wideControl, control);
	expect(probe->HasUmp(control, 2), "32-bit control is a type 4 packet");
	expect(probe->SawControl() && probe->Channel() == 4
		&& probe->Controller() == B_MAIN_VOLUME && probe->ControlValue() == 0x40,
		"ControlChange receives the top 7 bits");
	expect(probe->FullControl() == wideControl,
		"Data keeps the 32-bit controller");

	probe->Reset();
	uint8 classicSysex[3] = { 0x7e, 0x7f, 0x06 };
	producer->SpraySystemExclusive(classicSysex, 3, 0);
	expect(probe->Wait() == B_OK, "MIDI 1.0 SysEx arrives");
	uchar wrapped[5] = { B_SYS_EX_START, 0x7e, 0x7f, 0x06, B_SYS_EX_END };
	expect(probe->HasMidi(wrapped, 5), "MIDI 1.0 SysEx keeps F0 and F7");
	expect(probe->SawSysex(classicSysex, 3),
		"SystemExclusive receives the bytes between F0 and F7");

	probe->Reset();
	uint8 longSysex[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	uint32 sysexWords[4];
	size_t sysexCount = 0;
	expect(midi2_encode_sysex(longSysex, 8, sysexWords, 4, &sysexCount) == B_OK,
		"SysEx UMP encodes");
	expect(producer->SprayUMP(sysexWords, 2) == B_OK, "SysEx start is accepted");
	expect(probe->Wait() == B_OK, "SysEx start arrives");
	expect(!probe->SawAnySysex(), "SysEx waits for the end packet");
	probe->Reset();
	expect(producer->SprayUMP(sysexWords + 2, 2) == B_OK, "SysEx end is accepted");
	expect(probe->Wait() == B_OK, "SysEx end arrives");
	expect(probe->SawSysex(longSysex, 8),
		"SystemExclusive receives the whole SysEx7 payload");

	probe->Reset();
	uint32 midi2Voice = (0x2u << 28) | ((uint32)(B_NOTE_ON + 1) << 16)
		| (67u << 8) | 40u;
	expect(producer->SprayUMP(&midi2Voice, 1) == B_OK,
		"MIDI 1.0 channel-voice UMP is accepted");
	expect(probe->Wait() == B_OK, "channel-voice UMP arrives");
	uchar voice[3] = { (uchar)(B_NOTE_ON + 1), 67, 40 };
	expect(probe->HasUmp(&midi2Voice, 1) && probe->HasMidi(voice, 3),
		"type 2 packet is also delivered as MIDI 1.0 bytes");
	expect(probe->SawNote() && probe->Channel() == 1 && probe->Key() == 67
		&& probe->NoteVelocity() == 40,
		"type 2 packet calls NoteOn");

	producer->Disconnect(probe);
	producer->Release();
	probe->Release();
}


int
main()
{
	test_helpers();
	test_spray();
	if (sFailures == 0) {
		printf("midi2test: all checks passed\n");
		return 0;
	}

	printf("midi2test: %d check(s) failed\n", sFailures);
	return 1;
}
