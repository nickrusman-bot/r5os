/* R5OS: one-shot IPv4 mDNS lookup for .local names (Haiku #1042). */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "r5_mdns.h"

#define MDNS_PORT		5353
#define MDNS_ADDR		"224.0.0.251"
#define MDNS_TIMEOUT_MS	800
#define MDNS_MAX_ADDRS	8
#define DNS_TYPE_A		1
#define DNS_CLASS_IN	1


static int
ends_with_local(const char* name)
{
	size_t len;
	const char* tail;

	if (name == NULL)
		return 0;
	len = strlen(name);
	if (len > 0 && name[len - 1] == '.')
		len--;
	if (len < 6)
		return 0;
	tail = name + len - 6;
	if (tail[0] != '.')
		return 0;
	return (strncasecmp(tail + 1, "local", 5) == 0);
}


int
r5_mdns_name_is_local(const char* hostname)
{
	return ends_with_local(hostname);
}


static size_t
put_dns_name(uint8_t* out, size_t cap, size_t off, const char* name)
{
	char copy[256];
	char* label;
	char* rest;
	size_t n;

	n = strlen(name);
	if (n >= sizeof(copy))
		return 0;
	memcpy(copy, name, n + 1);
	if (n > 0 && copy[n - 1] == '.')
		copy[n - 1] = '\0';

	rest = copy;
	while (rest != NULL && *rest != '\0') {
		label = rest;
		rest = strchr(rest, '.');
		if (rest != NULL) {
			*rest = '\0';
			rest++;
		}
		n = strlen(label);
		if (n == 0 || n > 63 || off + n + 1 >= cap)
			return 0;
		out[off++] = (uint8_t)n;
		memcpy(out + off, label, n);
		off += n;
	}
	if (off + 1 >= cap)
		return 0;
	out[off++] = 0;
	return off;
}


static int
skip_dns_name(const uint8_t* msg, size_t len, size_t* off)
{
	int jumps = 0;
	size_t pos = *off;
	int consumed = 0;

	while (pos < len) {
		uint8_t lab = msg[pos];
		if (lab == 0) {
			if (!consumed)
				*off = pos + 1;
			return 0;
		}
		if ((lab & 0xc0) == 0xc0) {
			if (pos + 1 >= len)
				return -1;
			if (!consumed)
				*off = pos + 2;
			pos = ((size_t)(lab & 0x3f) << 8) | msg[pos + 1];
			if (++jumps > 16)
				return -1;
			consumed = 1;
			continue;
		}
		if ((lab & 0xc0) != 0 || pos + 1 + lab >= len)
			return -1;
		pos += 1 + lab;
		if (!consumed)
			*off = pos;
	}
	return -1;
}


static int
read_u16(const uint8_t* msg, size_t len, size_t* off, uint16_t* value)
{
	if (*off + 2 > len)
		return -1;
	*value = (uint16_t)((msg[*off] << 8) | msg[*off + 1]);
	*off += 2;
	return 0;
}


static size_t
build_query(uint8_t* out, size_t cap, const char* name)
{
	size_t off = 12;

	if (cap < 12)
		return 0;
	memset(out, 0, 12);
	out[5] = 1; /* qdcount = 1 */
	off = put_dns_name(out, cap, off, name);
	if (off == 0 || off + 4 > cap)
		return 0;
	out[off++] = 0;
	out[off++] = DNS_TYPE_A;
	out[off++] = 0;
	out[off++] = DNS_CLASS_IN;
	return off;
}


static int
parse_a_records(const uint8_t* msg, size_t len, struct in_addr* addrs, int max)
{
	size_t off = 12;
	uint16_t qd, an, i;
	int count = 0;

	if (len < 12)
		return 0;
	qd = (uint16_t)((msg[4] << 8) | msg[5]);
	an = (uint16_t)((msg[6] << 8) | msg[7]);

	for (i = 0; i < qd; i++) {
		uint16_t type, classValue;
		if (skip_dns_name(msg, len, &off) != 0)
			return count;
		if (read_u16(msg, len, &off, &type) != 0
			|| read_u16(msg, len, &off, &classValue) != 0)
			return count;
	}

	for (i = 0; i < an && count < max; i++) {
		uint16_t type, classValue, rdlength;
		uint32_t ttl;

		if (skip_dns_name(msg, len, &off) != 0)
			return count;
		if (read_u16(msg, len, &off, &type) != 0
			|| read_u16(msg, len, &off, &classValue) != 0)
			return count;
		if (off + 4 > len)
			return count;
		ttl = ((uint32_t)msg[off] << 24) | ((uint32_t)msg[off + 1] << 16)
			| ((uint32_t)msg[off + 2] << 8) | msg[off + 3];
		(void)ttl;
		off += 4;
		if (read_u16(msg, len, &off, &rdlength) != 0)
			return count;
		if (off + rdlength > len)
			return count;
		if (type == DNS_TYPE_A && rdlength == 4) {
			memcpy(&addrs[count], msg + off, 4);
			count++;
		}
		off += rdlength;
	}
	return count;
}


static struct addrinfo*
make_ai(const struct addrinfo* pai, struct in_addr addr)
{
	struct addrinfo* ai;
	struct sockaddr_in* sin;

	ai = (struct addrinfo*)calloc(1, sizeof(*ai) + sizeof(*sin));
	if (ai == NULL)
		return NULL;
	sin = (struct sockaddr_in*)(ai + 1);
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	sin->sin_addr = addr;

	if (pai != NULL) {
		ai->ai_flags = pai->ai_flags;
		ai->ai_socktype = pai->ai_socktype;
		ai->ai_protocol = pai->ai_protocol;
	}
	ai->ai_family = AF_INET;
	ai->ai_addrlen = sizeof(*sin);
	ai->ai_addr = (struct sockaddr*)sin;
	return ai;
}


int
r5_mdns_getaddrinfo(const char* hostname, const struct addrinfo* pai,
	struct addrinfo** res)
{
	uint8_t query[512];
	uint8_t reply[1500];
	struct sockaddr_in dest;
	struct addrinfo* head = NULL;
	struct addrinfo* tail = NULL;
	struct in_addr found[MDNS_MAX_ADDRS];
	struct pollfd pfd;
	size_t qlen;
	int sock;
	int nfound = 0;
	int i;

	if (res == NULL)
		return EAI_FAIL;
	*res = NULL;
	if (hostname == NULL || !ends_with_local(hostname))
		return EAI_NONAME;
	if (pai != NULL && pai->ai_family != AF_UNSPEC && pai->ai_family != AF_INET)
		return EAI_FAMILY;

	qlen = build_query(query, sizeof(query), hostname);
	if (qlen == 0)
		return EAI_FAIL;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		return EAI_SYSTEM;

	memset(&dest, 0, sizeof(dest));
	dest.sin_len = sizeof(dest);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(MDNS_PORT);
	if (inet_pton(AF_INET, MDNS_ADDR, &dest.sin_addr) != 1) {
		close(sock);
		return EAI_SYSTEM;
	}

	if (sendto(sock, query, qlen, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
		close(sock);
		return EAI_SYSTEM;
	}

	pfd.fd = sock;
	pfd.events = POLLIN;
	while (nfound < MDNS_MAX_ADDRS) {
		int n;
		ssize_t got;

		n = poll(&pfd, 1, nfound == 0 ? MDNS_TIMEOUT_MS : 150);
		if (n <= 0)
			break;
		got = recv(sock, reply, sizeof(reply), 0);
		if (got <= 0)
			break;
		nfound += parse_a_records(reply, (size_t)got, found + nfound,
			MDNS_MAX_ADDRS - nfound);
	}
	close(sock);

	for (i = 0; i < nfound; i++) {
		struct addrinfo* ai = make_ai(pai, found[i]);
		if (ai == NULL) {
			freeaddrinfo(head);
			return EAI_MEMORY;
		}
		if (head == NULL)
			head = ai;
		else
			tail->ai_next = ai;
		tail = ai;
	}

	if (head == NULL)
		return EAI_NODATA;
	*res = head;
	return 0;
}
