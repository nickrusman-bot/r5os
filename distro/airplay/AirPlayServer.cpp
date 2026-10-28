/*
 * R5OS AirPlay receiver: classic RAOP (AirPlay 1) audio plus AirPlay 2
 * advertisement/info. Pair-Setup/PTP of full AirPlay 2 is not implemented.
 *
 * RTSP/RAOP flow follows Shairport (James Laird / Mike Brady, MIT).
 */

#include "AirPlayCrypto.h"
#include "AirPlayMessages.h"
#include "AirPlayPlayer.h"
#include "AirPlaySender.h"

#include <Application.h>
#include <Message.h>
#include <Locker.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>


static const uint16 kAirPlayPort = 7000;


struct Session {
	AirPlayPlayer	player;
	uint8_t			aesKey[16];
	uint8_t			aesIv[16];
	int32			fmtp[12];
	bool			haveKey;
	int				rtpSock;
	int				ctlSock;
	uint16			rtpPort;
	uint16			ctlPort;
	thread_id		rtpThread;
	volatile bool	running;
};


static Session gSession;
static BLocker gSessionLock("airplay-session");
static AirPlaySender gSender;


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
		p = strstr(p, "\n");
		if (p != NULL)
			p++;
	}
	return -1;
}


static int
sdp_attr(const char* body, const char* key, char* out, size_t cap)
{
	char needle[64];
	const char* p;

	snprintf(needle, sizeof(needle), "a=%s:", key);
	p = strstr(body, needle);
	if (p == NULL)
		return -1;
	p += strlen(needle);
	size_t n = 0;
	while (p[n] != '\0' && p[n] != '\r' && p[n] != '\n' && n + 1 < cap)
		n++;
	memcpy(out, p, n);
	out[n] = '\0';
	return 0;
}


static bool
first_mac(uint8_t mac[6])
{
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;

	memset(mac, 0, 6);
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		BNetworkAddress hw;
		if (interface.GetHardwareAddress(hw) != B_OK)
			continue;
		const sockaddr_dl* dl = reinterpret_cast<const sockaddr_dl*>(
			&hw.SockAddr());
		if (dl->sdl_family != AF_LINK || dl->sdl_alen != 6)
			continue;
		memcpy(mac, LLADDR(dl), 6);
		return true;
	}
	mac[0] = 0x02;
	mac[5] = 0x05;
	return false;
}


static int
bind_udp()
{
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in addr;
	if (sock < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(sock);
		return -1;
	}
	return sock;
}


static uint16
udp_port(int sock)
{
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (getsockname(sock, (struct sockaddr*)&addr, &len) != 0)
		return 0;
	return ntohs(addr.sin_port);
}


static int32
rtp_thread(void* data)
{
	Session* session = (Session*)data;
	uint8_t buf[2048];

	while (session->running) {
		struct pollfd pfd;
		pfd.fd = session->rtpSock;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 250) <= 0)
			continue;
		ssize_t n = recv(session->rtpSock, buf, sizeof(buf), 0);
		if (n > 12)
			session->player.PushRtpPayload(buf, (int)n);
	}
	return 0;
}


static void
write_all(int sock, const char* data)
{
	size_t n = strlen(data);
	send(sock, data, n, 0);
}


static void
rtsp_reply(int sock, int code, const char* cseq, const char* extra,
	const char* body)
{
	char buf[2048];
	const char* text = code == 200 ? "OK" : (code == 404 ? "Not Found"
		: (code == 403 ? "Forbidden" : "Error"));
	int bodyLen = body != NULL ? (int)strlen(body) : 0;

	snprintf(buf, sizeof(buf),
		"RTSP/1.0 %d %s\r\nCSeq: %s\r\nServer: R5OS/AirPlay\r\n"
		"Session: 1\r\n%sContent-Length: %d\r\n\r\n%s",
		code, text, cseq != NULL && cseq[0] ? cseq : "1",
		extra != NULL ? extra : "", bodyLen, body != NULL ? body : "");
	write_all(sock, buf);
}


static void
http_reply(int sock, int code, const char* ctype, const char* body)
{
	char hdr[256];
	int bodyLen = body != NULL ? (int)strlen(body) : 0;
	const char* text = code == 200 ? "OK" : "Forbidden";
	snprintf(hdr, sizeof(hdr),
		"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
		"Server: R5OS/AirPlay\r\nConnection: close\r\n\r\n",
		code, text, ctype, bodyLen);
	write_all(sock, hdr);
	if (body != NULL)
		write_all(sock, body);
}


static void
handle_info(int sock)
{
	uint8_t mac[6];
	char body[1024];
	char deviceid[24];
	char name[128];

	first_mac(mac);
	snprintf(deviceid, sizeof(deviceid), "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	if (gethostname(name, sizeof(name)) != 0)
		strlcpy(name, "R5OS", sizeof(name));

	snprintf(body, sizeof(body),
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
		"\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\"><dict>\n"
		"<key>deviceid</key><string>%s</string>\n"
		"<key>features</key><integer>%d</integer>\n"
		"<key>model</key><string>R5OS1,1</string>\n"
		"<key>name</key><string>%s</string>\n"
		"<key>pw</key><false/>\n"
		"<key>srcvers</key><string>220.68</string>\n"
		"<key>vv</key><integer>2</integer>\n"
		"<key>statusFlags</key><integer>4</integer>\n"
		"<key>protocolVersion</key><string>1.1</string>\n"
		"</dict></plist>\n",
		deviceid, 0x4A7FFFF7, name);
	http_reply(sock, 200, "text/x-apple-plist+xml", body);
}


static void
apple_challenge(int sock, const char* challenge, char* extra, size_t cap)
{
	uint8_t chal[64];
	uint8_t signedBuf[256];
	uint8_t payload[64];
	uint8_t mac[6];
	char b64[400];
	struct sockaddr_storage local;
	socklen_t localLen = sizeof(local);
	int nchal;
	int nsign;
	int poff = 0;

	nchal = r5_b64_decode(challenge, chal, (int)sizeof(chal));
	if (nchal <= 0)
		return;
	memcpy(payload, chal, (size_t)nchal);
	poff = nchal;
	if (getsockname(sock, (struct sockaddr*)&local, &localLen) == 0) {
		if (local.ss_family == AF_INET) {
			struct sockaddr_in* sin = (struct sockaddr_in*)&local;
			memcpy(payload + poff, &sin->sin_addr, 4);
			poff += 4;
		} else if (local.ss_family == AF_INET6) {
			struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&local;
			memcpy(payload + poff, &sin6->sin6_addr, 16);
			poff += 16;
		}
	}
	first_mac(mac);
	memcpy(payload + poff, mac, 6);
	poff += 6;
	while (poff < 32)
		payload[poff++] = 0;
	nsign = r5_rsa_pkcs1_sign(payload, 32, signedBuf, (int)sizeof(signedBuf));
	if (nsign <= 0)
		return;
	if (r5_b64_encode(signedBuf, nsign, b64, (int)sizeof(b64)) < 0)
		return;
	snprintf(extra, cap, "Apple-Response: %s\r\n", b64);
}


static void
handle_announce(int sock, const char* cseq, const char* body, char* extra)
{
	char keyB64[512];
	char ivB64[64];
	char fmtpLine[128];
	uint8_t rsaBuf[256];
	int nkey;
	int i;

	gSessionLock.Lock();
	gSession.haveKey = false;
	memset(gSession.fmtp, 0, sizeof(gSession.fmtp));
	gSession.fmtp[1] = 352;
	gSession.fmtp[3] = 16;
	gSession.fmtp[11] = 44100;

	if (sdp_attr(body, "rsaaeskey", keyB64, sizeof(keyB64)) == 0) {
		nkey = r5_b64_decode(keyB64, rsaBuf, (int)sizeof(rsaBuf));
		if (nkey > 0) {
			uint8_t plain[256];
			int np = r5_rsa_oaep_decrypt(rsaBuf, nkey, plain, (int)sizeof(plain));
			if (np >= 16) {
				memcpy(gSession.aesKey, plain, 16);
				gSession.haveKey = true;
			}
		}
	}
	if (sdp_attr(body, "aesiv", ivB64, sizeof(ivB64)) == 0) {
		r5_b64_decode(ivB64, gSession.aesIv, 16);
	}
	if (sdp_attr(body, "fmtp", fmtpLine, sizeof(fmtpLine)) == 0) {
		char* p = fmtpLine;
		for (i = 0; i < 12; i++) {
			gSession.fmtp[i] = (int32)strtol(p, &p, 10);
			while (*p == ' ')
				p++;
			if (*p == '\0')
				break;
		}
	}
	gSessionLock.Unlock();
	rtsp_reply(sock, 200, cseq, extra, NULL);
}


static void
handle_setup(int sock, const char* cseq, char* extra)
{
	char more[256];

	gSessionLock.Lock();
	if (gSession.rtpSock >= 0)
		close(gSession.rtpSock);
	if (gSession.ctlSock >= 0)
		close(gSession.ctlSock);
	gSession.rtpSock = bind_udp();
	gSession.ctlSock = bind_udp();
	gSession.rtpPort = udp_port(gSession.rtpSock);
	gSession.ctlPort = udp_port(gSession.ctlSock);
	gSessionLock.Unlock();

	snprintf(more, sizeof(more),
		"%sTransport: RTP/AVP/UDP;unicast;mode=record;server_port=%u;"
		"control_port=%u;timing_port=%u\r\n",
		extra != NULL ? extra : "",
		gSession.rtpPort, gSession.ctlPort, gSession.ctlPort);
	rtsp_reply(sock, 200, cseq, more, NULL);
}


static void
handle_record(int sock, const char* cseq, char* extra)
{
	char more[128];

	gSessionLock.Lock();
	gSession.player.Configure(gSession.fmtp, gSession.aesKey, gSession.aesIv,
		gSession.haveKey);
	gSession.player.Start();
	gSession.running = true;
	gSession.rtpThread = spawn_thread(rtp_thread, "airplay-rtp",
		B_NORMAL_PRIORITY, &gSession);
	if (gSession.rtpThread >= 0)
		resume_thread(gSession.rtpThread);
	gSessionLock.Unlock();

	snprintf(more, sizeof(more), "%sAudio-Latency: 11025\r\n",
		extra != NULL ? extra : "");
	rtsp_reply(sock, 200, cseq, more, NULL);
}


static void
handle_teardown(int sock, const char* cseq, char* extra)
{
	gSessionLock.Lock();
	gSession.running = false;
	if (gSession.rtpThread >= 0) {
		wait_for_thread(gSession.rtpThread, NULL);
		gSession.rtpThread = -1;
	}
	gSession.player.Stop();
	if (gSession.rtpSock >= 0) {
		close(gSession.rtpSock);
		gSession.rtpSock = -1;
	}
	if (gSession.ctlSock >= 0) {
		close(gSession.ctlSock);
		gSession.ctlSock = -1;
	}
	gSessionLock.Unlock();
	rtsp_reply(sock, 200, cseq, extra, NULL);
}


static void
handle_set_parameter(int sock, const char* cseq, const char* body, char* extra)
{
	if (body != NULL && strncmp(body, "volume:", 7) == 0) {
		float db = (float)atof(body + 7);
		gSession.player.SetVolumeDb(db);
	}
	rtsp_reply(sock, 200, cseq, extra, NULL);
}


static int
read_request(int sock, char* buf, int cap, char** bodyOut)
{
	int n = 0;
	*bodyOut = NULL;
	while (n + 1 < cap) {
		ssize_t got = recv(sock, buf + n, 1, 0);
		if (got <= 0)
			return -1;
		n += (int)got;
		buf[n] = '\0';
		if (n >= 4 && strcmp(buf + n - 4, "\r\n\r\n") == 0)
			break;
	}
	char clen[32];
	int content = 0;
	if (header_value(buf, "Content-Length", clen, sizeof(clen)) == 0)
		content = atoi(clen);
	if (content > 0 && n + content < cap) {
		int have = 0;
		while (have < content) {
			ssize_t got = recv(sock, buf + n + have, content - have, 0);
			if (got <= 0)
				break;
			have += (int)got;
		}
		n += have;
		buf[n] = '\0';
		*bodyOut = buf + (n - have);
	}
	return n;
}


static int32
client_thread(void* data)
{
	int sock = (int)(addr_t)data;
	char req[8192];
	char* body = NULL;
	char cseq[32];
	char challenge[128];
	char extra[512];

	for (;;) {
		bool http = false;
		bool done = false;

		if (read_request(sock, req, sizeof(req), &body) < 0)
			break;

		header_value(req, "CSeq", cseq, sizeof(cseq));
		extra[0] = '\0';
		if (header_value(req, "Apple-Challenge", challenge,
				sizeof(challenge)) == 0)
			apple_challenge(sock, challenge, extra, sizeof(extra));

		if (strncmp(req, "GET /info", 9) == 0
			|| strncmp(req, "POST /info", 10) == 0) {
			handle_info(sock);
			http = true;
		} else if (strncmp(req, "POST /pair-setup", 16) == 0
			|| strncmp(req, "POST /pair-verify", 17) == 0) {
			http_reply(sock, 403, "text/plain",
				"AirPlay 2 pairing is not implemented; use AirPlay 1 / RAOP.\n");
			http = true;
		} else if (strncmp(req, "OPTIONS", 7) == 0) {
			char more[256];
			snprintf(more, sizeof(more),
				"%sPublic: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, "
				"SET_PARAMETER, GET_PARAMETER, OPTIONS\r\n", extra);
			rtsp_reply(sock, 200, cseq, more, NULL);
		} else if (strncmp(req, "ANNOUNCE", 8) == 0) {
			handle_announce(sock, cseq, body != NULL ? body : "", extra);
		} else if (strncmp(req, "SETUP", 5) == 0) {
			handle_setup(sock, cseq, extra);
		} else if (strncmp(req, "RECORD", 6) == 0) {
			handle_record(sock, cseq, extra);
		} else if (strncmp(req, "TEARDOWN", 8) == 0) {
			handle_teardown(sock, cseq, extra);
			done = true;
		} else if (strncmp(req, "FLUSH", 5) == 0
			|| strncmp(req, "PAUSE", 5) == 0) {
			rtsp_reply(sock, 200, cseq, extra, NULL);
		} else if (strncmp(req, "SET_PARAMETER", 13) == 0) {
			handle_set_parameter(sock, cseq, body, extra);
		} else if (strncmp(req, "GET_PARAMETER", 13) == 0) {
			rtsp_reply(sock, 200, cseq, extra, "volume: 0.000000\r\n");
		} else {
			rtsp_reply(sock, 404, cseq, extra, NULL);
		}

		if (http || done)
			break;
	}

	close(sock);
	return 0;
}


static int
listen_tcp(int family, uint16 port)
{
	int sock = socket(family, SOCK_STREAM, 0);
	int reuse = 1;
	int only = 1;

	if (sock < 0)
		return -1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	if (family == AF_INET6)
		setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only));

	if (family == AF_INET) {
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_len = sizeof(addr);
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0
			|| listen(sock, 8) < 0) {
			close(sock);
			return -1;
		}
	} else {
		struct sockaddr_in6 addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin6_len = sizeof(addr);
		addr.sin6_family = AF_INET6;
		addr.sin6_port = htons(port);
		if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0
			|| listen(sock, 8) < 0) {
			close(sock);
			return -1;
		}
	}
	return sock;
}


static int32
listen_thread(void*)
{
	int s4 = listen_tcp(AF_INET, kAirPlayPort);
	int s6 = listen_tcp(AF_INET6, kAirPlayPort);
	struct pollfd pfd[2];

	fprintf(stderr, "airplay_server: listening on TCP %u (IPv4=%d IPv6=%d)\n",
		kAirPlayPort, s4 >= 0, s6 >= 0);

	if (s4 < 0 && s6 < 0)
		return B_ERROR;

	gSession.rtpSock = -1;
	gSession.ctlSock = -1;
	gSession.rtpThread = -1;
	gSession.running = false;

	for (;;) {
		int n = 0;
		if (s4 >= 0) {
			pfd[n].fd = s4;
			pfd[n].events = POLLIN;
			n++;
		}
		if (s6 >= 0) {
			pfd[n].fd = s6;
			pfd[n].events = POLLIN;
			n++;
		}
		if (poll(pfd, n, 1000) <= 0)
			continue;
		for (int i = 0; i < n; i++) {
			if ((pfd[i].revents & POLLIN) == 0)
				continue;
			int client = accept(pfd[i].fd, NULL, NULL);
			if (client < 0)
				continue;
			thread_id t = spawn_thread(client_thread, "airplay-client",
				B_NORMAL_PRIORITY, (void*)(addr_t)client);
			if (t >= 0)
				resume_thread(t);
			else
				close(client);
		}
	}
}


class AirPlayApp : public BApplication {
public:
	AirPlayApp()
		:
		BApplication(kAirPlaySignature),
		fTrayInstalled(false)
	{
	}

	virtual void ReadyToRun()
	{
		thread_id listen = spawn_thread(listen_thread, "airplay-listen",
			B_NORMAL_PRIORITY, NULL);
		if (listen >= 0)
			resume_thread(listen);
		SetPulseRate(1000000);
		PostMessage(kMsgAirPlayInstallTray);
	}

	virtual void Pulse()
	{
		_TryInstallTray();
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgAirPlayInstallTray:
				_TryInstallTray();
				break;
			case kMsgAirPlayPlayTo:
			{
				const char* host = NULL;
				int32 port = 0;
				int32 ipv4 = 0;
				message->FindString("host", &host);
				message->FindInt32("port", &port);
				message->FindInt32("ipv4", &ipv4);
				if (host != NULL && port > 0)
					gSender.Start(host, (uint16)port, (uint32)ipv4);
				break;
			}
			case kMsgAirPlayStopSend:
				gSender.Stop();
				break;
			default:
				BApplication::MessageReceived(message);
				break;
		}
	}

private:
			void				_TryInstallTray()
			{
				if (fTrayInstalled)
					return;
				if (r5_install_airplay_deskbar() == B_OK) {
					fTrayInstalled = true;
					SetPulseRate(0);
				}
			}

			bool				fTrayInstalled;
};


int
main()
{
	AirPlayApp app;
	app.Run();
	return 0;
}
