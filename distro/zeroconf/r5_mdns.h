#ifndef R5_MDNS_H
#define R5_MDNS_H

#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

int r5_mdns_name_is_local(const char* hostname);
int r5_mdns_getaddrinfo(const char* hostname, const struct addrinfo* pai,
	struct addrinfo** res);

#ifdef __cplusplus
}
#endif

#endif
