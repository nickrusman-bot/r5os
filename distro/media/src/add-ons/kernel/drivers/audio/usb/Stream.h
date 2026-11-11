/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *
 */
#ifndef _USB_AUDIO_STREAM_H_
#define _USB_AUDIO_STREAM_H_


#include "AudioStreamingInterface.h"
#include "usb_audio_packets.h"


class Device;

class Stream : public AudioStreamingInterface {
	friend	class			Device;
public:
							Stream(Device* device, size_t interface,
								usb_interface_list* List);
							~Stream();

			status_t		Init();
			status_t		InitCheck() { return fStatus; }

			status_t		Start();
			status_t		Stop();
			bool			IsRunning() { return fIsRunning; }

			// UAC 2.0 start, run for every stream before any of them queues
			// a transfer. RenkuOS a842555ed6.
			status_t		SelectAlternate(size_t alternate);
			status_t		ProgramClock();
			uint8			ClockId() const { return fClockId; }
			void			OnRemove();

			status_t		GetBuffers(multi_buffer_list* List);

			status_t		OnSetConfiguration(usb_device device,
							const usb_configuration_info* config);

			bool			ExchangeBuffer(multi_buffer_info* Info);
			status_t		GetEnabledChannels(uint32& offset,
								multi_channel_enable* Enable);
			status_t		SetEnabledChannels(uint32& offset,
								multi_channel_enable* Enable);
			status_t		GetGlobalFormat(multi_format_info* Format);
			status_t		SetGlobalFormat(multi_format_info* Format);

protected:
			Device*			fDevice;
			status_t		fStatus;

			usb_pipe		fStreamEndpoint;
			const usb_configuration_info* fConfig;

			bool			fIsRunning;
			area_id			fArea, fKernelArea;
			size_t			fAreaSize;
			usb_iso_packet_descriptor* fDescriptors;
			size_t			fDescriptorsCount;
			uint8*			fBuffers;
			uint8*			fKernelBuffers;
			size_t			fCurrentBuffer;
			size_t			fSamplesCount;
			uint32			fConfiguredFrames;

			bigtime_t		fRealTime;
			uint32			fStartingFrame;
			int32			fProcessedBuffers;
			int32			fInsideNotify;
			int32			fQueueStopped;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			int32			fTransferErrors;
			usb_audio_packet_plan fPlan;
			size_t			fRegionBytes;	// one buffer's packets, in bytes
			uint8*			fCarry;			// recording: frames for next time
			uint32			fCarryFrames;
			uint32*			fActualBytes;	// recording: bytes per packet
			uint32			fPadded;
			uint32			fDropped;
			uint8			fClockId;		// UAC 2.0 clock source, 0 if none
			usb_pipe		fFeedbackPipe;	// explicit feedback, or 0
			int32			fFeedbackSample;// 16.16 frames per packet, 0 if none
			int32			fFeedbackError;
			uint32			fFeedbackFrame;
			usb_iso_packet_descriptor fFeedbackPacket;
			uint8			fFeedbackBytes[4];
			uint8*			fPlayScratch;
			uint8*			fPlayCarry;
			uint32			fPlayCarryFrames;
// SPDX-SnippetEnd

private:
			status_t		_ChooseAlternate();
			status_t		_SetupBuffers();
			status_t		_QueueNextTransfer(size_t buffer, bool start);
			status_t		_QueuePlayback(size_t buffer);
			void			_QueueFeedback();
	static	void			_TransferCallback(void* cookie, status_t status,
								void* data, size_t actualLength);
	static	void			_FeedbackCallback(void* cookie, status_t status,
								void* data, size_t actualLength);
			void			_DumpDescriptors();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			bool			_JoinRecorded(size_t buffer);
			void			_QueryClockRates();
// SPDX-SnippetEnd
};


#endif // _USB_AUDIO_STREAM_H_

