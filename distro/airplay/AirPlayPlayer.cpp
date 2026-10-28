#include "AirPlayPlayer.h"
#include "AirPlayCrypto.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static const int kRingFrames = 44100 * 2;


AirPlayPlayer::AirPlayPlayer()
	:
	fLock("airplay-player"),
	fPlayer(NULL),
	fAlac(NULL),
	fEncrypted(false),
	fPcm(false),
	fFrameBytes(0),
	fGain(1.0f),
	fRing(NULL),
	fRingBytes(0),
	fRead(0),
	fWrite(0),
	fAvail(0)
{
	memset(fAesKey, 0, sizeof(fAesKey));
	memset(fAesIv, 0, sizeof(fAesIv));
}


AirPlayPlayer::~AirPlayPlayer()
{
	Stop();
}


status_t
AirPlayPlayer::Configure(const int32* fmtp, const uint8_t* aesKey,
	const uint8_t* aesIv, bool encrypted)
{
	int frameSize = fmtp != NULL ? fmtp[1] : 352;
	int sampleSize = fmtp != NULL ? fmtp[3] : 16;
	int rate = fmtp != NULL ? fmtp[11] : 44100;

	if (frameSize <= 0)
		frameSize = 352;
	if (sampleSize != 16)
		sampleSize = 16;
	if (rate <= 0)
		rate = 44100;

	Stop();

	fEncrypted = encrypted;
	fPcm = (fmtp == NULL || fmtp[0] == 0);
	fFrameBytes = frameSize * 2 * (sampleSize / 8);
	if (aesKey != NULL)
		memcpy(fAesKey, aesKey, 16);
	if (aesIv != NULL)
		memcpy(fAesIv, aesIv, 16);

	if (!fPcm) {
		fAlac = alac_create(sampleSize, 2);
		if (fAlac == NULL)
			return B_NO_MEMORY;
		fAlac->setinfo_max_samples_per_frame = (uint32_t)frameSize;
		fAlac->setinfo_7a = (uint8_t)fmtp[2];
		fAlac->setinfo_sample_size = (uint8_t)sampleSize;
		fAlac->setinfo_rice_historymult = (uint8_t)fmtp[4];
		fAlac->setinfo_rice_initialhistory = (uint8_t)fmtp[5];
		fAlac->setinfo_rice_kmodifier = (uint8_t)fmtp[6];
		fAlac->setinfo_7f = (uint8_t)fmtp[7];
		fAlac->setinfo_80 = (uint16_t)fmtp[8];
		fAlac->setinfo_82 = (uint32_t)fmtp[9];
		fAlac->setinfo_86 = (uint32_t)fmtp[10];
		fAlac->setinfo_8a_rate = (uint32_t)rate;
		alac_allocate_buffers(fAlac);
	}

	fRingBytes = kRingFrames * 4;
	fRing = (uint8_t*)malloc((size_t)fRingBytes);
	if (fRing == NULL)
		return B_NO_MEMORY;
	fRead = fWrite = fAvail = 0;

	media_raw_audio_format format = media_raw_audio_format::wildcard;
	format.frame_rate = (float)rate;
	format.channel_count = 2;
	format.format = media_raw_audio_format::B_AUDIO_SHORT;
	format.byte_order = B_MEDIA_LITTLE_ENDIAN;
	format.buffer_size = 4096;

	fPlayer = new BSoundPlayer(&format, "AirPlay", _PlayBuffer, NULL, this);
	if (fPlayer->InitCheck() != B_OK) {
		status_t err = fPlayer->InitCheck();
		delete fPlayer;
		fPlayer = NULL;
		return err;
	}
	fPlayer->SetHasData(true);
	return B_OK;
}


status_t
AirPlayPlayer::Start()
{
	if (fPlayer == NULL)
		return B_NO_INIT;
	return fPlayer->Start();
}


void
AirPlayPlayer::Stop()
{
	if (fPlayer != NULL) {
		fPlayer->Stop();
		delete fPlayer;
		fPlayer = NULL;
	}
	if (fAlac != NULL) {
		alac_free(fAlac);
		fAlac = NULL;
	}
	free(fRing);
	fRing = NULL;
	fRingBytes = 0;
	fRead = fWrite = fAvail = 0;
}


void
AirPlayPlayer::SetVolumeDb(float db)
{
	if (db > 0)
		db = 0;
	if (db < -30)
		db = -30;
	fGain = powf(10.0f, db / 20.0f);
	if (fPlayer != NULL)
		fPlayer->SetVolume(fGain);
}


void
AirPlayPlayer::PushRtpPayload(const uint8_t* data, int len)
{
	uint8_t packet[2048];
	int16_t pcm[4096];
	const uint8_t* payload;
	int payloadLen;
	int outBytes;

	if (len <= 12 || fRing == NULL)
		return;

	payload = data + 12;
	payloadLen = len - 12;
	if (payloadLen > (int)sizeof(packet))
		payloadLen = (int)sizeof(packet);
	memcpy(packet, payload, (size_t)payloadLen);

	if (fEncrypted)
		r5_aes_cbc_decrypt(fAesKey, fAesIv, packet, payloadLen);

	if (fPcm || fAlac == NULL) {
		outBytes = payloadLen;
		if (outBytes > (int)sizeof(pcm))
			outBytes = (int)sizeof(pcm);
		memcpy(pcm, packet, (size_t)outBytes);
	} else {
		int outsize = (int)sizeof(pcm);
		alac_decode_frame(fAlac, packet, pcm, &outsize);
		outBytes = outsize;
	}

	fLock.Lock();
	int off = 0;
	while (off < outBytes && fAvail < fRingBytes) {
		int chunk = outBytes - off;
		int space = fRingBytes - fWrite;
		int room = fRingBytes - fAvail;
		if (chunk > space)
			chunk = space;
		if (chunk > room)
			chunk = room;
		if (chunk <= 0)
			break;
		memcpy(fRing + fWrite, (uint8_t*)pcm + off, (size_t)chunk);
		fWrite = (fWrite + chunk) % fRingBytes;
		fAvail += chunk;
		off += chunk;
	}
	fLock.Unlock();
}


void
AirPlayPlayer::_PlayBuffer(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& /*format*/)
{
	static_cast<AirPlayPlayer*>(cookie)->_Fill(buffer, size);
}


void
AirPlayPlayer::_Fill(void* buffer, size_t size)
{
	uint8_t* out = (uint8_t*)buffer;
	size_t filled = 0;

	fLock.Lock();
	while (filled < size && fAvail > 0) {
		int chunk = (int)(size - filled);
		int contig = fRingBytes - fRead;
		if (chunk > contig)
			chunk = contig;
		if (chunk > fAvail)
			chunk = fAvail;
		memcpy(out + filled, fRing + fRead, (size_t)chunk);
		fRead = (fRead + chunk) % fRingBytes;
		fAvail -= chunk;
		filled += (size_t)chunk;
	}
	fLock.Unlock();

	if (filled < size)
		memset(out + filled, 0, size - filled);
}
