/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * SHA-256 after FIPS 180-4 (sections 4.1.2, 4.2.2, 5.1.1, 6.2), for the
 * firmware check at boot. Written from the standard; no code copied.
 * One call hashes one buffer.
 */

#ifndef R5_SHA256_H
#define R5_SHA256_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const uint32_t kR5Sha256K[64] = {
	0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
	0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
	0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
	0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
	0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
	0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
	0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
	0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
	0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
	0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
	0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
	0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
	0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};


static inline uint32_t
r5_sha256_rotr(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32u - n));
}


static inline void
r5_sha256_block(uint32_t h[8], const uint8_t block[64])
{
	uint32_t w[64];
	for (unsigned t = 0; t < 16; t++) {
		w[t] = ((uint32_t)block[t * 4] << 24)
			| ((uint32_t)block[t * 4 + 1] << 16)
			| ((uint32_t)block[t * 4 + 2] << 8) | block[t * 4 + 3];
	}
	for (unsigned t = 16; t < 64; t++) {
		uint32_t s0 = r5_sha256_rotr(w[t - 15], 7)
			^ r5_sha256_rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
		uint32_t s1 = r5_sha256_rotr(w[t - 2], 17)
			^ r5_sha256_rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
		w[t] = w[t - 16] + s0 + w[t - 7] + s1;
	}
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
	uint32_t e = h[4], f = h[5], g = h[6], k = h[7];
	for (unsigned t = 0; t < 64; t++) {
		uint32_t s1 = r5_sha256_rotr(e, 6) ^ r5_sha256_rotr(e, 11)
			^ r5_sha256_rotr(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t t1 = k + s1 + ch + kR5Sha256K[t] + w[t];
		uint32_t s0 = r5_sha256_rotr(a, 2) ^ r5_sha256_rotr(a, 13)
			^ r5_sha256_rotr(a, 22);
		uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
		uint32_t t2 = s0 + maj;
		k = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
	h[4] += e;
	h[5] += f;
	h[6] += g;
	h[7] += k;
}


/* out is 32 bytes. */
static inline void
r5_sha256(const uint8_t* data, size_t bytes, uint8_t out[32])
{
	uint32_t h[8] = {
		0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
		0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
	};
	size_t done = 0;
	while (bytes - done >= 64) {
		r5_sha256_block(h, data + done);
		done += 64;
	}
	uint8_t tail[128];
	size_t rest = bytes - done;
	memset(tail, 0, sizeof(tail));
	if (rest > 0)
		memcpy(tail, data + done, rest);
	tail[rest] = 0x80;
	size_t tailBytes = rest < 56 ? 64 : 128;
	uint64_t bits = (uint64_t)bytes * 8u;
	for (unsigned i = 0; i < 8; i++)
		tail[tailBytes - 1 - i] = (uint8_t)(bits >> (i * 8));
	r5_sha256_block(h, tail);
	if (tailBytes == 128)
		r5_sha256_block(h, tail + 64);
	for (unsigned i = 0; i < 8; i++) {
		out[i * 4] = (uint8_t)(h[i] >> 24);
		out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
		out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
		out[i * 4 + 3] = (uint8_t)h[i];
	}
}


/* hex is 64 lower-case digits. 1 when the digest of the buffer matches. */
static inline int
r5_sha256_matches(const uint8_t* data, size_t bytes, const char* hex)
{
	static const char kDigits[] = "0123456789abcdef";
	uint8_t digest[32];
	r5_sha256(data, bytes, digest);
	for (unsigned i = 0; i < 32; i++) {
		if (hex[i * 2] != kDigits[digest[i] >> 4]
			|| hex[i * 2 + 1] != kDigits[digest[i] & 0xf])
			return 0;
	}
	return hex[64] == '\0';
}

#endif
