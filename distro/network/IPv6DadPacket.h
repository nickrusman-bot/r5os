/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 *
 * RFC 4861 / 4862 helpers for DAD Neighbor Solicitation (header-only).
 */
#ifndef R5_IPV6_DAD_PACKET_H
#define R5_IPV6_DAD_PACKET_H


#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>


enum r5_ipv6_dad_event {
	R5_IPV6_DAD_IGNORE = 0,
	R5_IPV6_DAD_OWN_NS,
	R5_IPV6_DAD_DUP_NA,
	R5_IPV6_DAD_DUP_NS
};


static inline void
r5_ipv6_solicited_node(const in6_addr& address, in6_addr& out)
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


static inline ssize_t
r5_ipv6_dad_build_ns(uint8_t* out, size_t cap, const in6_addr& tentative)
{
	const ssize_t length = (ssize_t)sizeof(struct nd_neighbor_solicit);
	if (out == NULL || cap < (size_t)length)
		return -1;
	memset(out, 0, (size_t)length);
	struct nd_neighbor_solicit* ns
		= reinterpret_cast<struct nd_neighbor_solicit*>(out);
	ns->nd_ns_type = ND_NEIGHBOR_SOLICIT;
	ns->nd_ns_code = 0;
	ns->nd_ns_target = tentative;
	return length;
}


static inline const uint8_t*
r5_ipv6_dad_icmp(const uint8_t* data, ssize_t length, ssize_t& icmpLen)
{
	icmpLen = 0;
	if (data == NULL || length <= 0)
		return NULL;
	if (length >= 40 && (data[0] >> 4) == 6) {
		data += 40;
		length -= 40;
	}
	if (length < (ssize_t)sizeof(struct nd_neighbor_solicit))
		return NULL;
	icmpLen = length;
	return data;
}


static inline r5_ipv6_dad_event
r5_ipv6_dad_event_for(const uint8_t* icmp, ssize_t icmpLen,
	const in6_addr& tentative, const uint8_t* ownNs, size_t ownNsLen,
	bool ownNsSeen)
{
	if (icmp == NULL || icmpLen < (ssize_t)sizeof(struct nd_neighbor_solicit))
		return R5_IPV6_DAD_IGNORE;

	uint8_t type = icmp[0];
	if (type == ND_NEIGHBOR_ADVERT) {
		if (icmpLen < (ssize_t)sizeof(struct nd_neighbor_advert))
			return R5_IPV6_DAD_IGNORE;
		const struct nd_neighbor_advert* na
			= reinterpret_cast<const struct nd_neighbor_advert*>(icmp);
		if (memcmp(&na->nd_na_target, &tentative, sizeof(in6_addr)) == 0)
			return R5_IPV6_DAD_DUP_NA;
		return R5_IPV6_DAD_IGNORE;
	}

	if (type != ND_NEIGHBOR_SOLICIT)
		return R5_IPV6_DAD_IGNORE;

	const struct nd_neighbor_solicit* ns
		= reinterpret_cast<const struct nd_neighbor_solicit*>(icmp);
	if (memcmp(&ns->nd_ns_target, &tentative, sizeof(in6_addr)) != 0)
		return R5_IPV6_DAD_IGNORE;

	if (ownNs != NULL && ownNsLen > 0
		&& (size_t)icmpLen >= ownNsLen
		&& memcmp(icmp, ownNs, ownNsLen) == 0 && !ownNsSeen)
		return R5_IPV6_DAD_OWN_NS;

	return R5_IPV6_DAD_DUP_NS;
}


static inline bool
r5_ipv6_dad_ok_to_assign(bool duplicate, int ioError)
{
	if (duplicate)
		return false;
	if (ioError == 0)
		return true;
	return false;
}


#endif	// R5_IPV6_DAD_PACKET_H
