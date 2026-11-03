/*
 * Copyright 2026, R5OS.
 * SPDX-License-Identifier: MIT
 *
 * Checks USB Audio descriptor parsing and PCM packing. No device.
 */


#include <stdio.h>
#include <string.h>

#include "../../add-ons/kernel/drivers/audio/usb/UsbAudioFormat.h"


static int sFailures = 0;


static void
expect(int ok, const char* name)
{
	if (ok) {
		printf("ok   %s\n", name);
		return;
	}
	printf("FAIL %s\n", name);
	sFailures++;
}


int
main()
{
	const uint8_t uac1Header[] = { 0x09, 0x24, 0x01, 0x00, 0x01, 0x00, 0x00 };
	const uint8_t uac2Header[] = { 0x09, 0x24, 0x01, 0x00, 0x02, 0x01, 0x00, 0x00 };
	uint16_t version = 0;
	expect(usb_audio_parse_ac_version(uac1Header, sizeof(uac1Header), &version)
			&& version == 0x0100,
		"UAC1 header version");
	expect(usb_audio_parse_ac_version(uac2Header, sizeof(uac2Header), &version)
			&& version == 0x0200,
		"UAC2 header version");

	const uint8_t uac1Format[] = {
		14, 0x24, 0x02, 0x01, 2, 3, 24, 2,
		0x44, 0xac, 0x00, 0x80, 0xbb, 0x00
	};
	usb_audio_parsed_format parsed;
	expect(usb_audio_parse_format_i(uac1Format, sizeof(uac1Format), 0x0100, 0,
			&parsed)
		&& parsed.channels == 2 && parsed.subframeSize == 3
		&& parsed.bitResolution == 24 && parsed.rateCount == 2
		&& parsed.rates[0] == 44100 && parsed.rates[1] == 48000,
		"UAC1 packed 24-bit lists 44.1 and 48");

	const uint8_t uac1Wide[] = {
		14, 0x24, 0x02, 0x01, 2, 4, 24, 2,
		0x44, 0xac, 0x00, 0x80, 0xbb, 0x00
	};
	expect(usb_audio_parse_format_i(uac1Wide, sizeof(uac1Wide), 0x0100, 0,
			&parsed)
		&& parsed.subframeSize == 4 && parsed.bitResolution == 24,
		"UAC1 24-bit in a 4-byte container");

	const uint8_t uac2General[] = {
		16, 0x24, 0x01, 1, 0, 0x01, 0x01, 0, 0, 0, 6, 0, 0, 0, 0, 0
	};
	uint8_t terminal = 0;
	uint16_t tag = 0;
	uint8_t channels = 0;
	expect(usb_audio_parse_as_general(uac2General, sizeof(uac2General), 0x0200,
			&terminal, &tag, &channels)
		&& terminal == 1 && tag == 0x0001 && channels == 6,
		"UAC2 general descriptor is 6-channel PCM");

	const uint8_t uac2Format[] = { 6, 0x24, 0x02, 0x01, 4, 24 };
	expect(usb_audio_parse_format_i(uac2Format, sizeof(uac2Format), 0x0200,
			channels, &parsed)
		&& parsed.channels == 6 && parsed.subframeSize == 4
		&& parsed.bitResolution == 24
		&& parsed.rates[0] == 44100 && parsed.rates[1] == 48000
		&& parsed.rates[2] == 88200 && parsed.rates[3] == 96000,
		"UAC2 format keeps 44.1, 48, 88.2, and 96");

	const uint8_t available[] = { 2, 6, 8 };
	expect(usb_audio_choose_channels(0, available, 3) == 8,
		"automatic layout uses the widest channel count");
	expect(usb_audio_choose_channels(6, available, 3) == 6,
		"5.1 selects six channels");
	expect(usb_audio_choose_channels(8, available, 1) == 2,
		"7.1 falls back to stereo when that is all the device has");

	uint8_t packed[4];
	int32_t sample = (int32_t)0x12345600;
	usb_audio_write_sample(packed, 3, 24, sample);
	expect(packed[0] == 0x56 && packed[1] == 0x34 && packed[2] == 0x12
		&& usb_audio_read_sample(packed, 3, 24) == sample,
		"packed 24-bit round-trips");
	usb_audio_write_sample(packed, 4, 24, sample);
	expect(packed[0] == 0x00 && packed[1] == 0x56 && packed[2] == 0x34
		&& packed[3] == 0x12
		&& usb_audio_read_sample(packed, 4, 24) == sample,
		"24-bit in 4 bytes is left-justified");

	if (sFailures == 0) {
		printf("usb_audiotest: all checks passed\n");
		return 0;
	}
	printf("usb_audiotest: %d check(s) failed\n", sFailures);
	return 1;
}
