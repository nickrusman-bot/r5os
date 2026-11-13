/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test for the four checks at boot and the switch defaults
 * (r5_gpu_checks.h), the boot firmware check (r5_sha256.h against the
 * table in r5_gfx9_firmware.h) and the capability bit. Run from the
 * repository root:
 *
 *   g++ -std=c++17 -Wall -Wextra -Werror \
 *     -Idistro/graphics/headers/private/r5gpu \
 *     -Idistro/graphics/src/add-ons/accelerants/radeon_hd/common \
 *     scripts/test-r5-gpu-checks.cpp -o /tmp/test-r5-gpu-checks \
 *     && /tmp/test-r5-gpu-checks distro/firmware/amdgpu
 *
 * Pass line: ok gpu checks <n> cases. Fail line: fail gpu checks <case>.
 */

#include "r5_gpu.h"
#include "r5_gpu_checks.h"
#include "r5_gfx9_firmware.h"
#include "gfx9_check.h"
#include "r5_sha256.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int sCases = 0;


static int
check(bool ok, const char* what)
{
	sCases++;
	if (!ok)
		fprintf(stderr, "fail gpu checks %s\n", what);
	return ok ? 0 : 1;
}


static bool
same(const char* a, const char* b)
{
	if (a == NULL || b == NULL)
		return a == b;
	return strcmp(a, b) == 0;
}


static int
reasons()
{
	const uint32_t release = R5_GPU_LEVEL_RELEASE;
	int failed = 0;
	failed |= check(same(r5_gpu_check_reason(release, 0, 0, 1, 1), NULL),
		"available");
	failed |= check(same(r5_gpu_check_reason(release, 1, 0, 1, 1), "opt-out"),
		"opt-out");
	/* gpu false: nothing ran, so the opt-out wins over every other check. */
	failed |= check(same(r5_gpu_check_reason(release, 1, 1, 0, 0), "opt-out"),
		"opt-out first");
	failed |= check(same(r5_gpu_check_reason(R5_GPU_LEVEL_DESKTOP, 0, 0, 1, 1),
		"not release-supported"), "desktop-capable is not release");
	failed |= check(same(r5_gpu_check_reason(R5_GPU_LEVEL_NONE, 0, 0, 1, 1),
		"not release-supported"), "no level");
	failed |= check(same(r5_gpu_check_reason(release, 0, 1, 1, 1),
		"switched off"), "switched off");
	failed |= check(same(r5_gpu_check_reason(release, 0, 0, 0, 1), "firmware"),
		"firmware");
	failed |= check(same(r5_gpu_check_reason(release, 0, 0, 0, 0), "firmware"),
		"firmware before selftest");
	failed |= check(same(r5_gpu_check_reason(release, 0, 0, 1, 0), "selftest"),
		"selftest");
	return failed;
}


static int
switches()
{
	const uint32_t release = R5_GPU_LEVEL_RELEASE;
	int failed = 0;
	failed |= check(r5_gpu_switch(release, 0, -1) == 1, "release default on");
	failed |= check(r5_gpu_switch(release, 0, 0) == 0, "release false wins");
	failed |= check(r5_gpu_switch(release, 0, 1) == 1, "release true");
	failed |= check(r5_gpu_switch(release, 1, -1) == 0, "opt-out default");
	failed |= check(r5_gpu_switch(release, 1, 1) == 0, "opt-out beats true");
	failed |= check(r5_gpu_switch(R5_GPU_LEVEL_DESKTOP, 0, -1) == 0,
		"other level default off");
	failed |= check(r5_gpu_switch(R5_GPU_LEVEL_NONE, 0, 1) == 1,
		"other level opt-in");
	failed |= check(r5_gpu_switch(R5_GPU_LEVEL_NONE, 1, 1) == 0,
		"other level opt-out");
	return failed;
}


static int
caps()
{
	int failed = 0;
	/* Additive: the new bit does not overlap a shipped one. */
	failed |= check((R5_GPU_CAP_AVAILABLE & (R5_GPU_CAP_RENDER | R5_GPU_CAP_LOST
		| R5_GPU_CAP_GUILTY)) == 0, "cap bit is new");
	/* EGL, the WSI and r5-gl-test read only the first 32 bytes. */
	failed |= check(offsetof(r5_gpu_info, capabilities) == 28,
		"capabilities at byte 28");
	failed |= check(R5_GPU_GET_INFO == 30479u, "GET_INFO opcode");
	failed |= check(R5_GPU_CAP_AVAILABLE == 8u, "cap bit value");
	return failed;
}


/* FIPS 180-4 examples (NIST CSRC "SHA256.pdf"). */
static int
sha256()
{
	int failed = 0;
	const char* abc = "abc";
	failed |= check(r5_sha256_matches((const uint8_t*)abc, 3,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
		"sha256 abc");
	const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	failed |= check(r5_sha256_matches((const uint8_t*)two, strlen(two),
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
		"sha256 two blocks");
	failed |= check(r5_sha256_matches((const uint8_t*)"", 0,
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
		"sha256 empty");
	/* 55 and 56 bytes: the length either fits the last block or not. */
	char a55[56];
	memset(a55, 'a', 55);
	a55[55] = 0;
	failed |= check(r5_sha256_matches((const uint8_t*)a55, 55,
		"9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"),
		"sha256 55 bytes");
	char a56[57];
	memset(a56, 'a', 56);
	a56[56] = 0;
	failed |= check(r5_sha256_matches((const uint8_t*)a56, 56,
		"b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"),
		"sha256 56 bytes");
	failed |= check(!r5_sha256_matches((const uint8_t*)abc, 2,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
		"sha256 mismatch");
	return failed;
}


/* Every blob the start loads is in the table, and each file in the
 * repository matches it (the same check as at boot). */
static int
firmware(const char* dir)
{
	int failed = 0;
	const char* stems[] = {"renoir", "green_sardine"};
	for (const char* stem : stems) {
		for (const char* part : kR5Gfx9FirmwareParts) {
			char name[48];
			snprintf(name, sizeof(name), "%s_%s.bin", stem, part);
			const char* expected = r5_gfx9_firmware_sha256(name);
			failed |= check(expected != NULL, name);
			if (dir == NULL || expected == NULL)
				continue;
			char path[512];
			snprintf(path, sizeof(path), "%s/%s", dir, name);
			FILE* file = fopen(path, "rb");
			if (file == NULL) {
				failed |= check(false, path);
				continue;
			}
			static uint8_t blob[1024 * 1024];
			size_t bytes = fread(blob, 1, sizeof(blob), file);
			fclose(file);
			failed |= check(r5_sha256_matches(blob, bytes, expected), path);
		}
	}
	failed |= check(r5_gfx9_fw_stem_for(0x15e7) != NULL, "5625U has a stem");
	failed |= check(r5_gfx9_firmware_sha256("none.bin") == NULL, "unknown blob");
	return failed;
}


int
main(int argc, char** argv)
{
	const char* dir = argc > 1 ? argv[1] : NULL;
	int failed = reasons() | switches() | caps() | sha256() | firmware(dir);
	if (failed)
		return 1;
	printf("ok gpu checks %d cases%s\n", sCases,
		dir != NULL ? "" : " (no firmware directory)");
	return 0;
}
