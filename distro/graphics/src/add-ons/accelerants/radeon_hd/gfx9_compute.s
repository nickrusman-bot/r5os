// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 R5OS Contributors
//
// Two compute kernels for GC 9.3 (gfx90c). The instructions are the test.
// Directives may be adjusted so llvm-mc accepts them.

.text
.globl r5_empty
.p2align 8
.type r5_empty,@function
r5_empty:
	s_endpgm
.size r5_empty, .-r5_empty

.globl r5_fill
.p2align 8
.type r5_fill,@function
r5_fill:
	s_lshl_b32 s3, s2, 6
	v_add_u32_e32 v1, s3, v0
	v_mul_u32_u24_e32 v2, 3, v1
	v_add_u32_e32 v2, 7, v2
	v_lshlrev_b32_e32 v3, 2, v1
	global_store_dword v3, v2, s[0:1]
	s_waitcnt vmcnt(0)
	s_endpgm
.size r5_fill, .-r5_fill

.rodata
.p2align 6
.amdhsa_kernel r5_empty
	.amdhsa_group_segment_fixed_size 0
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_system_vgpr_workitem_id 0
	.amdhsa_next_free_vgpr 4
	.amdhsa_next_free_sgpr 4
	.amdhsa_reserve_flat_scratch 0
	.amdhsa_reserve_xnack_mask 0
	.amdhsa_reserve_vcc 0
	.amdhsa_ieee_mode 0
	.amdhsa_dx10_clamp 0
.end_amdhsa_kernel

.amdhsa_kernel r5_fill
	.amdhsa_group_segment_fixed_size 0
	.amdhsa_private_segment_fixed_size 0
	.amdhsa_user_sgpr_kernarg_segment_ptr 1
	.amdhsa_system_sgpr_workgroup_id_x 1
	.amdhsa_system_vgpr_workitem_id 0
	.amdhsa_next_free_vgpr 4
	.amdhsa_next_free_sgpr 4
	.amdhsa_reserve_flat_scratch 0
	.amdhsa_reserve_xnack_mask 0
	.amdhsa_reserve_vcc 0
	.amdhsa_ieee_mode 0
	.amdhsa_dx10_clamp 0
.end_amdhsa_kernel
