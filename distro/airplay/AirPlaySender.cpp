#include "AirPlaySender.h"
#include "AirPlayCrypto.h"

#include <cerrno>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/rand.h>


static const int kFrameSamples = 352;
static const int kFrameBytes = kFrameSamples * 4;


static int
header_value(const char* req, const char* name, char* out, size_t cap)
{
	const char* p = req;
	size_t nlen = strlen(name);

	out[0] = '\0';
	while (p != NULL && *p != '\0') {
		if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
			p += nlen + 1;
			while (*p == ' ' || *p == '\t')
				p++;
			size_t n = 0;
			while (p[n] != '\0' && p[n] != '\r' && p[n] != '\n' && n + 1 < cap)
				n++;
			memcpy(out, p, n);
			out[n] = '\0';
			return 0;
		}
		p = strchr(p, '\n');
		if (p != NULL)
			p++;
	}
	return -1;
}


AirPlaySender::AirPlaySender()
	:
	fLock("airplay-sender"),
	fRecorder(NULL),
	fRtsp(-1),
	fRtp(-1),
	fSeq(0),
	fStamp(0),
	fSsrc(0x52553053),
	fEncrypted(true),
	fSending(false),
	fAcc(NULL),
	fAccBytes(0),
	fCSeq(1)
{
	memset(fAesKey, 0, sizeof(fAesKey));
	memset(fAesIv, 0, sizeof(fAesIv));
	fHost[0] = '\0';
	fLocal[0] = '\0';
}


AirPlaySender::~AirPlaySender()
{
	Stop();
}


bool
AirPlaySender::IsSending() const
{
	return fSending;
}


void
AirPlaySender::Stop()
{
	fLock.Lock();
	fSending = false;
	if (fRecorder != NULL) {
		fRecorder->Stop();
		fRecorder->Disconnect();
		delete fRecorder;
		fRecorder = NULL;
	}
	if (fRtsp >= 0) {
		char stop[128];
		snprintf(stop, sizeof(stop),
			"TEARDOWN rtsp://%s/audio RTSP/1.0\r\nCSeq: %d\r\n"
			"Session: 1\r\n\r\n", fHost, fCSeq++);
		send(fRtsp, stop, strlen(stop), 0);
		close(fRtsp);
		fRtsp = -1;
	}
	if (fRtp >= 0) {
		close(fRtp);
		fRtp = -1;
	}
	free(fAcc);
	fAcc = NULL;
	fAccBytes = 0;
	fLock.Unlock();
}


status_t
AirPlaySender::Start(const char* host, uint16 port, uint32 ipv4)
{
	Stop();

	if (host == NULL || port == 0)
		return B_BAD_VALUE;

	strlcpy(fHost, host, sizeof(fHost));
	RAND_bytes(fAesKey, 16);
	RAND_bytes(fAesIv, 16);
	fSeq = 0;
	fStamp = 0;
	fCSeq = 1;
	fAcc = (uint8*)malloc(kFrameBytes * 4);
	if (fAcc == NULL)
		return B_NO_MEMORY;
	fAccBytes = 0;

	status_t err = _OpenRtsp(host, port);
	if (err != B_OK)
		return err;
	err = _Announce();
	if (err != B_OK) {
		Stop();
		return err;
	}
	err = _Setup();
	if (err != B_OK) {
		Stop();
		return err;
	}
	err = _Record();
	if (err != B_OK) {
		Stop();
		return err;
	}

	(void)ipv4;
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.frame_rate = 44100;
	format.u.raw_audio.channel_count = 2;

	fRecorder = new BMediaRecorder("AirPlay send", B_MEDIA_RAW_AUDIO);
	if (fRecorder->InitCheck() != B_OK) {
		err = fRecorder->InitCheck();
		Stop();
		return err;
	}
	fRecorder->SetAcceptedFormat(format);
	fRecorder->SetHooks(RecordHook, NULL, this);
	err = fRecorder->Connect(format);
	if (err != B_OK) {
		Stop();
		return err;
	}
	err = fRecorder->Start();
	if (err != B_OK) {
		Stop();
		return err;
	}
	fSending = true;
	return B_OK;
}


status_t
AirPlaySender::_OpenRtsp(const char* host, uint16 port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	struct sockaddr_in local;
	socklen_t localLen = sizeof(local);

	if (sock < 0)
		return errno;
	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		close(sock);
		return B_BAD_VALUE;
	}
	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		int err = errno;
		close(sock);
		return err;
	}
	if (getsockname(sock, (struct sockaddr*)&local, &localLen) == 0)
		inet_ntop(AF_INET, &local.sin_addr, fLocal, sizeof(fLocal));
	else
		strlcpy(fLocal, "0.0.0.0", sizeof(fLocal));
	fRtsp = sock;
	return B_OK;
}


static status_t
rtsp_transact(int sock, const char* req, char* resp, size_t cap)
{
	char* body;
	int n = 0;

	if (send(sock, req, strlen(req), 0) < 0)
		return errno;
	resp[0] = '\0';
	while (n + 1 < (int)cap) {
		ssize_t got = recv(sock, resp + n, 1, 0);
		if (got <= 0)
			return B_ERROR;
		n += (int)got;
		resp[n] = '\0';
		if (n >= 4 && strcmp(resp + n - 4, "\r\n\r\n") == 0)
			break;
	}
	char clen[32];
	int content = 0;
	if (header_value(resp, "Content-Length", clen, sizeof(clen)) == 0)
		content = atoi(clen);
	body = resp + n;
	(void)body;
	while (content > 0 && n + 1 < (int)cap) {
		ssize_t got = recv(sock, resp + n, content, 0);
		if (got <= 0)
			break;
		n += (int)got;
		content -= (int)got;
		resp[n] = '\0';
	}
	if (strncmp(resp, "RTSP/1.0 200", 12) != 0)
		return B_ERROR;
	return B_OK;
}


status_t
AirPlaySender::_Announce()
{
	char keyB64[512];
	char ivB64[64];
	char sdp[1024];
	char req[1536];
	char resp[2048];
	uint8 rsaBuf[256];
	int nenc;

	nenc = r5_rsa_oaep_encrypt(fAesKey, 16, rsaBuf, (int)sizeof(rsaBuf));
	if (nenc <= 0)
		return B_ERROR;
	r5_b64_encode(rsaBuf, nenc, keyB64, (int)sizeof(keyB64));
	r5_b64_encode(fAesIv, 16, ivB64, (int)sizeof(ivB64));

	snprintf(sdp, sizeof(sdp),
		"v=0\r\n"
		"o=R5OS 0 0 IN IP4 %s\r\n"
		"s=R5OS\r\n"
		"c=IN IP4 %s\r\n"
		"t=0 0\r\n"
		"m=audio 0 RTP/AVP 96\r\n"
		"a=rtpmap:96 L16/44100/2\r\n"
		"a=fmtp:96 352 0 16 40 10 14 2 255 0 0 44100\r\n"
		"a=rsaaeskey:%s\r\n"
		"a=aesiv:%s\r\n",
		fLocal, fLocal, keyB64, ivB64);
	snprintf(req, sizeof(req),
		"ANNOUNCE rtsp://%s/audio RTSP/1.0\r\n"
		"CSeq: %d\r\n"
		"Content-Type: application/sdp\r\n"
		"Content-Length: %u\r\n\r\n%s",
		fHost, fCSeq++, (unsigned)strlen(sdp), sdp);
	return rtsp_transact(fRtsp, req, resp, sizeof(resp));
}


status_t
AirPlaySender::_Setup()
{
	char req[512];
	char resp[2048];
	char transport[256];
	char* p;
	uint16 serverPort = 0;
	struct sockaddr_in dest;
	status_t err;

	snprintf(req, sizeof(req),
		"SETUP rtsp://%s/audio RTSP/1.0\r\n"
		"CSeq: %d\r\n"
		"Transport: RTP/AVP/UDP;unicast;mode=record;control_port=6001;"
		"timing_port=6002\r\n\r\n",
		fHost, fCSeq++);
	err = rtsp_transact(fRtsp, req, resp, sizeof(resp));
	if (err != B_OK)
		return err;
	if (header_value(resp, "Transport", transport, sizeof(transport)) != 0)
		return B_ERROR;
	p = strstr(transport, "server_port=");
	if (p == NULL)
		return B_ERROR;
	serverPort = (uint16)atoi(p + 12);
	if (serverPort == 0)
		return B_ERROR;

	fRtp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fRtp < 0)
		return errno;
	memset(&dest, 0, sizeof(dest));
	dest.sin_len = sizeof(dest);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(serverPort);
	inet_pton(AF_INET, fHost, &dest.sin_addr);
	if (connect(fRtp, (struct sockaddr*)&dest, sizeof(dest)) < 0)
		return errno;
	return B_OK;
}


status_t
AirPlaySender::_Record()
{
	char req[256];
	char resp[1024];

	snprintf(req, sizeof(req),
		"RECORD rtsp://%s/audio RTSP/1.0\r\n"
		"CSeq: %d\r\n"
		"Session: 1\r\n"
		"Range: npt=0-\r\n"
		"RTP-Info: seq=0;rtptime=0\r\n\r\n",
		fHost, fCSeq++);
	return rtsp_transact(fRtsp, req, resp, sizeof(resp));
}


void
AirPlaySender::RecordHook(void* cookie, bigtime_t /*timestamp*/, void* data,
	size_t size, const media_format& format)
{
	static_cast<AirPlaySender*>(cookie)->_PushPcm(data, size, format);
}


void
AirPlaySender::_PushPcm(const void* data, size_t size, const media_format& format)
{
	const uint8* in = (const uint8*)data;
	uint8 packet[12 + kFrameBytes];
	int16 tmp[kFrameSamples * 2];
	int channels = 2;
	uint32 sampleFormat = media_raw_audio_format::B_AUDIO_SHORT;
	int bytesPerSample = 2;
	int frames;

	if (!fSending || fRtp < 0 || data == NULL || size == 0)
		return;

	if (format.type == B_MEDIA_RAW_AUDIO) {
		if (format.u.raw_audio.channel_count != 0)
			channels = (int)format.u.raw_audio.channel_count;
		sampleFormat = format.u.raw_audio.format;
	}

	if (sampleFormat == media_raw_audio_format::B_AUDIO_FLOAT) {
		const float* f = (const float*)data;
		int n = (int)(size / sizeof(float));
		int i;
		int16* dst = tmp;
		int out = 0;
		for (i = 0; i < n && out < kFrameSamples * 2; ) {
			float l = f[i++];
			float r = (channels > 1 && i < n) ? f[i++] : l;
			if (channels > 2)
				i += channels - 2;
			if (l > 1)
				l = 1;
			if (l < -1)
				l = -1;
			if (r > 1)
				r = 1;
			if (r < -1)
				r = -1;
			dst[out++] = (int16)(l * 32767.0f);
			dst[out++] = (int16)(r * 32767.0f);
		}
		in = (const uint8*)tmp;
		size = (size_t)out * 2;
		bytesPerSample = 2;
		channels = 2;
	}

	fLock.Lock();
	if (fAcc == NULL) {
		fLock.Unlock();
		return;
	}
	int copy = (int)size;
	if (fAccBytes + copy > kFrameBytes * 4)
		copy = kFrameBytes * 4 - fAccBytes;
	if (copy > 0) {
		memcpy(fAcc + fAccBytes, in, (size_t)copy);
		fAccBytes += copy;
	}

	while (fAccBytes >= kFrameBytes) {
		int i;
		packet[0] = 0x80;
		packet[1] = 0x60;
		packet[2] = (uint8)(fSeq >> 8);
		packet[3] = (uint8)fSeq;
		packet[4] = (uint8)(fStamp >> 24);
		packet[5] = (uint8)(fStamp >> 16);
		packet[6] = (uint8)(fStamp >> 8);
		packet[7] = (uint8)fStamp;
		packet[8] = (uint8)(fSsrc >> 24);
		packet[9] = (uint8)(fSsrc >> 16);
		packet[10] = (uint8)(fSsrc >> 8);
		packet[11] = (uint8)fSsrc;
		for (i = 0; i < kFrameSamples * 2; i++) {
			uint8 lo = fAcc[i * 2];
			uint8 hi = fAcc[i * 2 + 1];
			packet[12 + i * 2] = hi;
			packet[12 + i * 2 + 1] = lo;
		}
		if (fEncrypted)
			r5_aes_cbc_encrypt(fAesKey, fAesIv, packet + 12, kFrameBytes);
		send(fRtp, packet, 12 + kFrameBytes, 0);
		fSeq++;
		fStamp += kFrameSamples;
		fAccBytes -= kFrameBytes;
		memmove(fAcc, fAcc + kFrameBytes, (size_t)fAccBytes);
	}
	fLock.Unlock();
	(void)bytesPerSample;
	(void)frames;
}
