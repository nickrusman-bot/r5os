/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GFX user job, matching the non-MCBP frame Linux puts around the
 * indirect buffers (gfx_v9_0.c emit_cntxcntl, emit_frame_cntl,
 * emit_ib_gfx, emit_fence, emit_sb, and amdgpu_ring_commit).
 * IB_BASE_HI is the full upper 32 bits. The RELEASE_MEM addresses are
 * VMID 0 MC addresses. The fence page is not mapped in VMID 1.
 */

#ifndef R5_CS_H
#define R5_CS_H

#include "r5_gc9_regs.h"

#include <stdint.h>

#include "gfx9_check.h"

/* 64-bit sequence on the kernel fence page, past the K0/K2 words.
 * The 32-bit interrupt fence sits 16 bytes later and does not overlap it. */
static const uint32_t kR5UserFenceOffset = 2048u;
static const uint32_t kR5UserIrqOffset = 2064u;

/* CACHE_FLUSH_AND_INV_TS_EVENT, EVENT_INDEX 5, the EOP action bits
 * from the 6 Nov 2026 Linux capture (0x00238514). */
static const uint32_t kR5CsEvent = 0x00238514u;
/* DATA_SEL 2, no interrupt. The user sequence is 64 bits. */
static const uint32_t kR5CsRel64 = 0x40000000u;
/* DATA_SEL 1 and INT_SEL 2. The second fence raises EOP. */
static const uint32_t kR5CsRel32Int = 0x22000000u;
/* gfx_v9_0 ring nop: PACKET3(PACKET3_NOP, 0x3FFF). */
static const uint32_t kR5CsNop = 0xffff1000u;
/* gfx_v9_ring_emit_cntxcntl, Linux v6.12 gfx_v9_0.c. load_enable is
 * always set. A context switch also sets load_global_config,
 * load_global_uconfig (0x8001), load_cs_sh_regs (0x01000000) and
 * load_per_context_state plus load_gfx_sh_regs (0x10002). That is
 * 0x81018003. dw3 stays 0. */
static const uint32_t kR5CsCntxKeep = 0x80000000u;
static const uint32_t kR5CsCntxLoad = 0x81018003u;
/* amdgpu_job.h. These are the status bits amdgpu_ib_schedule passes
 * to emit_cntxcntl, not the IB flags. */
static const uint32_t kR5PreamblePresent = 1u << 0;
static const uint32_t kR5PreambleFirst = 1u << 1;
/* include/uapi/drm/amdgpu_drm.h AMDGPU_IB_FLAG_PREAMBLE. */
static const uint32_t kR5IbPreamble = 1u << 1;


/* The last context on the ring. Linux stores this as ring->current_ctx,
 * the scheduler fence context of the job (dma_fence_context_alloc).
 * 0 is the kernel's own jobs and is never handed out. */
struct r5_cs_ring_ctx {
	uint64_t	ctx;
	int		valid;
};


struct r5_ctx_counter {
	uint64_t	next;
};


/* dma_fence_context_alloc. The first handed-out value is 1. */
static inline uint64_t
r5_ctx_handout(r5_ctx_counter* counter)
{
	if (counter == 0)
		return 0;
	if (counter->next == 0)
		counter->next = 1;
	uint64_t value = counter->next;
	counter->next++;
	return value;
}


/* amdgpu_ib_schedule: need_ctx_switch = ring->current_ctx != fence_ctx. */
static inline int
r5_cs_ctx_switch(const r5_cs_ring_ctx* last, uint64_t ctx)
{
	if (last == 0 || last->valid == 0)
		return 1;
	return last->ctx != ctx;
}


static inline void
r5_cs_ctx_note(r5_cs_ring_ctx* last, uint64_t ctx)
{
	if (last == 0)
		return;
	last->ctx = ctx;
	last->valid = 1;
}


/* gfx_v9_ring_emit_cntxcntl. preamble is AMDGPU_PREAMBLE_IB_PRESENT
 * and AMDGPU_PREAMBLE_IB_PRESENT_FIRST. A switch with the present bit
 * sets load_ce_ram (0x10000000). Without a switch, only the first
 * presentation of a preamble sets that bit. */
static inline uint32_t
r5_cs_cntx(int haveSwitch, uint32_t preamble)
{
	uint32_t dw2 = 0x80000000u;
	if (haveSwitch) {
		dw2 |= 0x8001u;
		dw2 |= 0x01000000u;
		dw2 |= 0x10002u;
		if ((preamble & kR5PreamblePresent) != 0)
			dw2 |= 0x10000000u;
	} else if ((preamble & kR5PreambleFirst) != 0) {
		dw2 |= 0x10000000u;
	}
	return dw2;
}


/* v6.12 amdgpu_ib_schedule still emits an IB that has
 * AMDGPU_IB_FLAG_PREAMBLE. The load_ce_ram bit above is the preamble
 * rule in gfx_v9_ring_emit_cntxcntl. A repeated preamble, with no
 * context switch and not the first presentation, is not written.
 * The ioctl still rejects every flag, so this does not change a
 * RADV submit until that rejection is lifted. */
static inline int
r5_cs_skip_preamble_ib(uint32_t ibFlags, int haveSwitch, uint32_t preamble)
{
	if ((ibFlags & kR5IbPreamble) == 0)
		return 0;
	if (haveSwitch)
		return 0;
	if ((preamble & kR5PreambleFirst) != 0)
		return 0;
	return 1;
}


/* CP_IB*_BASE_HI keeps bits 15:0. Bit 15 of that field is VA bit 47.
 * A high VA is sign-extended from that bit, so the dump can match the
 * address the submit stored. */
static inline uint64_t
r5_cs_ib_va(uint32_t lo, uint32_t hi)
{
	uint64_t base = ((uint64_t)(hi & 0xffffu) << 32) | (uint64_t)lo;
	if ((base & (1ull << 47)) != 0)
		base |= 0xffff000000000000ull;
	return base;
}


static inline uint32_t
r5_cs_packet3(uint32_t opcode, uint32_t count)
{
	return (3u << 30) | ((count & 0x3fffu) << 16) | ((opcode & 0xffu) << 8);
}


/* gfx_v9_0_ring_emit_ib_gfx: length in the low bits, VMID in bits 24-27.
 * The GFX path does not set INDIRECT_BUFFER_VALID. That bit is the
 * compute path. */
static inline uint32_t
r5_cs_ib_control(uint32_t dwords, uint32_t vmid)
{
	return (dwords & 0xfffffu) | ((vmid & 0xfu) << 24);
}


/* Two SWITCH_BUFFER packets, CONTEXT_CONTROL, both FRAME_CONTROL
 * packets, one IB, both RELEASE_MEM packets and the closing
 * SWITCH_BUFFER: 33 dwords. Each further IB adds 4. Eight IBs is
 * the submit limit. */
static inline uint32_t
r5_cs_user_words(uint32_t count)
{
	if (count == 0 || count > 8)
		return 0;
	return 33u + (count - 1u) * 4u;
}


/* amdgpu_ring_commit with align_mask 0xff. Zero when wptr is already
 * a multiple of 256. */
static inline uint32_t
r5_cs_pad_dwords(uint64_t wptrAfter)
{
	uint32_t mis = (uint32_t)(wptrAfter & 0xffu);
	if (mis == 0)
		return 0;
	return 256u - mis;
}


static inline uint32_t
r5_cs_user_commit(uint64_t wptr, uint32_t count)
{
	uint32_t body = r5_cs_user_words(count);
	if (body == 0)
		return 0;
	return body + r5_cs_pad_dwords(wptr + body);
}


static inline uint32_t
r5_cs_put(uint32_t* out, uint32_t n, uint32_t cap, uint32_t word)
{
	if (out != 0 && n < cap)
		out[n] = word;
	return n + 1u;
}


/* Unpadded frame. out may be NULL to count. Returns 0 when cap is short
 * or an IB is not dword aligned. */
static inline uint32_t
r5_cs_user_body(uint32_t* out, uint32_t cap, const uint64_t* ib,
	const uint32_t* dwords, const uint32_t* vmid, uint32_t count,
	uint64_t userFence, uint64_t sequence, uint64_t irqFence,
	uint32_t cntx, uint32_t skipOpen)
{
	uint32_t n = 0;
	uint32_t i;
	uint32_t expect;
	if (r5_cs_user_words(count) == 0 || ib == 0 || dwords == 0 || vmid == 0)
		return 0;
	if (cntx != r5_cs_cntx(0, 0) && cntx != r5_cs_cntx(1, 0)
		&& cntx != r5_cs_cntx(1, kR5PreamblePresent)
		&& cntx != r5_cs_cntx(0, kR5PreambleFirst)
		&& cntx != r5_cs_cntx(1, kR5PreamblePresent | kR5PreambleFirst))
		return 0;
	/* Two SWITCH_BUFFER packets open the job, as in the capture.
	 * gfx9_flush_fence moves that pair to after the flush fence, so
	 * skipOpen leaves them out of this frame. */
	if (skipOpen == 0) {
		n = r5_cs_put(out, n, cap, r5_cs_packet3(0x8b, 0));
		n = r5_cs_put(out, n, cap, 0);
		n = r5_cs_put(out, n, cap, r5_cs_packet3(0x8b, 0));
		n = r5_cs_put(out, n, cap, 0);
	}
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x28, 1));
	n = r5_cs_put(out, n, cap, cntx);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x90, 0));
	n = r5_cs_put(out, n, cap, 0);
	for (i = 0; i < count; i++) {
		if ((ib[i] & 3ull) != 0 || dwords[i] == 0 || dwords[i] > 0xfffffu
			|| vmid[i] > 15u)
			return 0;
		n = r5_cs_put(out, n, cap, r5_cs_packet3(0x3f, 2));
		n = r5_cs_put(out, n, cap, (uint32_t)ib[i]);
		n = r5_cs_put(out, n, cap, (uint32_t)(ib[i] >> 32));
		n = r5_cs_put(out, n, cap, r5_cs_ib_control(dwords[i], vmid[i]));
	}
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x90, 0));
	n = r5_cs_put(out, n, cap, 0x10000000u);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x49, 6));
	n = r5_cs_put(out, n, cap, kR5CsEvent);
	n = r5_cs_put(out, n, cap, kR5CsRel64);
	n = r5_cs_put(out, n, cap, (uint32_t)userFence);
	n = r5_cs_put(out, n, cap, (uint32_t)(userFence >> 32));
	n = r5_cs_put(out, n, cap, (uint32_t)sequence);
	n = r5_cs_put(out, n, cap, (uint32_t)(sequence >> 32));
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x49, 6));
	n = r5_cs_put(out, n, cap, kR5CsEvent);
	n = r5_cs_put(out, n, cap, kR5CsRel32Int);
	n = r5_cs_put(out, n, cap, (uint32_t)irqFence);
	n = r5_cs_put(out, n, cap, (uint32_t)(irqFence >> 32));
	n = r5_cs_put(out, n, cap, (uint32_t)sequence);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x8b, 0));
	n = r5_cs_put(out, n, cap, 0);
	expect = r5_cs_user_words(count);
	if (skipOpen != 0) {
		if (expect < 4u)
			return 0;
		expect -= 4u;
	}
	if (n != expect || (out != 0 && n > cap))
		return 0;
	return n;
}


/* Linux v6.12 gfx_v9_0.c:gfx_v9_0_ring_emit_fence with
 * AMDGPU_FENCE_FLAG_INT, then the two SWITCH_BUFFER packets.
 * The event word is the same CACHE_FLUSH_AND_INV_TS index 5 packet
 * the job fence uses. 32-bit data, interrupt. 12 dwords. */
static inline uint32_t
r5_cs_flush_fence(uint32_t* out, uint32_t cap, uint32_t n, uint64_t addr,
	uint64_t seq)
{
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x49, 6));
	n = r5_cs_put(out, n, cap, kR5CsEvent);
	n = r5_cs_put(out, n, cap, kR5CsRel32Int);
	n = r5_cs_put(out, n, cap, (uint32_t)addr);
	n = r5_cs_put(out, n, cap, (uint32_t)(addr >> 32));
	n = r5_cs_put(out, n, cap, (uint32_t)seq);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x8b, 0));
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x8b, 0));
	n = r5_cs_put(out, n, cap, 0);
	return n;
}


/* Linux v6.12 gfx_v9_0.c:gfx_v9_0_ring_emit_init_cond_exec.
 * PACKET3_COND_EXEC is 0x22 in sid.h. The last dword is patched with
 * the number of following dwords. The GPU skips them only when the
 * dword at addr is 0. The kernel stores 1 there. */
static inline uint32_t
r5_cs_cond_exec(uint32_t* out, uint32_t cap, uint32_t n, uint64_t addr)
{
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x22, 3));
	n = r5_cs_put(out, n, cap, (uint32_t)addr);
	n = r5_cs_put(out, n, cap, (uint32_t)(addr >> 32));
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, 0);
	return n;
}


static inline void
r5_cs_cond_exec_patch(uint32_t* out, uint32_t slot, uint32_t follow)
{
	if (out != 0)
		out[slot] = follow;
}


/* The GDS resources last written for one VMID. valid is clear until
 * the first switch, which is the same force as a GPU reset in
 * amdgpu_vm_flush. */
struct r5_gds {
	uint32_t	gds_base;
	uint32_t	gds_size;
	uint32_t	gws_base;
	uint32_t	gws_size;
	uint32_t	oa_base;
	uint32_t	oa_size;
	int		valid;
};


/* amdgpu_ids.c:amdgpu_vmid_gds_switch_needed. */
static inline int
r5_gds_switch_needed(const struct r5_gds* id, const struct r5_gds* job)
{
	if (id == 0 || job == 0 || id->valid == 0)
		return 1;
	return id->gds_base != job->gds_base || id->gds_size != job->gds_size
		|| id->gws_base != job->gws_base || id->gws_size != job->gws_size
		|| id->oa_base != job->oa_base || id->oa_size != job->oa_size;
}


/* gfx_v9_0.c:gfx_v9_0_write_data_to_reg. Engine 0, register destination,
 * no write confirm, so the second dword is 0. PACKET3_WRITE_DATA is
 * 0x37 in sid.h. */
static inline uint32_t
r5_cs_write_reg(uint32_t* out, uint32_t cap, uint32_t n, uint32_t reg,
	uint32_t value)
{
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x37, 3));
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, reg);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, value);
	return n;
}


/* gfx_v9_ring_insert_nop. One dword is the single nop 0xFFFF1000.
 * A longer pad is PACKET3(NOP, min(count - 2, 0x3FFE)) and then
 * count - 1 times that nop. count - 1 in the header reads one dword
 * past the pad. */
static inline void
r5_cs_pad(uint32_t* out, uint32_t count)
{
	uint32_t i;
	uint32_t n;
	if (out == 0 || count == 0)
		return;
	if (count == 1) {
		out[0] = kR5CsNop;
		return;
	}
	n = count - 2u;
	if (n > 0x3ffeu)
		n = 0x3ffeu;
	out[0] = r5_cs_packet3(0x10, n);
	for (i = 1; i < count; i++)
		out[i] = kR5CsNop;
}


/* Dword register offset for a CP packet. gc_byte() in the driver is
 * this value times four, and that form is only for MMIO. */
static inline uint32_t
r5_cs_gc_reg(uint32_t index)
{
	return GC_BASE__INST0_SEG0 + index;
}


static inline uint32_t
r5_cs_oss_reg(uint32_t index)
{
	return OSSSYS_BASE__INST0_SEG0 + index;
}


/* gmc_v9_0.c:gmc_v9_0_emit_pasid_mapping for GFXHUB.
 * gfx_v9_0_ring_emit_wreg: WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM. */
static inline uint32_t
r5_cs_pasid_map(uint32_t* out, uint32_t cap, uint32_t n, uint32_t vmid,
	uint32_t pasid)
{
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x37, 3));
	n = r5_cs_put(out, n, cap, 0x40100000u);
	n = r5_cs_put(out, n, cap, r5_cs_oss_reg(mmIH_VMID_0_LUT) + vmid);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, pasid);
	return n;
}


/* gfx_v9_0.c:gfx_v9_0_ring_emit_gds_switch. Twenty dwords. */
static inline uint32_t
r5_cs_gds_switch(uint32_t* out, uint32_t cap, uint32_t n, uint32_t vmid,
	const struct r5_gds* job)
{
	uint32_t oa = 0;
	if (job->oa_base < 32u && job->oa_size < 32u
		&& job->oa_base + job->oa_size < 32u)
		oa = (1u << (job->oa_size + job->oa_base))
			- (1u << job->oa_base);
	n = r5_cs_write_reg(out, cap, n,
		r5_cs_gc_reg(mmGDS_VMID0_BASE) + 2u * vmid, job->gds_base);
	n = r5_cs_write_reg(out, cap, n,
		r5_cs_gc_reg(mmGDS_VMID0_SIZE) + 2u * vmid, job->gds_size);
	n = r5_cs_write_reg(out, cap, n,
		r5_cs_gc_reg(mmGDS_GWS_VMID0) + vmid,
		(job->gws_size << GDS_GWS_VMID0__SIZE__SHIFT) | job->gws_base);
	n = r5_cs_write_reg(out, cap, n,
		r5_cs_gc_reg(mmGDS_OA_VMID0) + vmid, oa);
	return n;
}


/* Linux v6.12 amdgpu_vm_flush emits the pipeline sync first, then the
 * VM flush. gfx_v9_0_ring_emit_vm_flush then writes PACKET3(PFP_SYNC_ME, 0).
 * gmc_v9_0_use_invalidate_semaphore is true only for MMHUB, so this GFXHUB
 * sequence does not write VM_INVALIDATE_ENG0_SEM. PASID is not part of
 * this packet: 26 dwords. gfx_v9_0_check_fw_write_wait leaves
 * me_fw_write_wait true in the default case, which is GC 9.3.
 * gfx_v9_0_ring_emit_wreg then writes a register with
 * WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM (0x40100000). The invalidate
 * request and its ack are one WAIT_REG_MEM, as
 * gfx_v9_0_ring_emit_reg_write_reg_wait does when that flag is set. */
static inline uint32_t
r5_cs_vm_switch_words(void)
{
	return 26u;
}


static inline uint32_t
r5_cs_vm_switch(uint32_t* out, uint32_t cap, uint32_t vmid, uint64_t pd,
	uint64_t fenceMc, uint64_t fenceSeq)
{
	uint32_t n = 0;
	uint32_t inv;
	uint32_t ack;
	uint32_t lo;
	if (vmid == 0 || vmid > 15u || pd == 0)
		return 0;
	inv = r5_gfx9_tlb_req(vmid, 0);
	ack = r5_gfx9_tlb_ack_mask(vmid);
	lo = r5_cs_gc_reg(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32 + vmid * R5_GC9_CTX_ADDR_DISTANCE);
	/* Pipeline sync on the previous user fence. Memory, equal, PFP.
	 * Control 0x113 is function 3, memory space, engine 1.
	 * The poll interval is 4, as in the Linux v6.12 pipeline sync. */
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x3c, 5));
	n = r5_cs_put(out, n, cap, 0x113u);
	n = r5_cs_put(out, n, cap, (uint32_t)fenceMc);
	n = r5_cs_put(out, n, cap, (uint32_t)(fenceMc >> 32));
	n = r5_cs_put(out, n, cap, (uint32_t)fenceSeq);
	n = r5_cs_put(out, n, cap, 0xffffffffu);
	n = r5_cs_put(out, n, cap, 4u);
	/* gfx_v9_0_ring_emit_wreg on the GFX ring.
	 * WRITE_DATA_ENGINE_SEL(1) | WR_CONFIRM is 0x40100000. */
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x37, 3));
	n = r5_cs_put(out, n, cap, 0x40100000u);
	n = r5_cs_put(out, n, cap, lo);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, (uint32_t)pd);
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x37, 3));
	n = r5_cs_put(out, n, cap, 0x40100000u);
	n = r5_cs_put(out, n, cap, lo + 1u);
	n = r5_cs_put(out, n, cap, 0);
	n = r5_cs_put(out, n, cap, (uint32_t)(pd >> 32));
	/* gfx_v9_0_ring_emit_reg_write_reg_wait with me_fw_write_wait.
	 * Control 0x143 is function 3 (equal), operation 1 (write-wait-write)
	 * and engine 1 (PFP). reg0 is the request, reg1 the ack. */
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x3c, 5));
	n = r5_cs_put(out, n, cap, 0x143u);
	n = r5_cs_put(out, n, cap, r5_cs_gc_reg(mmVM_INVALIDATE_ENG0_REQ));
	n = r5_cs_put(out, n, cap, r5_cs_gc_reg(mmVM_INVALIDATE_ENG0_ACK));
	n = r5_cs_put(out, n, cap, inv);
	n = r5_cs_put(out, n, cap, ack);
	n = r5_cs_put(out, n, cap, 0x20u);
	/* PACKET3_PFP_SYNC_ME is 0x42. The PFP must not prefetch with the
	 * old translation. */
	n = r5_cs_put(out, n, cap, r5_cs_packet3(0x42, 0));
	n = r5_cs_put(out, n, cap, 0);
	if (n != r5_cs_vm_switch_words() || (out != 0 && n > cap))
		return 0;
	return n;
}

#endif
