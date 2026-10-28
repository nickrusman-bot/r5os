/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 */
#ifndef R5_IPV6_DAD_H
#define R5_IPV6_DAD_H


#include <netinet/in.h>
#include <SupportDefs.h>


status_t r5_ipv6_dad(const char* device, uint32 ifIndex, const uint8* mac,
	const in6_addr& tentative);


#endif	// R5_IPV6_DAD_H
