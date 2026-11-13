/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * R5OS: advertise hostname.local (A/AAAA) and AirPlay/RAOP over mDNS
 * on IPv4 (224.0.0.251) and IPv6 (ff02::fb), plus SSH and FTP when they
 * are on.
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

#include <Entry.h>
#include <FindDirectory.h>
#include <Looper.h>
#include <NetworkInterface.h>
#include <NetworkNotifications.h>
#include <NetworkRoster.h>
#include <NetworkSettings.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <Path.h>
#include <SupportDefs.h>


using BNetworkKit::BNetworkServiceSettings;
using BNetworkKit::BNetworkSettings;


static const uint16 kMdnsPort = 5353;
static const char* kMdnsAddr4 = "224.0.0.251";
static const char* kMdnsAddr6 = "ff02::fb";
static const uint32 kAnnounceUsec = 120 * 1000000;
static const uint16 kAirPlayPort = 7000;
static const char* kDiscoverySettings = "network/discovery";
static const uint32 kServiceSSH = 0x01;
static const uint32 kServiceFTP = 0x02;

// what is announced besides AirPlay (kService*)
static uint32 sServices = 0;


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


// "name._ssh._tcp.local" for "name.local"
static void
service_instance(char* out, size_t cap, const char* host, const char* type)
{
	char shortHost[128];
	strlcpy(shortHost, host, sizeof(shortHost));
	size_t n = strlen(shortHost);
	if (n > 6 && strcasecmp(shortHost + n - 6, ".local") == 0)
		shortHost[n - 6] = '\0';
	snprintf(out, cap, "%s.%s", shortHost, type);
}


static bool
service_matches(const char* qname, const char* host, const char* type)
{
	char instance[192];
	service_instance(instance, sizeof(instance), host, type);
	return strcasecmp(qname, type) == 0 || strcasecmp(qname, instance) == 0;
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

	struct {
		uint32		flag;
		const char*	type;
		uint16_t	port;
	} services[] = {
		{ kServiceSSH, "_ssh._tcp.local", 22 },
		{ kServiceFTP, "_ftp._tcp.local", 21 }
	};
	const char* emptyTxt[] = { "", NULL };
	for (size_t s = 0; s < sizeof(services) / sizeof(services[0]); s++) {
		if ((sServices & services[s].flag) == 0)
			continue;
		char instance[192];
		service_instance(instance, sizeof(instance), host, services[s].type);
		off = put_ptr(out, cap, off, services[s].type, instance);
		if (off == 0)
			return 0;
		off = put_srv(out, cap, off, instance, host, services[s].port);
		if (off == 0)
			return 0;
		off = put_txt(out, cap, off, instance, emptyTxt);
		if (off == 0)
			return 0;
		nrr += 3;
	}

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
		if ((sServices & kServiceSSH) != 0
			&& service_matches(qname, host, "_ssh._tcp.local"))
			return true;
		if ((sServices & kServiceFTP) != 0
			&& service_matches(qname, host, "_ftp._tcp.local"))
			return true;
	}
	return false;
}


// The groups are joined later, per interface (update_groups()).
static int
open_mdns4()
{
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in bindAddr;
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
	if (bind(sock, reinterpret_cast<struct sockaddr*>(&bindAddr),
			sizeof(bindAddr)) < 0) {
		syslog(LOG_ERR, "mdns: IPv4 bind: %s\n", strerror(errno));
		close(sock);
		return -1;
	}
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
	return sock;
}


static int
open_mdns6()
{
	int sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in6 bindAddr;
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
	if (bind(sock, reinterpret_cast<struct sockaddr*>(&bindAddr),
			sizeof(bindAddr)) < 0) {
		syslog(LOG_ERR, "mdns: IPv6 bind: %s\n", strerror(errno));
		close(sock);
		return -1;
	}
	setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop));
	return sock;
}


// #pragma mark - group membership per interface


static const int32 kMaxGroups = 8;

struct Group {
	uint32			index;
	struct in_addr	addr;			// IPv4 only
	char			name[IF_NAMESIZE];
	bool			joined;
	status_t		lastError;		// last one logged
};

static Group sGroups4[kMaxGroups];
static int32 sCount4 = 0;
static Group sGroups6[kMaxGroups];
static int32 sCount6 = 0;


static int32
find_group(const Group* groups, int32 count, uint32 index,
	const struct in_addr* addr)
{
	for (int32 i = 0; i < count; i++) {
		if (groups[i].index != index)
			continue;
		if (addr == NULL || groups[i].addr.s_addr == addr->s_addr)
			return i;
	}
	return -1;
}


static bool
join4(int sock, const Group& group, bool join)
{
	struct ip_mreq mreq;
	memset(&mreq, 0, sizeof(mreq));
	inet_pton(AF_INET, kMdnsAddr4, &mreq.imr_multiaddr);
	mreq.imr_interface = group.addr;
	return setsockopt(sock, IPPROTO_IP,
		join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, &mreq,
		sizeof(mreq)) == 0;
}


static bool
join6(int sock, const Group& group, bool join)
{
	struct ipv6_mreq mreq;
	memset(&mreq, 0, sizeof(mreq));
	inet_pton(AF_INET6, kMdnsAddr6, &mreq.ipv6mr_multiaddr);
	mreq.ipv6mr_interface = group.index;
	return setsockopt(sock, IPPROTO_IPV6,
		join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP, &mreq, sizeof(mreq)) == 0;
}


// Logs a failure once, and tries again on the next network event.
static bool
try_join(int sock, Group& group, bool v6)
{
	if (group.joined)
		return false;
	if (v6 ? join6(sock, group, true) : join4(sock, group, true)) {
		group.joined = true;
		group.lastError = B_OK;
		if (v6) {
			syslog(LOG_INFO, "mdns: joined %s on %s\n", kMdnsAddr6,
				group.name);
		} else {
			char text[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &group.addr, text, sizeof(text));
			syslog(LOG_INFO, "mdns: joined %s on %s (%s)\n", kMdnsAddr4,
				group.name, text);
		}
		return true;
	}
	if (group.lastError != errno) {
		group.lastError = errno;
		syslog(LOG_ERR, "mdns: join %s on %s: %s\n",
			v6 ? kMdnsAddr6 : kMdnsAddr4, group.name, strerror(errno));
	}
	return false;
}


static void
merge_groups(int sock, Group* groups, int32& count, const Group* now,
	int32 nowCount, bool v6, bool& joined)
{
	for (int32 i = 0; i < count; i++) {
		if (find_group(now, nowCount, groups[i].index,
				v6 ? NULL : &groups[i].addr) >= 0)
			continue;
		if (groups[i].joined) {
			if (v6)
				join6(sock, groups[i], false);
			else
				join4(sock, groups[i], false);
			syslog(LOG_INFO, "mdns: left %s on %s\n",
				v6 ? kMdnsAddr6 : kMdnsAddr4, groups[i].name);
		}
		groups[i] = groups[--count];
		i--;
	}
	for (int32 i = 0; i < nowCount; i++) {
		int32 at = find_group(groups, count, now[i].index,
			v6 ? NULL : &now[i].addr);
		if (at < 0) {
			if (count >= kMaxGroups)
				continue;
			groups[count] = now[i];
			at = count++;
		}
		if (try_join(sock, groups[at], v6))
			joined = true;
	}
}


// Joins new interfaces, leaves gone ones. True if anything was joined.
static bool
update_groups(int sock4, int sock6)
{
	Group now4[kMaxGroups];
	Group now6[kMaxGroups];
	int32 count4 = 0;
	int32 count6 = 0;

	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_UP) == 0
			|| (interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		uint32 index = interface.Index();
		if (index == 0)
			continue;
		bool have4 = false;
		bool have6 = false;
		int32 n = interface.CountAddresses();
		for (int32 i = 0; i < n; i++) {
			BNetworkInterfaceAddress address;
			if (interface.GetAddressAt(i, address) != B_OK)
				continue;
			const BNetworkAddress& addr = address.Address();
			if (addr.Family() == AF_INET && !have4 && count4 < kMaxGroups) {
				const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(
					&addr.SockAddr());
				if (sin->sin_addr.s_addr == INADDR_ANY)
					continue;
				Group& group = now4[count4++];
				memset(&group, 0, sizeof(group));
				group.index = index;
				group.addr = sin->sin_addr;
				strlcpy(group.name, interface.Name(), sizeof(group.name));
				have4 = true;
			} else if (addr.Family() == AF_INET6 && !have6
				&& count6 < kMaxGroups) {
				Group& group = now6[count6++];
				memset(&group, 0, sizeof(group));
				group.index = index;
				strlcpy(group.name, interface.Name(), sizeof(group.name));
				have6 = true;
			}
		}
	}

	bool joined = false;
	if (sock4 >= 0)
		merge_groups(sock4, sGroups4, sCount4, now4, count4, false, joined);
	if (sock6 >= 0)
		merge_groups(sock6, sGroups6, sCount6, now6, count6, true, joined);
	return joined;
}


// #pragma mark - announcing


static bool sAnnounceFailed = false;


// One line, until an announcement works again.
static void
announce_failed(const char* family, const char* name)
{
	if (sAnnounceFailed)
		return;
	sAnnounceFailed = true;
	syslog(LOG_ERR, "mdns: %s announce on %s: %s\n", family, name,
		strerror(errno));
}


static void
send_announce(int sock4, int sock6, const uint8_t* buf, size_t rlen)
{
	bool sent = false;

	if (sock4 >= 0) {
		struct sockaddr_in dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin_len = sizeof(dest);
		dest.sin_family = AF_INET;
		dest.sin_port = htons(kMdnsPort);
		inet_pton(AF_INET, kMdnsAddr4, &dest.sin_addr);
		for (int32 i = 0; i < sCount4; i++) {
			if (!sGroups4[i].joined)
				continue;
			setsockopt(sock4, IPPROTO_IP, IP_MULTICAST_IF, &sGroups4[i].addr,
				sizeof(sGroups4[i].addr));
			if (sendto(sock4, buf, rlen, 0,
					reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest)) < 0)
				announce_failed("IPv4", sGroups4[i].name);
			else
				sent = true;
		}
	}
	if (sock6 >= 0) {
		struct sockaddr_in6 dest;
		memset(&dest, 0, sizeof(dest));
		dest.sin6_len = sizeof(dest);
		dest.sin6_family = AF_INET6;
		dest.sin6_port = htons(kMdnsPort);
		inet_pton(AF_INET6, kMdnsAddr6, &dest.sin6_addr);
		for (int32 i = 0; i < sCount6; i++) {
			if (!sGroups6[i].joined)
				continue;
			dest.sin6_scope_id = sGroups6[i].index;
			if (sendto(sock6, buf, rlen, 0,
					reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest)) < 0)
				announce_failed("IPv6", sGroups6[i].name);
			else
				sent = true;
		}
	}
	if (sent)
		sAnnounceFailed = false;
}


// #pragma mark - settings


// "Let other devices find this computer" (Settings, Discovery). Our own
// .local lookups (r5_mdns.c) do not depend on it.
static bool
read_discoverable()
{
	BPath path;
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append(kDiscoverySettings) != B_OK)
		return true;
	FILE* file = fopen(path.Path(), "r");
	if (file == NULL)
		return true;
	bool discoverable = true;
	char line[128];
	while (fgets(line, sizeof(line), file) != NULL) {
		char key[64];
		char value[32];
		if (line[0] == '#' || sscanf(line, "%63s %31s", key, value) != 2)
			continue;
		if (strcmp(key, "discoverable") == 0)
			discoverable = strcmp(value, "false") != 0;
	}
	fclose(file);
	return discoverable;
}


// SSH and FTP are announced while they are enabled.
static uint32
read_services()
{
	BNetworkSettings settings;
	uint32 services = 0;
	BMessage message;
	if (settings.GetService("ssh", message) == B_OK
		&& BNetworkServiceSettings(message).IsEnabled())
		services |= kServiceSSH;
	message.MakeEmpty();
	if (settings.GetService("ftp", message) == B_OK
		&& BNetworkServiceSettings(message).IsEnabled())
		services |= kServiceFTP;
	return services;
}


// #pragma mark - watching


// Set by the watcher, picked up by the r5-mdns thread within a second.
static int32 sNetworkChanged = 1;
static int32 sSettingsChanged = 1;


// Watches the interfaces and the hostname, discovery and services files.
class MdnsWatcher : public BLooper {
public:
								MdnsWatcher();

	virtual	void				MessageReceived(BMessage* message);

			void				WatchFiles();
};


MdnsWatcher::MdnsWatcher()
	:
	BLooper("r5-mdns watcher")
{
}


void
MdnsWatcher::WatchFiles()
{
	stop_watching(this);
	BPath directory;
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &directory) != B_OK
		|| directory.Append("network") != B_OK)
		return;
	node_ref ref;
	BEntry entry(directory.Path());
	if (entry.GetNodeRef(&ref) == B_OK)
		watch_node(&ref, B_WATCH_DIRECTORY, this);
	const char* files[] = { "hostname", "discovery", "services" };
	for (uint32 i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		BPath path(directory.Path(), files[i]);
		BEntry file(path.Path());
		if (file.GetNodeRef(&ref) == B_OK)
			watch_node(&ref, B_WATCH_STAT, this);
	}
}


void
MdnsWatcher::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NETWORK_MONITOR:
			atomic_set(&sNetworkChanged, 1);
			break;
		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) == B_OK
				&& opcode != B_STAT_CHANGED) {
				// created, removed or replaced: watch again
				WatchFiles();
			}
			atomic_set(&sSettingsChanged, 1);
			break;
		}
		default:
			BLooper::MessageReceived(message);
			break;
	}
}


static int32
mdns_thread(void* /*data*/)
{
	int sock4 = open_mdns4();
	int sock6 = open_mdns6();
	char fqdn[256];
	bigtime_t nextAnnounce = 0;
	bool discoverable = true;

	if (sock4 < 0 && sock6 < 0)
		return B_ERROR;

	our_local_name(fqdn, sizeof(fqdn));

	for (;;) {
		uint8_t buf[1500];
		struct pollfd pfd[2];
		int nfds = 0;
		int n;

		if (atomic_get_and_set(&sNetworkChanged, 0) != 0) {
			if (update_groups(sock4, sock6))
				nextAnnounce = 0;
		}
		if (atomic_get_and_set(&sSettingsChanged, 0) != 0) {
			char name[256];
			our_local_name(name, sizeof(name));
			if (strcasecmp(name, fqdn) != 0) {
				strlcpy(fqdn, name, sizeof(fqdn));
				syslog(LOG_INFO, "mdns: host name is now %s\n", fqdn);
				nextAnnounce = 0;
			}
			bool value = read_discoverable();
			if (value != discoverable) {
				syslog(LOG_INFO, "mdns: other devices %s find %s\n",
					value ? "can" : "cannot", fqdn);
				nextAnnounce = 0;
			}
			discoverable = value;
			uint32 services = read_services();
			if (services != sServices)
				nextAnnounce = 0;
			sServices = services;
		}

		bigtime_t now = system_time();
		if (discoverable && now >= nextAnnounce) {
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
				reinterpret_cast<struct sockaddr*>(&from), &fromLen);
			if (got <= 0 || !discoverable)
				continue;
			if (query_is_ours(buf, static_cast<size_t>(got), fqdn)) {
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
	MdnsWatcher* watcher = new MdnsWatcher();
	watcher->Run();
	if (start_watching_network(B_WATCH_NETWORK_INTERFACE_CHANGES
			| B_WATCH_NETWORK_LINK_CHANGES, watcher) != B_OK)
		syslog(LOG_ERR, "mdns: cannot watch the network interfaces\n");
	if (watcher->Lock()) {
		watcher->WatchFiles();
		watcher->Unlock();
	}

	thread_id thread = spawn_thread(mdns_thread, "r5-mdns", B_NORMAL_PRIORITY,
		NULL);
	if (thread >= 0)
		resume_thread(thread);
}
