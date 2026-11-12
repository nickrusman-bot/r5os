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

#include "../../add-ons/kernel/drivers/midi/usb_midi/usb_midi_velocity.h"


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
	expect(midi2_note_on_midi1_velocity(0) == 1, "velocity 0 is MIDI 1.0 velocity 1");
	expect(midi2_note_on_midi1_velocity(1) == 1, "velocity 1 stays a note-on");
	expect(midi2_note_on_midi1_velocity(511) == 1, "velocity 511 stays a note-on");
	expect(midi2_note_on_midi1_velocity(512) == 1, "velocity 512 is MIDI 1.0 velocity 1");
	expect(midi2_note_on_midi1_velocity(0x8000) == 0x40,
		"velocity 0x8000 is MIDI 1.0 velocity 0x40");
	uint16 samples[] = { 0, 1, 511, 512, 0x8000, 0xffff };
	bool velocityCopiesMatch = true;
	for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		if (midi2_note_on_midi1_velocity(samples[i])
			!= usb_midi_note_on_midi1_velocity(samples[i]))
			velocityCopiesMatch = false;
	}
	expect(velocityCopiesMatch, "note-on velocity copies match");

	uint32 fromMidi1[2];
	midi2_translate_midi1_note(0, 60, 0, true, fromMidi1);
	expect(((fromMidi1[0] >> 20) & 0x0f) == 0x8
		&& midi2_note_velocity(fromMidi1, 2) == 0x8000
		&& (fromMidi1[0] & 0xff) == 0 && (fromMidi1[1] & 0xffff) == 0,
		"MIDI 1.0 note-on velocity 0 is a MIDI 2.0 note-off at 0x8000");
	midi2_translate_midi1_note(0, 60, 1, true, fromMidi1);
	expect(((fromMidi1[0] >> 20) & 0x0f) == 0x9
		&& midi2_note_velocity(fromMidi1, 2) == 0x0200,
		"MIDI 1.0 note-on velocity 1 is MIDI 2.0 velocity 0x0200");
	expect(midi2_scale_up_7_to_16(10) == 0x1400
		&& midi2_scale_up_7_to_16(64) == 0x8000
		&& midi2_scale_up_7_to_16(87) == 0xaeba
		&& midi2_scale_up_7_to_16(127) == 0xffff,
		"7-bit velocities scale to 16 bits");
	uint32 kernelNote[2];
	usb_midi_translate_midi1_note(0, 60, 0, 1, kernelNote);
	midi2_translate_midi1_note(0, 60, 0, true, fromMidi1);
	expect(kernelNote[0] == fromMidi1[0] && kernelNote[1] == fromMidi1[1],
		"velocity 0 translation copies match");
	usb_midi_translate_midi1_note(3, 61, 1, 1, kernelNote);
	midi2_translate_midi1_note(3, 61, 1, true, fromMidi1);
	expect(kernelNote[0] == fromMidi1[0] && kernelNote[1] == fromMidi1[1],
		"velocity 1 translation copies match");
	{
		usb_midi_translate_state state;
		uint32 words[2];
		memset(&state, 0, sizeof(state));
		expect(usb_midi_translate_channel(1, 0x90, 60, 0, &state, words) == 0,
			"MIDI 1.0 protocol keeps a velocity 0 note-on as type 0x2");
		expect(usb_midi_translate_channel(2, 0x90, 60, 0, &state, words) == 2
			&& ((words[0] >> 28) == 0x4) && ((words[0] >> 20) & 0x0f) == 0x8,
			"MIDI 2.0 protocol turns velocity 0 into a type 0x4 note-off");
		expect(usb_midi_translate_channel(2, 0xb0, 7, 127, &state, words) == 2
			&& ((words[0] >> 20) & 0x0f) == 0x0b && words[1] == 0xffffffffu,
			"MIDI 2.0 protocol scales a control change to 32 bits");
		expect(usb_midi_translate_channel(2, 0xb0, 0, 3, &state, words) == -1
			&& usb_midi_translate_channel(2, 0xb0, 32, 4, &state, words) == -1
			&& usb_midi_translate_channel(2, 0xc0, 12, 0, &state, words) == 2
			&& (words[0] & 0xff) == 1 && ((words[1] >> 24) & 0x7f) == 12
			&& ((words[1] >> 8) & 0x7f) == 3 && (words[1] & 0x7f) == 4,
			"bank select is held until the program change");
		memset(&state, 0, sizeof(state));
		expect(usb_midi_translate_channel(2, 0xb0, 101, 1, &state, words) == -1
			&& usb_midi_translate_channel(2, 0xb0, 100, 2, &state, words) == -1
			&& usb_midi_translate_channel(2, 0xb0, 6, 0x40, &state, words) == -1
			&& usb_midi_translate_channel(2, 0xb0, 38, 0, &state, words) == 2
			&& ((words[0] >> 20) & 0x0f) == 0x2
			&& ((words[0] >> 8) & 0x7f) == 1 && (words[0] & 0x7f) == 2,
			"an RPN is one type 0x4 message after the data LSB");
		expect(usb_midi_translate_channel(1, 0xb0, 7, 10, &state, words) == 0,
			"MIDI 1.0 protocol does not mix in a type 0x4 control");
	}

	uint32 voice[2];
	uint8 midiBytes[12];
	voice[0] = (0x4u << 28) | (0x9u << 20) | (60u << 8);
	voice[1] = 0;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 3
		&& midiBytes[0] == 0x90 && midiBytes[1] == 60 && midiBytes[2] == 1,
		"translated note-on velocity 0 is 1");
	voice[0] = (0x4u << 28) | (0xau << 20) | (61u << 8);
	voice[1] = 0x80000000;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 3
		&& midiBytes[0] == 0xa0 && midiBytes[1] == 61 && midiBytes[2] == 0x40,
		"poly pressure keeps the top 7 bits");
	voice[0] = (0x4u << 28) | (0xdu << 20);
	voice[1] = 0xff000000;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 2
		&& midiBytes[0] == 0xd0 && midiBytes[1] == 0x7f,
		"channel pressure keeps the top 7 bits");
	voice[0] = (0x4u << 28) | (0xeu << 20) | (2u << 16);
	voice[1] = 0x80000000;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 3
		&& midiBytes[0] == 0xe2 && midiBytes[1] == 0x00 && midiBytes[2] == 0x40,
		"pitch bend center is LSB then MSB");
	voice[0] = (0x4u << 28) | (0xcu << 20) | 0;
	voice[1] = (12u << 24);
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 2
		&& midiBytes[0] == 0xc0 && midiBytes[1] == 12,
		"program change without a bank is one message");
	voice[0] = (0x4u << 28) | (0xcu << 20) | 1;
	voice[1] = (12u << 24) | (3u << 8) | 4u;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 8
		&& midiBytes[0] == 0xb0 && midiBytes[1] == 0 && midiBytes[2] == 3
		&& midiBytes[3] == 0xb0 && midiBytes[4] == 32 && midiBytes[5] == 4
		&& midiBytes[6] == 0xc0 && midiBytes[7] == 12,
		"program change with a bank is MSB, LSB, then program");
	voice[0] = (0x4u << 28) | (0x2u << 20) | (1u << 8) | 2u;
	voice[1] = 0x80000000;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 12
		&& midiBytes[1] == 101 && midiBytes[2] == 1
		&& midiBytes[4] == 100 && midiBytes[5] == 2
		&& midiBytes[7] == 6 && midiBytes[8] == 0x40
		&& midiBytes[10] == 38 && midiBytes[11] == 0,
		"registered controller is four control changes");
	voice[0] = (0x4u << 28) | (0x3u << 20) | (5u << 8) | 6u;
	voice[1] = 0;
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 12
		&& midiBytes[1] == 99 && midiBytes[2] == 5
		&& midiBytes[4] == 98 && midiBytes[5] == 6,
		"assignable controller is four control changes");
	voice[0] = (0x4u << 28) | (0x4u << 20);
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 0,
		"a relative controller stays UMP");
	voice[0] = (0x4u << 28) | (0x6u << 20) | (60u << 8);
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 0,
		"per-note pitch bend stays UMP");
	voice[0] = (0x4u << 28) | (0xfu << 20) | (60u << 8);
	expect(midi2_channel_voice_to_midi1(voice, 2, midiBytes) == 0,
		"per-note management stays UMP");
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


static void
test_property()
{
	const char* header = "{\"resource\":\"DeviceInfo\"}";
	midi2_pe_message request;
	memset(&request, 0, sizeof(request));
	request.deviceId = B_MIDI2_PE_WHOLE_BLOCK;
	request.subId = B_MIDI2_PE_GET;
	request.version = B_MIDI2_CI_VERSION;
	request.sourceMuid = 0x1234;
	request.destinationMuid = 1;
	request.requestId = 7;
	request.header = header;
	request.headerLength = strlen(header);
	request.chunkCount = 1;
	request.chunkNumber = 1;

	uint8 encoded[128];
	size_t encodedLength = 0;
	expect(midi2_pe_encode(&request, encoded, sizeof(encoded), &encodedLength)
		== B_OK, "Get Property Data encodes");
	midi2_pe_message decoded;
	expect(midi2_pe_decode(encoded, encodedLength, &decoded) == B_OK
		&& decoded.subId == B_MIDI2_PE_GET && decoded.requestId == 7
		&& decoded.sourceMuid == 0x1234 && decoded.chunkNumber == 1
		&& decoded.chunkCount == 1 && decoded.bodyLength == 0
		&& decoded.headerLength == strlen(header)
		&& memcmp(decoded.header, header, decoded.headerLength) == 0,
		"Get Property Data decodes");

	uint8 broken[8];
	memset(broken, 0x7e, sizeof(broken));
	expect(midi2_pe_decode(broken, sizeof(broken), &decoded) == B_BAD_DATA,
		"short Property Exchange is rejected");
	encoded[0] = 0x00;
	expect(midi2_pe_decode(encoded, encodedLength, &decoded) == B_BAD_VALUE,
		"a non MIDI-CI payload is rejected");

	uint32 words[32];
	size_t wordCount = 0;
	encoded[0] = B_MIDI2_CI_UNIVERSAL;
	expect(midi2_encode_sysex(encoded, encodedLength, words, 32, &wordCount)
		== B_OK && wordCount == midi2_sysex_packet_count(encodedLength) * 2,
		"Get Property Data becomes several SysEx7 packets");
	uint8 rebuilt[128];
	size_t rebuiltLength = 0;
	bool packetsOk = true;
	for (size_t i = 0; i < wordCount; i += 2) {
		uint8 status = 0;
		uint8 piece[6];
		size_t pieceLength = 0;
		if (midi2_sysex_payload(words + i, 2, &status, piece, 6, &pieceLength)
			!= B_OK || rebuiltLength + pieceLength > sizeof(rebuilt)) {
			packetsOk = false;
			break;
		}
		memcpy(rebuilt + rebuiltLength, piece, pieceLength);
		rebuiltLength += pieceLength;
	}
	expect(packetsOk && rebuiltLength == encodedLength
		&& memcmp(rebuilt, encoded, encodedLength) == 0
		&& midi2_pe_decode(rebuilt, rebuiltLength, &decoded) == B_OK,
		"SysEx7 packets rebuild the Get inquiry");

	uint8 body[20];
	memset(body, 'A', sizeof(body));
	midi2_pe_message reply = request;
	reply.subId = B_MIDI2_PE_GET_REPLY;
	reply.header = "{\"status\":200}";
	reply.headerLength = strlen(reply.header);
	reply.body = body;
	reply.bodyLength = sizeof(body);
	size_t maxPayload = B_MIDI2_PE_CHUNK_OVERHEAD + reply.headerLength + 8;
	midi2_pe_message chunks[4];
	size_t chunkCount = 0;
	expect(midi2_pe_split(&reply, maxPayload, chunks, 4, &chunkCount) == B_OK
		&& chunkCount == 2 && chunks[0].bodyLength == 8
		&& chunks[1].headerLength == 0 && chunks[1].bodyLength == 12
		&& chunks[0].chunkCount == 2 && chunks[1].chunkNumber == 2,
		"a long reply is two chunks");

	midi2_pe_assembly assembly;
	midi2_pe_assembly_init(&assembly);
	uint8 chunkBytes[96];
	size_t chunkLength = 0;
	expect(midi2_pe_encode(&chunks[0], chunkBytes, sizeof(chunkBytes),
			&chunkLength) == B_OK
		&& midi2_pe_decode(chunkBytes, chunkLength, &decoded) == B_OK
		&& midi2_pe_assembly_add(&assembly, &decoded) == B_OK
		&& !assembly.complete, "the first chunk waits for the rest");
	expect(midi2_pe_encode(&chunks[1], chunkBytes, sizeof(chunkBytes),
			&chunkLength) == B_OK
		&& midi2_pe_decode(chunkBytes, chunkLength, &decoded) == B_OK
		&& midi2_pe_assembly_add(&assembly, &decoded) == B_OK
		&& assembly.complete && assembly.bodyLength == sizeof(body)
		&& memcmp(assembly.body, body, sizeof(body)) == 0
		&& strcmp(assembly.header, "{\"status\":200}") == 0,
		"the second chunk completes the reply");

	uint8 high[1] = { 0x80 };
	reply.body = high;
	reply.bodyLength = 1;
	reply.chunkCount = 1;
	reply.chunkNumber = 1;
	expect(midi2_pe_encode(&reply, encoded, sizeof(encoded), &encodedLength)
		== B_BAD_VALUE, "a byte above 7 bits is rejected");

	midi2_pe_message capabilities;
	memset(&capabilities, 0, sizeof(capabilities));
	capabilities.subId = B_MIDI2_PE_CAPABILITIES;
	capabilities.sourceMuid = 4;
	capabilities.destinationMuid = 1;
	capabilities.simultaneous = 1;
	expect(midi2_pe_encode(&capabilities, encoded, sizeof(encoded),
			&encodedLength) == B_OK
		&& midi2_pe_decode(encoded, encodedLength, &decoded) == B_OK
		&& decoded.subId == B_MIDI2_PE_CAPABILITIES
		&& decoded.simultaneous == 1 && decoded.sourceMuid == 4,
		"Property Exchange capabilities round-trip");

	midi2_pe_catalog catalog;
	midi2_pe_catalog_init(&catalog);
	char replyHeader[160];
	uint8 replyBody[512];
	size_t replyHeaderLength = 0;
	size_t replyBodyLength = 0;
	midi2_pe_message answered;
	expect(midi2_pe_respond(&catalog, &request, replyHeader, sizeof(replyHeader),
			&replyHeaderLength, replyBody, sizeof(replyBody), &replyBodyLength,
			&answered) == B_OK
		&& answered.subId == B_MIDI2_PE_GET_REPLY
		&& answered.requestId == 7
		&& midi2_pe_header_number(replyHeader, replyHeaderLength, "status")
			== B_MIDI2_PE_STATUS_OK
		&& replyBodyLength > 0
		&& memcmp(replyBody, catalog.deviceInfo, replyBodyLength) == 0,
		"DeviceInfo replies with status 200");

	midi2_pe_message missing = request;
	missing.header = "{\"resource\":\"NoSuch\"}";
	missing.headerLength = strlen(missing.header);
	expect(midi2_pe_respond(&catalog, &missing, replyHeader, sizeof(replyHeader),
			&replyHeaderLength, replyBody, sizeof(replyBody), &replyBodyLength,
			&answered) == B_OK
		&& midi2_pe_header_number(replyHeader, replyHeaderLength, "status")
			== B_MIDI2_PE_STATUS_NOT_FOUND,
		"an unknown resource replies 404");

	midi2_pe_message empty = request;
	empty.header = "{}";
	empty.headerLength = 2;
	expect(midi2_pe_respond(&catalog, &empty, replyHeader, sizeof(replyHeader),
			&replyHeaderLength, replyBody, sizeof(replyBody), &replyBodyLength,
			&answered) == B_OK
		&& midi2_pe_header_number(replyHeader, replyHeaderLength, "status")
			== B_MIDI2_PE_STATUS_BAD_REQUEST,
		"a Get without a resource replies 400");

	midi2_pe_message map = request;
	map.header = "{\"resource\":\"ControllerMap\",\"resId\":\"volume\"}";
	map.headerLength = strlen(map.header);
	expect(midi2_pe_respond(&catalog, &map, replyHeader, sizeof(replyHeader),
			&replyHeaderLength, replyBody, sizeof(replyBody), &replyBodyLength,
			&answered) == B_OK
		&& midi2_pe_header_number(replyHeader, replyHeaderLength, "status")
			== 200
		&& replyBodyLength == strlen(catalog.controllerMap),
		"ControllerMap returns the CtrlMapList body");

	midi2_pe_message subscribe = request;
	subscribe.subId = B_MIDI2_PE_SUBSCRIBE;
	subscribe.header = "{\"command\":\"start\",\"resource\":\"ChannelList\"}";
	subscribe.headerLength = strlen(subscribe.header);
	expect(midi2_pe_respond(&catalog, &subscribe, replyHeader,
			sizeof(replyHeader), &replyHeaderLength, replyBody,
			sizeof(replyBody), &replyBodyLength, &answered) == B_OK
		&& answered.subId == B_MIDI2_PE_SUBSCRIBE_REPLY && catalog.subscribed,
		"subscribe start is accepted");
	char subscribeId[16];
	expect(midi2_pe_header_string(answered.header, answered.headerLength,
			"subscribeId", subscribeId, sizeof(subscribeId))
		&& strcmp(subscribeId, "sub1") == 0,
		"subscribe start returns subscribeId");

	char updateHeader[80];
	size_t updateLength = 0;
	const uint8* updateBody = NULL;
	size_t updateBodyLength = 0;
	expect(midi2_pe_subscription_full(&catalog, updateHeader,
			sizeof(updateHeader), &updateLength, &updateBody,
			&updateBodyLength) == B_OK
		&& midi2_pe_header_string(updateHeader, updateLength, "command",
			subscribeId, sizeof(subscribeId))
		&& strcmp(subscribeId, "full") == 0
		&& updateBodyLength == strlen(catalog.channelList),
		"a subscription sends the full resource");

	subscribe.header = "{\"command\":\"end\",\"subscribeId\":\"sub1\"}";
	subscribe.headerLength = strlen(subscribe.header);
	expect(midi2_pe_respond(&catalog, &subscribe, replyHeader,
			sizeof(replyHeader), &replyHeaderLength, replyBody,
			sizeof(replyBody), &replyBodyLength, &answered) == B_OK
		&& !catalog.subscribed
		&& midi2_pe_subscription_full(&catalog, updateHeader,
			sizeof(updateHeader), &updateLength, &updateBody,
			&updateBodyLength) == B_BAD_VALUE,
		"subscribe end stops notifications");

	midi2_pe_message notify;
	memset(&notify, 0, sizeof(notify));
	notify.subId = B_MIDI2_PE_NOTIFY;
	notify.sourceMuid = 1;
	notify.destinationMuid = 0x1234;
	notify.requestId = 7;
	notify.header = "{\"status\":144}";
	notify.headerLength = strlen(notify.header);
	notify.chunkCount = 1;
	notify.chunkNumber = 1;
	expect(midi2_pe_encode(&notify, encoded, sizeof(encoded), &encodedLength)
		== B_OK && midi2_pe_decode(encoded, encodedLength, &decoded) == B_OK
		&& decoded.subId == B_MIDI2_PE_NOTIFY
		&& midi2_pe_header_number(decoded.header, decoded.headerLength,
			"status") == B_MIDI2_PE_STATUS_TERMINATE,
		"Notify carries terminate status 144");
	expect(midi2_pe_respond(&catalog, &decoded, replyHeader, sizeof(replyHeader),
			&replyHeaderLength, replyBody, sizeof(replyBody), &replyBodyLength,
			&answered) == B_OK && answered.subId == 0,
		"Notify has no reply");
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
	uint8		fSysexBytes[160];
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
	producer->SprayNoteOn(0, 60, 100);
	expect(probe->Wait() == B_OK, "bare integer note arrives");
	uchar bare[3] = { (uchar)(B_NOTE_ON + 0), 60, 100 };
	expect(probe->HasMidi(bare, 3), "bare integer note is MIDI 1.0");

	probe->Reset();
	uint16 wide = 0x8000;
	producer->SprayNoteOn2(2, 64, wide);
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
	producer->SprayControlChange(0, 7, 100);
	expect(probe->Wait() == B_OK, "bare integer control arrives");
	uchar bareControl[3] = { (uchar)(B_CONTROL_CHANGE + 0), 7, 100 };
	expect(probe->HasMidi(bareControl, 3), "bare integer control is MIDI 1.0");

	probe->Reset();
	uint32 wideControl = 0x80000000;
	producer->SprayControlChange2(4, B_MAIN_VOLUME, wideControl);
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

	const char* peHeader = "{\"resource\":\"DeviceInfo\"}";
	midi2_pe_message peRequest;
	memset(&peRequest, 0, sizeof(peRequest));
	peRequest.subId = B_MIDI2_PE_GET;
	peRequest.sourceMuid = 2;
	peRequest.destinationMuid = 1;
	peRequest.requestId = 3;
	peRequest.header = peHeader;
	peRequest.headerLength = strlen(peHeader);
	peRequest.chunkCount = 1;
	peRequest.chunkNumber = 1;
	uint8 pePayload[128];
	size_t peLength = 0;
	uint32 peWords[40];
	size_t peWordCount = 0;
	expect(midi2_pe_encode(&peRequest, pePayload, sizeof(pePayload), &peLength)
		== B_OK
		&& midi2_encode_sysex(pePayload, peLength, peWords, 40, &peWordCount)
			== B_OK,
		"Property Exchange UMP encodes");
	bool peSprayed = true;
	for (size_t i = 0; i + 2 < peWordCount; i += 2) {
		probe->Reset();
		if (producer->SprayUMP(peWords + i, 2) != B_OK || probe->Wait() != B_OK
			|| probe->SawAnySysex()) {
			peSprayed = false;
			break;
		}
	}
	probe->Reset();
	expect(peSprayed && peWordCount >= 4
		&& producer->SprayUMP(peWords + peWordCount - 2, 2) == B_OK
		&& probe->Wait() == B_OK && probe->SawSysex(pePayload, peLength),
		"Property Exchange arrives as one SystemExclusive");

	probe->Reset();
	uint32 perNote[2];
	midi2_encode_per_note(1, 60, 7, 0x80000000, false, perNote);
	expect(producer->SprayUMP(perNote, 2) == B_OK,
		"per-note controller is accepted");
	expect(probe->Wait() == B_OK && probe->HasUmp(perNote, 2)
		&& !probe->SawNote() && !probe->SawControl(),
		"per-note controller arrives without a MIDI 1.0 hook");

	probe->Reset();
	uint32 discovery[4];
	expect(midi2_stream_endpoint_discovery(B_MIDI2_STREAM_FILTER_INFO, discovery)
			== B_OK
		&& producer->SprayUMP(discovery, 4) == B_OK && probe->Wait() == B_OK
		&& probe->HasUmp(discovery, 4),
		"endpoint discovery arrives as one UMP");

	midi2_profile_message enabledReport;
	memset(&enabledReport, 0, sizeof(enabledReport));
	enabledReport.subId = B_MIDI2_PROFILE_ENABLED;
	enabledReport.sourceMuid = 1;
	enabledReport.hasProfile = true;
	enabledReport.profile.bytes[0] = 0x7e;
	enabledReport.profile.bytes[4] = 1;
	uint8 profilePayload[32];
	size_t profileLength = 0;
	uint32 profileWords[16];
	size_t profileWordCount = 0;
	expect(midi2_profile_encode(&enabledReport, profilePayload,
			sizeof(profilePayload), &profileLength) == B_OK
		&& midi2_encode_sysex(profilePayload, profileLength, profileWords, 16,
			&profileWordCount) == B_OK,
		"profile enabled report encodes");
	bool profileSprayed = true;
	for (size_t i = 0; i + 2 < profileWordCount; i += 2) {
		probe->Reset();
		if (producer->SprayUMP(profileWords + i, 2) != B_OK
			|| probe->Wait() != B_OK || probe->SawAnySysex()) {
			profileSprayed = false;
			break;
		}
	}
	probe->Reset();
	expect(profileSprayed
		&& producer->SprayUMP(profileWords + profileWordCount - 2, 2) == B_OK
		&& probe->Wait() == B_OK
		&& probe->SawSysex(profilePayload, profileLength),
		"profile enabled report arrives as one SystemExclusive");

	producer->Disconnect(probe);
	producer->Release();
	probe->Release();
}


static void
test_more()
{
	uint32 perNote[2];
	midi2_encode_per_note(4, 0x6f, 0xb1, 0x46105ee5, false, perNote);
	expect(perNote[0] == 0x40146fb1 && perNote[1] == 0x46105ee5,
		"assignable per-note controller word");
	expect(midi2_is_per_note(perNote, 2) && !midi2_per_note_registered(perNote, 2)
		&& midi2_per_note_note(perNote, 2) == 0x6f
		&& midi2_per_note_index(perNote, 2) == 0xb1
		&& midi2_per_note_value(perNote, 2) == 0x46105ee5,
		"assignable per-note controller reads back");
	midi2_encode_per_note(4, 0x6f, 3, 0x1000, true, perNote);
	expect(midi2_per_note_registered(perNote, 2)
		&& ((perNote[0] >> 20) & 0xf) == 0 && midi2_per_note_index(perNote, 2) == 3,
		"registered per-note controller uses status 0");
	expect(!midi2_is_note(perNote, 2) && !midi2_is_control(perNote, 2),
		"a per-note controller is not a note or a channel controller");

	midi2_profile_catalog profiles;
	midi2_profile_catalog_init(&profiles);
	midi2_profile_message inquiry;
	memset(&inquiry, 0, sizeof(inquiry));
	inquiry.subId = B_MIDI2_PROFILE_INQUIRY;
	inquiry.sourceMuid = 2;
	inquiry.destinationMuid = 1;
	uint8 profileBytes[64];
	size_t profileLength = 0;
	midi2_profile_message decodedProfile;
	expect(midi2_profile_encode(&inquiry, profileBytes, sizeof(profileBytes),
			&profileLength) == B_OK
		&& midi2_profile_decode(profileBytes, profileLength, &decodedProfile)
			== B_OK
		&& decodedProfile.subId == B_MIDI2_PROFILE_INQUIRY,
		"profile inquiry round-trips");

	midi2_profile_message profileReply;
	expect(midi2_profile_respond(&profiles, &decodedProfile, &profileReply)
			== B_OK
		&& profileReply.subId == B_MIDI2_PROFILE_INQUIRY_REPLY
		&& profileReply.disabledCount == 1 && profileReply.enabledCount == 0,
		"a new profile starts disabled");

	midi2_profile_message setOn;
	memset(&setOn, 0, sizeof(setOn));
	setOn.subId = B_MIDI2_PROFILE_SET_ON;
	setOn.sourceMuid = 2;
	setOn.hasProfile = true;
	setOn.profile = profiles.ids[0];
	expect(midi2_profile_encode(&setOn, profileBytes, sizeof(profileBytes),
			&profileLength) == B_OK
		&& midi2_profile_decode(profileBytes, profileLength, &decodedProfile)
			== B_OK
		&& midi2_profile_respond(&profiles, &decodedProfile, &profileReply)
			== B_OK
		&& profileReply.subId == B_MIDI2_PROFILE_ENABLED
		&& profiles.enabled[0],
		"Set Profile On reports the profile enabled");

	uint8 specific[3] = { 1, 2, 3 };
	midi2_profile_message dataMessage;
	memset(&dataMessage, 0, sizeof(dataMessage));
	dataMessage.subId = B_MIDI2_PROFILE_SPECIFIC;
	dataMessage.sourceMuid = 2;
	dataMessage.hasProfile = true;
	dataMessage.profile = profiles.ids[0];
	dataMessage.data = specific;
	dataMessage.dataLength = sizeof(specific);
	expect(midi2_profile_encode(&dataMessage, profileBytes, sizeof(profileBytes),
			&profileLength) == B_OK
		&& midi2_profile_decode(profileBytes, profileLength, &decodedProfile)
			== B_OK
		&& decodedProfile.dataLength == 3 && decodedProfile.data[2] == 3
		&& midi2_profile_respond(&profiles, &decodedProfile, &profileReply)
			== B_OK
		&& profiles.specificLength == 3 && profiles.specific[0] == 1,
		"profile specific data is stored");

	setOn.profile.bytes[4] = 0x7f;
	expect(midi2_profile_respond(&profiles, &setOn, &profileReply)
		== B_NAME_NOT_FOUND, "an unknown profile is rejected");

	uint32 discovery[4];
	expect(midi2_stream_endpoint_discovery(
			B_MIDI2_STREAM_FILTER_INFO | B_MIDI2_STREAM_FILTER_CONFIG, discovery)
			== B_OK
		&& discovery[0] == 0xf0000101 && discovery[1] == 0x00000011,
		"endpoint discovery packet");

	{
		// A program's own identity, here a made-up three-byte SysEx ID.
		midi2_identity identity = {};
		identity.manufacturer[0] = 0x00;
		identity.manufacturer[1] = 0x21;
		identity.manufacturer[2] = 0x09;
		identity.model[0] = 3;
		identity.manufacturerName = "Example";
		identity.modelName = "Synth";
		identity.versionName = "1.2";
		midi2_endpoint own;
		midi2_endpoint_init(&own);
		uint32 words[4];
		expect(own.manufacturer[0] == B_MIDI2_NONCOMMERCIAL_ID
			&& midi2_endpoint_set_identity(&own, &identity, "Example Synth",
				NULL) == B_OK
			&& strcmp(own.name, "Example Synth") == 0
			&& own.manufacturer[1] == 0x21,
			"endpoint takes the program's identity");
		midi2_stream_identity(&own, words);
		expect(words[1] == 0x00002109 && ((words[2] >> 8) & 0x7f) == 3,
			"device identity carries the program's SysEx ID and model");

		midi2_pe_catalog own_catalog;
		midi2_pe_catalog_init(&own_catalog);
		expect(midi2_pe_catalog_set_identity(&own_catalog, &identity) == B_OK
			&& strstr(own_catalog.deviceInfo,
				"\"manufacturerId\":[0,33,9],\"manufacturer\":\"Example\"")
				!= NULL,
			"DeviceInfo is written from the program's identity");
		identity.modelName = "Bad\"name";
		expect(midi2_pe_catalog_set_identity(&own_catalog, &identity)
				== B_BAD_VALUE,
			"a name that needs escaping is refused");
		identity.modelName = "Synth";
		identity.manufacturer[1] = 0x80;
		expect(midi2_endpoint_set_identity(&own, &identity, NULL, NULL)
				== B_BAD_VALUE,
			"an 8-bit identity byte is refused");
	}

	midi2_endpoint endpoint;
	midi2_endpoint_init(&endpoint);
	uint32 replies[8 * 4];
	size_t replyCount = 0;
	expect(midi2_stream_reply(&endpoint, discovery, replies, 8, &replyCount)
			== B_OK && replyCount == 2
		&& midi2_stream_status(replies[0]) == B_MIDI2_STREAM_ENDPOINT_INFO
		&& midi2_stream_status(replies[4]) == B_MIDI2_STREAM_CONFIG_NOTIFY
		&& ((replies[4] >> 8) & 0xff) == B_MIDI2_STREAM_PROTOCOL_MIDI1,
		"endpoint discovery replies with info and MIDI 1.0 protocol");
	uint32 configRequest[4];
	memset(configRequest, 0, sizeof(configRequest));
	configRequest[0] = midi2_stream_pack(0xf0, 0x05,
		B_MIDI2_STREAM_PROTOCOL_MIDI2, 0);
	expect(midi2_stream_configure(&endpoint, configRequest) == B_OK
		&& endpoint.protocol == B_MIDI2_STREAM_PROTOCOL_MIDI2,
		"stream configuration selects MIDI 2.0 protocol");
	configRequest[0] = midi2_stream_pack(0xf0, 0x05, 0x7f, 0);
	expect(midi2_stream_configure(&endpoint, configRequest) == B_BAD_VALUE
		&& endpoint.protocol == B_MIDI2_STREAM_PROTOCOL_MIDI2,
		"an unknown protocol is rejected");

	uint32 blockRequest[4];
	expect(midi2_stream_block_discovery(0,
			B_MIDI2_STREAM_BLOCK_FILTER_INFO | B_MIDI2_STREAM_BLOCK_FILTER_NAME,
			blockRequest) == B_OK
		&& blockRequest[0] == 0xf0100003, "function block discovery packet");
	expect(midi2_stream_reply(&endpoint, blockRequest, replies, 8, &replyCount)
			== B_OK && replyCount == 2
		&& midi2_stream_status(replies[0]) == B_MIDI2_STREAM_BLOCK_INFO
		&& midi2_stream_status(replies[4]) == B_MIDI2_STREAM_BLOCK_NAME,
		"function block discovery replies with info and a name");
	uint32 nameWord0 = replies[4];
	uint32 nameWord1 = replies[5];
	char blockName[6];
	blockName[0] = nameWord0 & 0xff;
	blockName[1] = (nameWord1 >> 24) & 0xff;
	blockName[2] = (nameWord1 >> 16) & 0xff;
	blockName[3] = (nameWord1 >> 8) & 0xff;
	blockName[4] = nameWord1 & 0xff;
	blockName[5] = '\0';
	expect(strcmp(blockName, "midi2") == 0, "function block name is midi2");
	expect(midi2_stream_block_discovery(9, B_MIDI2_STREAM_BLOCK_FILTER_INFO,
			blockRequest) == B_OK
		&& midi2_stream_reply(&endpoint, blockRequest, replies, 8, &replyCount)
			== B_NAME_NOT_FOUND,
		"an unknown function block is rejected");

	midi2_usb_header usbHeader;
	memset(&usbHeader, 0, sizeof(usbHeader));
	usbHeader.version = B_MIDI2_USB_VERSION;
	usbHeader.totalLength = 32;
	uint8 usbBytes[16];
	size_t usbLength = 0;
	midi2_usb_header parsedHeader;
	expect(midi2_usb_write_header(&usbHeader, usbBytes, sizeof(usbBytes),
			&usbLength) == B_OK
		&& midi2_usb_parse_header(usbBytes, usbLength, &parsedHeader) == B_OK
		&& parsedHeader.version == 0x0200 && parsedHeader.totalLength == 32,
		"USB MIDI 2.0 interface header round-trips");

	midi2_usb_group_terminal block;
	memset(&block, 0, sizeof(block));
	block.blockId = 1;
	block.direction = B_MIDI2_USB_BLOCK_BIDIRECTIONAL;
	block.firstGroup = 0;
	block.groupCount = 1;
	block.protocol = B_MIDI2_USB_PROTOCOL_MIDI2;
	block.maxInputBandwidth = 0x0100;
	midi2_usb_group_terminal parsedBlock;
	expect(midi2_usb_write_group_terminal(&block, usbBytes, sizeof(usbBytes),
			&usbLength) == B_OK
		&& usbBytes[1] == B_MIDI2_USB_GROUP_TERMINAL_TYPE
		&& midi2_usb_parse_group_terminal(usbBytes, usbLength, &parsedBlock)
			== B_OK
		&& parsedBlock.protocol == B_MIDI2_USB_PROTOCOL_MIDI2
		&& parsedBlock.blockId == 1 && parsedBlock.maxInputBandwidth == 0x0100,
		"USB Group Terminal Block round-trips");
	block.groupCount = 0;
	expect(midi2_usb_write_group_terminal(&block, usbBytes, sizeof(usbBytes),
			&usbLength) == B_BAD_VALUE,
		"a Group Terminal Block needs at least one group");

	midi2_usb_interface usbInterface;
	memset(&usbInterface, 0, sizeof(usbInterface));
	usbInterface.interfaceNumber = 2;
	usbInterface.alternateSetting = 1;
	usbInterface.numEndpoints = 2;
	midi2_usb_interface parsedInterface;
	expect(midi2_usb_write_interface(&usbInterface, usbBytes, sizeof(usbBytes),
			&usbLength) == B_OK
		&& usbBytes[3] == 1
		&& midi2_usb_parse_interface(usbBytes, usbLength, &parsedInterface)
			== B_OK
		&& parsedInterface.alternateSetting == 1
		&& parsedInterface.deviceClass == B_MIDI2_USB_AUDIO_CLASS
		&& parsedInterface.subclass == B_MIDI2_USB_MIDISTREAMING,
		"USB MIDI alternate setting round-trips");
}


int
main()
{
	test_helpers();
	test_property();
	test_more();
	test_spray();
	if (sFailures == 0) {
		printf("midi2test: all checks passed\n");
		return 0;
	}

	printf("midi2test: %d check(s) failed\n", sFailures);
	return 1;
}
