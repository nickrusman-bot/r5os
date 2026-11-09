/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host test for the SysEx packet split in usb_midi_write. It runs the same
 * helper as the driver (usb_midi_sysex_packet) through the same loop shape:
 * transfers of buff_lim bytes, packets of at most three bytes.
 *
 * Build and run (from the repository root):
 *   g++ -std=c++17 -fsanitize=address,undefined -I scripts/midi2-host \
 *     -I distro/media/headers/os/midi2 \
 *     -I distro/media/src/add-ons/kernel/drivers/midi/usb_midi \
 *     scripts/test-usb-midi-sysex.cpp -o /tmp/test-usb-midi-sysex \
 *   && /tmp/test-usb-midi-sysex
 */

#include <stdio.h>
#include <string.h>

#include "usb_midi_parse.h"


static int sFailures = 0;


static void
check(bool ok, const char* what, size_t length, size_t outMaxPkt)
{
	if (!ok) {
		printf("FAIL %s (length %zu, outMaxPkt %zu)\n", what, length, outMaxPkt);
		sFailures++;
	}
}


/* One SysEx write of `length` bytes, as usb_midi_write splits it. */
static void
split(size_t length, size_t outMaxPkt)
{
	const size_t packetsPerTransfer = outMaxPkt / 4;
	const size_t buffLim = packetsPerTransfer * 3;
	size_t bytesLeft = length;
	size_t sent = 0;
	size_t packets = 0;
	uint8_t lastCin = 0;
	uint8_t lastCount = 0;
	bool overrun = false;

	while (bytesLeft) {
		size_t xferBytes = bytesLeft < buffLim ? bytesLeft : buffLim;
		size_t packetCount = 0;
		while (xferBytes) {
			uint8_t cin = 0;
			uint8_t count = 0;
			usb_midi_sysex_packet(bytesLeft, &cin, &count);
			if (count > xferBytes)
				count = (uint8_t)xferBytes;
			if (count == 0)
				break;
			if (cin == 0x4 && count != 3)
				check(false, "continue packet must carry three bytes", length,
					outMaxPkt);
			xferBytes -= count;
			bytesLeft -= count;
			sent += count;
			packetCount++;
			packets++;
			lastCin = cin;
			lastCount = count;
		}
		if (packetCount > packetsPerTransfer)
			overrun = true;
		if (packetCount == 0)
			break;
	}

	check(sent == length, "all bytes sent", length, outMaxPkt);
	check(!overrun, "no transfer has more packets than out_buffer holds",
		length, outMaxPkt);
	check(packets == (length + 2) / 3, "packet count", length, outMaxPkt);
	if (length > 0) {
		size_t tail = length % 3 == 0 ? 3 : length % 3;
		check(lastCount == tail, "last packet length", length, outMaxPkt);
		check(lastCin == 0x4 + tail, "last packet is an end packet (CIN 5-7)",
			length, outMaxPkt);
	}
}


int
main()
{
	const size_t maxPackets[] = { 4, 8, 64, 512 };
	for (size_t m = 0; m < sizeof(maxPackets) / sizeof(maxPackets[0]); m++) {
		for (size_t length = 1; length <= 64; length++)
			split(length, maxPackets[m]);
		split(1000, maxPackets[m]);
	}

	/* The helper on its own. */
	uint8_t cin = 0;
	uint8_t count = 0;
	usb_midi_sysex_packet(0, &cin, &count);
	check(count == 0, "nothing left sends nothing", 0, 0);
	usb_midi_sysex_packet(1, &cin, &count);
	check(cin == 0x5 && count == 1, "one byte left is CIN 5", 1, 0);
	usb_midi_sysex_packet(2, &cin, &count);
	check(cin == 0x6 && count == 2, "two bytes left is CIN 6", 2, 0);
	usb_midi_sysex_packet(3, &cin, &count);
	check(cin == 0x7 && count == 3, "three bytes left is CIN 7", 3, 0);
	usb_midi_sysex_packet(4, &cin, &count);
	check(cin == 0x4 && count == 3, "four bytes left is CIN 4", 4, 0);

	if (sFailures != 0) {
		printf("usb_midi sysex: %d failures\n", sFailures);
		return 1;
	}
	printf("ok usb_midi sysex split\n");
	return 0;
}
