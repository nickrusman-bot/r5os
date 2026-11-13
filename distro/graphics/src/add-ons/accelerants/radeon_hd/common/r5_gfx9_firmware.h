/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The GFX 9.3 firmware table: one row per blob the fixed start loads
 * (gfx9_start.cpp radeon_psp_load_engines: rlc, pfp, ce, me, mec; MEC2
 * is not sent on GC 9.3.0), with its sha256. These are the hashes in
 * THIRD_PARTY_NOTICES.md. scripts/apply-amdgpu-fw.py reads this table,
 * so the build check and the boot check use the same values.
 */

#ifndef R5_GFX9_FIRMWARE_H
#define R5_GFX9_FIRMWARE_H

#include <stdint.h>
#include <string.h>

typedef struct {
	const char*	name;
	const char*	sha256;
} r5_gfx9_firmware_blob;

static const r5_gfx9_firmware_blob kR5Gfx9Firmware[] = {
	{"renoir_pfp.bin",
		"0c815572cc466d6c1e608e1d36ab16f35660179e19f0feeabc448b85e8158af4"},
	{"renoir_me.bin",
		"38574ad3c583f3965cf1c4be84f9d0c3818511781c03733b28f07683061b1189"},
	{"renoir_ce.bin",
		"b4ea0bf5d589d82ccef52e54536221f094b66b648514b14f03cf4ad50f9a4f72"},
	{"renoir_rlc.bin",
		"66f4397c2e9d19af245f9da290c8678eb1a51cc815fa7864a9383c2cd0db319d"},
	{"renoir_mec.bin",
		"0e4c6712f75af494c3fd70386bc07739cab3aafd77c4fe8efb46c0bc1ae4eba6"},
	{"green_sardine_pfp.bin",
		"94e1474b3c8d422e35661d2d584297d891464c173ebf4ad45536277bdf7bdcdc"},
	{"green_sardine_me.bin",
		"671af908c8d631e659895d62db2e6bffc1d2c87d6db4c08246be0216f468fb9c"},
	{"green_sardine_ce.bin",
		"3bba10cff48ec51eb63e78078456596258c9fea2e8b38d91d02be66c0aca70fd"},
	{"green_sardine_rlc.bin",
		"66f4397c2e9d19af245f9da290c8678eb1a51cc815fa7864a9383c2cd0db319d"},
	{"green_sardine_mec.bin",
		"0e4c6712f75af494c3fd70386bc07739cab3aafd77c4fe8efb46c0bc1ae4eba6"},
	{"green_sardine_mec2.bin",
		"0e4c6712f75af494c3fd70386bc07739cab3aafd77c4fe8efb46c0bc1ae4eba6"},
};

/* The five blobs the start loads for one stem, in load order. */
static const char* const kR5Gfx9FirmwareParts[] = {
	"rlc", "pfp", "ce", "me", "mec"
};

/* The expected sha256 of <stem>_<part>.bin, or NULL when the blob is not
 * in the table. */
static inline const char*
r5_gfx9_firmware_sha256(const char* fileName)
{
	for (size_t i = 0; i < sizeof(kR5Gfx9Firmware) / sizeof(kR5Gfx9Firmware[0]);
			i++) {
		if (strcmp(kR5Gfx9Firmware[i].name, fileName) == 0)
			return kR5Gfx9Firmware[i].sha256;
	}
	return NULL;
}

#endif
