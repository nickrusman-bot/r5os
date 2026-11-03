/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * USB Audio 1.0 and 2.0 descriptor bytes, channel choice, and PCM packing.
 * No USB stack. The driver and usb_audiotest both use this.
 */


#ifndef USB_AUDIO_FORMAT_H
#define USB_AUDIO_FORMAT_H

#include <stdint.h>
#include <string.h>


static const uint32_t kUsbAudioRates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000,
	44100, 48000, 88200, 96000, 176400, 192000
};


struct usb_audio_parsed_format {
	uint8_t		channels;
	uint8_t		subframeSize;
	uint8_t		bitResolution;
	uint16_t	formatTag;
	uint8_t		rateCount;
	uint32_t	rates[16];
};


inline uint32_t
usb_audio_read24(const uint8_t* bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
		| ((uint32_t)bytes[2] << 16);
}


inline int
usb_audio_parse_ac_version(const uint8_t* bytes, size_t length,
	uint16_t* version)
{
	if (bytes == NULL || version == NULL || length < 5)
		return 0;
	if (bytes[1] != 0x24 || bytes[2] != 0x01)
		return 0;
	uint16_t value = (uint16_t)bytes[3] | ((uint16_t)bytes[4] << 8);
	if (value != 0x0100 && value != 0x0200)
		return 0;
	*version = value;
	return 1;
}


inline int
usb_audio_parse_as_general(const uint8_t* bytes, size_t length,
	uint16_t version, uint8_t* terminalLink, uint16_t* formatTag,
	uint8_t* channels)
{
	if (bytes == NULL || terminalLink == NULL || formatTag == NULL
		|| channels == NULL || length < 4) {
		return 0;
	}
	if (bytes[1] != 0x24 || bytes[2] != 0x01)
		return 0;
	*terminalLink = bytes[3];
	if (version >= 0x0200) {
		if (length < 11)
			return 0;
		uint32_t formats = (uint32_t)bytes[6] | ((uint32_t)bytes[7] << 8)
			| ((uint32_t)bytes[8] << 16) | ((uint32_t)bytes[9] << 24);
		if ((formats & 0x4) != 0 && (formats & 0x1) == 0)
			*formatTag = 0x0003;
		else if ((formats & 0x2) != 0 && (formats & 0x1) == 0)
			*formatTag = 0x0002;
		else
			*formatTag = 0x0001;
		*channels = bytes[10];
		return 1;
	}
	if (length < 7)
		return 0;
	*formatTag = (uint16_t)bytes[5] | ((uint16_t)bytes[6] << 8);
	*channels = 0;
	return 1;
}


inline void
usb_audio_rates_between(uint32_t minRate, uint32_t maxRate,
	usb_audio_parsed_format* format)
{
	format->rateCount = 0;
	if (minRate > maxRate) {
		uint32_t swap = minRate;
		minRate = maxRate;
		maxRate = swap;
	}
	for (size_t i = 0; i < sizeof(kUsbAudioRates) / sizeof(kUsbAudioRates[0]);
			i++) {
		if (kUsbAudioRates[i] < minRate || kUsbAudioRates[i] > maxRate)
			continue;
		if (format->rateCount >= 16)
			break;
		format->rates[format->rateCount++] = kUsbAudioRates[i];
	}
}


inline int
usb_audio_parse_format_i(const uint8_t* bytes, size_t length, uint16_t version,
	uint8_t channelsFromGeneral, usb_audio_parsed_format* format)
{
	if (bytes == NULL || format == NULL || length < 6)
		return 0;
	if (bytes[1] != 0x24 || bytes[2] != 0x02 || bytes[3] != 0x01)
		return 0;
	memset(format, 0, sizeof(*format));
	format->formatTag = 0x0001;
	if (version >= 0x0200) {
		format->channels = channelsFromGeneral;
		format->subframeSize = bytes[4];
		format->bitResolution = bytes[5];
		usb_audio_rates_between(44100, 96000, format);
		return format->channels > 0 && format->channels <= 16
			&& format->subframeSize >= 1 && format->subframeSize <= 4;
	}
	if (length < 8)
		return 0;
	format->channels = bytes[4];
	format->subframeSize = bytes[5];
	format->bitResolution = bytes[6];
	uint8_t freqType = bytes[7];
	if (freqType == 0) {
		if (length < 14)
			return 0;
		usb_audio_rates_between(usb_audio_read24(bytes + 8),
			usb_audio_read24(bytes + 11), format);
	} else {
		if (length < 8 + (size_t)freqType * 3)
			return 0;
		for (uint8_t i = 0; i < freqType && format->rateCount < 16; i++)
			format->rates[format->rateCount++]
				= usb_audio_read24(bytes + 8 + i * 3);
	}
	return format->channels > 0 && format->channels <= 16
		&& format->subframeSize >= 1 && format->subframeSize <= 4;
}


inline uint8_t
usb_audio_choose_channels(uint8_t wanted, const uint8_t* available,
	size_t count)
{
	uint8_t best = 0;
	uint8_t bestDistance = 255;
	for (size_t i = 0; i < count; i++) {
		uint8_t channels = available[i];
		if (channels == 0 || channels > 16)
			continue;
		if (wanted == 0) {
			if (channels > best)
				best = channels;
			continue;
		}
		uint8_t distance = channels > wanted
			? channels - wanted : wanted - channels;
		if (best == 0 || distance < bestDistance
			|| (distance == bestDistance && channels < best)) {
			best = channels;
			bestDistance = distance;
		}
	}
	return best;
}


inline void
usb_audio_write_sample(uint8_t* out, uint32_t stride, uint32_t validBits,
	int32_t sample)
{
	if (out == NULL || stride < 3)
		return;
	if ((stride % 4) != 0) {
		int32_t packed = sample >> 8;
		out[0] = (uint8_t)packed;
		out[1] = (uint8_t)(packed >> 8);
		out[2] = (uint8_t)(packed >> 16);
		return;
	}
	if (validBits == 24)
		sample &= ~(int32_t)0xff;
	memcpy(out, &sample, 4);
}


inline int32_t
usb_audio_read_sample(const uint8_t* in, uint32_t stride, uint32_t validBits)
{
	if (in == NULL || stride < 3)
		return 0;
	if ((stride % 4) != 0) {
		int32_t packed = (int32_t)in[0] | ((int32_t)in[1] << 8)
			| ((int32_t)in[2] << 16);
		if ((packed & 0x800000) != 0)
			packed |= (int32_t)0xff000000;
		return packed << 8;
	}
	int32_t sample = 0;
	memcpy(&sample, in, 4);
	if (validBits == 24)
		sample &= ~(int32_t)0xff;
	return sample;
}


#endif // USB_AUDIO_FORMAT_H
