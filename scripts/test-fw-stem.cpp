/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Known PCI ids keep their firmware stem. An unknown id does not.
 */

#include "gfx9_check.h"

#include <stdio.h>
#include <string.h>

int
main()
{
	if (strcmp(r5_gfx9_fw_stem_for(0x1636), "renoir") != 0
		|| strcmp(r5_gfx9_fw_stem_for(0x164c), "renoir") != 0)
		return 1;
	if (strcmp(r5_gfx9_fw_stem_for(0x15e7), "green_sardine") != 0
		|| strcmp(r5_gfx9_fw_stem_for(0x1638), "green_sardine") != 0)
		return 2;
	if (r5_gfx9_fw_stem_for(0x1234) != NULL
		|| r5_gfx9_fw_stem_for(0) != NULL)
		return 3;
	printf("ok fw stem\n");
	return 0;
}
