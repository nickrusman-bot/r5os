/* SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * generated from gfx9_compute.s, do not edit by hand
 * llvm-mc Ubuntu LLVM version 18.1.8
 * llvm-mc -triple=amdgcn-amd-amdhsa -mcpu=gfx90c -mattr=-xnack -filetype=obj gfx9_compute.s -o gfx9_compute.o
 */
#ifndef GFX9_COMPUTE_SHADER_H
#define GFX9_COMPUTE_SHADER_H

static const uint32 kGfx9EmptyCode[] = {
	0xbf810000u
};
static const uint32 kGfx9EmptyRsrc1 = 0x000c0000u;
static const uint32 kGfx9EmptyRsrc2 = 0x00000084u;
static const uint32 kGfx9FillCode[] = {
	0x8e038602u,
	0x68020003u,
	0x10040283u,
	0x68040487u,
	0x24060282u,
	0xdc708000u,
	0x00000203u,
	0xbf8c0f70u,
	0xbf810000u
};
static const uint32 kGfx9FillRsrc1 = 0x000c0000u;
static const uint32 kGfx9FillRsrc2 = 0x00000084u;

#endif
