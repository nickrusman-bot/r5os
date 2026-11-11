/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host test for the usb_audio packet plan and the recording join
 * (distro/media/src/add-ons/kernel/drivers/audio/usb/usb_audio_packets.h).
 *
 * Build and run (from the repository root):
 *   g++ -std=c++17 -fsanitize=address,undefined \
 *     -I distro/media/src/add-ons/kernel/drivers/audio/usb \
 *     scripts/test-usb-audio-packets.cpp -o /tmp/test-usb-audio-packets \
 *   && /tmp/test-usb-audio-packets
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "usb_audio_packets.h"


static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("FAIL line %d: ", __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		sFailures++; \
	} \
} while (0)


static void
check_plan(uint32_t rate, uint32_t frameBytes, uint16_t maxPacket,
	uint8_t interval, bool highSpeed, uint32_t want, bool input)
{
	usb_audio_packet_plan plan;
	bool ok = usb_audio_plan_packets(rate, frameBytes, maxPacket, interval,
		highSpeed, want, input, &plan);
	CHECK(ok, "plan %u Hz fb %u max %u int %u hs %d", rate, frameBytes,
		maxPacket, interval, highSpeed);
	if (!ok)
		return;

	uint64_t sum = 0;
	for (uint32_t i = 0; i < plan.packetsPerBuffer; i++) {
		uint32_t frames = usb_audio_packet_frames(&plan, i);
		CHECK(frames <= plan.nominalMaxFrames, "packet %u has %u frames", i,
			frames);
		CHECK(frames * frameBytes <= plan.maxPacketBytes,
			"packet %u bigger than wMaxPacketSize", i);
		sum += frames;
	}
	CHECK(sum == plan.framesPerBuffer, "%u Hz: packets add up to %llu, buffer"
		" %u", rate, (unsigned long long)sum, plan.framesPerBuffer);
	CHECK(plan.framesPerBuffer >= want, "%u Hz: buffer %u below wanted %u",
		rate, plan.framesPerBuffer, want);
	// A buffer is a whole number of packets with an exact frame count, so the
	// rate over one buffer is exact.
	CHECK((uint64_t)plan.framesPerBuffer * plan.packetsPerSecond
		== (uint64_t)rate * plan.packetsPerBuffer, "%u Hz: rate not exact",
		rate);
	if (input) {
		CHECK(plan.requestFrames >= plan.nominalMaxFrames,
			"record request below nominal");
		CHECK(plan.regionBytes == plan.packetsPerBuffer * plan.requestFrames
			* frameBytes, "record region size");
	} else {
		CHECK(plan.regionBytes == plan.framesPerBuffer * frameBytes,
			"play region size");
	}
}


// A device that sends nominal frames, sometimes one more or one fewer.
static void
check_record(uint32_t rate, bool highSpeed, uint16_t maxPacket, int drift)
{
	const uint32_t fb = 4;	// one 32-bit sample, the frame number
	usb_audio_packet_plan plan;
	if (!usb_audio_plan_packets(rate, fb, maxPacket, 1, highSpeed, 256, true,
			&plan)) {
		CHECK(false, "record plan %u Hz", rate);
		return;
	}

	std::vector<uint8_t> region(plan.regionBytes);
	std::vector<uint8_t> carry((size_t)(plan.framesPerBuffer + 1) * fb);
	std::vector<uint32_t> actual(plan.packetsPerBuffer);
	uint32_t carryFrames = 0;
	uint32_t padded = 0;
	uint32_t dropped = 0;
	uint32_t sent = 0;		// next frame number the device sends
	uint32_t expect = 0;	// next frame number the reader should see
	uint64_t accumulator = 0;

	for (int buffer = 0; buffer < 400; buffer++) {
		memset(region.data(), 0xee, region.size());
		for (uint32_t i = 0; i < plan.packetsPerBuffer; i++) {
			accumulator += rate;
			uint32_t frames = (uint32_t)(accumulator / plan.packetsPerSecond);
			accumulator %= plan.packetsPerSecond;
			if (drift > 0 && (buffer * plan.packetsPerBuffer + i) % 97 == 0)
				frames++;
			if (drift < 0 && (buffer * plan.packetsPerBuffer + i) % 97 == 0
				&& frames > 0)
				frames--;
			if (frames > plan.requestFrames)
				frames = plan.requestFrames;
			for (uint32_t f = 0; f < frames; f++) {
				uint32_t value = sent++;
				memcpy(region.data() + ((size_t)i * plan.requestFrames + f) * fb,
					&value, fb);
			}
			actual[i] = frames * fb;
		}

		uint32_t dropBefore = dropped;
		bool ready = usb_audio_compact_record(&plan, region.data(),
			actual.data(), carry.data(), &carryFrames, &padded, &dropped);
		CHECK(padded == 0, "%u Hz: a short capture must not be repeated", rate);
		if (!ready)
			continue;

		// The reader takes framesPerBuffer frames. Frames run on in order,
		// except where the join dropped some that would not fit in the carry.
		for (uint32_t f = 0; f < plan.framesPerBuffer; f++) {
			uint32_t value;
			memcpy(&value, region.data() + (size_t)f * fb, fb);
			if (value == expect) {
				expect++;
				continue;
			}
			if (dropped > dropBefore && value > expect) {
				expect = value + 1;
				continue;
			}
			CHECK(false, "%u Hz drift %d: buffer %d frame %u is %u, want %u",
				rate, drift, buffer, f, value, expect);
			return;
		}
	}
	if (drift == 0) {
		CHECK(padded == 0 && dropped == 0,
			"%u Hz steady clock padded %u dropped %u", rate, padded, dropped);
	}
}


int
main()
{
	// Full speed, UAC 1.0.
	check_plan(44100, 4, 192, 1, false, 2048, false);
	check_plan(44100, 4, 192, 1, false, 128, false);
	check_plan(48000, 4, 192, 1, false, 256, false);
	check_plan(44100, 6, 288, 1, false, 2048, true);
	check_plan(22050, 2, 64, 1, false, 300, false);
	check_plan(32000, 4, 132, 1, false, 100, false);

	// High speed: eight packets per millisecond at bInterval 1.
	check_plan(48000, 8, 56, 1, true, 256, false);
	check_plan(44100, 8, 56, 1, true, 2048, false);
	check_plan(96000, 8, 104, 1, true, 128, false);
	check_plan(192000, 8, 200, 1, true, 512, true);
	check_plan(48000, 8, 104, 2, true, 256, false);	// every second microframe

	{
		usb_audio_packet_plan plan;
		usb_audio_plan_packets(48000, 8, 56, 1, true, 256, false, &plan);
		CHECK(plan.packetsPerSecond == 8000, "48 kHz HS is 8000 packets/s");
		for (uint32_t i = 0; i < plan.packetsPerBuffer; i++) {
			CHECK(usb_audio_packet_frames(&plan, i) == 6,
				"48 kHz HS packet %u is not 6 frames", i);
		}
		usb_audio_plan_packets(44100, 4, 192, 1, false, 441, false, &plan);
		uint32_t big = 0;
		for (uint32_t i = 0; i < 10; i++)
			big += usb_audio_packet_frames(&plan, i) == 45;
		CHECK(big == 1, "44.1 kHz FS: one packet of 45 frames in ten, got %u",
			big);
	}

	// An alternate that cannot carry the rate is refused, not clamped.
	{
		usb_audio_packet_plan plan;
		CHECK(!usb_audio_plan_packets(96000, 8, 56, 1, true, 256, false, &plan),
			"96 kHz in 56-byte packets must be refused");
		// Bits 11-12 (extra transactions) do not add room: Haiku's XHCI
		// driver refuses packets above bits 0-10.
		CHECK(!usb_audio_plan_packets(96000, 8, 0x1800 | 56, 1, true, 256,
			false, &plan), "mult bits must not count as room");
		CHECK(!usb_audio_plan_packets(48000, 0, 56, 1, true, 256, false, &plan),
			"zero frame size refused");
	}

	// Speed guess.
	CHECK(usb_audio_high_speed(0x0200, 48000, 8, 1024), "UAC2 is high speed");
	CHECK(!usb_audio_high_speed(0x0100, 48000, 4, 196), "UAC1 192 B is FS");
	CHECK(usb_audio_high_speed(0x0100, 48000, 4, 28), "UAC1 28 B is HS");

	// Recording join.
	check_record(44100, false, 192, 0);
	check_record(44100, false, 192, 1);
	check_record(44100, false, 192, -1);
	check_record(48000, true, 56, 0);
	check_record(48000, true, 56, 1);
	check_record(44100, true, 56, -1);

	if (sFailures != 0) {
		printf("usb_audio packets: %d failures\n", sFailures);
		return 1;
	}
	printf("ok usb_audio packets\n");
	return 0;
}
