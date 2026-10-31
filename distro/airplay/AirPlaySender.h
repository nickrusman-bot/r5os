/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_AIRPLAY_SENDER_H
#define R5_AIRPLAY_SENDER_H

#include <Locker.h>
#include <MediaRecorder.h>
#include <SupportDefs.h>

#include <stdint.h>


class AirPlaySender {
public:
								AirPlaySender();
								~AirPlaySender();

			status_t			Start(const char* host, uint16 port,
									uint32 ipv4);
			void				Stop();
			bool				IsSending() const;

	static	void				RecordHook(void* cookie, bigtime_t timestamp,
									void* data, size_t size,
									const media_format& format);

private:
			status_t			_OpenRtsp(const char* host, uint16 port);
			status_t			_Announce();
			status_t			_Setup();
			status_t			_Record();
			void				_PushPcm(const void* data, size_t size,
									const media_format& format);

			BLocker				fLock;
			BMediaRecorder*		fRecorder;
			int					fRtsp;
			int					fRtp;
			uint16				fSeq;
			uint32				fStamp;
			uint32				fSsrc;
			uint8				fAesKey[16];
			uint8				fAesIv[16];
			bool				fEncrypted;
			bool				fSending;
			uint8*				fAcc;
			int					fAccBytes;
			int					fCSeq;
			char				fHost[256];
			char				fLocal[64];
};


#endif
