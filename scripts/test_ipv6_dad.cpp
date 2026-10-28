/*
 * Host-side tests for RFC 4862 DAD packet helpers.
 * g++ -std=c++11 -I../distro/network -o /tmp/test_ipv6_dad test_ipv6_dad.cpp
 */
#include "IPv6DadPacket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int gFailed = 0;


static void
expect(bool ok, const char* name)
{
	if (!ok) {
		fprintf(stderr, "FAIL %s\n", name);
		gFailed++;
	} else
		printf("ok %s\n", name);
}


static in6_addr
addr(const char* text)
{
	in6_addr out;
	memset(&out, 0, sizeof(out));
	inet_pton(AF_INET6, text, &out);
	return out;
}


int
main()
{
	in6_addr tentative = addr("fdae:aeb4:a752:9ae4:a00:27ff:fe44:61dd");
	uint8_t ns[64];
	memset(ns, 0xaa, sizeof(ns));
	ssize_t nsLen = r5_ipv6_dad_build_ns(ns, sizeof(ns), tentative);
	expect(nsLen == (ssize_t)sizeof(struct nd_neighbor_solicit),
		"build_ns_length");
	expect(ns[0] == ND_NEIGHBOR_SOLICIT, "build_ns_type");
	expect(nsLen == 24, "build_ns_no_slla");
	expect(ns[24] == 0xaa, "build_ns_does_not_write_slla");

	in6_addr group;
	r5_ipv6_solicited_node(tentative, group);
	expect(group.s6_addr[0] == 0xff && group.s6_addr[1] == 0x02
		&& group.s6_addr[11] == 0x01 && group.s6_addr[12] == 0xff
		&& group.s6_addr[13] == tentative.s6_addr[13]
		&& group.s6_addr[15] == tentative.s6_addr[15],
		"solicited_node");

	uint8_t na[sizeof(struct nd_neighbor_advert)];
	memset(na, 0, sizeof(na));
	struct nd_neighbor_advert* advert
		= reinterpret_cast<struct nd_neighbor_advert*>(na);
	advert->nd_na_type = ND_NEIGHBOR_ADVERT;
	advert->nd_na_target = tentative;
	expect(r5_ipv6_dad_event_for(na, sizeof(na), tentative, ns, (size_t)nsLen,
		false) == R5_IPV6_DAD_DUP_NA, "na_conflict");

	in6_addr other = addr("fe80::1");
	advert->nd_na_target = other;
	expect(r5_ipv6_dad_event_for(na, sizeof(na), tentative, ns, (size_t)nsLen,
		false) == R5_IPV6_DAD_IGNORE, "na_other_target");

	expect(r5_ipv6_dad_event_for(ns, nsLen, tentative, ns, (size_t)nsLen,
		false) == R5_IPV6_DAD_OWN_NS, "own_loopback_ns");
	expect(r5_ipv6_dad_event_for(ns, nsLen, tentative, ns, (size_t)nsLen,
		true) == R5_IPV6_DAD_DUP_NS, "competing_dad_ns");

	uint8_t foreign[sizeof(struct nd_neighbor_solicit)];
	r5_ipv6_dad_build_ns(foreign, sizeof(foreign), tentative);
	foreign[7] = 0x01;
	expect(r5_ipv6_dad_event_for(foreign, sizeof(foreign), tentative, ns,
		(size_t)nsLen, false) == R5_IPV6_DAD_DUP_NS, "foreign_ns");

	uint8_t ra[16];
	memset(ra, 0, sizeof(ra));
	ra[0] = ND_ROUTER_ADVERT;
	expect(r5_ipv6_dad_event_for(ra, sizeof(ra), tentative, ns, (size_t)nsLen,
		false) == R5_IPV6_DAD_IGNORE, "ignore_ra");

	uint8_t ip6[40 + sizeof(struct nd_neighbor_advert)];
	memset(ip6, 0, sizeof(ip6));
	ip6[0] = 0x60;
	memcpy(ip6 + 40, na, sizeof(struct nd_neighbor_advert));
	reinterpret_cast<struct nd_neighbor_advert*>(ip6 + 40)->nd_na_target
		= tentative;
	reinterpret_cast<struct nd_neighbor_advert*>(ip6 + 40)->nd_na_type
		= ND_NEIGHBOR_ADVERT;
	ssize_t icmpLen = 0;
	const uint8_t* icmp = r5_ipv6_dad_icmp(ip6, sizeof(ip6), icmpLen);
	expect(icmp != NULL && icmpLen == (ssize_t)sizeof(struct nd_neighbor_advert),
		"strip_ipv6_header");
	expect(r5_ipv6_dad_event_for(icmp, icmpLen, tentative, ns, (size_t)nsLen,
		false) == R5_IPV6_DAD_DUP_NA, "na_after_ipv6_header");

	expect(r5_ipv6_dad_ok_to_assign(false, 0), "timeout_success");
	expect(!r5_ipv6_dad_ok_to_assign(true, 0), "na_conflict_blocks");
	expect(!r5_ipv6_dad_ok_to_assign(false, ESOCKTNOSUPPORT),
		"socket_creation_failure");
	expect(!r5_ipv6_dad_ok_to_assign(false, ENETUNREACH), "send_failure");
	expect(!r5_ipv6_dad_ok_to_assign(false, ECONNRESET), "receive_failure");

	expect(r5_ipv6_dad_build_ns(ns, 8, tentative) < 0, "build_ns_short_buffer");
	expect(r5_ipv6_dad_event_for(NULL, 0, tentative, ns, (size_t)nsLen, false)
		== R5_IPV6_DAD_IGNORE, "empty_recv");

	if (gFailed != 0) {
		fprintf(stderr, "%d failed\n", gFailed);
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
