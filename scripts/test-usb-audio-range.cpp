/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test for usb_audio_parse_rate_range (UsbAudioFormat.h), the parser of
 * the UAC 2.0 RANGE answer on a clock source's sampling frequency control.
 *
 * Build and run (from the repository root):
 *   g++ -std=c++17 -fsanitize=address,undefined \
 *     -I distro/media/src/add-ons/kernel/drivers/audio/usb \
 *     scripts/test-usb-audio-range.cpp -o /tmp/test-usb-audio-range \
 *   && /tmp/test-usb-audio-range
 */

#include <stdio.h>

#include <vector>

#include "UsbAudioFormat.h"


static int sFailures = 0;

#define CHECK(cond, what) do { \
	if (!(cond)) { \
		printf("FAIL line %d: %s\n", __LINE__, what); \
		sFailures++; \
	} \
} while (0)


static void
put32(std::vector<uint8_t>& out, uint32_t value)
{
	for (int i = 0; i < 4; i++)
		out.push_back((uint8_t)(value >> (8 * i)));
}


static std::vector<uint8_t>
ranges(const std::vector<uint32_t>& triples)
{
	std::vector<uint8_t> out;
	uint16_t count = (uint16_t)(triples.size() / 3);
	out.push_back((uint8_t)count);
	out.push_back((uint8_t)(count >> 8));
	for (uint32_t value : triples)
		put32(out, value);
	return out;
}


static bool
has(const usb_audio_parsed_format& format, uint32_t rate)
{
	for (uint8_t i = 0; i < format.rateCount; i++) {
		if (format.rates[i] == rate)
			return true;
	}
	return false;
}


int
main()
{
	usb_audio_parsed_format format;

	// Discrete rates, as most interfaces report them.
	std::vector<uint8_t> bytes = ranges({44100, 44100, 0, 48000, 48000, 0,
		96000, 96000, 0, 192000, 192000, 0});
	CHECK(usb_audio_parse_rate_range(bytes.data(), bytes.size(), &format) == 4,
		"four discrete rates");
	CHECK(has(format, 192000), "192 kHz kept (the old list stopped at 96 kHz)");

	// A span with a resolution: 48 kHz family only.
	bytes = ranges({48000, 192000, 48000});
	usb_audio_parse_rate_range(bytes.data(), bytes.size(), &format);
	CHECK(has(format, 48000) && has(format, 96000) && has(format, 192000),
		"48k grid");
	CHECK(!has(format, 44100) && !has(format, 88200), "no 44.1k on 48k grid");

	// A continuous span offers the common rates inside it.
	bytes = ranges({8000, 48000, 1});
	usb_audio_parse_rate_range(bytes.data(), bytes.size(), &format);
	CHECK(has(format, 8000) && has(format, 44100) && has(format, 48000),
		"continuous span");
	CHECK(!has(format, 96000), "nothing above the span");

	// Duplicates appear once.
	bytes = ranges({48000, 48000, 0, 48000, 48000, 0});
	CHECK(usb_audio_parse_rate_range(bytes.data(), bytes.size(), &format) == 1,
		"duplicate rate once");

	// A short answer: subranges past the end are ignored, no overread.
	bytes = ranges({44100, 44100, 0, 48000, 48000, 0});
	bytes[0] = 5;	// claims five subranges, holds two
	CHECK(usb_audio_parse_rate_range(bytes.data(), bytes.size(), &format) == 2,
		"short answer");
	CHECK(usb_audio_parse_rate_range(bytes.data(), 1, &format) == 0,
		"one byte is nothing");
	CHECK(usb_audio_parse_rate_range(NULL, 10, &format) == 0, "NULL");

	// Never more than 16 rates.
	std::vector<uint32_t> many;
	for (uint32_t r = 1; r <= 40; r++) {
		many.push_back(r * 1000);
		many.push_back(r * 1000);
		many.push_back(0);
	}
	bytes = ranges(many);
	CHECK(usb_audio_parse_rate_range(bytes.data(), bytes.size(), &format) == 16,
		"capped at 16");

	if (sFailures != 0) {
		printf("usb_audio range: %d failures\n", sFailures);
		return 1;
	}
	printf("ok usb_audio range\n");
	return 0;
}
