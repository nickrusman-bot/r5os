/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage P. Each step is one packet type copied from the eleventh-stick
 * dump, with only the addresses moved onto the caller's test page.
 * The host test and the kernel build the same dwords.
 */

#ifndef R5_STAGE_P_H
#define R5_STAGE_P_H

#include <stdint.h>

#define R5_P_WRITE_DATA 0u
#define R5_P_RELEASE_MEM 1u
#define R5_P_ACQUIRE_MEM 2u
#define R5_P_EVENT_2C 3u
#define R5_P_EVENT_2E 4u
#define R5_P_EVENT_0E 5u
#define R5_P_EVENT_407 6u
#define R5_P_EVENT_410 7u
#define R5_P_EVENT_ZPASS 8u
#define R5_P_DMA_DATA 9u
#define R5_P_LOAD_CONTEXT 10u
#define R5_P_INDEX_PRIM 11u
#define R5_P_INDEX_TYPE 12u
#define R5_P_IA_MULTI 13u
#define R5_P_RESET_EN 14u
#define R5_P_COHER 15u
#define R5_P_RSRC3_VS 16u
#define R5_P_DISPATCH 17u
/* The thirteenth-stick fault is before the first marker. These steps
 * are still one VMID 1 indirect buffer each, on kernel VRAM. */
#define R5_P_IB2 18u
#define R5_P_CONTEXT_CONTROL 19u
#define R5_P_CLEAR_STATE 20u
#define R5_P_EVENT_19 21u
#define R5_P_EVENT_1A 22u
#define R5_P_UC_30A00 23u
#define R5_P_UC_30920 24u
#define R5_P_UC_30968 25u
#define R5_P_UC_30E00 26u
#define R5_P_CONTEXT4 27u
#define R5_P_SH_PREFIX 28u
/* The preamble is split so a privilege fault names the half.
 * p30 is the first 0x34 dwords, p31 the second, p32 the whole 0x68.
 * p33 is the 24-dword IB. The review counted that IB as p31. */
#define R5_P_PREAMBLE_LO 29u
#define R5_P_PREAMBLE_HI 30u
#define R5_P_PREAMBLE 31u
#define R5_P_IB24 32u
#define R5_P_COUNT 33u

/* s_endpgm. The dispatch step points COMPUTE_PGM at this dword. */
#define R5_P_SHADER 0xbf810000u
#define R5_P_SHADER_OFF 256u
/* Written by the marker self-test and by the IB2 body. */
#define R5_P_MARK_VALUE 0xa5a5a5a5u


static inline const char*
r5_stage_p_name(uint32_t step)
{
	static const char* const names[R5_P_COUNT] = {
		"WRITE_DATA",
		"RELEASE_MEM",
		"ACQUIRE_MEM",
		"EVENT_2C",
		"EVENT_2E",
		"EVENT_0E",
		"EVENT_407",
		"EVENT_410",
		"EVENT_ZPASS",
		"DMA_DATA",
		"LOAD_CONTEXT",
		"INDEX_PRIM",
		"INDEX_TYPE",
		"IA_MULTI_VGT",
		"MULTI_PRIM_RESET",
		"CP_COHER_DELAY",
		"SH_RSRC3_VS",
		"DISPATCH",
		"IB2",
		"CONTEXT_CONTROL",
		"CLEAR_STATE",
		"EVENT_19",
		"EVENT_1A",
		"UC_30A00",
		"UC_30920",
		"UC_30968",
		"UC_30E00",
		"CONTEXT4",
		"SH_PREFIX",
		"PREAMBLE_LO",
		"PREAMBLE_HI",
		"PREAMBLE",
		"IB24"
	};
	if (step >= R5_P_COUNT)
		return "";
	return names[step];
}


static inline uint32_t
r5_p_put(uint32_t* out, uint32_t n, uint32_t cap, uint32_t word)
{
	if (out != 0 && n < cap)
		out[n] = word;
	return n + 1u;
}


static inline uint32_t
r5_p_words(uint32_t* out, uint32_t cap, const uint32_t* src, uint32_t count)
{
	uint32_t n = 0;
	uint32_t i;
	for (i = 0; i < count; i++)
		n = r5_p_put(out, n, cap, src[i]);
	return n;
}


/* Thirteenth stick, generated/boot-5625u-13/syslog.
 * Preamble is the first 0x68 dwords of ih ib2 at 0x100015000
 * (size word 0x68, last dword ffff1000). Context is the first four
 * SET_CONTEXT_REG packets in that preamble. SH prefix is every
 * SET_SH_REG packet that starts in the first 0x20 dwords of ih ib1
 * at 0x100001000. WRITE_DATA, EVENT_WRITE and RELEASE_MEM are out. */
static const uint32_t kR5Preamble[] = {
	0xc0012800u, 0x80000000u, 0x80000000u, 0xc0001200u,
	0x00000000u, 0xc0026900u, 0x00000090u, 0x80000000u,
	0x40004000u, 0xc0016900u, 0x00000286u, 0x42800000u,
	0xc0026900u, 0x00000020u, 0x00000000u, 0x00000000u,
	0xc0027900u, 0x00000280u, 0x00000000u, 0x00000000u,
	0xc0017600u, 0x00000007u, 0x003fffffu, 0xc0017600u,
	0x00000105u, 0x00000000u, 0xc0017600u, 0x00000085u,
	0x00000000u, 0xc0016900u, 0x000002d4u, 0xd8181e0cu,
	0xc0016900u, 0x000002a8u, 0x00000001u, 0xc0037900u,
	0x00000248u, 0xffffffffu, 0x00000000u, 0x00000000u,
	0xc0016900u, 0x00000018u, 0x00000002u, 0xc0017600u,
	0x00000107u, 0xffff003fu, 0xc0016900u, 0x00000312u,
	0x03ff007fu, 0xc0016900u, 0x000002abu, 0x00000001u,
	0xc0017900u, 0x0000025au, 0x00000000u, 0xc0017600u,
	0x00000049u, 0x00000000u, 0xc0016900u, 0x00000103u,
	0xffffffffu, 0xc0026900u, 0x00000280u, 0x00080008u,
	0xffff0000u, 0xc0016900u, 0x0000020cu, 0x00000001u,
	0xc0016900u, 0x000001b5u, 0x0000086bu, 0xc0016900u,
	0x000002f9u, 0x0000002du, 0xc0016900u, 0x00000206u,
	0x0000043fu, 0xc0016900u, 0x0000020au, 0x3f800000u,
	0xc0017600u, 0x0000020du, 0x00000000u, 0xc0027600u,
	0x00000216u, 0xffffffffu, 0x00000000u, 0xc0027600u,
	0x00000219u, 0x00000000u, 0x00000000u, 0xc0017900u,
	0x0000007bu, 0x00000000u, 0xc0027900u, 0x00000380u,
	0x00000000u, 0x00000000u, 0xc0037600u, 0x00000204u,
	0x00000000u, 0x00000000u, 0x00000000u, 0xffff1000u,
};
static const uint32_t kR5Context4[] = {
	0xc0026900u, 0x00000090u, 0x80000000u, 0x40004000u,
	0xc0016900u, 0x00000286u, 0x42800000u, 0xc0026900u,
	0x00000020u, 0x00000000u, 0x00000000u, 0xc0016900u,
	0x000002d4u, 0xd8181e0cu,
};
static const uint32_t kR5ShPrefix[] = {
	0xc0017600u, 0x0000020cu, 0x00000102u, 0xc0027600u,
	0x00000212u, 0x002c0041u, 0x0000008cu, 0xc0017600u,
	0x00000215u, 0x00800118u, 0xc0037600u, 0x00000207u,
	0x00000040u, 0x00000001u, 0x00000001u, 0xc0047600u,
	0x00000242u, 0x00170000u, 0x00000001u, 0x00003ff0u,
	0xfffc000fu,
};

static inline const uint32_t*
r5_stage_p_preamble(uint32_t* count)
{
	if (count != 0)
		*count = (uint32_t)(sizeof(kR5Preamble) / sizeof(kR5Preamble[0]));
	return kR5Preamble;
}


static inline const uint32_t*
r5_stage_p_context4(uint32_t* count)
{
	if (count != 0)
		*count = (uint32_t)(sizeof(kR5Context4) / sizeof(kR5Context4[0]));
	return kR5Context4;
}


static inline const uint32_t*
r5_stage_p_sh_prefix(uint32_t* count)
{
	if (count != 0)
		*count = (uint32_t)(sizeof(kR5ShPrefix) / sizeof(kR5ShPrefix[0]));
	return kR5ShPrefix;
}


/* WRITE_DATA of R5_P_MARK_VALUE. This is the IB2 body for p19. */
static inline uint32_t
r5_stage_p_ib2_body(uint64_t page, uint32_t* out, uint32_t cap)
{
	uint32_t lo = (uint32_t)page;
	uint32_t hi = (uint32_t)(page >> 32);
	const uint32_t src[] = {
		0xc0043700u, 0x40100500u, lo, hi, R5_P_MARK_VALUE, 0
	};
	uint32_t n = r5_p_words(out, cap, src, 6);
	if (n != 6 || n > cap)
		return 0;
	return n;
}


/* Returns the dword count, or 0 when the step is unknown or cap is short.
 * ib2 is the VMID 1 address of the second indirect buffer. p19 and p33
 * need it. The other steps ignore it. */
static inline uint32_t
r5_stage_p_build(uint32_t step, uint64_t page, uint32_t* out, uint32_t cap,
	uint64_t ib2)
{
	uint32_t n = 0;
	uint32_t lo = (uint32_t)page;
	uint32_t hi = (uint32_t)(page >> 32);
	uint64_t shader = page + (uint64_t)R5_P_SHADER_OFF;
	uint32_t shaderLo = (uint32_t)(shader >> 8);
	uint32_t shaderHi = (uint32_t)(shader >> 40);
	if (step >= R5_P_COUNT || out == 0)
		return 0;
	switch (step) {
		case R5_P_WRITE_DATA: {
			/* c0043700 40100500 addr 0 0 0 */
			const uint32_t src[] = {
				0xc0043700u, 0x40100500u, lo, hi, 0, 0
			};
			n = r5_p_words(out, cap, src, 6);
			break;
		}
		case R5_P_RELEASE_MEM: {
			/* RELEASE_MEM 00220514 23000000, then WAIT_REG_MEM == 1. */
			const uint32_t src[] = {
				0xc0064900u, 0x00220514u, 0x23000000u, lo, hi, 1u, 0, 0,
				0xc0053c00u, 0x00000013u, lo, hi, 1u, 0xffffffffu, 4u
			};
			n = r5_p_words(out, cap, src, 15);
			break;
		}
		case R5_P_ACQUIRE_MEM: {
			const uint32_t src[] = {
				0xc0004200u, 0,
				0xc0055800u, 0x00400000u, 0xffffffffu, 0x00ffffffu, 0, 0, 0x0au
			};
			n = r5_p_words(out, cap, src, 9);
			break;
		}
		case R5_P_EVENT_2C:
			n = r5_p_put(out, n, cap, 0xc0004600u);
			n = r5_p_put(out, n, cap, 0x2cu);
			break;
		case R5_P_EVENT_2E:
			n = r5_p_put(out, n, cap, 0xc0004600u);
			n = r5_p_put(out, n, cap, 0x2eu);
			break;
		case R5_P_EVENT_0E:
			n = r5_p_put(out, n, cap, 0xc0004600u);
			n = r5_p_put(out, n, cap, 0x0eu);
			break;
		case R5_P_EVENT_407:
			n = r5_p_put(out, n, cap, 0xc0004600u);
			n = r5_p_put(out, n, cap, 0x407u);
			break;
		case R5_P_EVENT_410:
			n = r5_p_put(out, n, cap, 0xc0004600u);
			n = r5_p_put(out, n, cap, 0x410u);
			break;
		case R5_P_EVENT_ZPASS: {
			/* ZPASS_DONE index 1, destination page+8 as in the dump. */
			const uint32_t src[] = {
				0xc0024600u, 0x115u, lo + 8u, hi
			};
			n = r5_p_words(out, cap, src, 4);
			break;
		}
		case R5_P_DMA_DATA: {
			uint32_t srcLo = (uint32_t)(page + 128u);
			uint32_t dstLo = (uint32_t)(page + 192u);
			const uint32_t src[] = {
				/* Stick 43: destination select 2 writes nothing.
				 * Select 0 is the K2 fill. CP_SYNC, engine 0. */
				0xc0055000u, 0x80000000u, srcLo, hi, dstLo, hi, 0x80000040u
			};
			n = r5_p_words(out, cap, src, 7);
			break;
		}
		case R5_P_LOAD_CONTEXT: {
			const uint32_t src[] = {
				0xc0039f00u, lo, hi, 0x0bu, 1u
			};
			n = r5_p_words(out, cap, src, 5);
			break;
		}
		case R5_P_INDEX_PRIM: {
			const uint32_t src[] = { 0xc0017a00u, 0x10000242u, 0x11u };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_INDEX_TYPE: {
			const uint32_t src[] = { 0xc0017a00u, 0x20000243u, 1u };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_IA_MULTI: {
			/* Plain SET_UCONFIG_REG. The dump used the INDEX form. */
			const uint32_t src[] = { 0xc0017900u, 0x258u, 0x0070007fu };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_RESET_EN: {
			const uint32_t src[] = { 0xc0017900u, 0x24bu, 0 };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_COHER: {
			const uint32_t src[] = { 0xc0017900u, 0x7bu, 0 };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_RSRC3_VS: {
			const uint32_t src[] = { 0xc0017600u, 0x30000046u, 0x003ffffeu };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_DISPATCH: {
			/* COMPUTE_PGM at the s_endpgm on this page, then 1x1x1. */
			const uint32_t src[] = {
				0xc0017600u, 0x20cu, shaderLo,
				0xc0017600u, 0x20du, shaderHi,
				0xc0027600u, 0x212u, 0x41u, 0,
				0xc0037600u, 0x207u, 1u, 1u, 1u,
				0xc0031502u, 1u, 1u, 1u, 0x47u
			};
			n = r5_p_words(out, cap, src, 20);
			break;
		}
		case R5_P_IB2: {
			/* PKT3 INDIRECT_BUFFER, size only. VMID stays 0 so the
			 * CP keeps the parent VMID. The body is six dwords.
			 * This control word ran without bit 23. */
			if (ib2 == 0)
				return 0;
			const uint32_t src[] = {
				0xc0023f00u, (uint32_t)ib2, (uint32_t)(ib2 >> 32), 6u
			};
			n = r5_p_words(out, cap, src, 4);
			break;
		}
		case R5_P_CONTEXT_CONTROL: {
			const uint32_t src[] = { 0xc0012800u, 0x80000000u, 0x80000000u };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_CLEAR_STATE: {
			const uint32_t src[] = { 0xc0001200u, 0 };
			n = r5_p_words(out, cap, src, 2);
			break;
		}
		case R5_P_EVENT_19: {
			const uint32_t src[] = { 0xc0004600u, 0x19u };
			n = r5_p_words(out, cap, src, 2);
			break;
		}
		case R5_P_EVENT_1A: {
			const uint32_t src[] = { 0xc0004600u, 0x1au };
			n = r5_p_words(out, cap, src, 2);
			break;
		}
		case R5_P_UC_30A00: {
			/* 0x30a00, count 2, zeros. Dword offset 0x280. */
			const uint32_t src[] = { 0xc0027900u, 0x280u, 0, 0 };
			n = r5_p_words(out, cap, src, 4);
			break;
		}
		case R5_P_UC_30920: {
			const uint32_t src[] = {
				0xc0037900u, 0x248u, 0xffffffffu, 0, 0
			};
			n = r5_p_words(out, cap, src, 5);
			break;
		}
		case R5_P_UC_30968: {
			const uint32_t src[] = { 0xc0017900u, 0x25au, 0 };
			n = r5_p_words(out, cap, src, 3);
			break;
		}
		case R5_P_UC_30E00: {
			const uint32_t src[] = { 0xc0027900u, 0x380u, 0, 0 };
			n = r5_p_words(out, cap, src, 4);
			break;
		}
		case R5_P_CONTEXT4:
			n = r5_p_words(out, cap, kR5Context4,
				(uint32_t)(sizeof(kR5Context4) / sizeof(kR5Context4[0])));
			break;
		case R5_P_SH_PREFIX:
			n = r5_p_words(out, cap, kR5ShPrefix,
				(uint32_t)(sizeof(kR5ShPrefix) / sizeof(kR5ShPrefix[0])));
			break;
		case R5_P_PREAMBLE_LO:
			n = r5_p_words(out, cap, kR5Preamble, 0x34u);
			break;
		case R5_P_PREAMBLE_HI:
			n = r5_p_words(out, cap, kR5Preamble + 0x34, 0x34u);
			break;
		case R5_P_PREAMBLE:
			n = r5_p_words(out, cap, kR5Preamble, 0x68u);
			break;
		case R5_P_IB24: {
			/* The thirteenth-stick syslog recorded "submit ib … dw 24"
			 * and did not dump those dwords. This is the Mesa 25.3.6
			 * initial preamble: an IB2 of the 0x68-dword copy, then
			 * PFP_SYNC_ME, ACQUIRE_MEM and EVENT_WRITE 0x19.
			 * ACQUIRE bases are 0. The coherency word is
			 * SH_ICACHE|SH_KCACHE|TC|TCL1|TC_WB from Linux v6.12
			 * gc_9_0_sh_mask.h (0x28c40000). A NOP fills the length
			 * out to 24. No scratch address: that VA was not in the log. */
			if (ib2 == 0)
				return 0;
			const uint32_t src[] = {
				0xc0023f00u, (uint32_t)ib2, (uint32_t)(ib2 >> 32), 0x68u,
				0xc0004200u, 0,
				0xc0055800u, 0x28c40000u, 0xffffffffu, 0x00ffffffu,
				0, 0, 0x0au,
				0xc0004600u, 0x19u,
				0xc0071000u, 0, 0, 0, 0, 0, 0, 0, 0
			};
			n = r5_p_words(out, cap, src, 24);
			break;
		}
		default:
			return 0;
	}
	if (n == 0 || n > cap)
		return 0;
	return n;
}

#endif
