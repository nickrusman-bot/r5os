/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R5 GPU interface, version 1.0. The calls are vendor-neutral.
 * AMD data lives only in the vendor block and in the command stream.
 * A shipped call does not change. A new call is added at the end.
 */

#ifndef R5_GPU_H
#define R5_GPU_H

#include <stdint.h>

#define R5_GPU_API_MAJOR 1u
#define R5_GPU_API_MINOR 0u

/* Haiku Drivers.h: B_DEVICE_OP_CODES_END is 9999. The numbers below are
 * that value plus 0x5000, then one fixed number per call. Never reuse one. */
#define R5_GPU_OP_BASE 30479u

enum {
	R5_GPU_GET_INFO = R5_GPU_OP_BASE + 0,
	R5_GPU_READ_REG = R5_GPU_OP_BASE + 1,
	R5_GPU_CTX_CREATE = R5_GPU_OP_BASE + 2,
	R5_GPU_CTX_DESTROY = R5_GPU_OP_BASE + 3,
	R5_GPU_BO_CREATE = R5_GPU_OP_BASE + 4,
	R5_GPU_BO_FREE = R5_GPU_OP_BASE + 5,
	R5_GPU_BO_MAP = R5_GPU_OP_BASE + 6,
	R5_GPU_VA_MAP = R5_GPU_OP_BASE + 7,
	R5_GPU_VA_UNMAP = R5_GPU_OP_BASE + 8,
	R5_GPU_CS_SUBMIT = R5_GPU_OP_BASE + 9,
	R5_GPU_FENCE_WAIT = R5_GPU_OP_BASE + 10,
	R5_GPU_SYNCOBJ_CREATE = R5_GPU_OP_BASE + 11,
	R5_GPU_SYNCOBJ_DESTROY = R5_GPU_OP_BASE + 12,
	R5_GPU_SYNCOBJ_WAIT = R5_GPU_OP_BASE + 13,
	R5_GPU_SYNCOBJ_SIGNAL = R5_GPU_OP_BASE + 14,
	R5_GPU_SYNCOBJ_RESET = R5_GPU_OP_BASE + 15,
	R5_GPU_SYNCOBJ_QUERY = R5_GPU_OP_BASE + 16,
	R5_GPU_BO_EXPORT = R5_GPU_OP_BASE + 17,
	R5_GPU_BO_IMPORT = R5_GPU_OP_BASE + 18
};

#define R5_GPU_CAP_RENDER 1u

#define R5_GPU_DOMAIN_VRAM 1u
#define R5_GPU_DOMAIN_GTT 2u

#define R5_GPU_VA_READ 1u
#define R5_GPU_VA_WRITE 2u
#define R5_GPU_VA_EXEC 4u

/* Linux v6.12 amdgpu_kms.c for a 48-bit GC 9.3 VM. See GRAPHICS.md. */
#define R5_GPU_VA_OFFSET 0x10000ull
#define R5_GPU_VA_MAX 0x800000000000ull
#define R5_GPU_HIGH_VA_OFFSET 0xffff800000000000ull
#define R5_GPU_HIGH_VA_MAX 0xffffffffffbfe000ull

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	api_major;
	uint32_t	api_minor;
	uint32_t	vendor;
	uint32_t	device;
	uint32_t	revision;
	uint32_t	capabilities;
	uint64_t	vram_bytes;
	uint64_t	gtt_bytes;
	uint64_t	va_offset;
	uint64_t	va_max;
	uint64_t	high_va_offset;
	uint64_t	high_va_max;
	uint64_t	timestamp_hz;
	uint32_t	vendor_block_size;
	uint32_t	vendor_block_version;
} r5_gpu_info;

/* AMD vendor block. version 1. Fields not read from the chip stay 0. */
typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	family;
	uint32_t	external_revision;
	uint32_t	max_shader_engines;
	uint32_t	max_shader_arrays;
	uint32_t	active_cu;
	uint32_t	gb_addr_config;
} r5_gpu_amd_info;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	offset;
	uint32_t	value;
} r5_gpu_read_reg;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	context;
} r5_gpu_ctx;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	domain;
	uint32_t	flags;
	uint64_t	bytes;
	uint32_t	handle;
	int32_t		area;
} r5_gpu_bo;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	context;
	uint32_t	handle;
	uint64_t	gpu_va;
	uint32_t	flags;
} r5_gpu_va;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	context;
	uint32_t	ring;
	uint64_t	ib_va;
	uint32_t	ib_dwords;
	uint32_t	flags;
	uint64_t	sequence;
} r5_gpu_cs;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint64_t	sequence;
	uint64_t	timeout_us;
} r5_gpu_fence;

typedef struct {
	uint32_t	size;
	uint32_t	version;
	uint32_t	handle;
	uint64_t	point;
	uint64_t	timeout_us;
} r5_gpu_syncobj;

#endif
