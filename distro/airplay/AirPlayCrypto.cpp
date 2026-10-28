/*
 * AirPlay 1 RSA/AES helpers.
 *
 * AES-128-CBC: tiny-AES-c by kokke (Unlicense / public domain).
 * RSA wrapping uses the well-known AirPort Express private key as published
 * in Shairport (James Laird, MIT). OpenSSL 3 is used for RSA.
 */

#include "AirPlayCrypto.h"

extern "C" {
#include "aes.h"
}

#include <ctype.h>
#include <string.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>


static const char kAirportExpressKey[] =
"-----BEGIN RSA PRIVATE KEY-----\n"
"MIIEpQIBAAKCAQEA59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUt\n"
"wC5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDRKSKv6kDqnw4U\n"
"wPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuBOitnZ/bDzPHrTOZz0Dew0uowxf\n"
"/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJQ+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/\n"
"UAaHqn9JdsBWLUEpVviYnhimNVvYFZeCXg/IdTQ+x4IRdiXNv5hEewIDAQABAoIBAQDl8Axy9XfW\n"
"BLmkzkEiqoSwF0PsmVrPzH9KsnwLGH+QZlvjWd8SWYGN7u1507HvhF5N3drJoVU3O14nDY4TFQAa\n"
"LlJ9VM35AApXaLyY1ERrN7u9ALKd2LUwYhM7Km539O4yUFYikE2nIPscEsA5ltpxOgUGCY7b7ez5\n"
"NtD6nL1ZKauw7aNXmVAvmJTcuPxWmoktF3gDJKK2wxZuNGcJE0uFQEG4Z3BrWP7yoNuSK3dii2jm\n"
"lpPHr0O/KnPQtzI3eguhe0TwUem/eYSdyzMyVx/YpwkzwtYL3sR5k0o9rKQLtvLzfAqdBxBurciz\n"
"aaA/L0HIgAmOit1GJA2saMxTVPNhAoGBAPfgv1oeZxgxmotiCcMXFEQEWflzhWYTsXrhUIuz5jFu\n"
"a39GLS99ZEErhLdrwj8rDDViRVJ5skOp9zFvlYAHs0xh92ji1E7V/ysnKBfsMrPkk5KSKPrnjndM\n"
"oPdevWnVkgJ5jxFuNgxkOLMuG9i53B4yMvDTCRiIPMQ++N2iLDaRAoGBAO9v//mU8eVkQaoANf0Z\n"
"oMjW8CN4xwWA2cSEIHkd9AfFkftuv8oyLDCG3ZAf0vrhrrtkrfa7ef+AUb69DNggq4mHQAYBp7L+\n"
"k5DKzJrKuO0r+R0YbY9pZD1+/g9dVt91d6LQNepUE/yY2PP5CNoFmjedpLHMOPFdVgqDzDFxU8hL\n"
"AoGBANDrr7xAJbqBjHVwIzQ4To9pb4BNeqDndk5Qe7fT3+/H1njGaC0/rXE0Qb7q5ySgnsCb3DvA\n"
"cJyRM9SJ7OKlGt0FMSdJD5KG0XPIpAVNwgpXXH5MDJg09KHeh0kXo+QA6viFBi21y340NonnEfdf\n"
"54PX4ZGS/Xac1UK+pLkBB+zRAoGAf0AY3H3qKS2lMEI4bzEFoHeK3G895pDaK3TFBVmD7fV0Zhov\n"
"17fegFPMwOII8MisYm9ZfT2Z0s5Ro3s5rkt+nvLAdfC/PYPKzTLalpGSwomSNYJcB9HNMlmhkGzc\n"
"1JnLYT4iyUyx6pcZBmCd8bD0iwY/FzcgNDaUmbX9+XDvRA0CgYEAkE7pIPlE71qvfJQgoA9em0gI\n"
"LAuE4Pu13aKiJnfft7hIjbK+5kyb3TysZvoyDnb3HOKvInK7vXbKuU4ISgxB2bB3HcYzQMGsz1qJ\n"
"2gG0N5hvJpzwwhbhXqFKA4zaaSrw622wDniAK5MlIE0tIAKKP4yxNGjoD2QYjhBGuhvkWKY=\n"
"-----END RSA PRIVATE KEY-----";


static const char kB64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


int
r5_b64_decode(const char* in, uint8_t* out, int outMax)
{
	int val = 0;
	int valb = -8;
	int n = 0;

	for (; *in != '\0'; in++) {
		const char* p;
		if (isspace((unsigned char)*in) || *in == '=')
			continue;
		p = strchr(kB64, *in);
		if (p == NULL)
			return -1;
		val = (val << 6) + (int)(p - kB64);
		valb += 6;
		if (valb >= 0) {
			if (n >= outMax)
				return -1;
			out[n++] = (uint8_t)((val >> valb) & 0xff);
			valb -= 8;
		}
	}
	return n;
}


int
r5_b64_encode(const uint8_t* in, int inLen, char* out, int outMax)
{
	int i;
	int o = 0;
	int rem;

	for (i = 0; i + 2 < inLen; i += 3) {
		if (o + 4 >= outMax)
			return -1;
		out[o++] = kB64[(in[i] >> 2) & 0x3f];
		out[o++] = kB64[((in[i] & 0x3) << 4) | ((in[i + 1] >> 4) & 0xf)];
		out[o++] = kB64[((in[i + 1] & 0xf) << 2) | ((in[i + 2] >> 6) & 0x3)];
		out[o++] = kB64[in[i + 2] & 0x3f];
	}
	rem = inLen - i;
	if (rem > 0) {
		if (o + 4 >= outMax)
			return -1;
		out[o++] = kB64[(in[i] >> 2) & 0x3f];
		if (rem == 1) {
			out[o++] = kB64[(in[i] & 0x3) << 4];
			out[o++] = '=';
			out[o++] = '=';
		} else {
			out[o++] = kB64[((in[i] & 0x3) << 4) | ((in[i + 1] >> 4) & 0xf)];
			out[o++] = kB64[(in[i + 1] & 0xf) << 2];
			out[o++] = '=';
		}
	}
	out[o] = '\0';
	return o;
}


static RSA*
airport_rsa()
{
	static RSA* rsa = NULL;
	if (rsa == NULL) {
		BIO* bio = BIO_new_mem_buf(kAirportExpressKey, -1);
		if (bio == NULL)
			return NULL;
		rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
		BIO_free(bio);
	}
	return rsa;
}


int
r5_rsa_oaep_decrypt(const uint8_t* in, int inLen, uint8_t* out, int outMax)
{
	RSA* rsa = airport_rsa();
	int n;

	if (rsa == NULL)
		return -1;
	if (outMax < RSA_size(rsa))
		return -1;
	n = RSA_private_decrypt(inLen, in, out, rsa, RSA_PKCS1_OAEP_PADDING);
	return n;
}


int
r5_rsa_pkcs1_sign(const uint8_t* in, int inLen, uint8_t* out, int outMax)
{
	RSA* rsa = airport_rsa();
	int n;

	if (rsa == NULL)
		return -1;
	if (outMax < RSA_size(rsa))
		return -1;
	n = RSA_private_encrypt(inLen, in, out, rsa, RSA_PKCS1_PADDING);
	return n;
}


void
r5_aes_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t* buf,
	int len)
{
	struct AES_ctx ctx;
	int aligned = len & ~15;

	if (aligned <= 0)
		return;
	AES_init_ctx_iv(&ctx, key, iv);
	AES_CBC_decrypt_buffer(&ctx, buf, (size_t)aligned);
}
