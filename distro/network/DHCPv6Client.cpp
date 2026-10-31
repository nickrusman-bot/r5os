/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * DHCPv6 (RFC 8415) for IA_NA and DNS (RFC 3646).
 */


#include "DHCPv6Client.h"

#include "IPv6Dad.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <Message.h>
#include <NetworkAddress.h>
#include <NetworkInterface.h>
#include <OS.h>
#include <String.h>

#include "NetServer.h"


static const uint16 kClientPort = 546;
static const uint16 kServerPort = 547;
static const int kMaxTries = 3;

static const uint8 kSolicit = 1;
static const uint8 kAdvertise = 2;
static const uint8 kRequest = 3;
static const uint8 kReply = 7;
static const uint8 kInformationRequest = 11;

static const uint16 kOptClientId = 1;
static const uint16 kOptServerId = 2;
static const uint16 kOptIaNa = 3;
static const uint16 kOptIaAddr = 5;
static const uint16 kOptOro = 6;
static const uint16 kOptElapsed = 8;
static const uint16 kOptDnsServers = 23;


static bool
append_option(uint8* out, size_t& used, size_t cap, uint16 code,
	const void* data, uint16 length)
{
	if (used + 4 + length > cap)
		return false;
	uint16 netCode = htons(code);
	uint16 netLength = htons(length);
	memcpy(out + used, &netCode, 2);
	memcpy(out + used + 2, &netLength, 2);
	if (length > 0 && data != NULL)
		memcpy(out + used + 4, data, length);
	used += 4 + length;
	return true;
}


DHCPv6Client::DHCPv6Client(BMessenger target, const char* device)
	:
	AutoconfigClient("dhcpv6", target, device),
	fThread(-1),
	fSocket(-1),
	fTransaction(0),
	fServerIdLength(0),
	fHaveMAC(false)
{
	memset(fMAC, 0, sizeof(fMAC));
	memset(fServerId, 0, sizeof(fServerId));

	BNetworkAddress link;
	BNetworkInterface interface(device);
	if (interface.GetHardwareAddress(link) == B_OK
		&& link.LinkLevelAddressLength() == 6) {
		memcpy(fMAC, link.LinkLevelAddress(), sizeof(fMAC));
		fHaveMAC = true;
	}
	fTransaction = ((uint32)system_time() ^ (uint32)find_thread(NULL))
		& 0x00ffffff;
}


static void
stop_worker(thread_id thread, int socket)
{
	if (socket >= 0)
		shutdown(socket, SHUT_RDWR);
	if (thread < 0)
		return;

	status_t ignored;
	wait_for_thread_etc(thread, B_RELATIVE_TIMEOUT, 500000, &ignored);
	kill_thread(thread);
	wait_for_thread_etc(thread, B_RELATIVE_TIMEOUT, 250000, &ignored);
}


DHCPv6Client::~DHCPv6Client()
{
	thread_id thread = fThread;
	int socket = fSocket;
	fThread = -1;
	fSocket = -1;
	if (thread != -1) {
		UnlockLooper();
		stop_worker(thread, socket);
		LockLooper();
	} else if (socket >= 0)
		shutdown(socket, SHUT_RDWR);
}


status_t
DHCPv6Client::Start()
{
	if (!fHaveMAC)
		return B_BAD_VALUE;
	if (fThread != -1)
		return EINPROGRESS;

	fThread = spawn_thread(_Thread, "DHCPv6", B_NORMAL_PRIORITY, this);
	return resume_thread(fThread);
}


status_t
DHCPv6Client::_Thread(void* data)
{
	DHCPv6Client* client = static_cast<DHCPv6Client*>(data);
	status_t status = client->_Run();
	client->fThread = -1;
	return status;
}


status_t
DHCPv6Client::_EnsureLinkLocal()
{
	in6_addr local;
	return r5_ipv6_link_local(Device(), local);
}


int
DHCPv6Client::_OpenSocket(uint32& ifIndex)
{
	BNetworkInterface interface(Device());
	ifIndex = interface.Index();
	if (ifIndex == 0)
		ifIndex = if_nametoindex(Device());

	int socket = ::socket(AF_INET6, SOCK_DGRAM, 0);
	if (socket < 0)
		return -1;

	int hops = 1;
	setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
	if (setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, &ifIndex,
			sizeof(ifIndex)) < 0) {
		syslog(LOG_ERR, "dhcpv6 %s: SO_BINDTODEVICE: %s\n", Device(),
			strerror(errno));
		close(socket);
		return -1;
	}

	sockaddr_in6 local;
	memset(&local, 0, sizeof(local));
	local.sin6_len = sizeof(local);
	local.sin6_family = AF_INET6;
	local.sin6_port = htons(kClientPort);
	local.sin6_scope_id = ifIndex;
	r5_ipv6_link_local(Device(), local.sin6_addr);

	if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
		syslog(LOG_ERR, "dhcpv6 %s: bind: %s\n", Device(), strerror(errno));
		close(socket);
		return -1;
	}

	ipv6_mreq membership;
	memset(&membership, 0, sizeof(membership));
	inet_pton(AF_INET6, "ff02::1:2", &membership.ipv6mr_multiaddr);
	membership.ipv6mr_interface = ifIndex;
	setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
		sizeof(membership));
	return socket;
}


ssize_t
DHCPv6Client::_BuildSolicit(uint8* out, size_t outSize, bool informationOnly)
{
	if (outSize < 4)
		return -1;
	out[0] = informationOnly ? kInformationRequest : kSolicit;
	out[1] = (fTransaction >> 16) & 0xff;
	out[2] = (fTransaction >> 8) & 0xff;
	out[3] = fTransaction & 0xff;
	size_t used = 4;

	uint8 duid[10];
	uint16 duidType = htons(3);
	uint16 hwType = htons(1);
	memcpy(duid, &duidType, 2);
	memcpy(duid + 2, &hwType, 2);
	memcpy(duid + 4, fMAC, 6);
	if (!append_option(out, used, outSize, kOptClientId, duid, sizeof(duid)))
		return -1;

	uint16 elapsed = 0;
	if (!append_option(out, used, outSize, kOptElapsed, &elapsed, 2))
		return -1;

	uint16 dns = htons(kOptDnsServers);
	if (!append_option(out, used, outSize, kOptOro, &dns, 2))
		return -1;

	if (!informationOnly) {
		uint8 ia[12];
		memset(ia, 0, sizeof(ia));
		memcpy(ia, fMAC + 2, 4);
		if (!append_option(out, used, outSize, kOptIaNa, ia, sizeof(ia)))
			return -1;
	}

	if (fServerIdLength > 0
		&& !append_option(out, used, outSize, kOptServerId, fServerId,
			fServerIdLength))
		return -1;

	if (fServerIdLength > 0 && !informationOnly)
		out[0] = kRequest;

	return (ssize_t)used;
}


status_t
DHCPv6Client::_Send(int socket, uint32 ifIndex, const uint8* packet,
	size_t length)
{
	sockaddr_in6 dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin6_len = sizeof(dest);
	dest.sin6_family = AF_INET6;
	dest.sin6_port = htons(kServerPort);
	dest.sin6_scope_id = ifIndex;
	inet_pton(AF_INET6, "ff02::1:2", &dest.sin6_addr);

	if (sendto(socket, packet, length, 0, reinterpret_cast<sockaddr*>(&dest),
			sizeof(dest)) < 0) {
		syslog(LOG_ERR, "dhcpv6 %s: send: %s\n", Device(), strerror(errno));
		return errno;
	}
	return B_OK;
}


status_t
DHCPv6Client::_HandleReply(const uint8* packet, ssize_t bytes, uint32 ifIndex)
{
	if (bytes < 4)
		return B_BAD_VALUE;
	uint8 type = packet[0];
	uint32 xid = ((uint32)packet[1] << 16) | ((uint32)packet[2] << 8) | packet[3];
	if (xid != fTransaction)
		return B_BAD_VALUE;
	if (type != kAdvertise && type != kReply)
		return B_BAD_VALUE;

	const uint8* option = packet + 4;
	const uint8* end = packet + bytes;
	in6_addr leased;
	bool haveAddress = false;
	BMessage resolver(kMsgConfigureResolver);
	int32 nameservers = 0;

	while (option + 4 <= end) {
		uint16 code;
		uint16 length;
		memcpy(&code, option, 2);
		memcpy(&length, option + 2, 2);
		code = ntohs(code);
		length = ntohs(length);
		const uint8* data = option + 4;
		if (data + length > end)
			break;

		if (code == kOptServerId && length <= sizeof(fServerId)) {
			memcpy(fServerId, data, length);
			fServerIdLength = length;
		} else if (code == kOptIaNa && length >= 12) {
			const uint8* inner = data + 12;
			const uint8* innerEnd = data + length;
			while (inner + 4 <= innerEnd) {
				uint16 innerCode;
				uint16 innerLength;
				memcpy(&innerCode, inner, 2);
				memcpy(&innerLength, inner + 2, 2);
				innerCode = ntohs(innerCode);
				innerLength = ntohs(innerLength);
				if (inner + 4 + innerLength > innerEnd)
					break;
				if (innerCode == kOptIaAddr && innerLength >= 24) {
					memcpy(&leased, inner + 4, 16);
					haveAddress = true;
				}
				inner += 4 + innerLength;
			}
		} else if (code == kOptDnsServers && length >= 16) {
			for (uint16 offset = 0; offset + 16 <= length; offset += 16) {
				in6_addr dns;
				memcpy(&dns, data + offset, 16);
				char text[INET6_ADDRSTRLEN];
				if (inet_ntop(AF_INET6, &dns, text, sizeof(text)) != NULL) {
					resolver.AddString("nameserver", text);
					nameservers++;
				}
			}
		}

		option += 4 + length;
	}

	if (type == kAdvertise)
		return 1;

	if (haveAddress) {
		char text[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &leased, text, sizeof(text));
		if (r5_ipv6_has_address(Device(), leased)) {
			syslog(LOG_INFO, "dhcpv6 %s: %s already assigned\n", Device(),
				text);
		} else {
			status_t dad = r5_ipv6_dad(Device(), ifIndex, fMAC, leased);
			if (dad != B_OK)
				return dad;

			BNetworkInterface interface(Device());
			BNetworkAddress address(leased, 0);
			BNetworkAddress mask(AF_INET6, "ffff:ffff:ffff:ffff::", (uint16)0,
				B_UNCONFIGURED_ADDRESS_FAMILIES);
			BNetworkInterfaceAddress slot;
			slot.SetAddress(address);
			slot.SetMask(mask);
			status_t status = interface.AddAddress(slot);
			if (status != B_OK) {
				syslog(LOG_ERR, "dhcpv6 %s: add address failed: %s\n",
					Device(), strerror(status));
				return status;
			}
			syslog(LOG_INFO, "dhcpv6 %s: %s\n", Device(), text);
		}
	}

	if (nameservers > 0) {
		resolver.AddString("device", Device());
		resolver.AddInt32("nameserver_count", nameservers);
		Target().SendMessage(&resolver);
	}

	if (!haveAddress && nameservers == 0)
		return B_BAD_VALUE;
	return B_OK;
}


status_t
DHCPv6Client::_Run()
{
	for (int32 i = 0; i < 15 && fThread != -1; i++) {
		if (_EnsureLinkLocal() == B_OK)
			break;
		snooze(200000);
	}
	if (_EnsureLinkLocal() != B_OK) {
		syslog(LOG_ERR, "dhcpv6 %s: waiting for link-local timed out\n",
			Device());
		return B_TIMED_OUT;
	}

	uint32 ifIndex = 0;
	int socket = _OpenSocket(ifIndex);
	if (socket < 0)
		return errno;
	fSocket = socket;

	bool informationOnly = false;
	status_t status = B_TIMED_OUT;

	for (int32 tries = 0; tries < kMaxTries && fThread != -1; tries++) {
		uint8 packet[768];
		ssize_t length = _BuildSolicit(packet, sizeof(packet), informationOnly);
		if (length < 0)
			break;
		if (_Send(socket, ifIndex, packet, (size_t)length) != B_OK)
			continue;

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socket, &readSet);
		timeval wait;
		wait.tv_sec = 2;
		wait.tv_usec = 0;
		if (select(socket + 1, &readSet, NULL, NULL, &wait) <= 0)
			continue;

		uint8 incoming[1280];
		sockaddr_in6 from;
		socklen_t fromLen = sizeof(from);
		ssize_t bytes = recvfrom(socket, incoming, sizeof(incoming), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (bytes < 0)
			continue;

		status = _HandleReply(incoming, bytes, ifIndex);
		if (status == 1) {
			length = _BuildSolicit(packet, sizeof(packet), false);
			if (length > 0)
				_Send(socket, ifIndex, packet, (size_t)length);
			FD_ZERO(&readSet);
			FD_SET(socket, &readSet);
			wait.tv_sec = 2;
			wait.tv_usec = 0;
			if (select(socket + 1, &readSet, NULL, NULL, &wait) <= 0)
				continue;
			bytes = recvfrom(socket, incoming, sizeof(incoming), 0,
				reinterpret_cast<sockaddr*>(&from), &fromLen);
			if (bytes > 0)
				status = _HandleReply(incoming, bytes, ifIndex);
		}
		if (status == B_OK)
			break;
	}

	if (status != B_OK && !informationOnly) {
		informationOnly = true;
		uint8 packet[768];
		ssize_t length = _BuildSolicit(packet, sizeof(packet), true);
		if (length > 0 && _Send(socket, ifIndex, packet, (size_t)length) == B_OK) {
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(socket, &readSet);
			timeval wait;
			wait.tv_sec = 2;
			wait.tv_usec = 0;
			if (select(socket + 1, &readSet, NULL, NULL, &wait) > 0) {
				uint8 incoming[1280];
				sockaddr_in6 from;
				socklen_t fromLen = sizeof(from);
				ssize_t bytes = recvfrom(socket, incoming, sizeof(incoming), 0,
					reinterpret_cast<sockaddr*>(&from), &fromLen);
				if (bytes > 0)
					status = _HandleReply(incoming, bytes, ifIndex);
			}
		}
	}

	fSocket = -1;
	close(socket);
	if (status != B_OK)
		syslog(LOG_INFO, "dhcpv6 %s: no server\n", Device());
	return status;
}
