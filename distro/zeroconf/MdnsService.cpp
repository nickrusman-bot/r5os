/*
 * R5OS: advertise hostname.local over IPv4 mDNS (Haiku #1042).
 */

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <OS.h>
#include <SupportDefs.h>


static const uint16 kMdnsPort = 5353;
static const char* kMdnsAddr = "224.0.0.251";
static const uint32 kAnnounceUsec = 120 * 1000000;


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
	if (off + 1 >= cap)
		return 0;
	out[off++] = 0;
	return off;
}


static int
skip_dns_name(const uint8_t* msg, size_t len, size_t* off)
{
	int jumps = 0;
	size_t pos = *off;
	int consumed = 0;

	while (pos < len) {
		uint8_t lab = msg[pos];
		if (lab == 0) {
			if (!consumed)
				*off = pos + 1;
			return 0;
		}
		if ((lab & 0xc0) == 0xc0) {
			if (pos + 1 >= len)
				return -1;
			if (!consumed)
				*off = pos + 2;
			pos = ((size_t)(lab & 0x3f) << 8) | msg[pos + 1];
			if (++jumps > 16)
				return -1;
			consumed = 1;
			continue;
		}
		if ((lab & 0xc0) != 0 || pos + 1 + lab >= len)
			return -1;
		pos += 1 + lab;
		if (!consumed)
			*off = pos;
	}
	return -1;
}


static bool
name_equals_ignore_case(const uint8_t* msg, size_t len, size_t off,
	const char* expected)
{
	char decoded[256];
	size_t dlen = 0;
	int jumps = 0;
	size_t pos = off;

	decoded[0] = '\0';
	while (pos < len) {
		uint8_t lab = msg[pos];
		if (lab == 0)
			break;
		if ((lab & 0xc0) == 0xc0) {
			if (pos + 1 >= len)
				return false;
			pos = ((size_t)(lab & 0x3f) << 8) | msg[pos + 1];
			if (++jumps > 16)
				return false;
			continue;
		}
		if ((lab & 0xc0) != 0 || pos + 1 + lab >= len)
			return false;
		if (dlen != 0) {
			if (dlen + 1 >= sizeof(decoded))
				return false;
			decoded[dlen++] = '.';
		}
		if (dlen + lab >= sizeof(decoded))
			return false;
		memcpy(decoded + dlen, msg + pos + 1, lab);
		dlen += lab;
		pos += 1 + lab;
	}
	decoded[dlen] = '\0';
	if (dlen > 0 && decoded[dlen - 1] == '.')
		decoded[dlen - 1] = '\0';
	return strcasecmp(decoded, expected) == 0;
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
build_response(uint8_t* out, size_t cap, const char* name,
	const struct in_addr* addrs, int naddrs)
{
	size_t off = 12;
	int i;

	if (naddrs <= 0 || cap < 32)
		return 0;
	memset(out, 0, 12);
	out[2] = 0x84;
	out[3] = 0x00;
	out[7] = (uint8_t)naddrs;

	for (i = 0; i < naddrs; i++) {
		off = put_dns_name(out, cap, off, name);
		if (off == 0 || off + 14 > cap)
			return 0;
		out[off++] = 0;
		out[off++] = 1;
		out[off++] = 0x80;
		out[off++] = 1;
		out[off++] = 0;
		out[off++] = 0;
		out[off++] = 0;
		out[off++] = 120;
		out[off++] = 0;
		out[off++] = 4;
		memcpy(out + off, &addrs[i], 4);
		off += 4;
	}
	return off;
}


static bool
query_matches(const uint8_t* msg, size_t len, const char* name)
{
	size_t off = 12;
	uint16_t qd;
	uint16_t i;

	if (len < 12 || (msg[2] & 0x80) != 0)
		return false;
	qd = (uint16_t)((msg[4] << 8) | msg[5]);
	for (i = 0; i < qd; i++) {
		size_t nameOff = off;
		uint16_t type;
		uint16_t classValue;
		if (skip_dns_name(msg, len, &off) != 0)
			return false;
		if (off + 4 > len)
			return false;
		type = (uint16_t)((msg[off] << 8) | msg[off + 1]);
		classValue = (uint16_t)((msg[off + 2] << 8) | msg[off + 3]);
		off += 4;
		(void)classValue;
		if ((type == 1 || type == 255)
			&& name_equals_ignore_case(msg, len, nameOff, name))
			return true;
	}
	return false;
}


static int32
mdns_thread(void* /*data*/)
{
	int sock;
	struct sockaddr_in bindAddr;
	struct ip_mreq mreq;
	char fqdn[256];
	bigtime_t nextAnnounce;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		return B_ERROR;

	int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_len = sizeof(bindAddr);
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(kMdnsPort);
	bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
		close(sock);
		return B_ERROR;
	}

	memset(&mreq, 0, sizeof(mreq));
	inet_pton(AF_INET, kMdnsAddr, &mreq.imr_multiaddr);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

	int loop = 1;
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	our_local_name(fqdn, sizeof(fqdn));
	nextAnnounce = 0;

	for (;;) {
		uint8_t buf[1500];
		struct sockaddr_in from;
		socklen_t fromLen = sizeof(from);
		struct pollfd pfd;
		int n;
		bigtime_t now = system_time();

		if (now >= nextAnnounce) {
			struct in_addr addrs[8];
			int naddrs = collect_ipv4(addrs, 8);
			size_t rlen = build_response(buf, sizeof(buf), fqdn, addrs, naddrs);
			if (rlen > 0) {
				struct sockaddr_in dest;
				memset(&dest, 0, sizeof(dest));
				dest.sin_len = sizeof(dest);
				dest.sin_family = AF_INET;
				dest.sin_port = htons(kMdnsPort);
				inet_pton(AF_INET, kMdnsAddr, &dest.sin_addr);
				sendto(sock, buf, rlen, 0, (struct sockaddr*)&dest, sizeof(dest));
			}
			nextAnnounce = now + kAnnounceUsec;
		}

		pfd.fd = sock;
		pfd.events = POLLIN;
		n = poll(&pfd, 1, 1000);
		if (n <= 0)
			continue;

		ssize_t got = recvfrom(sock, buf, sizeof(buf), 0,
			(struct sockaddr*)&from, &fromLen);
		if (got <= 0)
			continue;
		if (query_matches(buf, (size_t)got, fqdn)) {
			struct in_addr addrs[8];
			int naddrs = collect_ipv4(addrs, 8);
			size_t rlen = build_response(buf, sizeof(buf), fqdn, addrs, naddrs);
			if (rlen > 0) {
				struct sockaddr_in dest;
				memset(&dest, 0, sizeof(dest));
				dest.sin_len = sizeof(dest);
				dest.sin_family = AF_INET;
				dest.sin_port = htons(kMdnsPort);
				inet_pton(AF_INET, kMdnsAddr, &dest.sin_addr);
				sendto(sock, buf, rlen, 0, (struct sockaddr*)&dest, sizeof(dest));
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
