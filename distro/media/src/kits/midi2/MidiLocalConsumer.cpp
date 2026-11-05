/*
 * Copyright 2006, Haiku.
 * 
 * Copyright (c) 2002-2003 Matthijs Hollemans
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Matthijs Hollemans
 */

#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include <Locker.h>
#include <MidiConsumer.h>
#include <MidiRoster.h>
#include "protocol.h"


struct SysexAssembly {
	BMidiLocalConsumer*	consumer;
	uint8				group;
	uint8*				data;
	size_t				size;
	size_t				used;
	bool				started;
	SysexAssembly*		next;
};


static BLocker sSysexLock("midi2 sysex");
static SysexAssembly* sSysex = NULL;


static SysexAssembly*
sysex_find(BMidiLocalConsumer* consumer, uint8 group)
{
	for (SysexAssembly* node = sSysex; node != NULL; node = node->next) {
		if (node->consumer == consumer && node->group == group)
			return node;
	}
	return NULL;
}


static void
sysex_forget(BMidiLocalConsumer* consumer)
{
	sSysexLock.Lock();
	SysexAssembly** link = &sSysex;
	while (*link != NULL) {
		if ((*link)->consumer == consumer) {
			SysexAssembly* node = *link;
			*link = node->next;
			free(node->data);
			free(node);
			continue;
		}
		link = &(*link)->next;
	}
	sSysexLock.Unlock();
}


static bool
sysex_append(SysexAssembly* node, const uint8* bytes, size_t length)
{
	if (node->used + length > 65536)
		return false;
	if (node->used + length > node->size) {
		size_t size = node->size == 0 ? 64 : node->size;
		while (size < node->used + length)
			size *= 2;
		uint8* grown = (uint8*)realloc(node->data, size);
		if (grown == NULL)
			return false;
		node->data = grown;
		node->size = size;
	}
	if (length > 0)
		memcpy(node->data + node->used, bytes, length);
	node->used += length;
	return true;
}


static void
dispatch_sysex(BMidiLocalConsumer* consumer, const uint32* words,
	bigtime_t time)
{
	uint8 status = 0;
	uint8 bytes[6];
	size_t count = 0;
	if (midi2_sysex_payload(words, 2, &status, bytes, sizeof(bytes), &count)
			!= B_OK || status > 3)
		return;

	uint8* owned = NULL;
	size_t ownedCount = 0;

	uint8 group = (uint8)((words[0] >> 24) & 0x0f);
	sSysexLock.Lock();
	SysexAssembly* node = sysex_find(consumer, group);
	if (node == NULL) {
		node = (SysexAssembly*)calloc(1, sizeof(SysexAssembly));
		if (node == NULL) {
			sSysexLock.Unlock();
			return;
		}
		node->consumer = consumer;
		node->group = group;
		node->next = sSysex;
		sSysex = node;
	}

	bool deliver = false;
	if (status == 0 || status == 1) {
		node->used = 0;
		node->started = status == 1;
	}
	if (status == 2 || status == 3) {
		if (!node->started) {
			sSysexLock.Unlock();
			return;
		}
	}
	if (!sysex_append(node, bytes, count)) {
		node->used = 0;
		node->started = false;
		sSysexLock.Unlock();
		return;
	}
	if (status == 0 || status == 3) {
		ownedCount = node->used;
		if (ownedCount > 0) {
			owned = (uint8*)malloc(ownedCount);
			if (owned != NULL)
				memcpy(owned, node->data, ownedCount);
		}
		deliver = owned != NULL || ownedCount == 0;
		node->used = 0;
		node->started = false;
	}
	sSysexLock.Unlock();

	if (deliver)
		consumer->SystemExclusive(owned, ownedCount, time);
	free(owned);
}


int32 
_midi_event_thread(void* data)
{
	return ((BMidiLocalConsumer*) data)->EventThread();
}


BMidiLocalConsumer::BMidiLocalConsumer(const char* name)
	: BMidiConsumer(name)
{
	TRACE(("BMidiLocalConsumer::BMidiLocalConsumer"))

	fIsLocal = true;
	fRefCount = 1;
	fTimeout = (bigtime_t) -1;
	fTimeoutData = NULL;

	fPort = create_port(1, "MidiEventPort");

	BMidiRoster::MidiRoster()->CreateLocal(this);

	BString tmp(name);
	if (tmp.IsEmpty())
		tmp << "Midi ID " << ID();
	tmp << " consumer";

	fThread = spawn_thread(_midi_event_thread, tmp.String(), B_REAL_TIME_PRIORITY, this);
	resume_thread(fThread);
}


BMidiLocalConsumer::~BMidiLocalConsumer()
{
	TRACE(("BMidiLocalConsumer::~BMidiLocalConsumer"))

	BMidiRoster::MidiRoster()->DeleteLocal(this);

	delete_port(fPort);

	status_t result;
	wait_for_thread(fThread, &result);
	sysex_forget(this);
}


void 
BMidiLocalConsumer::SetLatency(bigtime_t latency_)
{
	if (latency_ < 0) {
		WARN("SetLatency() does not accept negative values");
		return;
	} else if (!IsValid()) {
		return;
	} else if (fLatency != latency_) {
		BMessage msg;
		msg.AddInt64("midi:latency", latency_);

		if (SendChangeRequest(&msg) == B_OK) {
			if (LockLooper()) {
				fLatency = latency_;
				UnlockLooper();
			}
		}
	}
}


int32 
BMidiLocalConsumer::GetProducerID()
{
	return fCurrentProducer;
}


void 
BMidiLocalConsumer::SetTimeout(bigtime_t when, void* data)
{
	fTimeout = when;
	fTimeoutData = data;
}


void 
BMidiLocalConsumer::Timeout(void* data)
{
	// Do nothing.
}


static void
dispatch_ump(BMidiLocalConsumer* consumer, uchar* data, size_t length,
	bigtime_t time)
{
	uint32 words[4];
	size_t wordCount = 0;
	if (midi2_ump_from_payload(data, length, words, 4, &wordCount) != B_OK)
		return;

	uint32 messageType = words[0] >> 28;
	if (messageType == 0x4 && wordCount == 2) {
		uchar status = (words[0] >> 20) & 0x0f;
		uchar channel = (words[0] >> 16) & 0x0f;
		uchar index = (words[0] >> 8) & 0xff;
		if (status == 0x8 || status == 0x9) {
			// MIDI 1.0 hooks keep the top 7 bits. Data() still has the packet.
			uint16 full = midi2_note_velocity(words, wordCount);
			uchar velocity = status == 0x9
				? midi2_note_on_midi1_velocity(full)
				: (uchar)(full >> 9);
			if (status == 0x9)
				consumer->NoteOn(channel, index, velocity, time);
			else
				consumer->NoteOff(channel, index, velocity, time);
		} else if (status == 0xb) {
			uchar value = (uchar)(midi2_control_value(words, wordCount) >> 25);
			consumer->ControlChange(channel, index, value, time);
		}
		// Status 0 and 1 are per-note controllers. They stay in Data().
		return;
	}

	if (messageType == 0x3 && wordCount == 2) {
		dispatch_sysex(consumer, words, time);
		return;
	}

	if (messageType == 0x2 && wordCount == 1) {
		uchar midi[3];
		midi[0] = (words[0] >> 16) & 0xff;
		midi[1] = (words[0] >> 8) & 0xff;
		midi[2] = words[0] & 0xff;
		uchar kind = midi[0] & 0xf0;
		size_t midiLength = (kind == B_PROGRAM_CHANGE
			|| kind == B_CHANNEL_PRESSURE) ? 2 : 3;
		consumer->Data(midi, midiLength, true, time);
	}
}


void 
BMidiLocalConsumer::Data(uchar* data, size_t length, bool atomic, bigtime_t time)
{
	if (data != NULL && length >= 8) {
		uint32 magic = 0;
		memcpy(&magic, data, sizeof(magic));
		if (magic == B_MIDI2_UMP_MAGIC) {
			dispatch_ump(this, data, length, time);
			return;
		}
	}
	if (atomic) {
		switch (data[0] & 0xF0) {
			case B_NOTE_OFF:
			{
				if (length == 3)
					NoteOff(data[0] & 0x0F, data[1], data[2], time);
				break;
			}

			case B_NOTE_ON:
			{
				if (length == 3)
					NoteOn(data[0] & 0x0F, data[1], data[2], time);
				break;
			}

			case B_KEY_PRESSURE:
			{
				if (length == 3)
					KeyPressure(data[0] & 0x0F, data[1], data[2], time);
				break;
			}

			case B_CONTROL_CHANGE:
			{
				if (length == 3)
					ControlChange(data[0] & 0x0F, data[1], data[2], time);
				break;
			}

			case B_PROGRAM_CHANGE:
			{
				if (length == 2)
					ProgramChange(data[0] & 0x0F, data[1], time);
				break;
			}

			case B_CHANNEL_PRESSURE:
			{
				if (length == 2)
					ChannelPressure(data[0] & 0x0F, data[1], time);
				break;
			}

			case B_PITCH_BEND:
			{
				if (length == 3)
					PitchBend(data[0] & 0x0F, data[1], data[2], time);
				break;
			}

			case 0xF0:
			{
				switch (data[0]) {
					case B_SYS_EX_START:
					{
						if (data[length - 1] == B_SYS_EX_END) {
							SystemExclusive(data + 1, length - 2, time);
						} else { // sysex-end is not required
							SystemExclusive(data + 1, length - 1, time);
						}
						break;
					}

					case B_TUNE_REQUEST:
					case B_SYS_EX_END:
					{
						if (length == 1) {
							SystemCommon(data[0], 0, 0, time);
						}
						break;
					}

					case B_CABLE_MESSAGE:
					case B_MIDI_TIME_CODE:
					case B_SONG_SELECT:
					{
						if (length == 2) {
							SystemCommon(data[0], data[1], 0, time);
						}
						break;
					}

					case B_SONG_POSITION:
					{
						if (length == 3) {
							SystemCommon(data[0], data[1], data[2], time);
						}
						break;
					}
	
					case B_TIMING_CLOCK:
					case B_START:
					case B_CONTINUE:
					case B_STOP:
					case B_ACTIVE_SENSING:
					{
						if (length == 1) {
							SystemRealTime(data[0], time);
						}
						break;
					}

					case B_SYSTEM_RESET:
					{
						if (length == 1) {
							SystemRealTime(data[0], time);
						} else if ((length == 6) && (data[1] == 0x51) 
								&& (data[2] == 0x03)) {
							int32 tempo = 
								(data[3] << 16) | (data[4] << 8) | data[5];

							TempoChange(60000000/tempo, time);
						}
					}
				}
				break;
			}
		}
	}
}


void 
BMidiLocalConsumer::NoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::NoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::KeyPressure(uchar channel, uchar note, uchar pressure, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::ControlChange(uchar channel, uchar controlNumber, uchar controlValue, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::ProgramChange(uchar channel, uchar programNumber, bigtime_t time)
{
	// Do nothing.
}


void BMidiLocalConsumer::ChannelPressure(uchar channel, uchar pressure, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::PitchBend(uchar channel, uchar lsb, uchar msb, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::SystemExclusive(
	void* data, size_t length, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::SystemCommon(
	uchar statusByte, uchar data1, uchar data2, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::SystemRealTime(uchar statusByte, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::TempoChange(int32 beatsPerMinute, bigtime_t time)
{
	// Do nothing.
}


void 
BMidiLocalConsumer::AllNotesOff(bool justChannel, bigtime_t time)
{
	// Do nothing.
}


void BMidiLocalConsumer::_Reserved1() { }
void BMidiLocalConsumer::_Reserved2() { }
void BMidiLocalConsumer::_Reserved3() { }
void BMidiLocalConsumer::_Reserved4() { }
void BMidiLocalConsumer::_Reserved5() { }
void BMidiLocalConsumer::_Reserved6() { }
void BMidiLocalConsumer::_Reserved7() { }
void BMidiLocalConsumer::_Reserved8() { }


int32 
BMidiLocalConsumer::EventThread()
{
	int32 msg_code;
	ssize_t msg_size;
	ssize_t buf_size = 100;
	uint8* buffer = (uint8*) malloc(buf_size);

	while (true) {
		if (fTimeout == (bigtime_t) -1) {
			msg_size = port_buffer_size(fPort);
		} else { // have timeout
			msg_size = port_buffer_size_etc(fPort, B_ABSOLUTE_TIMEOUT, fTimeout);
			if (msg_size == B_TIMED_OUT) {
				Timeout(fTimeoutData);
				fTimeout = (bigtime_t) -1;
				fTimeoutData = NULL;
				continue;
			}
		}

		if (msg_size < 0) 
			break;  // error reading port

		if (msg_size > buf_size) {
			uint8* tmp_buffer = (uint8*) realloc(buffer, msg_size);
			if (tmp_buffer == NULL)
				break; // error in realloc()
			buffer = tmp_buffer;
			buf_size = msg_size;
		}

		read_port(fPort, &msg_code, buffer, msg_size);

		if (msg_size > 20) { // minimum valid size
			#ifdef DEBUG
			printf("*** received: ");
			for (int32 t = 0; t < msg_size; ++t) {
				printf("%02X, ", ((uint8*) buffer)[t]);
			}
			printf("\n");
			#endif

			fCurrentProducer = *((uint32*)    (buffer +  0));
			int32 targetId  = *((uint32*)    (buffer +  4));
			bigtime_t time  = *((bigtime_t*) (buffer +  8));
			bool atomic     = *((bool*)      (buffer + 16));

			if (targetId == fId) { // only if we are the destination
				Data((uchar*) (buffer + 20), msg_size - 20, atomic, time);
			}
		}
	}

	free(buffer);
	return 0;
}

