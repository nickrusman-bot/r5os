/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * R5OS: advertise hostname.local (A/AAAA) and AirPlay/RAOP over mDNS
 * on IPv4 (224.0.0.251) and IPv6 (ff02::fb).
 */

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>

#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <OS.h>
#include <SupportDefs.h>


static const uint16 kMdnsPort = 5353;
static const char* kMdnsAddr4 = "224.0.0.251";
static const char* kMdnsAddr6 = "ff02::fb";
static const uint32 kAnnounceUsec = 120 * 1000000;
static const uint16 kAirPlayPort = 7000;


static size_t
put_dns_name(uint8_t* out, size_t cap, size_t off, const char* name)
{
	char copy[256];
	char* label;
	char* rest;
	size_t n = strlen(name);
	if (n >= sizeof(copy))
		return 0;
	memcpy(copy, name, n + 1);
	if (n > 0 && copy[n - 1] == '.')
		copy[n - 1] = '\0';

	rest = copy;
	while (rest != NULL && *rest != '\0') {
		label = rest;
		rest = strchr(rest, '.');
		if (rest != NULL) {
			*rest = '\0';
			rest++;
		}
		n = strlen(label);
		if (n == 0 || n > 63 || off + n + 1 >= cap)
			return 0;
		out[off++] = (uint8_t)n;
		memcpy(out + off, label, n);
		off += n;
	}
	if (off + 1 > cap)
		return 0;
	out[off++] = 0;
	return off;
}


static int
skip_dns_name(const uint8_t* msg, size_t len, size_t* off)
{
	int jumps = 0;
	size_t pos = *off;
	size_t end = *off;
	bool jumped = false;

	while (pos < len) {
		uint8_t lab = msg[pos];
		if (lab == 0) {
			if (!jumped)
				end = pos + 1;
			*off = end;
			return 0;
		}
		if ((lab & 0xc0) == 0xc0) {
			if (pos + 1 >= len)
				return -1;
			if (!jumped)
				end = pos + 2;
			pos = (size_t)(((lab & 0x3f) << 8) | msg[pos + 1]);
			jumped = true;
			if (++jumps > 10)
				return -1;
			continue;
		}
		if (pos + 1 + lab > len)
			return -1;
		pos += 1 + lab;
	}
	return -1;
}


static int
decode_dns_name(const uint8_t* msg, size_t len, size_t off, char* out,
	size_t cap)
{
	size_t o = 0;
	int jumps = 0;

	while (off < len) {
		uint8_t lab = msg[off];
		if (lab == 0) {
			if (o == 0) {
				if (cap < 2)
					return -1;
				out[0] = '\0';
				return 0;
			}
			if (o > 0 && out[o - 1] == '.')
				o--;
			if (o + 1 > cap)
				return -1;
			out[o] = '\0';
			return 0;
		}
		if ((lab & 0xc0) == 0xc0) {
			if (off + 1 >= len)
				return -1;
			off = (size_t)(((lab & 0x3f) << 8) | msg[off + 1]);
			if (++jumps > 10)
				return -1;
			continue;
		}
		if (off + 1 + lab > len || lab > 63)
			return -1;
		if (o + lab + 2 > cap)
			return -1;
		memcpy(out + o, msg + off + 1, lab);
		o += lab;
		out[o++] = '.';
		off += 1 + lab;
	}
	return -1;
}


static size_t
put_rr_header(uint8_t* out, size_t cap, size_t off, const char* name,
	uint16_t type, uint16_t rdlen)
{
	off = put_dns_name(out, cap, off, name);
	if (off == 0 || off + 10 + rdlen > cap)
		return 0;
	out[off++] = (uint8_t)(type >> 8);
	out[off++] = (uint8_t)type;
	out[off++] = 0x80;
	out[off++] = 1;
	out[off++] = 0;
	out[off++] = 0;
	out[off++] = 0;
	out[off++] = 120;
	out[off++] = (uint8_t)(rdlen >> 8);
	out[off++] = (uint8_t)rdlen;
	return off;
}


static size_t
put_a(uint8_t* out, size_t cap, size_t off, const char* name,
	const struct in_addr* addr)
{
	off = put_rr_header(out, cap, off, name, 1, 4);
	if (off == 0)
		return 0;
	memcpy(out + off, addr, 4);
	return off + 4;
}


static size_t
put_aaaa(uint8_t* out, size_t cap, size_t off, const char* name,
	const struct in6_addr* addr)
{
	off = put_rr_header(out, cap, off, name, 28, 16);
	if (off == 0)
		return 0;
	memcpy(out + off, addr, 16);
	return off + 16;
}


static size_t
put_ptr(uint8_t* out, size_t cap, size_t off, const char* owner,
	const char* target)
{
	uint8_t tmp[256];
	size_t n = put_dns_name(tmp, sizeof(tmp), 0, target);
	if (n == 0)
		return 0;
	off = put_rr_header(out, cap, off, owner, 12, (uint16_t)n);
	if (off == 0)
		return 0;
	memcpy(out + off, tmp, n);
	return off + n;
}


static size_t
put_srv(uint8_t* out, size_t cap, size_t off, const char* owner,
	const char* host, uint16_t port)
{
	uint8_t tmp[256];
	size_t n = put_dns_name(tmp, sizeof(tmp), 0, host);
	if (n == 0)
		return 0;
	off = put_rr_header(out, cap, off, owner, 33, (uint16_t)(6 + n));
	if (off == 0)
		return 0;
	out[off++] = 0;
	out[off++] = 0;
	out[off++] = 0;
	out[off++] = 0;
	out[off++] = (uint8_t)(port >> 8);
	out[off++] = (uint8_t)port;
	memcpy(out + off, tmp, n);
	return off + n;
}


static size_t
put_txt(uint8_t* out, size_t cap, size_t off, const char* owner,
	const char* const* pairs)
{
	uint8_t tmp[512];
	size_t n = 0;
	int i;

	for (i = 0; pairs[i] != NULL; i++) {
		size_t len = strlen(pairs[i]);
		if (len > 255 || n + 1 + len > sizeof(tmp))
			return 0;
		tmp[n++] = (uint8_t)len;
		memcpy(tmp + n, pairs[i], len);
		n += len;
	}
	off = put_rr_header(out, cap, off, owner, 16, (uint16_t)n);
	if (off == 0)
		return 0;
	memcpy(out + off, tmp, n);
	return off + n;
}


static int
collect_ipv4(struct in_addr* addrs, int max)
{
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	int count = 0;

	while (count < max && roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_UP) == 0
			|| (interface.Flags() & IFF_LOOPBACK) != 0)
			continue;

		int32 n = interface.CountAddresses();
		for (int32 i = 0; i < n && count < max; i++) {
			BNetworkInterfaceAddress address;
			if (interface.GetAddressAt(i, address) != B_OK)
				continue;
			const BNetworkAddress& addr = address.Address();
			if (addr.Family() != AF_INET)
				continue;
			const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(
				&addr.SockAddr());
			uint32 ip = ntohl(sin->sin_addr.s_addr);
			if ((ip >> 24) == 127)
				continue;
			addrs[count++] = sin->sin_addr;
		}
	}
	return count;
}


static int
collect_ipv6(struct in6_addr* addrs, int max)
{
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	int count = 0;

	while (count < max && roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_UP) == 0
			|| (interface.Flags() & IFF_LOOPBACK) != 0)
			continue;

		int32 n = interface.CountAddresses();
		for (int32 i = 0; i < n && count < max; i++) {
			BNetworkInterfaceAddress address;
			if (interface.GetAddressAt(i, address) != B_OK)
				continue;
			const BNetworkAddress& addr = address.Address();
			if (addr.Family() != AF_INET6)
				continue;
			const sockaddr_in6* sin6 = reinterpret_cast<const sockaddr_in6*>(
				&addr.SockAddr());
			if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)
				|| IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)
				|| IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
				continue;
			addrs[count++] = sin6->sin6_addr;
		}
	}
	return count;
}


static int
collect_ifindexes(uint32* indexes, int max)
{
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	int count = 0;

	while (count < max && roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_UP) == 0
			|| (interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		uint32 index = interface.Index();
		if (index == 0)
			continue;
		bool seen = false;
		for (int i = 0; i < count; i++) {
			if (indexes[i] == index)
				seen = true;
		}
		if (seen)
			continue;
		indexes[count++] = index;
	}
	return count;
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
		bool zero = true;
		for (int i = 0; i < 6; i++) {
			if (mac[i] != 0)
				zero = false;
		}
		if (!zero)
			return true;
	}
	mac[0] = 0x02;
	mac[5] = 0x05;
	return false;
}


static void
our_local_name(char* out, size_t cap)
{
	char host[128];
	size_t n;

	if (gethostname(host, sizeof(host)) != 0)
		strlcpy(host, "r5os", sizeof(host));
	host[sizeof(host) - 1] = '\0';
	n = strlen(host);
	if (n > 0 && host[n - 1] == '.')
		host[n - 1] = '\0';
	if (strlen(host) >= 6
		&& strcasecmp(host + strlen(host) - 6, ".local") == 0)
		strlcpy(out, host, cap);
	else
		snprintf(out, cap, "%s.local", host);
}


static size_t
build_response(uint8_t* out, size_t cap, const char* host)
{
	struct in_addr v4[8];
	struct in6_addr v6[8];
	uint8_t mac[6];
	char raopInst[128];
	char airInst[128];
	char deviceid[24];
	char pkPair[80];
	int n4;
	int n6;
	int nrr = 0;
	size_t off = 12;
	int i;

	n4 = collect_ipv4(v4, 8);
	n6 = collect_ipv6(v6, 8);
	first_mac(mac);
	{
		char shortHost[128];
		strlcpy(shortHost, host, sizeof(shortHost));
		size_t n = strlen(shortHost);
		if (n > 6 && strcasecmp(shortHost + n - 6, ".local") == 0)
			shortHost[n - 6] = '\0';
		snprintf(raopInst, sizeof(raopInst),
			"%02X%02X%02X%02X%02X%02X@%s._raop._tcp.local",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], shortHost);
		snprintf(airInst, sizeof(airInst), "%s._airplay._tcp.local", shortHost);
	}
	snprintf(deviceid, sizeof(deviceid), "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	snprintf(pkPair, sizeof(pkPair), "deviceid=%s", deviceid);

	const char* raopTxt[] = {
		"txtvers=1",
		"ch=2",
		"cn=0,1",
		"da=true",
		"et=0,1",
		"md=0,1,2",
		"pw=false",
		"sr=44100",
		"ss=16",
		"tp=UDP",
		"vn=65537",
		"vs=220.68",
		"am=R5OS1,1",
		"ek=1",
		"sv=false",
		"sf=0x4",
		NULL
	};
	const char* airTxt[] = {
		pkPair,
		"features=0x5A7FFFF7,0x1E",
		"flags=0x4",
		"model=R5OS1,1",
		"manufacturer=R5OS",
		"srcvers=220.68",
		"pw=false",
		"vv=2",
		NULL
	};

	if (n4 + n6 <= 0 || cap < 64)
		return 0;

	memset(out, 0, 12);
	out[2] = 0x84;

	for (i = 0; i < n4; i++) {
		off = put_a(out, cap, off, host, &v4[i]);
		if (off == 0)
			return 0;
		nrr++;
	}
	for (i = 0; i < n6; i++) {
		off = put_aaaa(out, cap, off, host, &v6[i]);
		if (off == 0)
			return 0;
		nrr++;
	}

	off = put_ptr(out, cap, off, "_raop._tcp.local", raopInst);
	if (off == 0)
		return 0;
	nrr++;
	off = put_srv(out, cap, off, raopInst, host, kAirPlayPort);
	if (off == 0)
		return 0;
	nrr++;
	off = put_txt(out, cap, off, raopInst, raopTxt);
	if (off == 0)
		return 0;
	nrr++;

	off = put_ptr(out, cap, off, "_airplay._tcp.local", airInst);
	if (off == 0)
		return 0;
	nrr++;
	off = put_srv(out, cap, off, airInst, host, kAirPlayPort);
	if (off == 0)
		return 0;
	nrr++;
	off = put_txt(out, cap, off, airInst, airTxt);
	if (off == 0)
		return 0;
	nrr++;

	out[6] = (uint8_t)(nrr >> 8);
	out[7] = (uint8_t)nrr;
	return off;
}


static bool
query_is_ours(const uint8_t* msg, size_t len, const char* host)
{
	size_t off = 12;
	uint16_t qd;
	uint16_t i;
	char qname[256];
	char raopInst[128];
	char airInst[128];
	uint8_t mac[6];

	if (len < 12 || (msg[2] & 0x80) != 0)
		return false;

	first_mac(mac);
	{
		char shortHost[128];
		strlcpy(shortHost, host, sizeof(shortHost));
		size_t n = strlen(shortHost);
		if (n > 6 && strcasecmp(shortHost + n - 6, ".local") == 0)
			shortHost[n - 6] = '\0';
		snprintf(raopInst, sizeof(raopInst),
			"%02X%02X%02X%02X%02X%02X@%s._raop._tcp.local",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], shortHost);
		snprintf(airInst, sizeof(airInst), "%s._airplay._tcp.local", shortHost);
	}

	qd = (uint16_t)((msg[4] << 8) | msg[5]);
	for (i = 0; i < qd; i++) {
		uint16_t type;
		if (decode_dns_name(msg, len, off, qname, sizeof(qname)) != 0)
			return false;
		if (skip_dns_name(msg, len, &off) != 0)
			return false;
		if (off + 4 > len)
			return false;
		type = (uint16_t)((msg[off] << 8) | msg[off + 1]);
		off += 4;
		(void)type;
		if (strcasecmp(qname, host) == 0
			|| strcasecmp(qname, "_raop._tcp.local") == 0
			|| strcasecmp(qname, "_airplay._tcp.local") == 0
			|| strcasecmp(qname, raopInst) == 0
			|| strcasecmp(qname, airInst) == 0)
			return true;
	}
	return false;
}


static int
open_mdns4()
{
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in bindAddr;
	struct ip_mreq mreq;
	int reuse = 1;
	int loop = 1;

	if (sock < 0)
		return -1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_len = sizeof(bindAddr);
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(kMdnsPort);
	bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
		close(sock);
		return -1;
	}
	memset(&mreq, 0, sizeof(mreq));
	inet_pton(AF_INET, kMdnsAddr4, &mreq.imr_multiaddr);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
	return sock;
}


static int
open_mdns6()
{
	int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in6 bindAddr;
	struct ipv6_mreq mreq;
	int reuse = 1;
	int only = 1;
	int loop = 1;

	if (sock < 0)
		return -1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only));
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin6_len = sizeof(bindAddr);
	bindAddr.sin6_family = AF_INET6;
	bindAddr.sin6_port = htons(kMdnsPort);
	if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
		close(sock);
		return -1;
	}
	memset(&mreq, 0, sizeof(mreq));
	inet_pton(AF_INET6, kMdnsAddr6, &mreq.ipv6mr_multiaddr);
	mreq.ipv6mr_interface = 0;
	setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
	uint32 indexes[8];
	int nIf = collect_ifindexes(indexes, 8);
	for (int i = 0; i < nIf; i++) {
		mreq.ipv6mr_interface = indexes[i];
		setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
	}
	setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));
	return sock;
}


static void
send_announce(int sock4, int sock6, const uint8_t* buf, size_t rlen)
{
	if (sock4 >= 0) {
		struct sockaddr_in dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin_len = sizeof(dest);
		dest.sin_family = AF_INET;
		dest.sin_port = htons(kMdnsPort);
		inet_pton(AF_INET, kMdnsAddr4, &dest.sin_addr);
		sendto(sock4, buf, rlen, 0, (struct sockaddr*)&dest, sizeof(dest));
	}
	if (sock6 >= 0) {
		struct sockaddr_in6 dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin6_len = sizeof(dest);
		dest.sin6_family = AF_INET6;
		dest.sin6_port = htons(kMdnsPort);
		inet_pton(AF_INET6, kMdnsAddr6, &dest.sin6_addr);
		uint32 indexes[8];
		int nIf = collect_ifindexes(indexes, 8);
		if (nIf == 0) {
			if (sendto(sock6, buf, rlen, 0, (struct sockaddr*)&dest,
					sizeof(dest)) < 0) {
				syslog(LOG_ERR, "mdns: IPv6 announce: %s\n", strerror(errno));
			}
		} else {
			for (int i = 0; i < nIf; i++) {
				dest.sin6_scope_id = indexes[i];
				if (sendto(sock6, buf, rlen, 0, (struct sockaddr*)&dest,
						sizeof(dest)) < 0) {
					syslog(LOG_ERR, "mdns: IPv6 announce if %" B_PRIu32 ": %s\n",
						indexes[i], strerror(errno));
				}
			}
		}
	}
}


static int32
mdns_thread(void* /*data*/)
{
	int sock4 = open_mdns4();
	int sock6 = open_mdns6();
	char fqdn[256];
	bigtime_t nextAnnounce;

	if (sock4 < 0 && sock6 < 0)
		return B_ERROR;

	our_local_name(fqdn, sizeof(fqdn));
	nextAnnounce = 0;

	for (;;) {
		uint8_t buf[1500];
		struct pollfd pfd[2];
		int nfds = 0;
		int n;
		bigtime_t now = system_time();

		if (now >= nextAnnounce) {
			size_t rlen = build_response(buf, sizeof(buf), fqdn);
			if (rlen > 0)
				send_announce(sock4, sock6, buf, rlen);
			nextAnnounce = now + kAnnounceUsec;
		}

		if (sock4 >= 0) {
			pfd[nfds].fd = sock4;
			pfd[nfds].events = POLLIN;
			nfds++;
		}
		if (sock6 >= 0) {
			pfd[nfds].fd = sock6;
			pfd[nfds].events = POLLIN;
			nfds++;
		}
		n = poll(pfd, nfds, 1000);
		if (n <= 0)
			continue;

		for (int i = 0; i < nfds; i++) {
			if ((pfd[i].revents & POLLIN) == 0)
				continue;
			sockaddr_storage from;
			socklen_t fromLen = sizeof(from);
			ssize_t got = recvfrom(pfd[i].fd, buf, sizeof(buf), 0,
				(struct sockaddr*)&from, &fromLen);
			if (got <= 0)
				continue;
			if (query_is_ours(buf, (size_t)got, fqdn)) {
				size_t rlen = build_response(buf, sizeof(buf), fqdn);
				if (rlen > 0)
					send_announce(sock4, sock6, buf, rlen);
			}
		}
	}
}


void
r5_start_mdns_service()
{
	thread_id thread = spawn_thread(mdns_thread, "r5-mdns", B_NORMAL_PRIORITY,
		NULL);
	if (thread >= 0)
		resume_thread(thread);
}
