/*
 * IPv6 Duplicate Address Detection (RFC 4862). Haiku #8319.
 */


#include "IPv6Dad.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>

#include <OS.h>


static void
solicited_node(const in6_addr& address, in6_addr& out)
{
	memset(&out, 0, sizeof(out));
	out.s6_addr[0] = 0xff;
	out.s6_addr[1] = 0x02;
	out.s6_addr[11] = 0x01;
	out.s6_addr[12] = 0xff;
	out.s6_addr[13] = address.s6_addr[13];
	out.s6_addr[14] = address.s6_addr[14];
	out.s6_addr[15] = address.s6_addr[15];
}


status_t
r5_ipv6_dad(const char* device, uint32 ifIndex, const uint8* mac,
	const in6_addr& tentative)
{
	int socket = ::socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (socket < 0)
		return errno;

	int hops = 255;
	setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
	setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifIndex,
		sizeof(ifIndex));

	in6_addr group;
	solicited_node(tentative, group);
	ipv6_mreq membership;
	memset(&membership, 0, sizeof(membership));
	membership.ipv6mr_multiaddr = group;
	membership.ipv6mr_interface = ifIndex;
	setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
		sizeof(membership));

	inet_pton(AF_INET6, "ff02::1", &membership.ipv6mr_multiaddr);
	setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
		sizeof(membership));

	sockaddr_in6 local;
	memset(&local, 0, sizeof(local));
	local.sin6_len = sizeof(local);
	local.sin6_family = AF_INET6;
	local.sin6_scope_id = ifIndex;
	bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local));

	uint8 packet[32];
	memset(packet, 0, sizeof(packet));
	struct nd_neighbor_solicit* ns
		= reinterpret_cast<struct nd_neighbor_solicit*>(packet);
	ns->nd_ns_type = ND_NEIGHBOR_SOLICIT;
	ns->nd_ns_code = 0;
	ns->nd_ns_target = tentative;

	sockaddr_in6 dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin6_len = sizeof(dest);
	dest.sin6_family = AF_INET6;
	dest.sin6_scope_id = ifIndex;
	dest.sin6_addr = group;

	ssize_t packetLen = sizeof(struct nd_neighbor_solicit);
	if (sendto(socket, packet, packetLen, 0,
			reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
		packet[24] = ND_OPT_SOURCE_LINKADDR;
		packet[25] = 1;
		memcpy(packet + 26, mac, 6);
		packetLen = sizeof(packet);
		if (sendto(socket, packet, packetLen, 0,
				reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
			status_t error = errno;
			close(socket);
			syslog(LOG_ERR, "dad %s: NS failed: %s\n", device, strerror(error));
			return error;
		}
	}

	timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	bigtime_t deadline = system_time() + 1000000;
	bool duplicate = false;
	while (system_time() < deadline) {
		uint8 incoming[1280];
		sockaddr_in6 from;
		socklen_t fromLen = sizeof(from);
		ssize_t bytes = recvfrom(socket, incoming, sizeof(incoming), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (bytes < 0)
			break;

		const uint8* data = incoming;
		ssize_t length = bytes;
		if (length >= 40 && (data[0] >> 4) == 6) {
			data += 40;
			length -= 40;
		}
		if (length < (ssize_t)sizeof(struct nd_neighbor_advert))
			continue;

		const struct nd_neighbor_advert* na
			= reinterpret_cast<const struct nd_neighbor_advert*>(data);
		if (na->nd_na_type != ND_NEIGHBOR_ADVERT)
			continue;
		if (memcmp(&na->nd_na_target, &tentative, sizeof(in6_addr)) == 0) {
			duplicate = true;
			break;
		}
	}

	close(socket);
	if (duplicate) {
		char text[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &tentative, text, sizeof(text));
		syslog(LOG_ERR, "dad %s: duplicate %s\n", device, text);
		return B_NAME_IN_USE;
	}
	return B_OK;
}
