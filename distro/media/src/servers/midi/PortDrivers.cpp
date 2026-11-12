/*
 * Copyright 2003-2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Matthijs Hollemans
 *		Christian Packmann
 *		Jerome Leveque
 *		Philippe Houdoin
 *		Pete Goodeve
 */


#include "PortDrivers.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <String.h>
#include <midi_driver.h>
#include <Midi2Defs.h>
#include <Midi2Translate.h>


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static void write_midi1(int fd, const uint32* words, size_t wordCount);
// SPDX-SnippetEnd


MidiPortConsumer::MidiPortConsumer(int fd, const char* name)
	:
	BMidiLocalConsumer(name),
	fFileDescriptor(fd)
{
}


void 
MidiPortConsumer::Data(uchar* data, size_t length, 
	bool atomic, bigtime_t time)
{
	snooze_until(time - Latency(), B_SYSTEM_TIMEBASE);

	uint32 words[4];
	size_t wordCount = 0;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (midi2_ump_from_payload(data, length, words, 4, &wordCount) == B_OK) {
		midi_ump_packet packet;
		packet.word_count = (uint32)wordCount;
		for (size_t i = 0; i < 4; i++)
			packet.words[i] = i < wordCount ? words[i] : 0;
		if (ioctl(fFileDescriptor, B_MIDI_WRITE_UMP, &packet, sizeof(packet))
				== 0) {
			return;
		}
		// ioctl() returns -1 and sets errno. A driver without UMP support
		// gets the same message as MIDI 1.0 bytes, one event per write().
		if (errno != B_DEV_INVALID_IOCTL && errno != B_UNSUPPORTED) {
			perror("Error sending UMP to driver");
			return;
		}
		write_midi1(fFileDescriptor, words, wordCount);
		return;
	}
	// SPDX-SnippetEnd

	if (write(fFileDescriptor, data, length) == -1) {
		perror("Error sending data to driver");
	}
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static void
write_midi1(int fd, const uint32* words, size_t wordCount)
{
	uint8 bytes[12];
	int length = 0;
	uint32 messageType = words[0] >> 28;
	if (messageType == 0x4 && wordCount == 2)
		length = midi2_channel_voice_to_midi1(words, wordCount, bytes);
	else if ((messageType == 0x1 || messageType == 0x2) && wordCount == 1) {
		bytes[0] = (words[0] >> 16) & 0xff;
		bytes[1] = (words[0] >> 8) & 0xff;
		bytes[2] = words[0] & 0xff;
		uint8 kind = bytes[0] & 0xf0;
		if (messageType == 0x2)
			length = (kind == 0xc0 || kind == 0xd0) ? 2 : 3;
		else if (bytes[0] >= 0xf8 || bytes[0] == 0xf6)
			length = 1;
		else if (bytes[0] == 0xf1 || bytes[0] == 0xf3)
			length = 2;
		else if (bytes[0] == 0xf2)
			length = 3;
	}

	int offset = 0;
	while (offset < length) {
		uint8 kind = bytes[offset] & 0xf0;
		int eventLength = length - offset;
		if (messageType == 0x4)
			eventLength = (kind == 0xc0 || kind == 0xd0) ? 2 : 3;
		if (write(fd, bytes + offset, eventLength) == -1) {
			perror("Error sending data to driver");
			return;
		}
		offset += eventLength;
	}
}
// SPDX-SnippetEnd


// #pragma mark -


MidiPortProducer::MidiPortProducer(int fd, const char *name)
	:
	BMidiLocalProducer(name),
	fFileDescriptor(fd),
	fKeepRunning(true)
{
	BString tmp = name;
	tmp << " producer";

	fReaderThread = spawn_thread(
		_ReaderThread, tmp.String(), B_URGENT_PRIORITY, this);

	resume_thread(fReaderThread);
}


MidiPortProducer::~MidiPortProducer()
{
	fKeepRunning = false;

	status_t dummy;
	wait_for_thread(fReaderThread, &dummy);
}


int32
MidiPortProducer::_ReaderThread(void* data)
{
	return ((MidiPortProducer*) data)->GetData();
}


int32
MidiPortProducer::GetData()
{
	uint8 bytes[256];
	ssize_t bytesCount = 0;
	uint8* nextByte = NULL;
	bigtime_t timestamp = 0;

	uint8 msgBuf[3];
	uint8* msgPtr = NULL;
	size_t msgSize = 0;
	size_t needed = 0;
	uint8 runningStatus = 0;

	bool haveSysEx = false;
	uint8* sysexBuffer = NULL;
	size_t sysexBufferSize = B_PAGE_SIZE;
	size_t sysexSize = 0;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// A MIDI 2.0 driver queues the original packet. read() still returns
	// MIDI 1.0 bytes for a program that does not use this ioctl. This reader
	// uses the packet, so NoteOn stays 7-bit and Data() keeps the full value.
	bool readUmp = true;

	while (fKeepRunning) {
		if (readUmp) {
			midi_ump_packet packet;
			// ioctl() returns -1 and puts the status in errno. Comparing the
			// return value with B_UNSUPPORTED never matched, so the reader
			// stopped on its first call and no MIDI device gave input.
			status_t umpStatus = B_OK;
			if (ioctl(fFileDescriptor, B_MIDI_READ_UMP, &packet,
					sizeof(packet)) < 0) {
				umpStatus = errno;
			}
			if (umpStatus == B_OK) {
				if (packet.word_count >= 1 && packet.word_count <= 4) {
					SprayUMP(packet.words, packet.word_count,
						system_time());
				}
				continue;
			}
			if (umpStatus == B_TIMED_OUT || umpStatus == B_INTERRUPTED)
				continue;
			if (umpStatus == B_UNSUPPORTED || umpStatus == B_DEV_INVALID_IOCTL)
				readUmp = false;
			else {
				fKeepRunning = false;
				break;
			}
		}
		// SPDX-SnippetEnd

		if (bytesCount == 0) {
			bytesCount = read(fFileDescriptor, bytes, sizeof(bytes));
			if (bytesCount < 0) {
				if (errno == B_CANCELED)
					fKeepRunning = false;
				else
					perror("Error reading data from driver");
				break;
			}

			timestamp = system_time();
			nextByte = bytes;
		}

		uint8 next = *nextByte++;
		bytesCount--;

		if (haveSysEx) {
			// System Exclusive mode
			if (next < 0x80) {
				// System Exclusive data byte
				sysexBuffer[sysexSize++] = next;
				if (sysexSize == sysexBufferSize) {
					sysexBufferSize *= 2;
					sysexBuffer = (uint8*)realloc(sysexBuffer, sysexBufferSize);
				}
				continue;
			} else if ((next & 0xF8) == 0xF8) {
				// System Realtime interleaved in System Exclusive sequence
				SpraySystemRealTime(next, timestamp);
				continue;
			} else {
				// Whatever byte, this one ends the running SysEx sequence
				SpraySystemExclusive(sysexBuffer, sysexSize, timestamp);
				haveSysEx = false;
				if (next == B_SYS_EX_END) {
					// swallow SysEx end byte
					continue;
				}
				// any other byte, while ending the SysEx sequence,
				// should be handled, not dropped
			}
		}

		if ((next & 0xF8) == 0xF8) {
			// System Realtime
			SpraySystemRealTime(next, timestamp);
		} else if ((next & 0xF0) == 0xF0) {
			// System Common
			runningStatus = 0;
			msgBuf[0] = next;
			msgPtr = msgBuf + 1;
			switch (next) {
				case B_SYS_EX_START:
					if (sysexBuffer == NULL) {
						// first SysEx, we need to buffer SysEx data
						sysexBuffer = (uint8*)malloc(sysexBufferSize);
					} else if (sysexBufferSize > 4 * B_PAGE_SIZE) {
						// Previous(es) SysEx was/were a bit large
						// let's restart with a more reasonable buffer size
						free(sysexBuffer);
						sysexBufferSize = B_PAGE_SIZE;
						sysexBuffer = (uint8*)malloc(sysexBufferSize);
					}
					sysexSize = 0;
					haveSysEx = true;
					break;

				case B_SONG_POSITION:
					needed = 2;
					msgSize = 3;
					break;

				case B_MIDI_TIME_CODE:
				case B_SONG_SELECT:
				case B_CABLE_MESSAGE:
					needed = 1;
					msgSize = 2;
					break;

				case B_SYS_EX_END:
					// Unpaired with B_SYS_EX_START, but pass it anyway...
				case B_TUNE_REQUEST:
					SpraySystemCommon(next, 0, 0, timestamp);
					break;
			}
		} else if ((next & 0x80) == 0x80) {
			// Voice message
			runningStatus = next;
			msgBuf[0] = next;
			msgPtr = msgBuf + 1;
			switch (next & 0xF0) {
				case B_NOTE_OFF:
				case B_NOTE_ON:
				case B_KEY_PRESSURE:
				case B_CONTROL_CHANGE:
				case B_PITCH_BEND:
					needed = 2;
					msgSize = 3;
					break;

				case B_PROGRAM_CHANGE:
				case B_CHANNEL_PRESSURE:
					needed = 1;
					msgSize = 2;
					break;
			}
		} else if (needed > 0) {
			// Data bytes to complete message
			*msgPtr++ = next;
			if (--needed == 0) {
				switch (msgBuf[0] & 0xF0) {
					case B_NOTE_OFF:
						SprayNoteOff(msgBuf[0] & 0x0F, msgBuf[1], msgBuf[2], timestamp);
						break;

					case B_NOTE_ON:
						SprayNoteOn(msgBuf[0] & 0x0F, msgBuf[1], msgBuf[2], timestamp);
						break;

					case B_KEY_PRESSURE:
						SprayKeyPressure(msgBuf[0] & 0x0F, msgBuf[1], msgBuf[2], timestamp);
						break;

					case B_CONTROL_CHANGE:
						SprayControlChange(msgBuf[0] & 0x0F, msgBuf[1], msgBuf[2], timestamp);
						break;

					case B_PROGRAM_CHANGE:
						SprayProgramChange(msgBuf[0] & 0x0F, msgBuf[1], timestamp);
						break;

					case B_CHANNEL_PRESSURE:
						SprayChannelPressure(msgBuf[0] & 0x0F, msgBuf[1], timestamp);
						break;

					case B_PITCH_BEND:
						SprayPitchBend(msgBuf[0] & 0x0F, msgBuf[1], msgBuf[2], timestamp);
						break;
				}

				switch (msgBuf[0]) {
					case B_SONG_POSITION:
						SpraySystemCommon(msgBuf[0], msgBuf[1], msgBuf[2], timestamp);
						break;

					case B_MIDI_TIME_CODE:
					case B_SONG_SELECT:
					case B_CABLE_MESSAGE:
						SpraySystemCommon(msgBuf[0], msgBuf[1], 0, timestamp);
						break;
				}
			}
		} else if (runningStatus != 0) {
			// Repeated voice command
			msgBuf[0] = runningStatus;
			msgBuf[1] = next;
			msgPtr = msgBuf + 2;
			needed = msgSize - 2;
		}

	}	// while fKeepRunning

	free(sysexBuffer);

	return fKeepRunning ? errno : B_OK;
}
