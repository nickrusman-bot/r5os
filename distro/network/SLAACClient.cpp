/*
 * IPv6 SLAAC for net_server (RFC 4861/4862) with DAD (Haiku #8319).
 */


#include "SLAACClient.h"
#include "IPv6Dad.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/icmp6.h>
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


static const int kMaxSolicits = 3;
static const bigtime_t kSolicitInterval = 4000000;
static const bigtime_t kRetryInterval = 60000000;
static const uint8 kRdnsOption = 25;


SLAACClient::SLAACClient(BMessenger target, const char* device)
	:
	AutoconfigClient("slaac", target, device),
	fThread(-1),
	fSocket(-1),
	fHaveMAC(false)
{
	memset(fMAC, 0, sizeof(fMAC));

	BNetworkAddress link;
	BNetworkInterface interface(device);
	if (interface.GetHardwareAddress(link) == B_OK
		&& link.LinkLevelAddressLength() == 6) {
		memcpy(fMAC, link.LinkLevelAddress(), sizeof(fMAC));
		fHaveMAC = true;
	}
}


SLAACClient::~SLAACClient()
{
	thread_id thread = fThread;
	fThread = -1;
	if (fSocket >= 0)
		shutdown(fSocket, SHUT_RDWR);
	if (thread != -1) {
		UnlockLooper();
		wait_for_thread(thread, NULL);
		LockLooper();
	}
}


status_t
SLAACClient::Start()
{
	if (!fHaveMAC)
		return B_BAD_VALUE;
	if (fThread != -1)
		return EINPROGRESS;

	fThread = spawn_thread(_Thread, "SLAAC", B_NORMAL_PRIORITY, this);
	return resume_thread(fThread);
}


status_t
SLAACClient::_Thread(void* data)
{
	SLAACClient* client = static_cast<SLAACClient*>(data);
	status_t status = client->_Run();
	client->fThread = -1;
	return status;
}


status_t
SLAACClient::_EnsureLinkLocal(BNetworkInterface& interface)
{
	in6_addr raw;
	memset(&raw, 0, sizeof(raw));
	raw.s6_addr[0] = 0xfe;
	raw.s6_addr[1] = 0x80;
	raw.s6_addr[8] = fMAC[0] ^ 0x02;
	raw.s6_addr[9] = fMAC[1];
	raw.s6_addr[10] = fMAC[2];
	raw.s6_addr[11] = 0xff;
	raw.s6_addr[12] = 0xfe;
	raw.s6_addr[13] = fMAC[3];
	raw.s6_addr[14] = fMAC[4];
	raw.s6_addr[15] = fMAC[5];

	in6_addr existing;
	if (r5_ipv6_link_local(Device(), existing) == B_OK)
		return B_OK;

	BNetworkAddress local(raw, 0);
	BNetworkAddress mask(AF_INET6, "ffff:ffff:ffff:ffff::", (uint16)0,
		B_UNCONFIGURED_ADDRESS_FAMILIES);
	BNetworkInterfaceAddress address;
	address.SetAddress(local);
	address.SetMask(mask);
	return interface.AddAddress(address);
}


void
SLAACClient::_BuildSlaacAddress(const in6_addr& prefix, in6_addr& out) const
{
	out = prefix;
	out.s6_addr[8] = fMAC[0] ^ 0x02;
	out.s6_addr[9] = fMAC[1];
	out.s6_addr[10] = fMAC[2];
	out.s6_addr[11] = 0xff;
	out.s6_addr[12] = 0xfe;
	out.s6_addr[13] = fMAC[3];
	out.s6_addr[14] = fMAC[4];
	out.s6_addr[15] = fMAC[5];
}


BString
SLAACClient::_ToString(const in6_addr& address) const
{
	char text[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &address, text, sizeof(text)) == NULL)
		return BString();
	return BString(text);
}


status_t
SLAACClient::_BindAndJoin(int socket, uint32 ifIndex)
{
	int hops = 255;
	setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
	setsockopt(socket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hops, sizeof(hops));
	in6_addr multicastIf;
	if (r5_ipv6_link_local(Device(), multicastIf) == B_OK) {
		setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_IF, &multicastIf,
			sizeof(multicastIf));
	}

	BNetworkInterface interface(Device());
	sockaddr_in6 local;
	memset(&local, 0, sizeof(local));
	local.sin6_len = sizeof(local);
	local.sin6_family = AF_INET6;
	local.sin6_scope_id = ifIndex;

	BNetworkInterfaceAddress slot;
	int32 count = interface.CountAddresses();
	for (int32 i = 0; i < count; i++) {
		if (interface.GetAddressAt(i, slot) != B_OK)
			continue;
		const BNetworkAddress& address = slot.Address();
		if (address.Family() != AF_INET6)
			continue;
		const sockaddr_in6& in6
			= *reinterpret_cast<const sockaddr_in6*>(&address.SockAddr());
		if (!IN6_IS_ADDR_LINKLOCAL(&in6.sin6_addr))
			continue;
		local.sin6_addr = in6.sin6_addr;
		break;
	}

	if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
		syslog(LOG_ERR, "slaac %s: bind failed: %s\n", Device(),
			strerror(errno));
	}

	ipv6_mreq membership;
	memset(&membership, 0, sizeof(membership));
	inet_pton(AF_INET6, "ff02::1", &membership.ipv6mr_multiaddr);
	membership.ipv6mr_interface = ifIndex;
	if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
			sizeof(membership)) < 0) {
		syslog(LOG_ERR, "slaac %s: join ff02::1 failed: %s (index %" B_PRIu32
			")\n", Device(), strerror(errno), ifIndex);
		return errno;
	}
	return B_OK;
}


status_t
SLAACClient::_SendSolicit(int socket, uint32 ifIndex)
{
	uint8 packet[16];
	memset(packet, 0, sizeof(packet));
	struct nd_router_solicit* rs
		= reinterpret_cast<struct nd_router_solicit*>(packet);
	rs->nd_rs_type = ND_ROUTER_SOLICIT;
	rs->nd_rs_code = 0;
	rs->nd_rs_cksum = 0;
	packet[8] = ND_OPT_SOURCE_LINKADDR;
	packet[9] = 1;
	memcpy(packet + 10, fMAC, 6);

	sockaddr_in6 dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin6_len = sizeof(dest);
	dest.sin6_family = AF_INET6;
	dest.sin6_scope_id = ifIndex;
	inet_pton(AF_INET6, "ff02::2", &dest.sin6_addr);

	if (sendto(socket, packet, sizeof(packet), 0,
			reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
		syslog(LOG_ERR, "slaac %s: RS send failed: %s\n", Device(),
			strerror(errno));
		return errno;
	}
	syslog(LOG_INFO, "slaac %s: router solicitation sent\n", Device());
	return B_OK;
}


status_t
SLAACClient::_ApplyAdvert(const uint8* buffer, ssize_t bytes,
	const sockaddr_in6& from)
{
	if (bytes < (ssize_t)sizeof(struct nd_router_advert))
		return B_BAD_VALUE;

	const uint8* data = buffer;
	ssize_t length = bytes;
	if (length >= 40 && (data[0] >> 4) == 6) {
		data += 40;
		length -= 40;
	}
	if (length < (ssize_t)sizeof(struct nd_router_advert))
		return B_BAD_VALUE;

	const struct nd_router_advert* ra
		= reinterpret_cast<const struct nd_router_advert*>(data);
	if (ra->nd_ra_type != ND_ROUTER_ADVERT || ra->nd_ra_code != 0)
		return B_BAD_VALUE;

	uint16 lifetime = ntohs(ra->nd_ra_router_lifetime);
	const uint8* option = data + sizeof(struct nd_router_advert);
	const uint8* end = data + length;

	in6_addr prefix;
	bool havePrefix = false;
	BMessage resolver(kMsgConfigureResolver);
	int32 nameservers = 0;
	uint8 raFlags = ra->nd_ra_flags_reserved;
	bool managed = (raFlags & ND_RA_FLAG_MANAGED) != 0;
	bool other = (raFlags & ND_RA_FLAG_OTHER) != 0;

	while (option + 2 <= end) {
		uint8 type = option[0];
		uint8 optLen = option[1];
		if (optLen == 0)
			break;
		size_t bytesLen = optLen * 8;
		if (option + bytesLen > end)
			break;

		if (type == ND_OPT_PREFIX_INFORMATION
			&& bytesLen >= sizeof(struct nd_opt_prefix_info)) {
			const struct nd_opt_prefix_info* info
				= reinterpret_cast<const struct nd_opt_prefix_info*>(option);
			char prefixText[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, &info->nd_opt_pi_prefix, prefixText,
				sizeof(prefixText));
			syslog(LOG_INFO, "slaac %s: PIO %s/%u A=%d L=%d\n", Device(),
				prefixText, info->nd_opt_pi_prefix_len,
				(info->nd_opt_pi_flags_reserved & ND_OPT_PI_FLAG_AUTO) != 0,
				(info->nd_opt_pi_flags_reserved & ND_OPT_PI_FLAG_ONLINK) != 0);
			if ((info->nd_opt_pi_flags_reserved & ND_OPT_PI_FLAG_AUTO) != 0
				&& info->nd_opt_pi_prefix_len == 64
				&& !IN6_IS_ADDR_LINKLOCAL(&info->nd_opt_pi_prefix)) {
				prefix = info->nd_opt_pi_prefix;
				havePrefix = true;
			}
		} else if (type == kRdnsOption && bytesLen >= 24) {
			const uint8* addr = option + 8;
			const uint8* addrEnd = option + bytesLen;
			while (addr + 16 <= addrEnd) {
				in6_addr dns;
				memcpy(&dns, addr, 16);
				BString text = _ToString(dns);
				if (text.Length() > 0) {
					resolver.AddString("nameserver", text.String());
					nameservers++;
				}
				addr += 16;
			}
		}

		option += bytesLen;
	}

	if (lifetime > 0) {
		BNetworkInterface routeInterface(Device());
		BNetworkAddress gateway(from.sin6_addr, 0);
		routeInterface.RemoveDefaultRoute(AF_INET6);
		status_t routeStatus = routeInterface.AddDefaultRoute(gateway);
		if (routeStatus != B_OK) {
			syslog(LOG_ERR, "slaac %s: default route failed: %s\n", Device(),
				strerror(routeStatus));
		}
	}

	if (!havePrefix) {
		syslog(LOG_INFO, "slaac %s: RA from %s M=%d O=%d without SLAAC /64 "
			"prefix (DHCPv6 %s)\n", Device(),
			_ToString(from.sin6_addr).String(), managed, other,
			(managed || other) ? "should run" : "may be absent");
		if (nameservers > 0) {
			resolver.AddString("device", Device());
			resolver.AddInt32("nameserver_count", nameservers);
			BMessage reply;
			Target().SendMessage(&resolver, &reply);
		}
		return B_BAD_VALUE;
	}

	in6_addr global;
	_BuildSlaacAddress(prefix, global);
	BString globalText = _ToString(global);
	if (globalText.Length() == 0)
		return B_ERROR;

	if (r5_ipv6_has_address(Device(), global)) {
		syslog(LOG_INFO, "slaac %s: %s already assigned\n", Device(),
			globalText.String());
		return B_OK;
	}

	BNetworkInterface interface(Device());
	BNetworkAddress address(global, 0);
	uint32 ifIndex = interface.Index();
	if (ifIndex == 0)
		ifIndex = if_nametoindex(Device());
	status_t dad = r5_ipv6_dad(Device(), ifIndex, fMAC, global);
	if (dad != B_OK)
		return dad;

	BNetworkAddress mask(AF_INET6, "ffff:ffff:ffff:ffff::", (uint16)0,
		B_UNCONFIGURED_ADDRESS_FAMILIES);
	BNetworkInterfaceAddress slot;
	slot.SetAddress(address);
	slot.SetMask(mask);
	status_t status = interface.AddAddress(slot);
	if (status != B_OK) {
		syslog(LOG_ERR, "slaac %s: add %s failed: %s\n", Device(),
			globalText.String(), strerror(status));
		return status;
	}

	if (nameservers > 0) {
		resolver.AddString("device", Device());
		resolver.AddInt32("nameserver_count", nameservers);
		BMessage reply;
		Target().SendMessage(&resolver, &reply);
	}

	syslog(LOG_INFO, "slaac %s: %s via %s\n", Device(), globalText.String(),
		_ToString(from.sin6_addr).String());
	return B_OK;
}


status_t
SLAACClient::_Run()
{
	BNetworkInterface interface(Device());
	status_t status = B_ERROR;
	for (int32 i = 0; i < 10; i++) {
		status = _EnsureLinkLocal(interface);
		if (status == B_OK)
			break;
		snooze(500000);
	}
	if (status != B_OK) {
		syslog(LOG_ERR, "slaac %s: no link-local: %s\n", Device(),
			strerror(status));
		return status;
	}

	int socket = ::socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (socket < 0) {
		syslog(LOG_ERR, "slaac %s: raw socket failed: %s\n", Device(),
			strerror(errno));
		return errno;
	}
	fSocket = socket;

	uint32 ifIndex = interface.Index();
	if (ifIndex == 0)
		ifIndex = if_nametoindex(Device());
	syslog(LOG_INFO, "slaac %s: listening, ifindex %" B_PRIu32 "\n", Device(),
		ifIndex);

	_BindAndJoin(socket, ifIndex);

	int32 solicits = 0;
	bigtime_t nextSolicit = 0;
	bool configured = false;

	while (fThread != -1) {
		bigtime_t now = system_time();
		if (now >= nextSolicit && (!configured || solicits < kMaxSolicits)) {
			_SendSolicit(socket, ifIndex);
			solicits++;
			if (solicits < kMaxSolicits)
				nextSolicit = now + kSolicitInterval;
			else
				nextSolicit = now + kRetryInterval;
		}

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socket, &readSet);
		timeval wait;
		wait.tv_sec = 1;
		wait.tv_usec = 0;
		int ready = select(socket + 1, &readSet, NULL, NULL, &wait);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EBADF || fThread == -1)
				break;
			syslog(LOG_ERR, "slaac %s: select: %s\n", Device(),
				strerror(errno));
			break;
		}
		if (ready == 0)
			continue;

		uint8 packet[1280];
		sockaddr_in6 from;
		socklen_t fromLen = sizeof(from);
		ssize_t bytes = recvfrom(socket, packet, sizeof(packet), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (bytes < 0) {
			if (errno != EAGAIN && errno != EINTR && errno != ETIMEDOUT)
				syslog(LOG_ERR, "slaac %s: recv: %s\n", Device(),
					strerror(errno));
			continue;
		}

		status = _ApplyAdvert(packet, bytes, from);
		if (status == B_OK) {
			configured = true;
			solicits = kMaxSolicits;
			nextSolicit = now + kRetryInterval;
		}
	}

	fSocket = -1;
	close(socket);
	return configured ? B_OK : B_TIMED_OUT;
}
