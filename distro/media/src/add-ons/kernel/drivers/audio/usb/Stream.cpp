/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 */


#include "Stream.h"

#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <driver_settings.h>
#include <kernel.h>
#include <usb/USB_audio.h>

#include "Device.h"
#include "Driver.h"
#include "Settings.h"


Stream::Stream(Device* device, size_t interface, usb_interface_list* List)
	:
	AudioStreamingInterface(&device->AudioControl(), interface, List),
	fDevice(device),
	fStatus(B_NO_INIT),
	fStreamEndpoint(0),
	fIsRunning(false),
	fArea(-1),
	fKernelArea(-1),
	fAreaSize(0),
	fDescriptors(NULL),
	fDescriptorsCount(0),
	fCurrentBuffer(0),
	fSamplesCount(0),
	fConfiguredFrames(0),
	fRealTime(0),
	fStartingFrame(0),
	fProcessedBuffers(0),
	fInsideNotify(0),
	fQueueStopped(0)
{
}


Stream::~Stream()
{
	delete_area(fArea);
	delete_area(fKernelArea);
	delete fDescriptors;
}


static uint8
layout_channels()
{
	void* handle = load_driver_settings(DRIVER_NAME);
	if (handle == NULL)
		return 0;

	const char* item = get_driver_parameter(handle, "channel_layout", NULL, NULL);
	uint8 channels = 0;
	if (item != NULL && strcmp(item, "auto") != 0) {
		char* end = NULL;
		unsigned long value = strtoul(item, &end, 0);
		if (end != item && end != NULL && *end == '\0'
			&& value >= 1 && value <= 16) {
			channels = (uint8)value;
		}
	}
	unload_driver_settings(handle);
	return channels;
}


status_t
Stream::_ChooseAlternate()
{
	// Automatic keeps the widest layout. A saved channel_layout prefers
	// that count, then the nearest one the device actually has.
	uint8 wanted = layout_channels();
	uint16 bestScore = 0;
	for (int i = 0; i < fAlternates.Count(); i++) {
		if (fAlternates[i]->Interface() == 0) {
			TRACE(INF, "Ignore alternate %d - zero interface description.\n", i);
			continue;
		}

		if (fAlternates[i]->Format() == 0) {
			TRACE(INF, "Ignore alternate %d - zero format description.\n", i);
			continue;
		}

		if (fAlternates[i]->Format()->fFormatType
				!= USB_AUDIO_FORMAT_TYPE_I) {
			TRACE(ERR, "Ignore alternate %d - format type %#02x "
				"is not supported.\n", i, fAlternates[i]->Format()->fFormatType);
			continue;
		}

		switch (fAlternates[i]->Interface()->fFormatTag) {
			case USB_AUDIO_FORMAT_PCM:
			case USB_AUDIO_FORMAT_PCM8:
			case USB_AUDIO_FORMAT_IEEE_FLOAT:
		//	case USB_AUDIO_FORMAT_ALAW:
		//	case USB_AUDIO_FORMAT_MULAW:
				break;
			default:
				TRACE(ERR, "Ignore alternate %d - format %#04x is not "
					"supported.\n", i, fAlternates[i]->Interface()->fFormatTag);
			continue;
		}

		TypeIFormatDescriptor* format
			= static_cast<TypeIFormatDescriptor*>(fAlternates[i]->Format());

		if (format->fNumChannels < 1 || format->fNumChannels > 16) {
			TRACE(ERR, "Ignore alternate %d - channel count %d "
				"is not supported.\n", i, format->fNumChannels);
			continue;
		}

		if (fAlternates[i]->Interface()->fFormatTag == USB_AUDIO_FORMAT_PCM) {
			switch(format->fBitResolution) {
				default:
				TRACE(ERR, "Ignore alternate %d - bit resolution %d "
					"is not supported.\n", i, format->fBitResolution);
					continue;
				case 8: case 16: case 18: case 20: case 24: case 32:
					break;
			}

			switch(format->fSubframeSize) {
				case 1: case 2: case 3: case 4:
					// 3 bytes is packed 24-bit. hmulti_audio writes that width
					// when the stride is not a multiple of 4 (Haiku #20267).
					break;
				default:
					TRACE(ERR, "Ignore alternate %d - subframe size %d is not supported.\n", i,
						format->fSubframeSize);
					continue;
			}
		}

		uint16 score = format->fBitResolution;
		if (wanted == 0)
			score = format->fNumChannels * 100 + format->fBitResolution;
		else {
			uint8 distance = format->fNumChannels > wanted
				? format->fNumChannels - wanted : wanted - format->fNumChannels;
			score = 5000 - distance * 100 - format->fNumChannels
				+ format->fBitResolution;
		}
		if (score > bestScore) {
			bestScore = score;
			fActiveAlternate = i;
		}
	}

	if (bestScore <= 0) {
		TRACE(ERR, "No compatible alternate found. "
			"Stream initialization failed.\n");
		return B_NO_INIT;
	}

	const ASEndpointDescriptor* endpoint
		= fAlternates[fActiveAlternate]->Endpoint();
	fIsInput = (endpoint->fEndpointAddress & USB_ENDPOINT_ADDR_DIR_IN)
		== USB_ENDPOINT_ADDR_DIR_IN;

	if (fIsInput)
		fCurrentBuffer = (size_t)-1;

	TRACE(INF, "Alternate %d EP:%x selected for %s!\n",
		fActiveAlternate, endpoint->fEndpointAddress,
		fIsInput ? "recording" : "playback");

	return B_OK;
}


status_t
Stream::Init()
{
	fStatus = _ChooseAlternate();
	return fStatus;
}


void
Stream::OnRemove()
{
	// the transfer callback schedule traffic - so we must ensure that we are
	// not inside the callback anymore before returning, as we would otherwise
	// violate the promise not to use any of the pipes after returning from the
	// removed callback
	atomic_set(&fQueueStopped, 1);
	fIsRunning = false;
	while (atomic_get(&fInsideNotify) != 0)
		snooze(100);

	gUSBModule->cancel_queued_transfers(fStreamEndpoint);
	while (atomic_get(&fInsideNotify) != 0)
		snooze(100);
}


static uint32
latency_frames(bool input)
{
	void* handle = load_driver_settings(DRIVER_NAME);
	if (handle == NULL)
		return 0;

	const char* key = input ? "record_buffer_frames" : "play_buffer_frames";
	const char* item = get_driver_parameter(handle, key, NULL, NULL);
	uint32 frames = 0;
	if (item != NULL) {
		char* end = NULL;
		unsigned long value = strtoul(item, &end, 0);
		if (end != item && end != NULL && *end == '\0'
			&& value >= 32 && value <= 8192) {
			frames = (uint32)value;
		}
	}
	unload_driver_settings(handle);
	return frames;
}


status_t
Stream::_SetupBuffers()
{
	// allocate buffer for worst (maximal size) case
	TypeIFormatDescriptor* format = static_cast<TypeIFormatDescriptor*>(
		fAlternates[fActiveAlternate]->Format());

	uint32 samplingRate = fAlternates[fActiveAlternate]->GetSamplingRate();
	uint32 sampleSize = format->fNumChannels * format->fSubframeSize;

	// data size pro 1 ms USB 1 frame
	size_t packetSize = samplingRate * sampleSize / 1000;
	TRACE(INF, "packetSize:%ld\n", packetSize);

	if (packetSize == 0) {
		TRACE(ERR, "computed packet size is 0!");
		return B_BAD_VALUE;
	}

	// Do not ask the host controller for a packet bigger than the endpoint.
	// High-speed UAC2 microframes are smaller than a full-speed 1 ms packet.
	uint16 maxPacket = fAlternates[fActiveAlternate]->Endpoint()->fMaxPacketSize
		& 0x7ff;
	if (maxPacket > 0 && packetSize > maxPacket) {
		packetSize = maxPacket - (maxPacket % sampleSize);
		if (packetSize == 0) {
			TRACE(ERR, "endpoint packet %u is smaller than one frame\n",
				maxPacket);
			return B_BAD_VALUE;
		}
	}

	if (fArea != -1) {
		Stop();
		delete_area(fArea);
		delete_area(fKernelArea);
		delete fDescriptors;
	}

	// 0 keeps the historical area (2048 samples times two buffers). A Media
	// preference writes 256 or 128. USB packets are 1 ms, so the frame count
	// is rounded up to a whole packet. The extra bytes of the page stay unused.
	uint32 frames = latency_frames(fIsInput);
	uint32 packetCount = 0;
	if (frames == 0) {
		fAreaSize = sampleSize * kSamplesBufferSize * kSamplesBufferCount;
	} else {
		uint32 samplesPerPacket = packetSize / sampleSize;
		if (samplesPerPacket == 0) {
			TRACE(ERR, "samples per packet is 0\n");
			return B_BAD_VALUE;
		}
		uint32 aligned = ((frames + samplesPerPacket - 1) / samplesPerPacket)
			* samplesPerPacket;
		packetCount = (aligned / samplesPerPacket) * kSamplesBufferCount;
		fAreaSize = packetCount * packetSize;
	}
	TRACE(INF, "estimate fAreaSize:%d\n", fAreaSize);

	// round up to B_PAGE_SIZE and create area
	fAreaSize = (fAreaSize + (B_PAGE_SIZE - 1)) &~ (B_PAGE_SIZE - 1);
	TRACE(INF, "rounded up fAreaSize:%d\n", fAreaSize);

	fArea = create_area(fIsInput ? DRIVER_NAME "_record_area"
		: DRIVER_NAME "_playback_area", (void**)&fBuffers,
		B_ANY_ADDRESS, fAreaSize, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA);
	if (fArea < 0) {
		TRACE(ERR, "Error of creating %#x - "
			"bytes size buffer area:%#010x\n", fAreaSize, fArea);
		fStatus = fArea;
		return fStatus;
	}

	// The kernel is not allowed to touch userspace areas, so we clone our
	// area into kernel space.
	fKernelArea = clone_area("usb_audio cloned area", (void**)&fKernelBuffers,
		B_ANY_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, fArea);
	if (fKernelArea < 0) {
		fStatus = fKernelArea;
		return fStatus;
	}

	TRACE(INF, "Created area id:%d at addr:%#010x size:%#010lx\n",
		fArea, fDescriptors, fAreaSize);

	if (frames == 0) {
		fDescriptorsCount = fAreaSize / packetSize;
		// we need same size sub-buffers. round it
		fDescriptorsCount = ROUNDDOWN(fDescriptorsCount, kSamplesBufferCount);
	} else
		fDescriptorsCount = packetCount;
	fDescriptors = new usb_iso_packet_descriptor[fDescriptorsCount];
	TRACE(INF, "descriptorsCount:%d\n", fDescriptorsCount);

	// samples count
	fSamplesCount = fDescriptorsCount * packetSize / sampleSize;
	fConfiguredFrames = frames;
	dprintf("usb_audio: %s buffer %" B_PRIu32 " frames (setting %" B_PRIu32
		")\n", fIsInput ? "record" : "play",
		(uint32)(fSamplesCount / kSamplesBufferCount), frames);
	TRACE(INF, "samplesCount:%d\n", fSamplesCount);

	// initialize descriptors array
	for (size_t i = 0; i < fDescriptorsCount; i++) {
		fDescriptors[i].request_length = packetSize;
		fDescriptors[i].actual_length = 0;
		fDescriptors[i].status = B_OK;
	}

	return fStatus;
}


status_t
Stream::OnSetConfiguration(usb_device device,
		const usb_configuration_info* config)
{
	if (config == NULL) {
		TRACE(ERR, "NULL configuration. Not set.\n");
		return B_ERROR;
	}

	usb_interface_info* interface
		= &config->interface[fInterface].alt[fActiveAlternate];
	if (interface == NULL) {
		TRACE(ERR, "NULL interface. Not set.\n");
		return B_ERROR;
	}

	status_t status = gUSBModule->set_alt_interface(device, interface);
	uint8 address = fAlternates[fActiveAlternate]->Endpoint()->fEndpointAddress;

	TRACE(INF, "set_alt_interface %x\n", status);

	for (size_t i = 0; i < interface->endpoint_count; i++) {
		if (interface->endpoint[i].descr == NULL)
			continue;
		if (address == interface->endpoint[i].descr->endpoint_address) {
			fStreamEndpoint = interface->endpoint[i].handle;
			TRACE(INF, "%s Stream Endpoint [address %#04x] handle is: %#010x.\n",
				fIsInput ? "Input" : "Output", address, fStreamEndpoint);
			return B_OK;
		}
	}

	TRACE(INF, "%s Stream Endpoint [address %#04x] was not found.\n",
		fIsInput ? "Input" : "Output", address);
	return B_ERROR;
}


status_t
Stream::Start()
{
	status_t result = B_BUSY;
	if (!fIsRunning) {
		atomic_set(&fQueueStopped, 0);
		for (size_t i = 0; i < kSamplesBufferCount; i++)
			result = _QueueNextTransfer(i, i == 0);
		fIsRunning = result == B_OK;
	}
	return result;
}


status_t
Stream::Stop()
{
	atomic_set(&fQueueStopped, 1);
	fIsRunning = false;
	while (atomic_get(&fInsideNotify) != 0)
		snooze(100);
	gUSBModule->cancel_queued_transfers(fStreamEndpoint);
	while (atomic_get(&fInsideNotify) != 0)
		snooze(100);

	return B_OK;
}


status_t
Stream::_QueueNextTransfer(size_t queuedBuffer, bool start)
{
	TypeIFormatDescriptor* format = static_cast<TypeIFormatDescriptor*>(
		fAlternates[fActiveAlternate]->Format());

	size_t bufferSize = format->fNumChannels * format->fSubframeSize;
	bufferSize *= fSamplesCount / kSamplesBufferCount;

	size_t packetsCount = fDescriptorsCount / kSamplesBufferCount;

	TRACE(DTA, "buffers:%#010x[%#x]\ndescrs:%#010x[%#x]\n",
		fKernelBuffers + bufferSize * queuedBuffer, bufferSize,
		fDescriptors + queuedBuffer * packetsCount, packetsCount);

	status_t status = gUSBModule->queue_isochronous(fStreamEndpoint,
		fKernelBuffers + bufferSize * queuedBuffer, bufferSize,
		fDescriptors + queuedBuffer * packetsCount, packetsCount,
		&fStartingFrame, USB_ISO_ASAP,
		Stream::_TransferCallback, this);

	TRACE(DTA, "frame:%#010x\n", fStartingFrame);
	return status;
}


void
Stream::_TransferCallback(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	Stream* stream = (Stream*)cookie;

	TRACE(status == B_OK ? DTA : ERR,
		"stream:%010x: status:%#010x, data:%#010x, len:%d\n",
		stream->fStreamEndpoint, status, data, actualLength);

	atomic_add(&stream->fInsideNotify, 1);
	if (status == B_CANCELED || atomic_get(&stream->fQueueStopped) != 0
		|| stream->fDevice->fRemoved || !stream->fIsRunning) {
		TRACE(ERR, "Cancelled: c:%p st:%#010x, data:%#010x, len:%d\n",
			cookie, status, data, actualLength);
		atomic_add(&stream->fInsideNotify, -1);
		return;
	}

#if 0
	stream->_DumpDescriptors();
#endif

	if (atomic_add(&stream->fProcessedBuffers, 1) > (int32)kSamplesBufferCount)
		TRACE(ERR, "Processed buffers overflow:%d\n", stream->fProcessedBuffers);
	stream->fRealTime = system_time();

	release_sem_etc(stream->fDevice->fBuffersReadySem, 1, B_DO_NOT_RESCHEDULE);

	// An error that comes straight back (babble, missed service) would
	// otherwise queue the next packet in a tight loop (Haiku #18842).
	if (status != B_OK)
		snooze(1000);

	stream->fCurrentBuffer = (stream->fCurrentBuffer + 1) % kSamplesBufferCount;
	status_t queued = stream->_QueueNextTransfer(stream->fCurrentBuffer, false);
	if (queued != B_OK) {
		TRACE(ERR, "queue next failed:%#010x\n", queued);
	}

	atomic_add(&stream->fInsideNotify, -1);
}


void
Stream::_DumpDescriptors()
{
	for (size_t i = 0; i < fDescriptorsCount; i++)
		TRACE(ISO, "%d:req_len:%d; act_len:%d; stat:%#010x\n", i,
			fDescriptors[i].request_length,	fDescriptors[i].actual_length,
			fDescriptors[i].status);
}


status_t
Stream::GetEnabledChannels(uint32& offset, multi_channel_enable* Enable)
{
	AudioChannelCluster* cluster = ChannelCluster();
	if (cluster == 0)
		return B_ERROR;

	for (size_t i = 0; i < cluster->ChannelsCount(); i++) {
		B_SET_CHANNEL(Enable->enable_bits, offset++, true);
		TRACE(INF, "Report channel %d as enabled.\n", offset);
	}

	return B_OK;
}


status_t
Stream::SetEnabledChannels(uint32& offset, multi_channel_enable* Enable)
{
	AudioChannelCluster* cluster = ChannelCluster();
	if (cluster == 0)
		return B_ERROR;

	for (size_t i = 0; i < cluster->ChannelsCount(); i++, offset++) {
		TRACE(INF, "%s channel %d.\n",
			(B_TEST_CHANNEL(Enable->enable_bits, offset)
			? "Enable" : "Disable"), offset + 1);
	}

	return B_OK;
}


status_t
Stream::GetGlobalFormat(multi_format_info* Format)
{
	_multi_format* format = fIsInput ? &Format->input : &Format->output;
	format->cvsr = fAlternates[fActiveAlternate]->GetSamplingRate();
	format->rate = fAlternates[fActiveAlternate]->GetSamplingRateId(0);
	format->format = fAlternates[fActiveAlternate]->GetFormatId();
	TRACE(INF, "%s.rate:%d cvsr:%f format:%#08x\n",
		fIsInput ? "input" : "ouput",
		format->rate, format->cvsr, format->format);
	return B_OK;
}


status_t
Stream::SetGlobalFormat(multi_format_info* Format)
{
	_multi_format* format = fIsInput ? &Format->input : &Format->output;
	AudioStreamAlternate* alternate = fAlternates[fActiveAlternate];
	if (format->rate == alternate->GetSamplingRateId(0)
			&& format->format == alternate->GetFormatId()
			&& latency_frames(fIsInput) == fConfiguredFrames) {
		TRACE(INF, "No changes required\n");
		return B_OK;
	}

	if (format->rate == alternate->GetSamplingRateId(0)
			&& format->format == alternate->GetFormatId()) {
		Stop();
		return _SetupBuffers();
	}

	alternate->SetSamplingRateById(format->rate);
	alternate->SetFormatId(format->format);
	TRACE(INF, "%s.rate:%d cvsr:%f format:%#08x\n",
		fIsInput ? "input" : "ouput",
		format->rate, format->cvsr, format->format);

	// cancel data flow - it will be rewaked at next buffer exchange call
	Stop();

	// TODO: wait for cancelling?

	// layout of buffers should be adjusted after changing sampling rate/format
	status_t status = _SetupBuffers();

	if (status != B_OK)
		return status;

	// set endpoint speed
	uint32 samplingRate = fAlternates[fActiveAlternate]->GetSamplingRate();
	size_t actualLength = 0;
	usb_audio_sampling_freq freq = _ASFormatDescriptor::GetSamFreq(samplingRate);
	uint8 address = fAlternates[fActiveAlternate]->Endpoint()->fEndpointAddress;

	if (fControlInterface->SpecReleaseNumber() >= 0x0200) {
		_AudioControl* control = fControlInterface->Find(TerminalLink());
		uint8 clock = control != NULL ? control->ClockSourceId() : 0;
		uint32 rate = samplingRate;
		status = gUSBModule->send_request(fDevice->fDevice,
			USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
			USB_AUDIO_SET_CUR, USB_AUDIO_SAMPLING_FREQ_CONTROL << 8,
			(clock << 8) | (fControlInterface->InterfaceNumber() & 0xff),
			sizeof(rate), &rate, &actualLength);
		TRACE(ERR, "UAC2 set_speed %lu clock %u: %s\n",
			samplingRate, clock, strerror(status));
		return status;
	}

	status = gUSBModule->send_request(fDevice->fDevice,
		USB_REQTYPE_CLASS | USB_REQTYPE_ENDPOINT_OUT,
		USB_AUDIO_SET_CUR, USB_AUDIO_SAMPLING_FREQ_CONTROL << 8,
		address, sizeof(freq), &freq, &actualLength);

	TRACE(ERR, "set_speed %02x%02x%02x for ep %#x %d: %s\n",
		freq.bytes[0], freq.bytes[1], freq.bytes[2],
		address, actualLength, strerror(status));
	return status;
}


status_t
Stream::GetBuffers(multi_buffer_list* List)
{
	if (fAreaSize == 0)
		return B_NO_INIT;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	// The loop below fills kSamplesBufferCount entries of the caller's array.
	if ((fIsInput ? List->request_record_buffers
			: List->request_playback_buffers) < (int32)kSamplesBufferCount) {
		return B_BAD_VALUE;
	}
// SPDX-SnippetEnd

	int32 startChannel = List->return_playback_channels;
	buffer_desc** Buffers = List->playback_buffers;

	if (fIsInput) {
		List->flags |= B_MULTI_BUFFER_RECORD;
		List->return_record_buffer_size = fSamplesCount / kSamplesBufferCount;
		List->return_record_buffers = kSamplesBufferCount;
		startChannel = List->return_record_channels;
		Buffers = List->record_buffers;

		TRACE(DTA, "flags:%#10x\nreturn_record_buffer_size:%#010x\n"
			"return_record_buffers:%#010x\n", List->flags,
			List->return_record_buffer_size, List->return_record_buffers);
	} else {
		List->flags |= B_MULTI_BUFFER_PLAYBACK;
		List->return_playback_buffer_size = fSamplesCount / kSamplesBufferCount;
		List->return_playback_buffers = kSamplesBufferCount;

		TRACE(DTA, "flags:%#10x\nreturn_playback_buffer_size:%#010x\n"
			"return_playback_buffers:%#010x\n", List->flags,
			List->return_playback_buffer_size, List->return_playback_buffers);
	}

	TypeIFormatDescriptor* format = static_cast<TypeIFormatDescriptor*>(
		fAlternates[fActiveAlternate]->Format());

	// [buffer][channel] init buffers
	for (size_t buffer = 0; buffer < kSamplesBufferCount; buffer++) {
		TRACE(DTA, "%s buffer #%d:\n", fIsInput ? "input" : "output", buffer + 1);

		struct buffer_desc descs[format->fNumChannels];
		for (size_t channel = startChannel;
				channel < format->fNumChannels; channel++) {
			// init stride to the same for all buffers
			uint32 stride = format->fSubframeSize * format->fNumChannels;
			descs[channel].stride = stride;

			size_t bufferSize = (fSamplesCount / kSamplesBufferCount) * stride;
			descs[channel].base = (char*)fBuffers;
			descs[channel].base += buffer * bufferSize;
			descs[channel].base += channel * format->fSubframeSize;

			TRACE(DTA, "%d:%d: base:%#010x; stride:%#010x\n", buffer, channel,
				descs[channel].base, descs[channel].stride);
		}
		if (!IS_USER_ADDRESS(Buffers[buffer])
				|| user_memcpy(Buffers[buffer], descs, sizeof(descs)) < B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	if (fIsInput) {
		List->return_record_channels += format->fNumChannels;
		TRACE(MIX, "return_record_channels:%#010x\n",
			List->return_record_channels);
	} else {
		List->return_playback_channels += format->fNumChannels;
		TRACE(MIX, "return_playback_channels:%#010x\n",
			List->return_playback_channels);
	}

	return B_OK;
}


bool
Stream::ExchangeBuffer(multi_buffer_info* Info)
{
	if (atomic_get(&fProcessedBuffers) <= 0)
		return false;

	if (fIsInput) {
		Info->recorded_real_time = fRealTime;
		Info->recorded_frames_count += fSamplesCount / kSamplesBufferCount;
		Info->record_buffer_cycle = fCurrentBuffer;
	} else {
		Info->played_real_time = fRealTime;
		Info->played_frames_count += fSamplesCount / kSamplesBufferCount;
		Info->playback_buffer_cycle = fCurrentBuffer;
	}

	atomic_add(&fProcessedBuffers, -1);

	return true;
}
