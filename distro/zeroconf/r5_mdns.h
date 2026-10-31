/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_MDNS_H
#define R5_MDNS_H

#include <netdb.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R5_MDNS_MAX_SERVICES	32

typedef struct r5_mdns_service {
	char			name[64];
	char			type[64];
	char			host[256];
	char			uri[256];
	char			txt[512];
	char			pdl[128];
	uint16_t		port;
	struct in_addr	addr;
} r5_mdns_service;

int r5_mdns_name_is_local(const char* hostname);
int r5_mdns_getaddrinfo(const char* hostname, const struct addrinfo* pai,
	struct addrinfo** res);
int r5_mdns_browse(const char* serviceType, r5_mdns_service* out, int max,
	int timeoutMs);

#ifdef __cplusplus
}
#endif

#endif
