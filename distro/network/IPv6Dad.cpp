/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * IPv6 Duplicate Address Detection (RFC 4862). Haiku #8319.
 */


#include "IPv6Dad.h"
#include "IPv6DadPacket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet6/in6.h>
#include <string.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <NetworkInterface.h>
#include <OS.h>


static const bigtime_t kRetransTimer = 1000000;


static bool
slot_link_local(const BNetworkInterfaceAddress& slot, in6_addr& out)
{
	if (slot.Address().Family() != AF_INET6)
		return false;
	const sockaddr_in6& in6
		= *reinterpret_cast<const sockaddr_in6*>(&slot.Address().SockAddr());
	if (!IN6_IS_ADDR_LINKLOCAL(&in6.sin6_addr))
		return false;
	out = in6.sin6_addr;
	return true;
}


static status_t
set_int_option(int socket, int level, int option, int value)
{
	if (setsockopt(socket, level, option, &value, sizeof(value)) < 0)
		return errno;
	return B_OK;
}


static status_t
join_group(int socket, uint32 ifIndex, const in6_addr& group)
{
	ipv6_mreq membership;
	memset(&membership, 0, sizeof(membership));
	membership.ipv6mr_multiaddr = group;
	membership.ipv6mr_interface = ifIndex;
	if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
			sizeof(membership)) < 0)
		return errno;
	return B_OK;
}


static int
wait_readable(int socket, bigtime_t remaining)
{
	if (remaining <= 0)
		return 0;

	fd_set readSet;
	FD_ZERO(&readSet);
	FD_SET(socket, &readSet);
	timeval wait;
	wait.tv_sec = remaining / 1000000;
	wait.tv_usec = remaining % 1000000;
	return select(socket + 1, &readSet, NULL, NULL, &wait);
}


status_t
r5_ipv6_link_local(const char* device, in6_addr& out)
{
	BNetworkInterface interface(device);
	BNetworkInterfaceAddress slot;
	int32 first = interface.FindFirstAddress(AF_INET6);
	if (first >= 0 && interface.GetAddressAt(first, slot) == B_OK
		&& slot_link_local(slot, out))
		return B_OK;
	for (int32 i = 0; i < 32; i++) {
		if (interface.GetAddressAt(i, slot) != B_OK)
			continue;
		if (slot_link_local(slot, out))
			return B_OK;
	}
	return EADDRNOTAVAIL;
}


bool
r5_ipv6_has_address(const char* device, const in6_addr& address)
{
	BNetworkInterface interface(device);
	BNetworkAddress needle(address, 0);
	if (interface.FindAddress(needle) >= 0)
		return true;
	BNetworkInterfaceAddress slot;
	int32 first = interface.FindFirstAddress(AF_INET6);
	int32 start = first >= 0 ? first : 0;
	for (int32 i = start; i < start + 32; i++) {
		if (interface.GetAddressAt(i, slot) != B_OK)
			continue;
		if (slot.Address().Family() != AF_INET6)
			continue;
		const sockaddr_in6& in6
			= *reinterpret_cast<const sockaddr_in6*>(&slot.Address().SockAddr());
		if (memcmp(&in6.sin6_addr, &address, sizeof(in6_addr)) == 0)
			return true;
	}
	return false;
}


status_t
r5_ipv6_dad(const char* device, uint32 ifIndex, const uint8* /*mac*/,
	const in6_addr& tentative)
{
	int socket = ::socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (socket < 0) {
		status_t error = errno;
		syslog(LOG_ERR, "dad %s: socket: %s\n", device, strerror(error));
		return error;
	}

	status_t status = set_int_option(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		255);
	if (status != B_OK) {
		close(socket);
		syslog(LOG_ERR, "dad %s: IPV6_MULTICAST_HOPS: %s\n", device,
			strerror(status));
		return status;
	}

	// Haiku IPv6 has no unicast route for multicast. IPV6_MULTICAST_IF
	// then does get_route(local) and returns ENETUNREACH. SO_BINDTODEVICE
	// uses the interface direct_route instead (datalink_send_data).
	if (setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, &ifIndex,
			sizeof(ifIndex)) < 0) {
		status = errno;
		close(socket);
		syslog(LOG_ERR, "dad %s: SO_BINDTODEVICE: %s\n", device,
			strerror(status));
		return status;
	}

	sockaddr_in6 local;
	memset(&local, 0, sizeof(local));
	local.sin6_len = sizeof(local);
	local.sin6_family = AF_INET6;
	local.sin6_scope_id = ifIndex;
	if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
		status = errno;
		close(socket);
		syslog(LOG_ERR, "dad %s: bind :: : %s\n", device, strerror(status));
		return status;
	}

	in6_addr group;
	r5_ipv6_solicited_node(tentative, group);
	status = join_group(socket, ifIndex, group);
	if (status != B_OK) {
		close(socket);
		syslog(LOG_ERR, "dad %s: join solicited-node: %s\n", device,
			strerror(status));
		return status;
	}

	in6_addr allNodes;
	memset(&allNodes, 0, sizeof(allNodes));
	inet_pton(AF_INET6, "ff02::1", &allNodes);
	status = join_group(socket, ifIndex, allNodes);
	if (status != B_OK) {
		close(socket);
		syslog(LOG_ERR, "dad %s: join ff02::1: %s\n", device, strerror(status));
		return status;
	}

	uint8 packet[sizeof(struct nd_neighbor_solicit)];
	ssize_t packetLen = r5_ipv6_dad_build_ns(packet, sizeof(packet), tentative);
	if (packetLen < 0) {
		close(socket);
		return B_BAD_VALUE;
	}

	sockaddr_in6 dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin6_len = sizeof(dest);
	dest.sin6_family = AF_INET6;
	dest.sin6_scope_id = ifIndex;
	dest.sin6_addr = group;

	if (sendto(socket, packet, (size_t)packetLen, 0,
			reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
		status = errno;
		close(socket);
		syslog(LOG_ERR, "dad %s: NS send: %s\n", device, strerror(status));
		return status;
	}

	bigtime_t deadline = system_time() + kRetransTimer;
	bool duplicate = false;
	bool ownNsSeen = false;
	int ioError = 0;

	while (system_time() < deadline) {
		int ready = wait_readable(socket, deadline - system_time());
		if (ready == 0)
			break;
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			ioError = errno;
			syslog(LOG_ERR, "dad %s: select: %s\n", device, strerror(ioError));
			break;
		}

		uint8 incoming[1280];
		sockaddr_in6 from;
		socklen_t fromLen = sizeof(from);
		ssize_t bytes = recvfrom(socket, incoming, sizeof(incoming), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (bytes < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == ETIMEDOUT)
				continue;
			ioError = errno;
			syslog(LOG_ERR, "dad %s: recv: %s\n", device, strerror(ioError));
			break;
		}

		ssize_t icmpLen = 0;
		const uint8* icmp = r5_ipv6_dad_icmp(incoming, bytes, icmpLen);
		r5_ipv6_dad_event event = r5_ipv6_dad_event_for(icmp, icmpLen,
			tentative, packet, (size_t)packetLen, ownNsSeen);
		if (event == R5_IPV6_DAD_OWN_NS) {
			ownNsSeen = true;
			continue;
		}
		if (event == R5_IPV6_DAD_DUP_NA || event == R5_IPV6_DAD_DUP_NS) {
			duplicate = true;
			break;
		}
	}

	close(socket);

	char text[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &tentative, text, sizeof(text));
	if (duplicate) {
		syslog(LOG_ERR, "dad %s: duplicate %s\n", device, text);
		return B_NAME_IN_USE;
	}
	if (ioError != 0)
		return ioError;
	syslog(LOG_INFO, "dad %s: %s unique\n", device, text);
	return B_OK;
}
