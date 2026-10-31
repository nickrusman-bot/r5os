/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_AIRPLAY_CRYPTO_H
#define R5_AIRPLAY_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int r5_b64_decode(const char* in, uint8_t* out, int outMax);
int r5_b64_encode(const uint8_t* in, int inLen, char* out, int outMax);

int r5_rsa_oaep_decrypt(const uint8_t* in, int inLen, uint8_t* out,
	int outMax);
int r5_rsa_oaep_encrypt(const uint8_t* in, int inLen, uint8_t* out,
	int outMax);
int r5_rsa_pkcs1_sign(const uint8_t* in, int inLen, uint8_t* out, int outMax);

void r5_aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
	uint8_t* buf, int len);
void r5_aes_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
	uint8_t* buf, int len);

#ifdef __cplusplus
}
#endif

#endif
