/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_AIRPLAY_PLAYER_H
#define R5_AIRPLAY_PLAYER_H

#include <Locker.h>
#include <SoundPlayer.h>
#include <SupportDefs.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "alac.h"
#ifdef __cplusplus
}
#endif

#include <stdint.h>


class AirPlayPlayer {
public:
								AirPlayPlayer();
								~AirPlayPlayer();

			status_t			Configure(const int32* fmtp, const uint8_t* aesKey,
									const uint8_t* aesIv, bool encrypted);
			status_t			Start();
			void				Stop();
			void				PushRtpPayload(const uint8_t* data, int len);
			void				SetVolumeDb(float db);

private:
	static	void				_PlayBuffer(void* cookie, void* buffer, size_t size,
									const media_raw_audio_format& format);

			void				_Fill(void* buffer, size_t size);

			BLocker				fLock;
			BSoundPlayer*		fPlayer;
			alac_file*			fAlac;
			uint8_t				fAesKey[16];
			uint8_t				fAesIv[16];
			bool				fEncrypted;
			bool				fPcm;
			int					fFrameBytes;
			float				fGain;
			uint8_t*			fRing;
			int					fRingBytes;
			int					fRead;
			int					fWrite;
			int					fAvail;
};


#endif
