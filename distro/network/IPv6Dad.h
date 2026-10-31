/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_IPV6_DAD_H
#define R5_IPV6_DAD_H


#include <netinet/in.h>
#include <SupportDefs.h>


status_t r5_ipv6_dad(const char* device, uint32 ifIndex, const uint8* mac,
	const in6_addr& tentative);
bool r5_ipv6_has_address(const char* device, const in6_addr& address);
status_t r5_ipv6_link_local(const char* device, in6_addr& out);


#endif	// R5_IPV6_DAD_H
