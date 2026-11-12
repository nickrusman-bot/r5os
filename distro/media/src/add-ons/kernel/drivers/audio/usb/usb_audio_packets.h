/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Isochronous packet plan for a usb_audio stream. The driver and the host
 * test (scripts/test-usb-audio-packets.cpp) include this file, so both run
 * the same arithmetic.
 *
 * One ISO packet descriptor is one service interval of the endpoint:
 * 2^(bInterval-1) frames of 1 ms at full speed, or 2^(bInterval-1)
 * microframes of 125 us at high speed (USB 2.0 § 9.6.6, Table 9-13).
 * A packet carries floor((i+1)*rate/pps) - floor(i*rate/pps) frames, so
 * 44100 Hz at 1000 packets per second is nine packets of 44 frames and one
 * of 45 in every ten, as Linux snd-usb-audio does with its fractional
 * accumulator. The packets of one buffer always add up to a whole number
 * of frames, so the pattern starts again at every buffer.
 */
#ifndef USB_AUDIO_PACKETS_H
#define USB_AUDIO_PACKETS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>


struct usb_audio_packet_plan {
	uint32_t	rate;
	uint32_t	frameBytes;
	uint32_t	packetsPerSecond;
	uint32_t	maxPacketBytes;		// wMaxPacketSize bits 0-10
	uint32_t	nominalMaxFrames;	// most frames one playback packet carries
	uint32_t	requestFrames;		// frames asked for per recording packet
	uint32_t	packetsPerBuffer;
	uint32_t	framesPerBuffer;
	uint32_t	regionBytes;		// bytes of one buffer's packets together
};


// Packets per buffer are capped so the descriptor arrays stay small.
static const uint32_t kUsbAudioMaxPacketsPerBuffer = 4096;


inline uint32_t
usb_audio_gcd(uint32_t a, uint32_t b)
{
	while (b != 0) {
		uint32_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}


// The bus speed is not visible to a USB driver on Haiku. UAC 2.0 devices are
// high speed in practice. A UAC 1.0 alternate is high speed when one
// millisecond of audio does not fit in its packet size.
inline bool
usb_audio_high_speed(uint16_t uacVersion, uint32_t rate, uint32_t frameBytes,
	uint16_t wMaxPacketSize)
{
	if (uacVersion >= 0x0200)
		return true;
	uint32_t perMs = (rate + 999) / 1000 * frameBytes;
	return perMs > (uint32_t)(wMaxPacketSize & 0x7ff);
}


inline uint32_t
usb_audio_packets_per_second(bool highSpeed, uint8_t bInterval)
{
	if (bInterval < 1)
		bInterval = 1;
	uint32_t base = highSpeed ? 8000 : 1000;
	if (bInterval - 1 >= 13)
		return 0;
	return base >> (bInterval - 1);
}


// Frames in packet `index` of a buffer (index 0 .. packetsPerBuffer-1).
inline uint32_t
usb_audio_packet_frames(const usb_audio_packet_plan* plan, uint32_t index)
{
	uint64_t next = (uint64_t)(index + 1) * plan->rate / plan->packetsPerSecond;
	uint64_t here = (uint64_t)index * plan->rate / plan->packetsPerSecond;
	return (uint32_t)(next - here);
}


// Returns false when this alternate cannot carry the rate: a packet would
// need more than the 11-bit wMaxPacketSize (a high-bandwidth endpoint,
// which the Haiku XHCI driver refuses), or the numbers are out of range.
inline bool
usb_audio_plan_packets(uint32_t rate, uint32_t frameBytes,
	uint16_t wMaxPacketSize, uint8_t bInterval, bool highSpeed,
	uint32_t wantFrames, bool input, usb_audio_packet_plan* plan)
{
	if (rate == 0 || frameBytes == 0 || plan == NULL)
		return false;

	uint32_t pps = usb_audio_packets_per_second(highSpeed, bInterval);
	if (pps == 0)
		return false;

	uint32_t maxBytes = wMaxPacketSize & 0x7ff;
	uint32_t nominalMax = (rate + pps - 1) / pps;
	if (nominalMax == 0 || (uint64_t)nominalMax * frameBytes > maxBytes)
		return false;

	uint32_t grain = pps / usb_audio_gcd(rate, pps);
	if (wantFrames == 0)
		return false;
	uint64_t packetsWanted = ((uint64_t)wantFrames * pps + rate - 1) / rate;
	uint64_t packets = (packetsWanted + grain - 1) / grain * grain;
	if (packets < grain)
		packets = grain;
	if (packets > kUsbAudioMaxPacketsPerBuffer)
		return false;

	plan->rate = rate;
	plan->frameBytes = frameBytes;
	plan->packetsPerSecond = pps;
	plan->maxPacketBytes = maxBytes;
	plan->nominalMaxFrames = nominalMax;
	plan->requestFrames = input ? maxBytes / frameBytes : nominalMax;
	plan->packetsPerBuffer = (uint32_t)packets;
	plan->framesPerBuffer = (uint32_t)(packets * rate / pps);
	plan->regionBytes = input
		? plan->packetsPerBuffer * plan->requestFrames * frameBytes
		: plan->framesPerBuffer * frameBytes;
	return true;
}


// Frames the carry buffer of a recording stream can hold: what is left of
// the region after one buffer of frames.
inline uint32_t
usb_audio_carry_capacity(const usb_audio_packet_plan* plan)
{
	uint32_t capacity = plan->packetsPerBuffer * plan->requestFrames;
	return capacity > plan->framesPerBuffer
		? capacity - plan->framesPerBuffer : 0;
}


// Recording. Packet i was written at offset i * requestFrames * frameBytes
// and holds actualBytes[i]. The packets are joined, frames carried from last
// time go in front, and a record buffer is published only once it holds
// framesPerBuffer frames. The rest starts the next one. A short transfer
// publishes nothing and is not padded by repeating the last frame: capture
// then follows the device clock instead of the host's. `carry` holds one
// buffer of frames. Returns true when `region` starts with a whole buffer.
// This function never pads: `padded` is left as it is, and the driver adds
// the silence it puts in for a missed packet itself.
// RenkuOS b3dc1212e4, on this join.
inline bool
usb_audio_compact_record(const usb_audio_packet_plan* plan, uint8_t* region,
	const uint32_t* actualBytes, uint8_t* carry, uint32_t* carryFrames,
	uint32_t* padded, uint32_t* dropped)
{
	(void)padded;
	const uint32_t fb = plan->frameBytes;
	const uint32_t slot = plan->requestFrames * fb;
	const uint32_t capacity = plan->packetsPerBuffer * plan->requestFrames;
	const uint32_t want = plan->framesPerBuffer;
	const uint32_t carryCap = want;

	uint32_t got = 0;
	for (uint32_t i = 0; i < plan->packetsPerBuffer; i++) {
		uint32_t bytes = actualBytes[i];
		if (bytes > slot)
			bytes = slot;
		bytes -= bytes % fb;
		if (bytes == 0)
			continue;
		uint8_t* from = region + (uint64_t)i * slot;
		uint8_t* to = region + (uint64_t)got * fb;
		if (from != to)
			memmove(to, from, bytes);
		got += bytes / fb;
	}

	uint32_t carried = *carryFrames;
	if (carried > carryCap)
		carried = carryCap;
	if (carried + got > capacity) {
		uint32_t drop = carried + got - capacity;
		*dropped += drop;
		if (drop < carried) {
			memmove(carry, carry + (uint64_t)drop * fb,
				(uint64_t)(carried - drop) * fb);
			carried -= drop;
		} else {
			carried = 0;
		}
	}
	if (carried > 0) {
		memmove(region + (uint64_t)carried * fb, region, (uint64_t)got * fb);
		memcpy(region, carry, (uint64_t)carried * fb);
	}

	uint32_t total = carried + got;
	if (total < want) {
		if (total > carryCap) {
			*dropped += total - carryCap;
			total = carryCap;
		}
		if (total > 0)
			memcpy(carry, region, (uint64_t)total * fb);
		*carryFrames = total;
		return false;
	}

	// The rest can be almost a whole buffer: after a short capture was held
	// back, the next one brings two. That is audio, not drift, so it is
	// kept; capping it here dropped most of a buffer on a slow device clock
	// (the host test catches that). The latency this costs is at most one
	// buffer; a smaller buffer in Media is the way to lower it.
	uint32_t rest = total - want;
	if (rest > carryCap) {
		*dropped += rest - carryCap;
		rest = carryCap;
	}
	if (rest > 0)
		memcpy(carry, region + (uint64_t)want * fb, (uint64_t)rest * fb);
	*carryFrames = rest;
	return true;
}


#endif	// USB_AUDIO_PACKETS_H
