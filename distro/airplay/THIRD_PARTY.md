# Third-party code in the R5OS AirPlay receiver

## Shairport / RAOP (MIT)

Classic AirPlay (RAOP) request flow, SDP `rsaaeskey` / `aesiv` / `fmtp`
handling, and the AirPort Express RSA private key used to unwrap AES session
keys come from Shairport by James Laird and later Shairport Sync by Mike Brady.

- https://github.com/abrasive/shairport
- https://github.com/mikebrady/shairport-sync

Permission is granted under the MIT License. The original copyright notices
are preserved in comments next to the adapted code.

AirPlay 2 pairing (HomeKit HAP / SRP), NQPTP timing, and FairPlay video are
**not** copied from Shairport Sync. R5OS advertises `_airplay._tcp` and serves
`/info`; pair-setup is not implemented.

## ALAC decoder (MIT)

`alac.c` / `alac.h` are David Hammerton's Apple Lossless decoder, as shipped
with Shairport.

http://crazney.net/programs/itunes/alac.html

## tiny-AES-c (Unlicense / public domain)

`aes.c` / `aes.h` are kokke's tiny-AES-c, used for AES-128-CBC of RAOP RTP.

https://github.com/kokke/tiny-AES-c

## OpenSSL 3

RSA OAEP unwrap and PKCS#1 Apple-Challenge responses use OpenSSL 3
(`libcrypto`) from HaikuPorts, shipped in the image as `openssl3`.
