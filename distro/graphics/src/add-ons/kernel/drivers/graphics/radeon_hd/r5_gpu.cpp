/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Render node /dev/gpu/radeon_hd/<n>. The graphics node is unchanged.
 * Without gpu_render the node only answers GET_INFO. VMID 1 is one
 * shared address space unless gfx9_vmid_per_file is set. A job that
 * stays in the ring for 5 seconds
 * marks the device lost. A client wait does not. There is no reset.
 * The GFX watchdog is 10 seconds, the same as amdgpu.lockup_timeout.
 */

#include "radeon_hd_private.h"
#include "gfx9_start.h"
#include "r5_gpu.h"
#include "r5_span.h"
#include "r5_wait.h"
#include "r5_stage_p.h"
#include "r5_stage_r1.h"
#include "r5_log.h"
#include "r5_vm.h"
#include "r5_pasid.h"
#include "r5_vmid.h"
#include "r5_defer.h"

#include <KernelExport.h>
#include <condition_variable.h>
#include <kernel.h>

#include <driver_settings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* vm.h pulls thread types in before the mutex type. These are the calls
 * radeon_hd already resolves from the kernel, same as vm_set_area_memory_type.
 */
extern "C" area_id vm_clone_area(team_id team, const char* name, void** address,
	uint32 addressSpec, uint32 protection, uint32 mapping, area_id sourceArea,
	bool kernel);
extern "C" status_t vm_delete_area(team_id teamID, area_id areaID, bool kernel);
extern "C" team_id team_get_current_team_id(void);
extern "C" bool team_is_valid(team_id id);
extern "C" status_t vm_set_area_memory_type(area_id id,
	phys_addr_t physicalBase, uint32 type);
area_id r5_gfx9_map_vram(uint64 offset, uint64 bytes, void** address);

/* headers/private/system/vm_defs.h: REGION_NO_PRIVATE_MAP = 0. */
static const uint32 kRegionNoPrivateMap = 0;


extern radeon_info* gDeviceInfo[];


static const uint32 kGcSegment = 0x2000;
static const uint32 kNbioSegment = 0x0D20;
static const uint32 kStrapIndex = 0x000f;
static const uint32 kStrapMask = 0x0F000000u;
static const uint32 kStrapShift = 24;
static const uint32 kShaderArrayConfig = 0x026f;
static const uint32 kUserShaderArrayConfig = 0x0270;
static const uint32 kGbAddrConfig = 0x063e;
static const uint32 kRbBackendDisable = 0x063d;
static const uint32 kUserRbBackendDisable = 0x06df;
static const uint32 kRbDisableShift = 16;
static const uint32 kRbDisableMask = 0x00ff0000u;
/* gfx_v9_0 for IP 9.3.0: one shader engine, one array, two backends. */
static const uint32 kMaxBackendsPerSe = 2;
static const uint32 kInactiveShift = 16;
static const uint32 kInactiveMask = 0xffff0000u;
static const uint32 kCuPerSh = 8;
static const uint32 kMaxBo = 256;
static const uint32 kMaxClientBo = 256;
static const uint32 kMaxSync = 128;
static const uint32 kMaxRange = 8;
static const uint32 kMaxJob = 64;
static const uint32 kMaxList = 8;
static const uint64 kMaxBoBytes = 64ull * 1024ull * 1024ull;
static const uint64 kRoomWaitUs = 100000;
static const uint64 kFreeWaitUs = 5000000;
/* amdgpu.lockup_timeout for GFX is 10000 ms. Client waits do not use this. */
static const bigtime_t kWatchdogUs = 10000000;

static int sErrorLines;
static int sOnceBits;
static int sDebugOn = -1;
static int sDeferred = 0;
static r5_defer sDefer[256];
static void gpu_defer_stamp(uint64 sequence);
static int sSelftestOn = -1;


static void
gpu_cache_log_settings()
{
	bool debugOn = false;
	bool selftestOn = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		debugOn = get_driver_boolean_parameter(settings, "gpu_debug",
			false, true);
		selftestOn = get_driver_boolean_parameter(settings, "gpu_selftest",
			false, true);
		r5_gfx9_set_flush_fence(get_driver_boolean_parameter(settings,
			"gfx9_flush_fence", false, true) ? 1 : 0);
		r5_gfx9_set_pasid_map(get_driver_boolean_parameter(settings,
			"gfx9_pasid_map", false, true) ? 1 : 0);
		r5_gfx9_set_ce_meta(get_driver_boolean_parameter(settings,
			"gfx9_ce_meta", false, true) ? 1 : 0);
		r5_gfx9_set_cond_exec(get_driver_boolean_parameter(settings,
			"gfx9_cond_exec", false, true) ? 1 : 0);
		r5_gfx9_set_gds_switch(get_driver_boolean_parameter(settings,
			"gfx9_gds_switch", false, true) ? 1 : 0);
		sDeferred = get_driver_boolean_parameter(settings,
			"gfx9_deferred_free", false, true) ? 1 : 0;
		unload_driver_settings(settings);
	} else {
		r5_gfx9_set_flush_fence(0);
		r5_gfx9_set_pasid_map(0);
		r5_gfx9_set_ce_meta(0);
		r5_gfx9_set_cond_exec(0);
		r5_gfx9_set_gds_switch(0);
		sDeferred = 0;
	}
	r5_gfx9_set_defer_stamp(sDeferred ? gpu_defer_stamp : 0);
	sDebugOn = debugOn ? 1 : 0;
	sSelftestOn = selftestOn ? 1 : 0;
}


static bool
gpu_debug_enabled()
{
	if (sDebugOn < 0)
		gpu_cache_log_settings();
	return sDebugOn != 0;
}


static bool
gpu_dump_begin()
{
	return r5_log_allow(0, 0, R5_LOG_DUMP, &sErrorLines, &sOnceBits, -1) != 0;
}


static bool
gpu_dump_line()
{
	return r5_log_allow(0, 0, R5_LOG_DUMP, &sErrorLines, &sOnceBits, 0) != 0;
}


static bool
gpu_error_line()
{
	return r5_log_allow(0, 0, R5_LOG_ERROR, &sErrorLines, &sOnceBits, 0) != 0;
}


bool
r5_gpu_log_error()
{
	return gpu_error_line();
}


/* The first 32 refusals in a boot. Later ones still return the status. */
static status_t
gpu_refuse(const char* op, const char* why, status_t status, uint32 handle,
	uint32 domain, uint64 bytes, uint64 va, uint64 offset, uint64 mapBytes,
	uint32 flags)
{
	if (gpu_error_line()) {
		dprintf("radeon_hd: refuse %s %s status %#x handle %lu domain %lu "
			"bytes %llu va %#llx offset %llu map %llu flags %#lx\n",
			op, why, (unsigned)status, (unsigned long)handle,
			(unsigned long)domain, (unsigned long long)bytes,
			(unsigned long long)va, (unsigned long long)offset,
			(unsigned long long)mapBytes, (unsigned long)flags);
	}
	return status;
}


static uint32
gpu_mmio(radeon_info* info, uint32 segment, uint32 index)
{
	uint32 byte = (segment + index) * 4u;
	if (info == NULL || info->registers_area < 0 || info->registers == 0)
		return 0xffffffffu;
	return *(volatile uint32*)(info->registers + byte);
}


static uint32
gpu_strap_rev(radeon_info* info)
{
	/* nbio_v7_0_get_rev_id: mmRCC_DEV0_EPF0_STRAP0 in NBIO segment 2. */
	uint32 strap = gpu_mmio(info, kNbioSegment, kStrapIndex);
	return (strap & kStrapMask) >> kStrapShift;
}


static uint32
gpu_active_cus(radeon_info* info, uint32* _bitmap)
{
	uint32 cc = gpu_mmio(info, kGcSegment, kShaderArrayConfig);
	uint32 user = gpu_mmio(info, kGcSegment, kUserShaderArrayConfig);
	uint32 inactive = ((cc | user) & kInactiveMask) >> kInactiveShift;
	uint32 mask = (1u << kCuPerSh) - 1u;
	uint32 bitmap = (~inactive) & mask;
	uint32 count = 0;
	for (uint32 bit = bitmap; bit != 0; bit >>= 1)
		count += bit & 1u;
	if (_bitmap != NULL)
		*_bitmap = bitmap;
	if (gpu_debug_enabled())
	dprintf("radeon_hd: t %lld gfx9 cu cc %08lx user %08lx active %lu bitmap %08lx\n",
		(long long)system_time(),
		(unsigned long)cc, (unsigned long)user, (unsigned long)count,
		(unsigned long)bitmap);
	return count;
}


/* gfx_v9_0_get_rb_active_bitmap and gfx_v9_0_setup_rb. One SE and one
 * SH, so this does not write GRBM_GFX_INDEX. num is the pipe count
 * Linux puts in num_rb_pipes (max backends times shader engines).
 * mask is backend_enable_mask. */
static void
gpu_rb(radeon_info* info, uint32* num, uint32* mask)
{
	uint32 cc = gpu_mmio(info, kGcSegment, kRbBackendDisable);
	uint32 user = gpu_mmio(info, kGcSegment, kUserRbBackendDisable);
	uint32 disabled = ((cc | user) & kRbDisableMask) >> kRbDisableShift;
	uint32 bits = (1u << kMaxBackendsPerSe) - 1u;
	uint32 active = (~disabled) & bits;
	if (num != NULL)
		*num = kMaxBackendsPerSe;
	if (mask != NULL)
		*mask = active;
	if (r5_log_allow(0, 0, R5_LOG_ONCE, &sErrorLines, &sOnceBits, R5_ONCE_RB))
	dprintf("radeon_hd: rb num %lu mask %lx cc %08lx user %08lx\n",
		(unsigned long)kMaxBackendsPerSe, (unsigned long)active,
		(unsigned long)cc, (unsigned long)user);
}


static bool
gpu_render_enabled()
{
	bool enabled = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		enabled = get_driver_boolean_parameter(settings, "gpu_render",
			false, true);
		unload_driver_settings(settings);
	}
	return enabled;
}


struct gpu_client;

struct gpu_sync {
	bool			live;
	volatile uint64	point;
	volatile uint64	submitted;
	gpu_client*		owner;
};


struct gpu_range {
	bool		live;
	uint64		va;
	uint64		offset;
	uint64		bytes;
	/* The tree this range was mapped in. Unmap uses this, not a global. */
	r5_vm*		vm;
};


/* The page table outlives the client while a parked buffer still
 * points at it. refs is the client plus each parked buffer. */
struct r5_file_tree {
	r5_vm			vm;
	r5_vm_page*		pages;
	uint32			pagesN;
	uint32			owner;
	uint64			seen;
	r5_file_count	count;
};


struct gpu_bo {
	bool			live;
	uint32			domain;
	uint32			flags;
	uint64			bytes;
	uint64			vramOffset;
	area_id			area;
	team_id			mapTeam;
	area_id			kernelArea;
	void*			kernel;
	gpu_client*		owner;
	bool			parked;
	uint64			parkSequence;
	/* One hold on the file tree, taken when this buffer is parked. */
	r5_file_tree*	hold;
	gpu_range		ranges[kMaxRange];
};


struct gpu_client;

struct gpu_job {
	bool			live;
	uint64			sequence;
	bigtime_t		submitted;
	uint32			count;
	gpu_sync*		syncs[kMaxList];
	uint64			points[kMaxList];
	gpu_client*		owner;
};


struct gpu_prt {
	bool			live;
	uint64			va;
	uint64			bytes;
	r5_vm*			vm;
};


struct gpu_client {
	radeon_info*	info;
	uint32			handles[kMaxClientBo];
	uint32			handleCount;
	int32			contexts;
	uint64			lastSequence;
	/* Linux amdgpu_ctx.preamble_presented, for this client's one context. */
	int32			preamblePresented;
	/* Handed out at CTX create. 0 means there is no live context. */
	uint64			fenceCtx;
	uint32			pasid;
	/* Own page table when gfx9_vmid_per_file is on. Off leaves this empty. */
	r5_file_tree*	tree;
	/* Set by the retire thread when this file's PASID faults. */
	bool			faulted;
	gpu_client*		next;
	gpu_sync		syncs[kMaxSync];
};


static const uint32 kMaxPrt = 32;

static gpu_bo sBo[kMaxBo];
static gpu_job sJobs[kMaxJob];
static gpu_prt sPrt[kMaxPrt];
static uint64 sHighestSequence;
static uint64 sLastIbVa;
static uint64 sLastIbList[8];
static uint32 sLastIbDw[8];
static uint32 sLastIbN;
/* Kernel jobs (mark, stage P, the copies) use fence context 0. */
static int32 sKernelPreamble;
static radeon_info* sInfo;
static r5_vmid_mgr sFileVmids;
static bool sFileVmidsReady = false;
static uint32 sNextFileOwner = 1;
static bool sReplacing;
static lock sGpuLock;
static ConditionVariable sEop;
static bool sReady = false;
static bool sLost = false;
static gpu_client* sGuilty = NULL;
static int32 sContexts = 0;
static r5_ctx_counter sCtxCounter;
static r5_pasid_ida sPasids;
static gpu_client* sClientList;
static sem_id sRetireSem = -1;
static thread_id sRetireThread = -1;


static void gpu_ib_words(const char* why);
static void gpu_dump_priv();


static void
gpu_lock()
{
	acquire_lock(&sGpuLock);
}


static void
gpu_unlock()
{
	release_lock(&sGpuLock);
}


static void
gpu_lost(radeon_info* info, const char* why)
{
	if (sLost)
		return;
	sLost = true;
	(void)info;
	for (uint32 i = 0; i < kMaxJob; i++)
		sJobs[i].live = false;
	sEop.NotifyAll();
	gpu_ib_words(why);
}


static status_t
gpu_copy_in(void* buffer, size_t length, void* local, size_t bytes)
{
	if (buffer == NULL || bytes < 8 || length < bytes)
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, bytes))
		return B_BAD_ADDRESS;
	if (user_memcpy(local, buffer, bytes) != B_OK)
		return B_BAD_ADDRESS;
	uint32 size = *(uint32*)local;
	if (size != (uint32)bytes || size > length)
		return B_BAD_VALUE;
	return B_OK;
}


static status_t
gpu_copy_out(void* buffer, size_t length, const void* local, size_t bytes)
{
	if (!is_user_address_range(buffer, bytes) || length < bytes)
		return B_BAD_ADDRESS;
	if (user_memcpy(buffer, local, bytes) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
gpu_copy_user(void* local, uint64 address, size_t bytes)
{
	if (bytes == 0)
		return B_OK;
	const void* user = (const void*)(addr_t)address;
	if (!is_user_address_range(user, bytes))
		return B_BAD_ADDRESS;
	if (user_memcpy(local, user, bytes) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static void r5_gpu_eop();
static void gpu_sync_host(gpu_sync* sync);


static void gpu_release_parked(uint64 seen);
static void gpu_fault_retire();
static void gpu_prt_release_tree(r5_vm* vm);
static status_t gpu_file_root(gpu_client* client);
static void gpu_defer_stamp(uint64 sequence);
static void gpu_watchdog();


static status_t
gpu_retire_thread(void* data)
{
	(void)data;
	while (sReady) {
		status_t got = acquire_sem_etc(sRetireSem, 1, B_RELATIVE_TIMEOUT,
			1000000);
		if (!sReady)
			break;
		if (got != B_OK && got != B_TIMED_OUT)
			break;
		gpu_lock();
		/* The one-second timeout is the check when no interrupt arrives.
		 * Retire from the user fence first, then look at what is left. */
		r5_gpu_eop();
		gpu_fault_retire();
		gpu_release_parked(r5_gfx9_user_seen());
		gpu_dump_priv();
		gpu_watchdog();
		gpu_unlock();
	}
	return B_OK;
}


extern "C" void
r5_gpu_retire_wake()
{
	if (sRetireSem >= 0)
		release_sem_etc(sRetireSem, 1, B_DO_NOT_RESCHEDULE);
}


extern "C" void
r5_gpu_init()
{
	if (sReady)
		return;
	gpu_cache_log_settings();
	init_lock(&sGpuLock, "r5 gpu");
	sEop.Init(&sEop, "r5 gpu eop");
	sRetireSem = create_sem(0, "r5 gpu retire");
	sReady = true;
	if (sRetireSem >= 0) {
		sRetireThread = spawn_kernel_thread(gpu_retire_thread,
			"r5 gpu retire", B_NORMAL_PRIORITY, NULL);
		if (sRetireThread >= 0)
			resume_thread(sRetireThread);
	}
}


extern "C" void
r5_gpu_uninit()
{
	sReady = false;
	if (sRetireSem >= 0)
		release_sem(sRetireSem);
	if (sRetireThread >= 0) {
		status_t result = B_OK;
		wait_for_thread(sRetireThread, &result);
		sRetireThread = -1;
	}
	if (sRetireSem >= 0) {
		delete_sem(sRetireSem);
		sRetireSem = -1;
	}
}


static void
r5_gpu_eop()
{
	if (!sReady)
		return;
	uint64 seen = r5_gfx9_user_seen();
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || job->sequence > seen)
			continue;
		for (uint32 n = 0; n < job->count; n++) {
			gpu_sync* sync = job->syncs[n];
			if (sync != NULL && sync->live && job->points[n] > sync->point)
				sync->point = job->points[n];
		}
		job->live = false;
	}
	sEop.NotifyAll();
}


static uint64
gpu_counter(volatile uint64* value)
{
	if (value == NULL)
		return r5_gfx9_user_seen();
	return *value;
}


static status_t
gpu_wait_value(radeon_info* info, volatile uint64* value, uint64 target,
	uint64 timeoutUs)
{
	(void)info;
	int step = r5_wait_step(gpu_counter(value), target, timeoutUs, 0);
	if (step == R5_WAIT_DONE)
		return B_OK;
	if (sLost)
		return ECANCELED;
	if (step == R5_WAIT_POLL)
		return B_TIMED_OUT;
	bigtime_t deadline = system_time() + (bigtime_t)timeoutUs;
	while (true) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		bigtime_t now = system_time();
		step = r5_wait_step(gpu_counter(value), target, timeoutUs,
			now >= deadline);
		if (step == R5_WAIT_DONE)
			return B_OK;
		if (sLost)
			return ECANCELED;
		if (step == R5_WAIT_TIMEOUT)
			return B_TIMED_OUT;
		bigtime_t left = deadline - now;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, left);
		gpu_lock();
	}
}


static status_t
gpu_get_info(gpu_client* client, void* buffer, size_t length)
{
	radeon_info* info = client != NULL ? client->info : NULL;
	if (info == NULL || info->pci == NULL || buffer == NULL || length < 8)
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, length))
		return B_BAD_ADDRESS;
	uint32 claimed = 0;
	if (user_memcpy(&claimed, buffer, sizeof(claimed)) != B_OK)
		return B_BAD_ADDRESS;
	if (claimed > length)
		return B_BAD_VALUE;

	r5_gpu_info out;
	memset(&out, 0, sizeof(out));
	out.size = (uint32)sizeof(out);
	out.version = 1;
	out.api_major = R5_GPU_API_MAJOR;
	out.api_minor = R5_GPU_API_MINOR;
	out.vendor = 0x1002;
	out.device = info->pci->device_id;
	out.revision = info->pci->revision;
	if (gpu_render_enabled())
		out.capabilities = R5_GPU_CAP_RENDER;
	gpu_lock();
	if (sLost)
		out.capabilities |= R5_GPU_CAP_LOST;
	if (sLost && client != NULL
		&& (client == sGuilty || client->faulted))
		out.capabilities |= R5_GPU_CAP_GUILTY;
	out.va_offset = R5_GPU_VA_OFFSET;
	out.va_max = R5_GPU_VA_MAX;
	/* amdgpu_kms.c for this VM: the hole ends at AMDGPU_GMC_HOLE_END
	 * and the high manager runs up to the byte size minus the reserved
	 * top. The page tables are the existing 4-level tables. */
	out.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	out.high_va_max = R5_GPU_HIGH_VA_MAX;
	out.vendor_block_size = (uint32)sizeof(r5_gpu_amd_info);
	out.vendor_block_version = 1;
	out.bus = info->pci->bus;
	out.dev = info->pci->device;
	out.func = info->pci->function;
	out.subsystem_vendor = info->pci->u.h0.subsystem_vendor_id;
	out.subsystem_device = info->pci->u.h0.subsystem_id;
	if (info->shared_info != NULL) {
		out.vram_bytes = (uint64)info->shared_info->frame_buffer_size * 1024ull;
		out.gtt_bytes = out.vram_bytes;
	}
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live)
			continue;
		if (sBo[i].domain == R5_GPU_DOMAIN_VRAM)
			out.vram_usage += sBo[i].bytes;
		else if (sBo[i].domain == R5_GPU_DOMAIN_GTT)
			out.gtt_usage += sBo[i].bytes;
	}
	/* The whole BAR is CPU-visible on this APU. There is no invisible heap. */
	out.visible_vram_usage = out.vram_usage;
	gpu_unlock();
	size_t copy = sizeof(out);
	if (length < copy)
		copy = length;
	if (user_memcpy(buffer, &out, copy) != B_OK)
		return B_BAD_ADDRESS;
	if (length < sizeof(out) + sizeof(r5_gpu_amd_info))
		return B_OK;

	r5_gpu_amd_info amd;
	memset(&amd, 0, sizeof(amd));
	amd.size = (uint32)sizeof(amd);
	amd.version = 1;
	uint32 strap = gpu_strap_rev(info);
	uint32 bitmap = 0;
	amd.device.device_id = info->pci->device_id;
	amd.device.chip_rev = strap;
	amd.device.external_rev = r5_amd_external_rev(info->pci->device_id, strap);
	amd.device.pci_rev = info->pci->revision;
	amd.device.family = R5_AMDGPU_FAMILY_RV;
	amd.device.num_shader_engines = 1;
	amd.device.num_shader_arrays_per_engine = 1;
	amd.device.cu_active_number = gpu_active_cus(info, &bitmap);
	amd.device.cu_bitmap[0][0] = bitmap;
	amd.device.num_cu_per_sh = kCuPerSh;
	amd.device.ids_flags = R5_AMDGPU_IDS_FLAGS_FUSION;
	{
		uint32 rbNum = 0;
		uint32 rbMask = 0;
		gpu_rb(info, &rbNum, &rbMask);
		amd.device.num_rb_pipes = rbNum;
		amd.device.enabled_rb_pipes_mask = rbMask;
	}
	/* gfx_v9_0_gpu_early_init, IP 9.3.0. */
	amd.device.num_hw_gfx_contexts = 8;
	/* gfx_v9_0_sw_init. The same value for every GFX9. */
	amd.device.ce_ram_size = 0x8000;
	/* Default VM fragment size 9. (1 << 9) * 4096. */
	amd.device.pte_fragment_size = 2097152;
	/* Mesa drm-shim for device 15e7. The SMU and the UMC table are
	 * not read. min equals max, as the non-DPM branch of amdgpu_kms.c
	 * does. Clocks are kHz. */
	amd.device.max_engine_clock = 1800000;
	amd.device.min_engine_clock = 1800000;
	amd.device.max_memory_clock = 1333000;
	amd.device.min_memory_clock = 1333000;
	amd.device.vram_type = 11;
	amd.device.vram_bit_width = 128;
	amd.device.gc_double_offchip_lds_buf = 1;
	/* gfx9 max_gprs, max_texture_channel_caches, and the GS depths
	 * from the Renoir config Linux copies into this struct. */
	amd.device.num_shader_visible_vgprs = 256;
	amd.device.num_tcc_blocks = 4;
	amd.device.gs_vgt_table_depth = 32;
	amd.device.gs_prim_buffer_depth = 1792;
	amd.device.max_gs_waves_per_vgt = 32;
	/* kHz. Mesa's captured amdgpu query for PCI 15e7 (Renoir) reports
	 * gpu_counter_freq 100000. Linux copies adev->clock.xclk into that
	 * field. A zero makes RADV warn that timestamps will be wrong. */
	if (info->pci->device_id == 0x15e7)
		amd.device.gpu_counter_freq = 100000;
	amd.device.virtual_address_offset = R5_GPU_VA_OFFSET;
	amd.device.virtual_address_max = R5_GPU_VA_MAX;
	amd.device.virtual_address_alignment = 4096;
	amd.device.gart_page_size = 4096;
	amd.device.wave_front_size = 64;
	amd.device.high_va_offset = R5_GPU_HIGH_VA_OFFSET;
	amd.device.high_va_max = R5_GPU_HIGH_VA_MAX;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major = 9;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_minor = 3;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_start_alignment = 32;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].ib_size_alignment = 4;
	amd.hw_ip[R5_AMDGPU_HW_IP_GFX].available_rings = 1;
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE] = amd.hw_ip[R5_AMDGPU_HW_IP_GFX];
	amd.hw_ip[R5_AMDGPU_HW_IP_COMPUTE].available_rings = 0;
	for (uint32 i = 0; i < R5_AMDGPU_FW_COUNT && i < 32; i++) {
		amd.firmware[i].ver = info->fw_version[i];
		amd.firmware[i].feature = info->fw_feature[i];
	}
	if (user_memcpy((uint8*)buffer + sizeof(out), &amd, sizeof(amd)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static r5_vm*
gpu_vm(gpu_client* client)
{
	if (client != NULL && client->tree != NULL)
		return &client->tree->vm;
	return r5_gfx9_shared_vm();
}


static r5_file_tree*
gpu_tree_of(r5_vm* vm)
{
	if (vm == NULL || vm == r5_gfx9_shared_vm())
		return NULL;
	return (r5_file_tree*)vm;
}


static void
r5_file_tree_free(r5_file_tree* tree)
{
	if (tree->owner != 0)
		r5_vmid_release(&sFileVmids, tree->owner, tree->seen);
	gpu_prt_release_tree(&tree->vm);
	free(tree->pages);
	free(tree);
}


static void
r5_file_tree_drop(r5_file_tree* tree)
{
	if (tree == NULL)
		return;
	if (r5_file_count_drop(&tree->count, (int32)tree->pagesN))
		r5_file_tree_free(tree);
}


static void
gpu_bo_hold_tree(gpu_bo* bo)
{
	if (bo->hold != NULL)
		return;
	for (uint32 i = 0; i < kMaxRange; i++) {
		if (!bo->ranges[i].live)
			continue;
		r5_file_tree* tree = gpu_tree_of(bo->ranges[i].vm);
		if (tree == NULL)
			continue;
		r5_file_count_hold(&tree->count);
		bo->hold = tree;
		return;
	}
}


static void
gpu_bo_unmap(gpu_bo* bo)
{
	bool any = false;
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live)
			continue;
		r5_vm* vm = range->vm != NULL ? range->vm : r5_gfx9_shared_vm();
		for (uint64 page = 0; page < range->bytes; page += 4096)
			r5_gfx9_vm_map(vm, range->va + page, 0);
		range->live = false;
		range->vm = NULL;
		any = true;
	}
	if (any)
		r5_gfx9_vmid1_flush();
}


static void
gpu_bo_release(gpu_bo* bo)
{
	if (!bo->live)
		return;
	gpu_bo_unmap(bo);
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		if (!sDeferred || !r5_defer_add(sDefer, 256, bo->vramOffset))
			r5_gfx9_free_user_vram(bo->vramOffset);
	}
	/* The user clone belongs to mapTeam. A team that has already ended
	 * took its areas with it. team_is_valid is the check; do not delete
	 * an id in a team that is gone.
	 */
	if (bo->area >= 0 && team_is_valid(bo->mapTeam))
		vm_delete_area(bo->mapTeam, bo->area, true);
	if (bo->kernelArea >= 0)
		delete_area(bo->kernelArea);
	bo->live = false;
	bo->owner = NULL;
	bo->parked = false;
	bo->parkSequence = 0;
	if (bo->hold != NULL) {
		r5_file_tree* tree = bo->hold;
		bo->hold = NULL;
		r5_file_tree_drop(tree);
	}
	bo->area = -1;
	bo->mapTeam = -1;
	bo->kernelArea = -1;
	bo->kernel = NULL;
}


static bool gpu_selftest_enabled();


static status_t
gpu_bo_create(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_CREATE", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.bytes == 0 || in.bytes > kMaxBoBytes)
		return gpu_refuse("BO_CREATE", "size", B_BAD_VALUE, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	if (in.domain != R5_GPU_DOMAIN_VRAM && in.domain != R5_GPU_DOMAIN_GTT)
		return gpu_refuse("BO_CREATE", "domain", B_BAD_VALUE, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	if (client->handleCount >= kMaxClientBo)
		return gpu_refuse("BO_CREATE", "handles", B_NO_MEMORY, 0, in.domain,
			in.bytes, 0, 0, 0, in.flags);
	uint64 bytes = (in.bytes + 4095ull) & ~4095ull;
	int slot = -1;
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return gpu_refuse("BO_CREATE", "slots", B_NO_MEMORY, 0, in.domain,
			bytes, 0, 0, 0, in.flags);
	gpu_bo* bo = &sBo[slot];
	memset(bo, 0, sizeof(*bo));
	bo->area = -1;
	bo->mapTeam = -1;
	bo->kernelArea = -1;
	bo->domain = in.domain;
	bo->flags = in.flags;
	bo->bytes = bytes;
	if (in.domain == R5_GPU_DOMAIN_VRAM) {
		uint64 offset = 0;
		if (client->tree != NULL && client->info != NULL
			&& client->info->shared_info != NULL) {
			uint64 limit = (uint64)client->info->shared_info->frame_buffer_size
				* 1024ull;
			uint64 held = 0;
			for (uint32 i = 0; i < kMaxBo; i++) {
				if (sBo[i].live && sBo[i].owner == client
					&& sBo[i].domain == R5_GPU_DOMAIN_VRAM)
					held += sBo[i].bytes;
			}
			if (!r5_vram_fits(limit, held, client->tree->vm.used, bytes))
				return gpu_refuse("BO_CREATE", "vram", B_NO_MEMORY, 0,
					in.domain, bytes, 0, 0, 0, in.flags);
		}
		if (r5_gfx9_alloc_user_vram(bytes, &offset) != B_OK)
			return gpu_refuse("BO_CREATE", "vram", B_NO_MEMORY, 0, in.domain,
				bytes, 0, 0, 0, in.flags);
		bo->vramOffset = offset;
	} else {
		void* kernel = NULL;
		/* Not B_CLONEABLE_AREA. The user mapping is vm_clone_area below,
		 * with kernel true, so the source does not have to be cloneable.
		 * A cloneable id would let any program map this buffer.
		 */
		area_id area = create_area("r5 gpu gtt", &kernel,
			B_ANY_KERNEL_ADDRESS, (size_t)bytes, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (area < 0 || kernel == NULL)
			return gpu_refuse("BO_CREATE", "gtt", area < 0 ? area : B_NO_MEMORY,
				0, in.domain, bytes, 0, 0, 0, in.flags);
		bo->kernelArea = area;
		bo->kernel = kernel;
	}
	/* R5_GPU_BO_WC stays stored. The RAM area stays write-back, and a
	 * GTT page is snooped. The reply carries the flags that were used. */
	{
		uint32 used = in.flags & ~R5_GPU_BO_WC;
		if (gpu_selftest_enabled()) {
			dprintf("radeon_hd: bo domain %lu requested %08lx used %08lx "
				"bytes %llu snooped %d\n",
				(unsigned long)in.domain, (unsigned long)in.flags,
				(unsigned long)used, (unsigned long long)bytes,
				in.domain == R5_GPU_DOMAIN_GTT ? 1 : 0);
		}
		in.flags = used;
	}
	bo->live = true;
	bo->owner = client;
	client->handles[client->handleCount++] = (uint32)slot + 1u;
	in.handle = (uint32)slot + 1u;
	in.area = bo->area;
	in.bytes = bytes;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_CREATE", "copy", status, in.handle, in.domain,
			bytes, 0, 0, 0, in.flags);
	return B_OK;
}


static gpu_bo*
gpu_bo_get(gpu_client* client, uint32 handle)
{
	if (handle == 0 || handle > kMaxBo)
		return NULL;
	gpu_bo* bo = &sBo[handle - 1u];
	if (!bo->live || bo->owner != client)
		return NULL;
	return bo;
}


static void
gpu_defer_stamp(uint64 sequence)
{
	r5_defer_stamp(sDefer, 256, sequence);
}


static void
gpu_defer_reclaim(uint64 seen)
{
	uint64 page = 0;
	while (r5_defer_take(sDefer, 256, seen, &page)) {
		dprintf("radeon_hd: deferred page %llx reused\n",
			(unsigned long long)page);
		r5_gfx9_free_user_vram(page);
	}
}


static void
gpu_fault_retire()
{
	uint32 pasid = 0;
	uint32 vmid = 0;
	uint64 addr = 0;
	while (r5_gfx9_fault_take(&pasid, &vmid, &addr)) {
		gpu_client* found = NULL;
		for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
			if (scan->pasid != 0 && scan->pasid == pasid) {
				found = scan;
				break;
			}
		}
		if (found == NULL) {
			dprintf("radeon_hd: vm fault pasid %lu unknown addr %016llx\n",
				(unsigned long)pasid, (unsigned long long)addr);
			continue;
		}
		found->faulted = true;
		if (sGuilty == NULL)
			sGuilty = found;
		dprintf("radeon_hd: vm fault pasid %lu vmid %lu addr %016llx\n",
			(unsigned long)pasid, (unsigned long)vmid,
			(unsigned long long)addr);
	}
}


static void
gpu_release_parked(uint64 seen)
{
	/* A lost device never retires the fence, so every parked buffer
	 * and the file tree it holds are released. */
	if (sLost)
		seen = ~0ull;
	gpu_defer_reclaim(seen);
	for (uint32 i = 0; i < kMaxBo; i++) {
		gpu_bo* bo = &sBo[i];
		if (!bo->live || !bo->parked)
			continue;
		if (seen != ~0ull && bo->parkSequence > seen)
			continue;
		gpu_bo_release(bo);
	}
}


static void
gpu_watchdog()
{
	if (sLost || sInfo == NULL)
		return;
	bigtime_t now = system_time();
	uint64 seen = r5_gfx9_user_seen();
	gpu_job* oldest = NULL;
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || job->sequence <= seen || job->submitted == 0)
			continue;
		if (oldest == NULL || job->submitted < oldest->submitted)
			oldest = job;
	}
	if (oldest == NULL || now - oldest->submitted < kWatchdogUs)
		return;
	if (sGuilty == NULL)
		sGuilty = oldest->owner;
	gpu_lost(sInfo, "gpu lost watchdog");
	gpu_release_parked(~0ull);
}


static void
gpu_bo_detach(gpu_client* client, gpu_bo* bo, uint32 handle)
{
	for (uint32 i = 0; i < client->handleCount; i++) {
		if (client->handles[i] != handle)
			continue;
		client->handles[i] = client->handles[client->handleCount - 1];
		client->handleCount--;
		break;
	}
	uint64 sequence = client->lastSequence;
	uint64 seen = r5_gfx9_user_seen();
	if (!sLost && sequence != 0 && seen < sequence) {
		bo->owner = NULL;
		bo->parked = true;
		bo->parkSequence = sequence;
		gpu_bo_hold_tree(bo);
		return;
	}
	gpu_bo_release(bo);
}


static status_t
gpu_bo_free(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return B_BAD_VALUE;
	gpu_bo_detach(client, bo, in.handle);
	return B_OK;
}


static status_t
gpu_bo_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_bo in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_MAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return gpu_refuse("BO_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0, 0, 0, 0,
			0);
	/* Another team does not get a clone until M5. The same team gets
	 * the clone it already has. clone_area() would put user pages in the
	 * kernel address space and add the kernel bits (SMAP). vm_clone_area
	 * with kernel true puts only the user bits in this team. The kernel
	 * does not read or write through that clone.
	 */
	team_id team = team_get_current_team_id();
	if (bo->area >= 0) {
		if (bo->mapTeam != team)
			return gpu_refuse("BO_MAP", "team", B_NOT_ALLOWED, in.handle,
				bo->domain, bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		in.area = bo->area;
		return gpu_copy_out(buffer, length, &in, sizeof(in));
	}
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		void* kernel = NULL;
		area_id kernelArea = r5_gfx9_map_vram(bo->vramOffset, bo->bytes,
			&kernel);
		if (kernelArea < 0 || kernel == NULL)
			return gpu_refuse("BO_MAP", "vram", kernelArea, in.handle, bo->domain,
				bo->bytes, 0, bo->vramOffset, 0, bo->flags);
		bo->kernelArea = kernelArea;
		bo->kernel = kernel;
	}
	void* user = NULL;
	area_id area = vm_clone_area(team, "r5 gpu bo", &user, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, kRegionNoPrivateMap, bo->kernelArea,
		true);
	if (area < 0)
		return gpu_refuse("BO_MAP", "clone", area, in.handle, bo->domain,
			bo->bytes, 0, bo->vramOffset, 0, bo->flags);
	bo->area = area;
	bo->mapTeam = team;
	in.area = area;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("BO_MAP", "copy", status, in.handle, bo->domain,
			bo->bytes, 0, bo->vramOffset, 0, bo->flags);
	return B_OK;
}


static uint64
gpu_pte_flags(uint32 flags)
{
	uint64 bits = (uint64)R5_GFX9_PTE_VALID | (uint64)R5_GFX9_PTE_READABLE;
	if ((flags & R5_GPU_VA_WRITE) != 0)
		bits |= (uint64)R5_GFX9_PTE_WRITEABLE;
	if ((flags & R5_GPU_VA_EXEC) != 0)
		bits |= (uint64)R5_GFX9_PTE_EXECUTABLE;
	return bits;
}


static bool
gpu_va_fits(uint64 va, uint64 bytes)
{
	return r5_vm_va_fits(va, bytes) != 0;
}


static void
gpu_va_clear(r5_vm* vm, uint64 va, uint64 bytes)
{
	if (vm == NULL)
		vm = r5_gfx9_shared_vm();
	for (uint64 page = 0; page < bytes; page += 4096)
		r5_gfx9_vm_map(vm, va + page, 0);
	if (bytes != 0)
		r5_gfx9_vmid1_flush();
}


static bool
gpu_span_inside(uint64 innerVa, uint64 innerBytes, uint64 va, uint64 bytes)
{
	uint64 innerEnd = innerVa + innerBytes;
	uint64 end = va + bytes;
	return innerBytes != 0 && innerEnd >= innerVa && end >= va
		&& innerVa >= va && innerEnd <= end;
}


static bool
gpu_same_vm(r5_vm* rangeVm, r5_vm* vm)
{
	if (rangeVm == NULL)
		rangeVm = r5_gfx9_shared_vm();
	if (vm == NULL)
		vm = r5_gfx9_shared_vm();
	return rangeVm == vm;
}


static bool
gpu_bo_partial(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (!sBo[b].live)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (!range->live || !gpu_same_vm(range->vm, vm))
				continue;
			if (!r5_range_overlap(va, bytes, range->va, range->bytes))
				continue;
			if (!gpu_span_inside(range->va, range->bytes, va, bytes))
				return true;
		}
	}
	return false;
}


static bool
gpu_va_bo_overlap(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		if (!sBo[b].live)
			continue;
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (range->live && gpu_same_vm(range->vm, vm)
				&& r5_range_overlap(va, bytes, range->va, range->bytes))
				return true;
		}
	}
	return false;
}


static bool
gpu_prt_overlaps(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && gpu_same_vm(sPrt[i].vm, vm)
			&& r5_range_overlap(va, bytes, sPrt[i].va, sPrt[i].bytes))
			return true;
	}
	return false;
}


static bool
gpu_prt_covers(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (!sPrt[i].live || !gpu_same_vm(sPrt[i].vm, vm))
			continue;
		if (gpu_span_inside(va, bytes, sPrt[i].va, sPrt[i].bytes))
			return true;
	}
	return false;
}


static void
gpu_ranges_drop_contained(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 b = 0; b < kMaxBo; b++) {
		for (uint32 i = 0; i < kMaxRange; i++) {
			gpu_range* range = &sBo[b].ranges[i];
			if (range->live && gpu_same_vm(range->vm, vm)
				&& gpu_span_inside(range->va, range->bytes, va, bytes))
				range->live = false;
		}
	}
}


static void
gpu_prt_drop_contained(r5_vm* vm, uint64 va, uint64 bytes)
{
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && gpu_same_vm(sPrt[i].vm, vm)
			&& gpu_span_inside(sPrt[i].va, sPrt[i].bytes, va, bytes))
			sPrt[i].live = false;
	}
}


static void
gpu_prt_release_tree(r5_vm* vm)
{
	if (vm == NULL || vm == r5_gfx9_shared_vm())
		return;
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live && sPrt[i].vm == vm)
			sPrt[i].live = false;
	}
}


static status_t
gpu_prt_add(r5_vm* vm, uint64 va, uint64 bytes)
{
	if (gpu_prt_covers(vm, va, bytes))
		return B_OK;
	for (uint32 i = 0; i < kMaxPrt; i++) {
		if (sPrt[i].live)
			continue;
		sPrt[i].live = true;
		sPrt[i].va = va;
		sPrt[i].bytes = bytes;
		sPrt[i].vm = vm;
		return B_OK;
	}
	return B_NO_MEMORY;
}


static status_t
gpu_va_write_prt(r5_vm* vm, uint64 va, uint64 bytes)
{
	/* The PTE is a value built here: bit 51, VALID clear, no address.
	 * r5_gfx9_vmid1_map writes that page-table entry. This path does
	 * not read or write a user address (SMAP).
	 */
	for (uint64 page = 0; page < bytes; page += 4096) {
		if (!r5_gfx9_vm_map(vm, va + page, kR5Gfx9PtePrt)) {
			gpu_va_clear(vm, va, page);
			return B_NO_MEMORY;
		}
	}
	if (!r5_gfx9_vmid1_flush()) {
		gpu_va_clear(vm, va, bytes);
		return B_ERROR;
	}
	return B_OK;
}


static status_t
gpu_va_map_copied(gpu_client* client, const r5_gpu_va* copied)
{
	r5_gpu_va in = *copied;
	status_t status = B_OK;
	if (in.context != 1 || !gpu_va_fits(in.gpu_va, in.map_bytes))
		return gpu_refuse("VA_MAP", "fit", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (in.handle == 0) {
		if ((in.flags & R5_GPU_VA_PRT) == 0 || in.bo_offset != 0)
			return gpu_refuse("VA_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (gpu_prt_overlaps(gpu_vm(client), in.gpu_va, in.map_bytes)
			|| gpu_va_bo_overlap(gpu_vm(client), in.gpu_va, in.map_bytes))
			return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		if (!r5_gfx9_vmid1_setup())
			return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (r5_gfx9_vm_lookup(gpu_vm(client), in.gpu_va + page)) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle, 0,
					0, in.gpu_va + page, in.bo_offset, in.map_bytes, in.flags);
			}
		}
		status = gpu_va_write_prt(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK)
			return gpu_refuse("VA_MAP", "prt", status, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		status = gpu_prt_add(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK) {
			gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
			return gpu_refuse("VA_MAP", "prt", status, in.handle, 0, 0,
				in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		}
		return B_OK;
	}
	if (!sReplacing && gpu_prt_overlaps(gpu_vm(client), in.gpu_va, in.map_bytes))
		return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL)
		return gpu_refuse("VA_MAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if ((in.bo_offset & 0xfffull) != 0 || in.bo_offset > bo->bytes
		|| in.map_bytes > bo->bytes - in.bo_offset)
		return gpu_refuse("VA_MAP", "offset", B_BAD_VALUE, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live)
			continue;
		if (r5_range_overlap(in.bo_offset, in.map_bytes, range->offset,
				range->bytes)
			|| r5_range_overlap(in.gpu_va, in.map_bytes, range->va,
				range->bytes))
			return gpu_refuse("VA_MAP", "overlap", B_BAD_VALUE, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes,
				in.flags);
	}
	int slot = -1;
	for (uint32 i = 0; i < kMaxRange; i++) {
		if (!bo->ranges[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0)
		return gpu_refuse("VA_MAP", "ranges", B_NO_MEMORY, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (!r5_gfx9_vmid1_setup())
		return gpu_refuse("VA_MAP", "vmid", B_NO_INIT, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	uint32 flags = in.flags;
	if (flags == 0)
		flags = R5_GPU_VA_READ | R5_GPU_VA_WRITE;
	if (bo->domain == R5_GPU_DOMAIN_VRAM) {
		uint32 fb = gpu_mmio(client->info, kGcSegment, 0x096b) & 0x00ffffffu;
		uint64 phys = ((uint64)fb << 24) + bo->vramOffset + in.bo_offset;
		uint64 pte = r5_gfx9_user_pte(phys, gpu_pte_flags(flags), 0, 0,
			&kR5Gfx93Mtype);
		for (uint64 page = 0; page < in.map_bytes; page += 4096) {
			if (r5_gfx9_vm_lookup(gpu_vm(client), in.gpu_va + page)) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			if (!r5_gfx9_vm_map(gpu_vm(client), in.gpu_va + page, pte)) {
				dprintf("radeon_hd: page table pool full\n");
				gpu_va_clear(gpu_vm(client), in.gpu_va, page);
				return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY, in.handle,
					bo->domain, bo->bytes, in.gpu_va + page, in.bo_offset,
					in.map_bytes, in.flags);
			}
			pte += 4096;
		}
	} else {
		/* get_memory_map() returns B_OK and a size-0 end entry, so a
		 * count taken from that return is always 0. get_memory_map_etc
		 * writes the count. The address is the kernel area, never the
		 * user clone, so this does not touch user pages (SMAP). A kernel
		 * address selects the kernel address space inside that call.
		 * More than 256 physical pieces continues after B_BUFFER_OVERFLOW.
		 */
		if (bo->kernel == NULL)
			return gpu_refuse("VA_MAP", "gtt", B_NO_INIT, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		uint8* cursor = (uint8*)bo->kernel + in.bo_offset;
		uint64 filled = 0;
		while (filled < in.map_bytes) {
			physical_entry entries[256];
			uint32 count = 256;
			status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, cursor,
				(size_t)(in.map_bytes - filled), entries, &count);
			if (mapped != B_OK && mapped != B_BUFFER_OVERFLOW) {
				gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
				return gpu_refuse("VA_MAP", "phys", mapped, in.handle,
					bo->domain, bo->bytes, in.gpu_va, in.bo_offset, filled,
					in.flags);
			}
			uint64 step = 0;
			for (uint32 entry = 0; entry < count && filled < in.map_bytes;
				entry++) {
				uint64 address = entries[entry].address;
				uint64 bytes = entries[entry].size;
				while (bytes >= 4096 && filled < in.map_bytes) {
					if (r5_gfx9_vm_lookup(gpu_vm(client), in.gpu_va + filled)) {
						gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "mapped", B_BAD_VALUE,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					uint64 pte = r5_gfx9_user_pte(address,
						gpu_pte_flags(flags), 1, 0, &kR5Gfx93Mtype);
					if (!r5_gfx9_vm_map(gpu_vm(client), in.gpu_va + filled, pte)) {
						dprintf("radeon_hd: page table pool full\n");
						gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
						return gpu_refuse("VA_MAP", "pte", B_NO_MEMORY,
							in.handle, bo->domain, bo->bytes,
							in.gpu_va + filled, in.bo_offset, in.map_bytes,
							in.flags);
					}
					address += 4096;
					bytes -= 4096;
					filled += 4096;
					step += 4096;
				}
			}
			if (step == 0 || mapped == B_OK)
				break;
			cursor += step;
		}
		if (filled != in.map_bytes) {
			gpu_va_clear(gpu_vm(client), in.gpu_va, filled);
			return gpu_refuse("VA_MAP", "short", B_BAD_DATA, in.handle,
				bo->domain, bo->bytes, in.gpu_va, in.bo_offset, filled,
				in.flags);
		}
	}
	if (!r5_gfx9_vmid1_flush()) {
		gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
		return gpu_refuse("VA_MAP", "flush", B_ERROR, in.handle, bo->domain,
			bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	}
	bo->ranges[slot].live = true;
	bo->ranges[slot].va = in.gpu_va;
	bo->ranges[slot].offset = in.bo_offset;
	bo->ranges[slot].bytes = in.map_bytes;
	bo->ranges[slot].vm = gpu_vm(client);
	if (gpu_debug_enabled())
		dprintf("radeon_hd: t %lld va map %016llx bytes %llu domain %lu\n",
			(long long)system_time(),
			(unsigned long long)in.gpu_va,
			(unsigned long long)in.map_bytes,
			(unsigned long)bo->domain);
	return B_OK;
}


static status_t
gpu_va_map(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("VA_MAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	return gpu_va_map_copied(client, &in);
}


static status_t
gpu_va_replace(gpu_client* client, void* buffer, size_t length, bool clearOnly)
{
	const char* op = clearOnly ? "VA_CLEAR" : "VA_REPLACE";
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse(op, "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.context != 1 || !gpu_va_fits(in.gpu_va, in.map_bytes))
		return gpu_refuse(op, "fit", B_BAD_VALUE, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	if (!clearOnly && in.handle == 0 && (in.flags & R5_GPU_VA_PRT) == 0)
		return gpu_refuse(op, "bo", B_BAD_VALUE, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	if (!clearOnly && in.handle != 0 && gpu_bo_get(client, in.handle) == NULL)
		return gpu_refuse(op, "bo", B_BAD_VALUE, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	if (gpu_bo_partial(gpu_vm(client), in.gpu_va, in.map_bytes))
		return gpu_refuse(op, "overlap", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	if (!r5_gfx9_vmid1_setup())
		return gpu_refuse(op, "vmid", B_NO_INIT, in.handle, 0, 0, in.gpu_va,
			in.bo_offset, in.map_bytes, in.flags);
	gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
	gpu_ranges_drop_contained(gpu_vm(client), in.gpu_va, in.map_bytes);
	if (clearOnly) {
		gpu_prt_drop_contained(gpu_vm(client), in.gpu_va, in.map_bytes);
		return B_OK;
	}
	if (in.handle == 0) {
		status = gpu_va_write_prt(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK)
			return gpu_refuse(op, "prt", status, in.handle, 0, 0, in.gpu_va,
				in.bo_offset, in.map_bytes, in.flags);
		status = gpu_prt_add(gpu_vm(client), in.gpu_va, in.map_bytes);
		if (status != B_OK) {
			gpu_va_clear(gpu_vm(client), in.gpu_va, in.map_bytes);
			return gpu_refuse(op, "prt", status, in.handle, 0, 0, in.gpu_va,
				in.bo_offset, in.map_bytes, in.flags);
		}
		return B_OK;
	}
	sReplacing = true;
	status = gpu_va_map_copied(client, &in);
	sReplacing = false;
	return status;
}


static status_t
gpu_va_unmap(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_va in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("VA_UNMAP", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	gpu_bo* bo = gpu_bo_get(client, in.handle);
	if (bo == NULL || in.context != 1)
		return gpu_refuse("VA_UNMAP", "bo", B_BAD_VALUE, in.handle, 0, 0,
			in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
	for (uint32 i = 0; i < kMaxRange; i++) {
		gpu_range* range = &bo->ranges[i];
		if (!range->live || range->va != in.gpu_va
			|| range->offset != in.bo_offset || range->bytes != in.map_bytes)
			continue;
		r5_vm* vm = range->vm != NULL ? range->vm : r5_gfx9_shared_vm();
		for (uint64 page = 0; page < range->bytes; page += 4096) {
			if (!r5_gfx9_vm_map(vm, range->va + page, 0))
				return gpu_refuse("VA_UNMAP", "pte", B_BAD_ADDRESS, in.handle,
					bo->domain, bo->bytes, range->va + page, in.bo_offset,
					in.map_bytes, in.flags);
		}
		if (!r5_gfx9_vmid1_flush())
			return gpu_refuse("VA_UNMAP", "flush", B_ERROR, in.handle, bo->domain,
				bo->bytes, in.gpu_va, in.bo_offset, in.map_bytes, in.flags);
		range->live = false;
		range->vm = NULL;
		return B_OK;
	}
	return gpu_refuse("VA_UNMAP", "range", B_BAD_VALUE, in.handle,
		bo != NULL ? bo->domain : 0, bo != NULL ? bo->bytes : 0, in.gpu_va,
		in.bo_offset, in.map_bytes, in.flags);
}


static bool
gpu_ib_mapped(gpu_client* client, uint64 va, uint32 dwords)
{
	r5_vm* vm = gpu_vm(client);
	uint64 first = 0;
	uint64 last = 0;
	if (!r5_ib_span(va, dwords, &first, &last))
		return false;
	for (uint64 page = first; ; page += 4096) {
		if (!r5_gfx9_vm_lookup(vm, page))
			return false;
		if (page == last)
			break;
	}
	return true;
}


/* The sync wait and the ring-room wait drop the lock. An unmap in that
 * window must fail the submit before the cache flush and the doorbell. */
static status_t
gpu_ibs_mapped(gpu_client* client, const r5_gpu_ib* ibs, uint32 count,
	uint32 ring)
{
	for (uint32 i = 0; i < count; i++) {
		if (!gpu_ib_mapped(client, ibs[i].gpu_va, ibs[i].dwords))
			return gpu_refuse("CS_SUBMIT", "mapped", B_BAD_ADDRESS,
				1, 0, ibs[i].dwords, ibs[i].gpu_va, 0, ring, ibs[i].flags);
	}
	return B_OK;
}


static status_t
gpu_wait_room(radeon_info* info, uint32 dwords)
{
	(void)info;
	if (sLost)
		return ECANCELED;
	if (r5_gfx9_user_room(dwords))
		return B_OK;
	bigtime_t deadline = system_time() + (bigtime_t)kRoomWaitUs;
	while (!r5_gfx9_user_room(dwords)) {
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		bigtime_t now = system_time();
		if (r5_gfx9_user_room(dwords))
			return B_OK;
		if (sLost)
			return ECANCELED;
		if (now >= deadline)
			return B_BUSY;
		gpu_unlock();
		entry.Wait(B_RELATIVE_TIMEOUT, deadline - now);
		gpu_lock();
	}
	return B_OK;
}


static bool
gpu_sync_signalled(uint64 value, uint64 submitted, uint64 target, bool binary)
{
	if (binary)
		return submitted > 0 && value >= submitted;
	return value >= target;
}


static bool gpu_job_blocks(const gpu_sync* sync, uint64 target, bool binary);


static bool
gpu_wait_ready(const gpu_sync* sync, const r5_gpu_cs_sync* wait)
{
	bool binary = (wait->flags & R5_GPU_CS_SYNC_BINARY) != 0;
	if (gpu_job_blocks(sync, wait->point, binary))
		return false;
	return gpu_sync_signalled(sync->point, sync->submitted, wait->point, binary);
}


static status_t
gpu_submit(gpu_client* client, void* buffer, size_t length)
{
	if (sLost)
		return gpu_refuse("CS_SUBMIT", "lost", ECANCELED, 0, 0, 0, 0, 0, 0, 0);
	r5_gpu_cs in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "copy", status, 0, 0, 0, 0, 0, 0, 0);
	if (in.context != 1 || in.ring != 0)
		return gpu_refuse("CS_SUBMIT", "context", B_BAD_VALUE, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	if (in.ib_count == 0 || in.ib_count > kMaxList
		|| in.wait_count > kMaxList || in.signal_count > kMaxList)
		return gpu_refuse("CS_SUBMIT", "count", B_BAD_VALUE, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	r5_gpu_ib ibs[kMaxList];
	r5_gpu_cs_sync waits[kMaxList];
	r5_gpu_cs_sync signals[kMaxList];
	status = gpu_copy_user(ibs, in.ibs, sizeof(r5_gpu_ib) * in.ib_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "ib", status, in.context, 0, in.ib_count,
			0, 0, in.ring, 0);
	status = gpu_copy_user(waits, in.waits, sizeof(r5_gpu_cs_sync) * in.wait_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "wait", status, in.context, 0, in.ib_count,
			0, 0, in.ring, 0);
	status = gpu_copy_user(signals, in.signals,
		sizeof(r5_gpu_cs_sync) * in.signal_count);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "signal", status, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	for (uint32 i = 0; i < in.ib_count; i++) {
		if (ibs[i].flags != 0)
			return gpu_refuse("CS_SUBMIT", "flags", B_BAD_VALUE, in.context, 0,
				in.ib_count, ibs[i].gpu_va, 0, in.ring, ibs[i].flags);
	}
	status = gpu_ibs_mapped(client, ibs, in.ib_count, in.ring);
	if (status != B_OK)
		return status;
	for (uint32 i = 0; i < in.wait_count; i++) {
		if (waits[i].handle == 0 || waits[i].handle > kMaxSync)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
		gpu_sync* sync = &client->syncs[waits[i].handle - 1u];
		if (!sync->live)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
		bigtime_t deadline = system_time() + (bigtime_t)kFreeWaitUs;
		while (!gpu_wait_ready(sync, &waits[i])) {
			if (sLost)
				return gpu_refuse("CS_SUBMIT", "lost", ECANCELED, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			bigtime_t now = system_time();
			if (now >= deadline)
				return gpu_refuse("CS_SUBMIT", "wait", B_TIMED_OUT, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			ConditionVariableEntry entry;
			sEop.Add(&entry);
			if (gpu_wait_ready(sync, &waits[i]))
				break;
			gpu_unlock();
			status_t waited = entry.Wait(
				B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, deadline - now);
			gpu_lock();
			status = gpu_ibs_mapped(client, ibs, in.ib_count, in.ring);
			if (status != B_OK)
				return status;
			if (!sync->live)
				return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			if (waited == B_INTERRUPTED || waited == B_WOULD_BLOCK)
				return gpu_refuse("CS_SUBMIT", "wait", waited, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
			if (waited == B_TIMED_OUT && !gpu_wait_ready(sync, &waits[i]))
				return gpu_refuse("CS_SUBMIT", "wait", B_TIMED_OUT, in.context, 0,
					in.ib_count, 0, waits[i].handle, in.ring, waits[i].flags);
		}
	}
	int jobSlot = -1;
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live) {
			jobSlot = (int)i;
			break;
		}
	}
	if (jobSlot < 0)
		return gpu_refuse("CS_SUBMIT", "job", B_BUSY, in.context, 0, in.ib_count,
			0, 0, in.ring, 0);
	r5_gfx9_user_ib ringIbs[kMaxList];
	for (uint32 i = 0; i < in.ib_count; i++) {
		ringIbs[i].va = ibs[i].gpu_va;
		ringIbs[i].dwords = ibs[i].dwords;
		ringIbs[i].vmid = 1;
		ringIbs[i].flags = ibs[i].flags;
	}
	uint32 dwords = r5_gfx9_user_commit_dwords(in.ib_count);
	/* The file's page-table base is written after the room wait, so the
	 * count includes one VM flush that touch() will make due. */
	if (client->tree != NULL && dwords != 0) {
		dwords += r5_cs_vm_switch_words();
		if (client->pasid != 0
			&& (r5_gfx9_pasid_map_on() || r5_gfx9_vmid_per_file()))
			dwords += 5u;
	}
	status = gpu_wait_room(client->info, dwords);
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "room", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
	status = gpu_ibs_mapped(client, ibs, in.ib_count, in.ring);
	if (status != B_OK)
		return status;
	/* Temporary. A GTT indirect buffer is MTYPE_NC and snooped. clflush
	 * plus mfence makes the CPU stores visible before the doorbell. The
	 * pointer is the kernel area, never the user clone (SMAP).
	 */
	for (uint32 i = 0; i < in.ib_count; i++) {
		uint64 bytes = (uint64)ibs[i].dwords * 4;
		for (uint32 h = 0; h < client->handleCount; h++) {
			gpu_bo* bo = gpu_bo_get(client, client->handles[h]);
			if (bo == NULL || bo->domain != R5_GPU_DOMAIN_GTT
				|| bo->kernel == NULL)
				continue;
			for (uint32 r = 0; r < kMaxRange; r++) {
				gpu_range* range = &bo->ranges[r];
				if (!range->live)
					continue;
				if (ibs[i].gpu_va < range->va
					|| ibs[i].gpu_va + bytes > range->va + range->bytes)
					continue;
				uint8* cpu = (uint8*)bo->kernel + range->offset
					+ (ibs[i].gpu_va - range->va);
				uintptr_t start = (uintptr_t)cpu & ~(uintptr_t)63;
				uintptr_t end = ((uintptr_t)cpu + (uintptr_t)bytes + 63)
					& ~(uintptr_t)63;
				for (uintptr_t line = start; line < end; line += 64)
					asm volatile("clflush (%0)" :: "r"((void*)line)
						: "memory");
			}
		}
	}
	asm volatile("mfence" ::: "memory");
	if (gpu_debug_enabled()) {
		for (uint32 i = 0; i < in.ib_count; i++) {
			dprintf("radeon_hd: t %lld submit ib va %016llx dw %lu flags %lu\n",
				(long long)system_time(),
				(unsigned long long)ibs[i].gpu_va,
				(unsigned long)ibs[i].dwords,
				(unsigned long)ibs[i].flags);
		}
	}
	gpu_job* job = &sJobs[jobSlot];
	memset(job, 0, sizeof(*job));
	uint32 signalFlags[kMaxList];
	memset(signalFlags, 0, sizeof(signalFlags));
	for (uint32 i = 0; i < in.signal_count; i++) {
		if (signals[i].handle == 0 || signals[i].handle > kMaxSync)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, signals[i].handle, in.ring, signals[i].flags);
		gpu_sync* sync = &client->syncs[signals[i].handle - 1u];
		if (!sync->live)
			return gpu_refuse("CS_SUBMIT", "sync", B_BAD_VALUE, in.context, 0,
				in.ib_count, 0, signals[i].handle, in.ring, signals[i].flags);
		job->syncs[i] = sync;
		job->points[i] = signals[i].point;
		signalFlags[i] = signals[i].flags;
	}
	job->count = in.signal_count;
	job->owner = client;
	uint64 sequence = 0;
	int fileHeld = 0;
	r5_gfx9_set_debug(gpu_debug_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(client->pasid, (uint64)(addr_t)client);
	if (client->contexts <= 0 || client->fenceCtx == 0)
		return gpu_refuse("CS_SUBMIT", "context", B_BAD_VALUE, in.context, 0,
			in.ib_count, 0, 0, in.ring, 0);
	if (client->tree != NULL) {
		int flush = 0;
		uint32 fileVmid = r5_vmid_reserve(&sFileVmids, client->tree->owner,
			r5_gfx9_user_seen(), client->tree->vm.used, &flush);
		if (fileVmid == 0)
			return gpu_refuse("CS_SUBMIT", "vmid", B_BUSY, in.context, 0,
				in.ib_count, 0, 0, in.ring, 0);
		fileHeld = 1;
		(void)flush;
		for (uint32 i = 0; i < in.ib_count; i++)
			ringIbs[i].vmid = fileVmid;
		r5_gfx9_vmid1_set_root(client->tree->vm.pages[0].physical
			| (uint64)R5_GFX9_PTE_VALID);
		r5_gfx9_vmid1_touch();
	}
	if (!r5_gfx9_user_submit(ringIbs, in.ib_count, client->fenceCtx,
			&client->preamblePresented, &sequence)) {
		if (fileHeld) {
			r5_vmid_cancel(&sFileVmids);
			r5_gfx9_vmid1_set_root(r5_gfx9_vmid1_shared_root());
			r5_gfx9_vmid1_touch();
		}
		return gpu_refuse("CS_SUBMIT", "ring", B_ERROR, in.context, 0,
			in.ib_count, ibs[0].gpu_va, 0, in.ring, 0);
	}
	if (fileHeld) {
		r5_vmid_arm(&sFileVmids, sequence);
		r5_gfx9_vmid1_set_root(r5_gfx9_vmid1_shared_root());
		r5_gfx9_vmid1_touch();
	}
	job->sequence = sequence;
	job->submitted = system_time();
	if (in.ib_count > 0)
		sLastIbVa = ibs[0].gpu_va;
	sLastIbN = in.ib_count > 8 ? 8 : in.ib_count;
	for (uint32 i = 0; i < sLastIbN; i++) {
		sLastIbList[i] = ibs[i].gpu_va;
		sLastIbDw[i] = ibs[i].dwords;
	}
	if (sequence > sHighestSequence)
		sHighestSequence = sequence;
	for (uint32 i = 0; i < in.signal_count; i++) {
		/* RADV's queue syncobj is signaled with timeline point 0.
		 * That is this job's sequence, the same as a binary signal.
		 * submitted moves here. point moves when the user fence
		 * shows this sequence. */
		if ((signalFlags[i] & R5_GPU_CS_SYNC_BINARY) != 0
			|| job->points[i] == 0)
			job->points[i] = sequence;
		gpu_sync* sync = job->syncs[i];
		if (sync != NULL && job->points[i] > sync->submitted)
			sync->submitted = job->points[i];
	}
	job->live = true;
	client->lastSequence = sequence;
	if (r5_gfx9_user_seen() >= sequence)
		r5_gpu_eop();
	in.sequence = sequence;
	status = gpu_copy_out(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return gpu_refuse("CS_SUBMIT", "copy", status, in.context, 0, in.ib_count,
			ibs[0].gpu_va, 0, in.ring, 0);
	return B_OK;
}


static status_t
gpu_fence_wait(radeon_info* info, void* buffer, size_t length)
{
	r5_gpu_fence in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	status = gpu_wait_value(info, NULL, in.sequence, in.timeout_us);
	if (status == B_TIMED_OUT && in.timeout_us != 0)
		gpu_ib_words("fence timeout");
	return status;
}


static gpu_sync*
gpu_sync_get(gpu_client* client, uint32 handle)
{
	if (handle == 0 || handle > kMaxSync)
		return NULL;
	gpu_sync* sync = &client->syncs[handle - 1u];
	if (!sync->live || sync->owner != client)
		return NULL;
	return sync;
}


static status_t
gpu_sync_create(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	int slot = -1;
	for (uint32 i = 0; i < kMaxSync; i++) {
		if (!client->syncs[i].live) {
			slot = (int)i;
			break;
		}
	}
	if (slot < 0) {
		if (gpu_error_line())
			dprintf("radeon_hd: syncobj limit\n");
		return B_NO_MEMORY;
	}
	client->syncs[slot].live = true;
	client->syncs[slot].point = (in.flags & R5_GPU_SYNCOBJ_SIGNALED) != 0
		? 1 : 0;
	client->syncs[slot].submitted = client->syncs[slot].point;
	client->syncs[slot].owner = client;
	in.handle = (uint32)slot + 1u;
	return gpu_copy_out(buffer, length, &in, sizeof(in));
}


static status_t
gpu_sync_destroy(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_sync* sync = gpu_sync_get(client, in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live)
			continue;
		for (uint32 n = 0; n < sJobs[i].count; n++) {
			if (sJobs[i].syncs[n] == sync)
				sJobs[i].syncs[n] = NULL;
		}
	}
	sync->live = false;
	sync->owner = NULL;
	return B_OK;
}


static status_t
gpu_sync_wait(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_sync* sync = gpu_sync_get(client, in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	if ((in.flags & R5_GPU_SYNCOBJ_BINARY) != 0) {
		if (sync->submitted == 0)
			return B_TIMED_OUT;
		status = gpu_wait_value(client->info, &sync->point, sync->submitted,
			in.timeout_us);
	} else
		status = gpu_wait_value(client->info, &sync->point, in.point,
			in.timeout_us);
	if (status == B_TIMED_OUT && in.timeout_us != 0)
		gpu_ib_words("sync wait");
	return status;
}


static bool
gpu_job_blocks(const gpu_sync* sync, uint64 target, bool binary)
{
	uint64 seen = r5_gfx9_user_seen();
	for (uint32 i = 0; i < kMaxJob; i++) {
		gpu_job* job = &sJobs[i];
		if (!job->live || job->sequence <= seen)
			continue;
		for (uint32 n = 0; n < job->count; n++) {
			if (job->syncs[n] != sync)
				continue;
			if (binary)
				return true;
			if (job->points[n] >= target && sync->point < target)
				return true;
		}
	}
	return false;
}


static bool
gpu_list_ready(const gpu_sync* sync, uint64 target, uint32 flags)
{
	bool binary = (flags & R5_GPU_WAIT_BINARY) != 0;
	bool available = (flags & R5_GPU_WAIT_AVAILABLE) != 0;
	if (!available && gpu_job_blocks(sync, target, binary))
		return false;
	uint64 value = available ? sync->submitted : sync->point;
	return gpu_sync_signalled(value, sync->submitted, target, binary);
}


static bool
gpu_list_submitted(const gpu_sync* sync, uint64 target, uint32 flags)
{
	if ((flags & R5_GPU_WAIT_BINARY) != 0)
		return sync->submitted > 0;
	return sync->submitted >= target;
}


static status_t
gpu_sync_wait_list(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj_wait in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (in.count == 0 || in.count > kMaxSync)
		return B_BAD_VALUE;
	uint32 handles[kMaxSync];
	uint64 points[kMaxSync];
	memset(handles, 0, sizeof(handles));
	memset(points, 0, sizeof(points));
	status = gpu_copy_user(handles, in.handles, sizeof(uint32) * in.count);
	if (status != B_OK)
		return status;
	if (in.points != 0) {
		status = gpu_copy_user(points, in.points, sizeof(uint64) * in.count);
		if (status != B_OK)
			return status;
	}
	gpu_sync* syncs[kMaxSync];
	for (uint32 i = 0; i < in.count; i++) {
		syncs[i] = gpu_sync_get(client, handles[i]);
		if (syncs[i] == NULL)
			return B_BAD_VALUE;
	}
	/* Once, over every handle, before any signalled handle can end the
	 * scan. Linux drm_syncobj_array_wait_timeout does the same. A reset
	 * during the wait is a timeout, not a second EINVAL.
	 */
	if ((in.flags & R5_GPU_WAIT_FOR_SUBMIT) == 0) {
		for (uint32 i = 0; i < in.count; i++) {
			if (!gpu_list_submitted(syncs[i], points[i], in.flags))
				return B_BAD_VALUE;
		}
	}
	bigtime_t deadline = system_time() + (bigtime_t)in.timeout_us;
	while (true) {
		int32 first = -1;
		uint32 met = 0;
		for (uint32 i = 0; i < in.count; i++) {
			if (!syncs[i]->live)
				return B_BAD_VALUE;
			if (!gpu_list_ready(syncs[i], points[i], in.flags))
				continue;
			met++;
			if (first < 0)
				first = (int32)i;
			if ((in.flags & R5_GPU_WAIT_ALL) == 0)
				break;
		}
		if ((in.flags & R5_GPU_WAIT_ALL) != 0 && met == in.count) {
			in.first_signaled = 0;
			return gpu_copy_out(buffer, length, &in, sizeof(in));
		}
		if ((in.flags & R5_GPU_WAIT_ALL) == 0 && first >= 0) {
			in.first_signaled = (uint32)first;
			return gpu_copy_out(buffer, length, &in, sizeof(in));
		}
		bigtime_t now = system_time();
		if (sLost)
			return ECANCELED;
		if (in.timeout_us == 0 || now >= deadline)
			return B_TIMED_OUT;
		ConditionVariableEntry entry;
		sEop.Add(&entry);
		gpu_unlock();
		status_t waited = entry.Wait(B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT,
			deadline - now);
		gpu_lock();
		if (waited == B_INTERRUPTED || waited == B_WOULD_BLOCK)
			return waited;
	}
}


static void
gpu_sync_set(gpu_sync* sync, uint64 point)
{
	if (point > sync->submitted)
		sync->submitted = point;
	if (point > sync->point)
		sync->point = point;
	sEop.NotifyAll();
}


static status_t
gpu_sync_signal(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	gpu_sync* sync = gpu_sync_get(client, in.handle);
	if (sync == NULL)
		return B_BAD_VALUE;
	if ((in.flags & R5_GPU_SYNCOBJ_BINARY) != 0) {
		gpu_sync_host(sync);
		return B_OK;
	}
	gpu_sync_set(sync, in.point);
	return B_OK;
}


static void
gpu_sync_host(gpu_sync* sync);


static bool
gpu_src_reached(const gpu_sync* sync, uint64 srcPoint)
{
	bool binary = srcPoint == 0;
	if (gpu_job_blocks(sync, srcPoint, binary))
		return false;
	return gpu_sync_signalled(sync->point, sync->submitted, srcPoint, binary);
}


static void
gpu_sync_host(gpu_sync* sync)
{
	uint64 stamp = sync->submitted;
	if (sync->point > stamp)
		stamp = sync->point;
	uint64 high = sHighestSequence == 0 ? 1 : sHighestSequence;
	if (high > stamp)
		stamp = high;
	sync->submitted = stamp;
	sync->point = stamp;
	sEop.NotifyAll();
}


static status_t
gpu_sync_transfer(gpu_client* client, void* buffer, size_t length)
{
	r5_gpu_syncobj_transfer in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (in.flags != 0)
		return B_BAD_VALUE;
	gpu_sync* src = gpu_sync_get(client, in.src_handle);
	gpu_sync* dst = gpu_sync_get(client, in.dst_handle);
	if (src == NULL || dst == NULL)
		return B_ENTRY_NOT_FOUND;
	bool srcBinary = in.src_point == 0;
	bool dstBinary = in.dst_point == 0;
	if (srcBinary && src->submitted == 0)
		return B_BAD_VALUE;
	if (!gpu_src_reached(src, in.src_point) && sLost)
		return ECANCELED;
	if (gpu_src_reached(src, in.src_point)) {
		if (dstBinary)
			gpu_sync_host(dst);
		else
			gpu_sync_set(dst, in.dst_point);
		return B_OK;
	}
	gpu_job* job = NULL;
	uint64 lowest = 0;
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live)
			continue;
		for (uint32 n = 0; n < sJobs[i].count; n++) {
			if (sJobs[i].syncs[n] != src)
				continue;
			if (srcBinary) {
				if (sJobs[i].points[n] == src->submitted)
					job = &sJobs[i];
			} else if (sJobs[i].points[n] >= in.src_point
				&& (job == NULL || sJobs[i].points[n] < lowest)) {
				job = &sJobs[i];
				lowest = sJobs[i].points[n];
			}
		}
	}
	if (job == NULL)
		return B_BAD_VALUE;
	uint64 dstPoint = dstBinary ? job->sequence : in.dst_point;
	for (uint32 n = 0; n < job->count; n++) {
		if (job->syncs[n] != dst)
			continue;
		if (dstPoint > job->points[n])
			job->points[n] = dstPoint;
		if (dstPoint > dst->submitted)
			dst->submitted = dstPoint;
		return B_OK;
	}
	if (job->count >= kMaxList)
		return B_NO_MEMORY;
	job->syncs[job->count] = dst;
	job->points[job->count] = dstPoint;
	job->count++;
	if (dstPoint > dst->submitted)
		dst->submitted = dstPoint;
	return B_OK;
}


static bool
gpu_selftest_enabled()
{
	if (sSelftestOn < 0)
		gpu_cache_log_settings();
	return sSelftestOn != 0;
}


static uint32
gpu_known_dw(uint64 va)
{
	for (uint32 i = 0; i < sLastIbN && i < 8; i++) {
		if (sLastIbList[i] == va && sLastIbDw[i] != 0)
			return sLastIbDw[i];
	}
	return 0;
}


static void gpu_dump_one(const char* why, const char* which, uint64 base,
	uint32 hint, uint32 cpOffset, int depth);


static void
gpu_mark(uint64 va)
{
	uint32 word = 0;
	uint32 got = 0;
	uint64 pte = 0;
	if (va != 0) {
		got = r5_gfx9_vmid1_copy(va, &word, 1);
		r5_gfx9_vmid1_pte(va, &pte);
	}
	/* The page number sits above the flag bits. */
	uint64 pa = pte & ~0xfffull;
	if (got == 1) {
		dprintf("radeon_hd: mark 0x%llx = %08lx pte %016llx pa %016llx\n",
			(unsigned long long)va, (unsigned long)word,
			(unsigned long long)pte, (unsigned long long)pa);
	} else {
		dprintf("radeon_hd: mark 0x%llx unmapped pte %016llx pa %016llx\n",
			(unsigned long long)va, (unsigned long long)pte,
			(unsigned long long)pa);
	}
}


static void
gpu_pm4_walk(const char* why, const uint32* words, uint32 count, int depth)
{
	uint32 i = 0;
	while (i < count) {
		uint32 header = words[i];
		uint32 type = header >> 30;
		if (type == 2) {
			i++;
			continue;
		}
		if (type != 3 || i + 1 >= count) {
			i++;
			continue;
		}
		uint32 opcode = (header >> 8) & 0xffu;
		uint32 body = ((header >> 16) & 0x3fffu) + 1u;
		if (i + 1u + body > count)
			break;
		const char* name = NULL;
		uint32 reg = 0;
		uint32 values = 0;
		if (opcode == 0x68 || opcode == 0x69 || opcode == 0x76
			|| opcode == 0x79 || opcode == 0x7a) {
			uint32 base = 0x8000;
			if (opcode == 0x69)
				base = 0x28000;
			else if (opcode == 0x76)
				base = 0xb000;
			else if (opcode == 0x79 || opcode == 0x7a)
				base = 0x30000;
			reg = base + ((words[i + 1] & 0xffffu) << 2);
			values = body > 0 ? body - 1u : 0;
			if (opcode == 0x68)
				name = "SET_CONFIG_REG";
			else if (opcode == 0x69)
				name = "SET_CONTEXT_REG";
			else if (opcode == 0x76)
				name = "SET_SH_REG";
			else if (opcode == 0x79)
				name = "SET_UCONFIG_REG";
			else
				name = "SET_UCONFIG_REG_INDEX";
		} else if (opcode == 0x37 && body >= 1) {
			uint32 dst = (words[i + 1] >> 8) & 0xfu;
			if (dst == 5 && body >= 3) {
				gpu_mark((uint64)words[i + 2]
					| ((uint64)words[i + 3] << 32));
			} else if (dst == 0 && body >= 3) {
				name = "WRITE_DATA";
				reg = words[i + 2];
				values = 1;
			}
		} else if (opcode == 0x49 && body >= 4) {
			gpu_mark((uint64)words[i + 3] | ((uint64)words[i + 4] << 32));
		} else if (opcode == 0x40 && body >= 4) {
			uint32 dst = (words[i + 1] >> 8) & 0xfu;
			if (dst == 0 || dst == 4) {
				name = "COPY_DATA";
				reg = words[i + 4] << 2;
				values = 1;
			}
		} else if (opcode == 0x3c && body >= 2) {
			if ((words[i + 1] & (3u << 4)) == 0) {
				name = "WAIT_REG_MEM";
				reg = words[i + 2];
				values = 1;
			}
		} else if (opcode == 0x3f && body >= 3 && depth == 0) {
			uint64 child = (uint64)words[i + 1]
				| ((uint64)words[i + 2] << 32);
			uint32 childDw = words[i + 3] & 0xfffffu;
			if (gpu_dump_line())
				dprintf("radeon_hd: pm4 +%04lx INDIRECT_BUFFER va %016llx dw %lu\n",
					(unsigned long)i, (unsigned long long)child,
					(unsigned long)childDw);
			gpu_dump_one(why, "nested", child, childDw, 0, 1);
		}
		if (name != NULL && gpu_dump_line()) {
			if (opcode == 0x68 || opcode == 0x69 || opcode == 0x76
				|| opcode == 0x79 || opcode == 0x7a) {
				dprintf("radeon_hd: pm4 +%04lx %s reg 0x%lx idx %lu count %lu\n",
					(unsigned long)i, name, (unsigned long)reg,
					(unsigned long)(words[i + 1] >> 28),
					(unsigned long)values);
			} else {
				dprintf("radeon_hd: pm4 +%04lx %s reg 0x%lx count %lu\n",
					(unsigned long)i, name, (unsigned long)reg,
					(unsigned long)values);
			}
		}
		i += 1u + body;
	}
}


static void
gpu_dump_one(const char* why, const char* which, uint64 base, uint32 hint,
	uint32 cpOffset, int depth)
{
	uint32 words[1024];
	uint32 want = hint;
	memset(words, 0, sizeof(words));
	if (want == 0 || want > 1024)
		want = 1024;
	uint32 got = 0;
	if (base != 0)
		got = r5_gfx9_vmid1_copy(base, words, want);
	if (depth == 0 && !gpu_dump_begin())
		return;
	if (!gpu_dump_line())
		return;
	dprintf("radeon_hd: %s %s va %016llx dw %lu got %lu cp_off %lu\n",
		why == NULL ? "ib" : why, which == NULL ? "ib" : which,
		(unsigned long long)base, (unsigned long)want,
		(unsigned long)got, (unsigned long)cpOffset);
	for (uint32 i = 0; i < got; i += 8) {
		uint32 line[8];
		uint32 n = got - i;
		memset(line, 0, sizeof(line));
		if (n > 8)
			n = 8;
		memcpy(line, words + i, n * 4);
		if (!gpu_dump_line())
			break;
		dprintf("radeon_hd: ib +%04lx %08lx %08lx %08lx %08lx "
			"%08lx %08lx %08lx %08lx\n",
			(unsigned long)i,
			(unsigned long)line[0], (unsigned long)line[1],
			(unsigned long)line[2], (unsigned long)line[3],
			(unsigned long)line[4], (unsigned long)line[5],
			(unsigned long)line[6], (unsigned long)line[7]);
	}
	gpu_pm4_walk(why, words, got, depth);
}


static void
gpu_dump_regs(const char* why, const r5_gfx9_ib_now* ib, const char* which)
{
	if (ib == NULL || ib->base == 0)
		return;
	uint32 known = gpu_known_dw(ib->base);
	uint32 hint = known != 0 ? known : 1024;
	gpu_dump_one(why, which, ib->base, hint, ib->offset, 0);
}


static void
gpu_ib_words(const char* why)
{
	r5_gfx9_ib_now ib1;
	r5_gfx9_ib_now ib2;
	memset(&ib1, 0, sizeof(ib1));
	memset(&ib2, 0, sizeof(ib2));
	r5_gfx9_ib_regs(&ib1, &ib2);
	gpu_dump_regs(why, &ib1, "ib1");
	if (ib2.base != ib1.base)
		gpu_dump_regs(why, &ib2, "ib2");
	if (ib1.base == 0 && ib2.base == 0 && !gpu_dump_begin())
		return;
	if (gpu_dump_line())
	dprintf("radeon_hd: %s roq ib1 %08lx\n",
		why == NULL ? "ib" : why, (unsigned long)r5_gfx9_roq_ib1());
	if (sInfo != NULL) {
		uint64 pte = 0;
		uint64 va = ib1.base != 0 ? ib1.base : sLastIbVa;
		if (va != 0)
			r5_gfx9_vmid1_pte(va, &pte);
		r5_gfx9_cp_dump(why == NULL ? "ib" : why, pte);
	}
}


static void
gpu_dump_priv()
{
	r5_gfx9_ib_now ib1;
	r5_gfx9_ib_now ib2;
	if (!r5_gfx9_take_priv(&ib1, &ib2))
		return;
	gpu_dump_regs("ih", &ib1, "ib1");
	if (ib2.base != ib1.base)
		gpu_dump_regs("ih", &ib2, "ib2");
	dprintf("radeon_hd: ih roq ib1 %08lx\n",
		(unsigned long)r5_gfx9_roq_ib1());
	r5_gfx9_pipe_log("ih");
}


static void
gpu_stage_words(const char* name, uint64 ibVa, uint8* cpu, uint64 phys)
{
	uint32 words[8];
	memset(words, 0, sizeof(words));
	if (cpu != NULL)
		memcpy(words, cpu, sizeof(words));
	dprintf("radeon_hd: %s ib %016llx phys %016llx "
		"%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		name,
		(unsigned long long)ibVa,
		(unsigned long long)phys,
		(unsigned long)words[0], (unsigned long)words[1],
		(unsigned long)words[2], (unsigned long)words[3],
		(unsigned long)words[4], (unsigned long)words[5],
		(unsigned long)words[6], (unsigned long)words[7]);
}


static void
gpu_stage_dump(const char* name, uint64 ibVa, uint8* cpu, uint64 phys)
{
	uint64 pte = 0;
	if (ibVa != 0)
		r5_gfx9_vmid1_pte(ibVa, &pte);
	r5_gfx9_cp_dump(name, pte);
	gpu_stage_words(name, ibVa, cpu, phys);
}


/* Just below the advertised limits, so a program that grows upward from
 * va_offset or high_va_offset does not land on a stage page. */
static const uint64 kStageLowVram = R5_GPU_VA_MAX - 0x3000ull;
static const uint64 kStageLowGtt = R5_GPU_VA_MAX - 0x2000ull;
static const uint64 kStageHighGtt = R5_GPU_HIGH_VA_MAX - 0x3000ull;
static const uint64 kStageHighVram = R5_GPU_HIGH_VA_MAX - 0x2000ull;
static uint64 sStageFresh = 0;


/* One page per step, above the pages stage P already released. */
static uint64
gpu_stage_fresh()
{
	if (sStageFresh == 0 || sStageFresh >= R5_GPU_VA_MAX - 0x10000ull)
		sStageFresh = R5_GPU_VA_MAX - 0x50000ull;
	uint64 va = sStageFresh;
	sStageFresh += 0x1000ull;
	return va;
}


struct gpu_stage_page {
	uint64		va;
	uint64		phys;
	uint64		vramOffset;
	uint8*		cpu;
	area_id		area;
	bool		vram;
	bool		gtt;
};


static void
gpu_stage_unbind(gpu_stage_page* page)
{
	if (page == NULL || page->va == 0)
		return;
	r5_gfx9_vmid1_map(page->va, 0);
	r5_gfx9_vmid1_flush();
	page->va = 0;
}


static bool
gpu_stage_bind(gpu_stage_page* page, uint64 va)
{
	uint64 flags = gpu_pte_flags(R5_GPU_VA_READ | R5_GPU_VA_WRITE
		| R5_GPU_VA_EXEC);
	uint64 pte = r5_gfx9_user_pte(page->phys, flags, page->gtt ? 1 : 0, 0,
		&kR5Gfx93Mtype);
	page->va = va;
	if (!r5_gfx9_vmid1_map(va, pte) || !r5_gfx9_vmid1_flush()) {
		page->va = 0;
		return false;
	}
	return true;
}


static void
gpu_stage_release(gpu_stage_page* page)
{
	if (page == NULL || page->va == 0)
		return;
	r5_gfx9_vmid1_map(page->va, 0);
	r5_gfx9_vmid1_flush();
	if (page->vram)
		r5_gfx9_free_user_vram(page->vramOffset);
	if (page->gtt && page->area >= 0)
		delete_area(page->area);
	memset(page, 0, sizeof(*page));
	page->area = -1;
}


static bool
gpu_stage_map(radeon_info* info, gpu_stage_page* page, uint64 va, bool gtt,
	bool wc = false)
{
	memset(page, 0, sizeof(*page));
	page->area = -1;
	page->va = va;
	uint64 phys = 0;
	uint64 flags = gpu_pte_flags(R5_GPU_VA_READ | R5_GPU_VA_WRITE | R5_GPU_VA_EXEC);
	if (!gtt) {
		if (r5_gfx9_alloc_user_vram(4096, &page->vramOffset) != B_OK)
			return false;
		page->cpu = r5_gfx9_vram_cpu(page->vramOffset);
		if (page->cpu == NULL) {
			r5_gfx9_free_user_vram(page->vramOffset);
			return false;
		}
		page->vram = true;
		uint32 fb = gpu_mmio(info, kGcSegment, 0x096b);
		phys = r5_gpu_carveout_base(fb) + page->vramOffset;
	} else {
		void* system = NULL;
		page->area = create_area("r5 gfx9 stage", &system,
			B_ANY_KERNEL_ADDRESS, 4096, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (page->area < 0 || system == NULL)
			return false;
		/* wc is accepted and ignored. A stage GTT page stays cached. */
		(void)wc;
		page->cpu = (uint8*)system;
		page->gtt = true;
		physical_entry entry;
		memset(&entry, 0, sizeof(entry));
		uint32 count = 1;
		status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, system, 4096,
			&entry, &count);
		if (mapped != B_OK || count == 0 || entry.size < 4096) {
			delete_area(page->area);
			page->area = -1;
			return false;
		}
		phys = entry.address;
	}
	page->phys = phys;
	uint64 pte = r5_gfx9_user_pte(phys, flags, gtt ? 1 : 0, 0,
		&kR5Gfx93Mtype);
	if (!r5_gfx9_vmid1_map(va, pte) || !r5_gfx9_vmid1_flush()) {
		gpu_stage_release(page);
		return false;
	}
	return true;
}


struct gpu_stage_pace {
	bool		hang;
	int32		ms;
	uint64		seen;
	uint32		word;
	uint32		cp;
};


/* The stage submit is a job with no sync object, so the retire thread's
 * five-second watchdog is what marks the device lost. */
static int
gpu_stage_slot(uint64 sequence)
{
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (sJobs[i].live)
			continue;
		memset(&sJobs[i], 0, sizeof(sJobs[i]));
		sJobs[i].live = true;
		sJobs[i].sequence = sequence;
		sJobs[i].submitted = system_time();
		return (int)i;
	}
	return -1;
}


static uint32
gpu_stage_word(uint8* destCpu, bool flush)
{
	if (destCpu == NULL)
		return 0;
	if (flush) {
		uintptr_t line = ((uintptr_t)destCpu + 64) & ~(uintptr_t)63;
		asm volatile("clflush (%0)" :: "r"((void*)line) : "memory");
		asm volatile("mfence" ::: "memory");
	}
	return *(volatile uint32*)(destCpu + 64);
}


/* Architectural PAT index: PAT is bit 7, PCD bit 4, PWT bit 3.
 * Write-back is 0. Haiku's write-combining PTE sets only the PAT bit
 * when PAT is on, which is index 4. */
static uint32
gpu_stage_pat(void* cpu)
{
	if (cpu == NULL)
		return 0xffffffffu;
	uint64 va = (uint64)(uintptr_t)cpu;
	uint64 table = 0;
	asm volatile("mov %%cr3, %0" : "=r"(table));
	table &= ~0xfffull;
	static const int shifts[4] = { 39, 30, 21, 12 };
	for (int level = 0; level < 4; level++) {
		void* mapped = NULL;
		area_id id = map_physical_memory("r5 pat", (phys_addr_t)table,
			4096, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA, &mapped);
		if (id < B_OK || mapped == NULL)
			return 0xffffffffu;
		uint32 slot = (uint32)((va >> shifts[level]) & 0x1ffu);
		uint64 entry = ((volatile uint64*)mapped)[slot];
		delete_area(id);
		if ((entry & 1ull) == 0)
			return 0xffffffffu;
		bool leaf = level == 3 || (entry & (1ull << 7)) != 0;
		if (leaf) {
			uint32 pat = (uint32)((entry >> 7) & 1ull);
			uint32 pcd = (uint32)((entry >> 4) & 1ull);
			uint32 pwt = (uint32)((entry >> 3) & 1ull);
			return (pat << 2) | (pcd << 1) | pwt;
		}
		table = entry & 0x000ffffffffff000ull;
	}
	return 0xffffffffu;
}


/* Caller holds the lock and drops it while polling. A miss is a landed
 * fence, or an idle CP, with the word absent: the series continues.
 * A hang is a busy CP at 5s, or the device already lost. */
static gpu_stage_pace
gpu_stage_pace_wait(radeon_info* info, int slot, uint64 sequence,
	bool expectWord, uint8* destCpu, bool flush, bool requireFence)
{
	bigtime_t start = slot >= 0 ? sJobs[slot].submitted : system_time();
	gpu_stage_pace pace;
	memset(&pace, 0, sizeof(pace));
	gpu_unlock();
	while (true) {
		pace.seen = r5_gfx9_user_seen();
		bool fence = pace.seen == sequence;
		pace.word = expectWord ? gpu_stage_word(destCpu, flush) : 0xDEADBEEF;
		pace.cp = gpu_mmio(info, kGcSegment, 0x01a0);
		bigtime_t elapsed = system_time() - start;
		bool got = pace.word == 0xDEADBEEF;
		if (fence && got) {
			pace.hang = false;
			break;
		}
		if (!requireFence && fence && !got && pace.cp == 0) {
			pace.hang = false;
			break;
		}
		if (sLost || elapsed >= 5000000) {
			if (requireFence)
				pace.hang = sLost || !fence || !got;
			else
				pace.hang = sLost || pace.cp != 0;
			break;
		}
		snooze(1000);
	}
	pace.ms = (int32)((system_time() - start) / 1000);
	gpu_lock();
	if (!pace.hang) {
		if (slot >= 0 && slot < (int)kMaxJob)
			sJobs[slot].live = false;
	} else if (slot >= 0) {
		bigtime_t deadline = system_time() + 2000000;
		while (!sLost && system_time() < deadline) {
			gpu_unlock();
			snooze(1000);
			gpu_lock();
		}
	}
	return pace;
}


static const char*
gpu_stage_how(const gpu_stage_pace& pace, bool expectWord)
{
	if (pace.hang)
		return "hang";
	if (expectWord && pace.word != 0xDEADBEEF)
		return "miss";
	return "ok";
}


/* One VMID 1 IB. The WRITE_DATA destination is destVa. A miss keeps
 * the series going. A hang stops it. linuxWords is the five-dword
 * wb test; the other steps keep the eight-dword packet. */
static bool
gpu_stage_step(radeon_info* info, const char* name, gpu_stage_page* ibPage,
	uint8* destCpu, uint64 destVa, uint64 destPhys, bool expectWord,
	bool ibFlush, bool readFlush, uint32 fill, uint32 dwords, bool linuxWords)
{
	if (ibPage == NULL || ibPage->cpu == NULL)
		return true;
	if (expectWord && destCpu != NULL) {
		*(volatile uint32*)(destCpu + 64) = fill;
		asm volatile("sfence" ::: "memory");
	}
	bool stored = false;
	if (linuxWords)
		stored = r5_gfx9_stage_wb(ibPage->cpu, destVa);
	else if (ibFlush)
		stored = r5_gfx9_stage_ib(ibPage->cpu, destVa);
	else
		stored = r5_gfx9_stage_ib_store(ibPage->cpu, destVa);
	if (!ibFlush)
		asm volatile("sfence" ::: "memory");
	if (!stored) {
		dprintf("radeon_hd: %s ib\n", name);
		return true;
	}
	r5_gfx9_user_ib ib;
	memset(&ib, 0, sizeof(ib));
	ib.va = ibPage->va;
	ib.dwords = dwords;
	ib.vmid = 1;
	sLastIbVa = ibPage->va;
	gpu_stage_words(name, ibPage->va, ibPage->cpu, ibPage->phys);
	uint64 sequence = 0;
	r5_gfx9_set_debug(gpu_debug_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(0, 0);
	if (!r5_gfx9_user_submit(&ib, 1, 0, &sKernelPreamble, &sequence)) {
		dprintf("radeon_hd: %s submit\n", name);
		gpu_stage_dump(name, ibPage->va, ibPage->cpu, ibPage->phys);
		return !sLost;
	}
	int slot = gpu_stage_slot(sequence);
	gpu_stage_pace pace = gpu_stage_pace_wait(info, slot, sequence, expectWord,
		expectWord ? destCpu : NULL, readFlush, false);
	uint32 pat = destCpu != NULL ? gpu_stage_pat(destCpu + 64) : 0;
	dprintf("radeon_hd: %s %s %ld ms seq %llu seen %llu word %08lx "
		"cp %08lx phys %016llx pat %lu\n",
		name, gpu_stage_how(pace, expectWord), (long)pace.ms,
		(unsigned long long)sequence, (unsigned long long)pace.seen,
		(unsigned long)pace.word, (unsigned long)pace.cp,
		(unsigned long long)destPhys, (unsigned long)pat);
	if (pace.hang) {
		gpu_stage_dump(name, ibPage->va, ibPage->cpu, ibPage->phys);
		gpu_ib_words(name);
	}
	return !pace.hang;
}


static uint64
gpu_stage_rdmsr(uint32 index)
{
	uint32 lo = 0;
	uint32 hi = 0;
	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(index));
	return ((uint64)hi << 32) | lo;
}


static void
gpu_stage_mtrr(const char* when)
{
	uint64 cap = gpu_stage_rdmsr(0xfeu);
	uint32 count = (uint32)(cap & 0xffu);
	if (count > 16)
		count = 16;
	dprintf("radeon_hd: mtrr %s cap %016llx count %lu\n",
		when, (unsigned long long)cap, (unsigned long)count);
	for (uint32 i = 0; i < count; i++) {
		uint64 base = gpu_stage_rdmsr(0x200u + i * 2u);
		uint64 mask = gpu_stage_rdmsr(0x201u + i * 2u);
		dprintf("radeon_hd: mtrr %s %lu base %016llx mask %016llx\n",
			when, (unsigned long)i, (unsigned long long)base,
			(unsigned long long)mask);
	}
}


/* Eight words through the page's own pointer, then through a second
 * mapping of the same physical page. A mismatch stops the series. */
static bool
gpu_stage_visible(const char* name, gpu_stage_page* page)
{
	uint32 store[8];
	uint32 same[8];
	uint32 other[8];
	if (page == NULL || page->cpu == NULL)
		return false;
	for (uint32 i = 0; i < 8; i++)
		store[i] = 0x51550000u + i;
	memcpy(page->cpu, store, sizeof(store));
	asm volatile("sfence" ::: "memory");
	memcpy(same, page->cpu, sizeof(same));
	memset(other, 0, sizeof(other));
	void* alias = NULL;
	area_id id = map_physical_memory("r5 stage alias",
		(phys_addr_t)page->phys, 4096, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA, &alias);
	bool mapped = id >= B_OK && alias != NULL;
	if (mapped)
		memcpy(other, alias, sizeof(other));
	if (id >= B_OK)
		delete_area(id);
	dprintf("radeon_hd: %s cpu phys %016llx "
		"same %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx "
		"other %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
		name, (unsigned long long)page->phys,
		(unsigned long)same[0], (unsigned long)same[1],
		(unsigned long)same[2], (unsigned long)same[3],
		(unsigned long)same[4], (unsigned long)same[5],
		(unsigned long)same[6], (unsigned long)same[7],
		(unsigned long)other[0], (unsigned long)other[1],
		(unsigned long)other[2], (unsigned long)other[3],
		(unsigned long)other[4], (unsigned long)other[5],
		(unsigned long)other[6], (unsigned long)other[7]);
	bool equal = mapped;
	for (uint32 i = 0; i < 8; i++) {
		if (same[i] != store[i] || other[i] != store[i])
			equal = false;
	}
	if (!equal) {
		dprintf("radeon_hd: %s pat %lu\n", name,
			(unsigned long)gpu_stage_pat(page->cpu));
		gpu_stage_mtrr("mismatch");
	}
	return equal;
}


/* One indirect buffer and one WRITE_DATA page. ibFlush makes the
 * packet visible. readFlush is only the CPU read of the word. */
static bool
gpu_stage_pair(radeon_info* info, const char* name, uint64 ibVa, bool ibGtt,
	bool ibWc, uint64 destVa, bool destGtt, bool destWc, bool ibFlush,
	bool readFlush, uint32 fill, uint32 dwords, bool linuxWords)
{
	gpu_stage_page ib;
	gpu_stage_page dest;
	if (!gpu_stage_map(info, &ib, ibVa, ibGtt, ibWc)) {
		dprintf("radeon_hd: %s map\n", name);
		return true;
	}
	if (!gpu_stage_map(info, &dest, destVa, destGtt, destWc)) {
		dprintf("radeon_hd: %s map\n", name);
		gpu_stage_release(&ib);
		return true;
	}
	if (!gpu_stage_visible(name, &ib) || !gpu_stage_visible(name, &dest)) {
		gpu_stage_release(&dest);
		gpu_stage_release(&ib);
		return false;
	}
	r5_gfx9_vmid1_levels(name, ib.va);
	r5_gfx9_vmid1_levels(name, dest.va);
	bool go = gpu_stage_step(info, name, &ib, dest.cpu, dest.va + 64,
		dest.phys, true, ibFlush, readFlush, fill, dwords, linuxWords);
	gpu_stage_release(&dest);
	gpu_stage_release(&ib);
	return go;
}


/* DMA_DATA from a page in the high range into a fresh low VRAM page.
 * The indirect buffer stays at a fresh low VA. A miss continues. A
 * busy CP at 5s stops the series. */
static bool
gpu_stage_dma(radeon_info* info, const char* name, uint64 srcVa, bool srcGtt)
{
	gpu_stage_page ib;
	gpu_stage_page src;
	gpu_stage_page dest;
	if (!gpu_stage_map(info, &ib, gpu_stage_fresh(), false, false)) {
		dprintf("radeon_hd: %s map\n", name);
		return true;
	}
	if (!gpu_stage_map(info, &src, srcVa, srcGtt, false)) {
		dprintf("radeon_hd: %s map\n", name);
		gpu_stage_release(&ib);
		return true;
	}
	if (!gpu_stage_map(info, &dest, gpu_stage_fresh(), false, false)) {
		dprintf("radeon_hd: %s map\n", name);
		gpu_stage_release(&src);
		gpu_stage_release(&ib);
		return true;
	}
	*(volatile uint32*)(src.cpu + 64) = 0xDEADBEEF;
	asm volatile("sfence" ::: "memory");
	r5_gfx9_stage_bytes(src.cpu, 128);
	uint32* words = (uint32*)ib.cpu;
	/* DMA_DATA control 0x60200000. This is the word the stage ran.
	 * Linux soc15d.h names DST_SEL 0, 1 and 3 and does not name 2.
	 * The checker allows this word and rejects any other DST_SEL 2.
	 * Whether 2 should be 3 waits for its own diagnostic boot. */
	words[0] = 0xc0055000u;
	words[1] = 0x60200000u;
	words[2] = (uint32)src.va;
	words[3] = (uint32)(src.va >> 32);
	words[4] = (uint32)dest.va;
	words[5] = (uint32)(dest.va >> 32);
	words[6] = 0x80000080u;
	r5_gfx9_stage_bytes(ib.cpu, 28);
	r5_gfx9_vmid1_levels(name, src.va);
	r5_gfx9_vmid1_levels(name, dest.va);
	r5_gfx9_user_ib submit;
	memset(&submit, 0, sizeof(submit));
	submit.va = ib.va;
	submit.dwords = 7;
	submit.vmid = 1;
	sLastIbVa = ib.va;
	gpu_stage_words(name, ib.va, ib.cpu, ib.phys);
	uint64 sequence = 0;
	r5_gfx9_set_debug(gpu_debug_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(0, 0);
	if (!r5_gfx9_user_submit(&submit, 1, 0, &sKernelPreamble, &sequence)) {
		dprintf("radeon_hd: %s submit\n", name);
		gpu_stage_dump(name, ib.va, ib.cpu, ib.phys);
		gpu_stage_release(&dest);
		gpu_stage_release(&src);
		gpu_stage_release(&ib);
		return !sLost;
	}
	int slot = gpu_stage_slot(sequence);
	gpu_stage_pace pace = gpu_stage_pace_wait(info, slot, sequence, true,
		dest.cpu, true, false);
	uint32 pat = gpu_stage_pat(dest.cpu + 64);
	dprintf("radeon_hd: %s %s %ld ms seq %llu seen %llu word %08lx "
		"cp %08lx phys %016llx pat %lu\n",
		name, gpu_stage_how(pace, true), (long)pace.ms,
		(unsigned long long)sequence, (unsigned long long)pace.seen,
		(unsigned long)pace.word, (unsigned long)pace.cp,
		(unsigned long long)dest.phys, (unsigned long)pat);
	if (pace.hang) {
		gpu_stage_dump(name, ib.va, ib.cpu, ib.phys);
		gpu_ib_words(name);
	}
	gpu_stage_release(&dest);
	gpu_stage_release(&src);
	gpu_stage_release(&ib);
	return !pace.hang;
}


static const uint64 kStagePIb = R5_GPU_VA_MAX - 0x8000ull;
static const uint64 kStagePPage = R5_GPU_VA_MAX - 0x7000ull;
static const uint64 kStagePIb2 = R5_GPU_VA_MAX - 0x6000ull;


/* One VMID 1 indirect buffer, one fence. step 0 is the marker
 * self-test and is not a numbered stage P line. ROOM prints the
 * pointers and does not write the packet. */
static uint32
gpu_stage_submit(radeon_info* info, uint32 step, const char* name,
	uint64 va, uint32 n, uint64 extraVa, uint32 extraN)
{
	r5_gfx9_user_ib submit;
	memset(&submit, 0, sizeof(submit));
	submit.va = va;
	submit.dwords = n;
	submit.vmid = 1;
	sLastIbVa = va;
	sLastIbN = extraN != 0 ? 2 : 1;
	sLastIbList[0] = va;
	sLastIbDw[0] = n;
	if (extraN != 0) {
		sLastIbList[1] = extraVa;
		sLastIbDw[1] = extraN;
	}
	uint64 sequence = 0;
	int32 before = r5_gfx9_priv_count();
	uint32 need = r5_gfx9_user_commit_dwords(extraN != 0 ? 2 : 1);
	if (need == 0 || !r5_gfx9_user_room(need)) {
		uint64 wptr = 0;
		uint64 rptr = 0;
		r5_gfx9_ring_ptrs(&wptr, &rptr);
		if (step == 0) {
			dprintf("radeon_hd: mark selftest room wptr %llx rptr %llx\n",
				(unsigned long long)wptr, (unsigned long long)rptr);
		} else {
			dprintf("radeon_hd: p%lu %s room wptr %llx rptr %llx\n",
				(unsigned long)step, name,
				(unsigned long long)wptr, (unsigned long long)rptr);
		}
		if (!sLost)
			gpu_lost(info, "p");
		return R5_GPU_P_ROOM;
	}
	r5_gfx9_set_debug(gpu_debug_enabled() ? 1 : 0);
	r5_gfx9_set_job_pasid(0, 0);
	if (!r5_gfx9_user_submit(&submit, 1, 0, &sKernelPreamble, &sequence)) {
		if (step == 0)
			dprintf("radeon_hd: mark selftest submit\n");
		else {
			dprintf("radeon_hd: p%lu %s submit\n",
				(unsigned long)step, name);
		}
		gpu_lost(info, "p");
		return R5_GPU_P_TIMEOUT;
	}
	int slot = gpu_stage_slot(sequence);
	gpu_stage_pace pace = gpu_stage_pace_wait(info, slot, sequence, false,
		NULL, false, true);
	int32 after = r5_gfx9_priv_count();
	uint32 status = R5_GPU_P_OK;
	const char* how = gpu_stage_how(pace, false);
	if (after != before) {
		status = R5_GPU_P_PRIV;
		how = "priv";
		if (!sLost)
			gpu_lost(info, "p");
	} else if (pace.hang) {
		status = R5_GPU_P_TIMEOUT;
		how = "timeout";
		if (!sLost)
			gpu_ib_words(step == 0 ? "mark" : name);
	}
	if (step == 0) {
		dprintf("radeon_hd: mark selftest %s %ld ms\n", how,
			(long)pace.ms);
	} else {
		dprintf("radeon_hd: p%lu %s %s %ld ms\n",
			(unsigned long)step, name, how, (long)pace.ms);
	}
	return status;
}


/* Kernel mappings only. The IB and the test page are VRAM windows,
 * never a user pointer. One packet type per step. A privilege fault
 * marks the device lost. A fence that is still out at 5s is lost
 * through the watchdog. U4 and U2 run later, from stage U. */
static status_t
gpu_stage_p(radeon_info* info, r5_gpu_stage_p* out)
{
	memset(out, 0, sizeof(*out));
	out->size = (uint32)sizeof(*out);
	out->version = 1;
	out->count = R5_P_COUNT;
	for (uint32 i = 0; i < R5_P_COUNT && i < R5_GPU_P_STEPS; i++) {
		const char* name = r5_stage_p_name(i);
		out->status[i] = R5_GPU_P_SKIP;
		for (uint32 c = 0; c < 23 && name[c] != '\0'; c++)
			out->name[i][c] = name[c];
	}
	if (sLost) {
		dprintf("radeon_hd: skipped, gpu lost\n");
		return ECANCELED;
	}
	if (!gpu_render_enabled() || !gpu_selftest_enabled())
		return B_NOT_ALLOWED;
	if (!r5_gfx9_vmid1_setup()) {
		dprintf("radeon_hd: p skip vmid\n");
		return B_NO_INIT;
	}
	gpu_stage_page ib;
	gpu_stage_page ib2;
	gpu_stage_page page;
	memset(&ib, 0, sizeof(ib));
	memset(&ib2, 0, sizeof(ib2));
	memset(&page, 0, sizeof(page));
	if (!gpu_stage_map(info, &ib, kStagePIb, false)
		|| !gpu_stage_map(info, &ib2, kStagePIb2, false)
		|| !gpu_stage_map(info, &page, kStagePPage, false)) {
		dprintf("radeon_hd: p map\n");
		gpu_stage_release(&ib);
		gpu_stage_release(&ib2);
		gpu_stage_release(&page);
		return B_NO_MEMORY;
	}
	memset(page.cpu, 0, 4096);
	*(volatile uint32*)(page.cpu + R5_P_SHADER_OFF) = R5_P_SHADER;
	r5_gfx9_stage_bytes(page.cpu, 4096);
	bool stop = false;
	uint32 probe[8];
	memset(probe, 0, sizeof(probe));
	uint32 probeN = r5_stage_p_build(R5_P_WRITE_DATA, page.va, probe, 8, 0);
	if (probeN == 6)
		probe[4] = R5_P_MARK_VALUE;
	if (probeN != 6) {
		dprintf("radeon_hd: mark selftest bad\n");
		stop = true;
	} else {
		memcpy(ib.cpu, probe, probeN * 4);
		r5_gfx9_stage_bytes(ib.cpu, probeN * 4);
		uint32 probeStatus = gpu_stage_submit(info, 0, "mark", ib.va,
			probeN, 0, 0);
		if (probeStatus != R5_GPU_P_OK) {
			dprintf("radeon_hd: mark selftest bad\n");
			stop = true;
		} else {
			uint32 back = 0;
			uint32 got = r5_gfx9_vmid1_copy(page.va, &back, 1);
			dprintf("radeon_hd: mark selftest %s\n",
				got == 1 && back == R5_P_MARK_VALUE ? "ok" : "bad");
			gpu_mark(page.va);
		}
	}
	for (uint32 i = 0; i < R5_P_COUNT && i < R5_GPU_P_STEPS; i++) {
		if (stop)
			break;
		uint32 words[256];
		memset(words, 0, sizeof(words));
		uint64 extraVa = 0;
		uint32 extraN = 0;
		if (i == R5_P_IB2) {
			uint32 body[8];
			memset(body, 0, sizeof(body));
			extraN = r5_stage_p_ib2_body(page.va, body, 8);
			if (extraN != 0) {
				memcpy(ib2.cpu, body, extraN * 4);
				r5_gfx9_stage_bytes(ib2.cpu, extraN * 4);
				extraVa = ib2.va;
			}
		} else if (i == R5_P_IB24) {
			uint32 preambleN = 0;
			const uint32* preamble = r5_stage_p_preamble(&preambleN);
			memcpy(ib2.cpu, preamble, preambleN * 4);
			r5_gfx9_stage_bytes(ib2.cpu, preambleN * 4);
			extraVa = ib2.va;
			extraN = preambleN;
		}
		uint32 n = r5_stage_p_build(i, page.va, words, 256, ib2.va);
		if (n == 0 || (i == R5_P_IB2 && extraN == 0)) {
			out->status[i] = R5_GPU_P_TIMEOUT;
			out->first_fail = i + 1;
			stop = true;
			continue;
		}
		memcpy(ib.cpu, words, n * 4);
		r5_gfx9_stage_bytes(ib.cpu, n * 4);
		uint32 status = gpu_stage_submit(info, i + 1, r5_stage_p_name(i),
			ib.va, n, extraVa, extraN);
		out->status[i] = status;
		if (status != R5_GPU_P_OK) {
			out->first_fail = i + 1;
			stop = true;
		}
	}
	gpu_stage_release(&page);
	gpu_stage_release(&ib2);
	gpu_stage_release(&ib);
	if (stop)
		dprintf("radeon_hd: skipped, gpu lost\n");
	return stop ? ECANCELED : B_OK;
}


/* Stick 25 does not call this. The CPU self-check stays for a later stick. */
__attribute__((unused))
static bool
gpu_stage_self(radeon_info* info, const char* name, bool gtt)
{
	gpu_stage_page page;
	if (!gpu_stage_map(info, &page, gpu_stage_fresh(), gtt, false)) {
		dprintf("radeon_hd: %s map\n", name);
		return false;
	}
	bool ok = gpu_stage_visible(name, &page);
	gpu_stage_release(&page);
	return ok;
}


/* CPU only. Eight words go out through the PCI BAR. The same offset
 * is read back through the carveout at once, then again after sfence.
 * No clflush: that would hide a write the HDP is still holding. */
static void
gpu_stage_hdp(radeon_info* info)
{
	(void)info;
	uint64 offset = 0;
	uint8* bar = NULL;
	if (r5_gfx9_alloc_vram(4096, 4096, &offset, &bar) != B_OK || bar == NULL) {
		dprintf("radeon_hd: hdp alloc fail\n");
		return;
	}
	uint8* alias = r5_gfx9_vram_cpu(offset);
	if (alias == NULL || alias == bar || r5_gfx9_vram_bar(offset) != bar) {
		dprintf("radeon_hd: hdp alias fail off %016llx\n",
			(unsigned long long)offset);
		return;
	}
	volatile uint32* src = (volatile uint32*)(bar + 64);
	volatile uint32* dst = (volatile uint32*)(alias + 64);
	for (uint32 i = 0; i < 8; i++)
		src[i] = 0xA1000000u + i;
	uint32 now[8];
	uint32 later[8];
	for (uint32 i = 0; i < 8; i++)
		now[i] = dst[i];
	asm volatile("sfence" ::: "memory");
	for (uint32 i = 0; i < 8; i++)
		later[i] = dst[i];
	dprintf("radeon_hd: hdp off %016llx now %08x %08x %08x %08x "
		"%08x %08x %08x %08x\n",
		(unsigned long long)offset,
		now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7]);
	dprintf("radeon_hd: hdp sfence %08x %08x %08x %08x %08x %08x %08x %08x\n",
		later[0], later[1], later[2], later[3],
		later[4], later[5], later[6], later[7]);
}


static uint32
gpu_stage_r1_word(gpu_stage_page* page)
{
	uint32 word = 0;
	if (page != NULL && page->cpu != NULL)
		memcpy(&word, page->cpu, sizeof(word));
	return word;
}


static uint32
gpu_stage_r1_job(radeon_info* info, const char* name, gpu_stage_page* ib,
	const uint32* words, uint32 n)
{
	memcpy(ib->cpu, words, n * 4);
	r5_gfx9_stage_bytes(ib->cpu, n * 4);
	return gpu_stage_submit(info, 1, name, ib->va, n, 0, 0);
}


static void
gpu_stage_r1_line(const char* what, uint64 x, uint64 y)
{
	dprintf("radeon_hd: t %lld %s x %016llx y %016llx "
		"inv %d hub %d vmids %d fence %d cond %d gds %d pasid %d ce %d\n",
		(long long)system_time(), what == NULL ? "" : what,
		(unsigned long long)x, (unsigned long long)y,
		r5_gfx9_inv_range_on(), r5_gfx9_hub_linux_on(),
		r5_gfx9_vmids_on(), r5_gfx9_flush_fence_on(),
		r5_gfx9_cond_exec_on(), r5_gfx9_gds_switch_on(),
		r5_gfx9_pasid_map_on(), r5_gfx9_ce_meta_on());
}


/* R1a WRITE_DATA and R1b RELEASE_MEM, or R1c the IB fetch.
 * Page A stays allocated until this returns, so a stale write still
 * has a page to land on. R1c is a separate call: a miss there can hang. */
static void
gpu_stage_r1(radeon_info* info, uint32 which, uint64 x, uint64 y,
	uint32_t* oldWord, uint32_t* newWord)
{
	gpu_stage_page a;
	gpu_stage_page b;
	gpu_stage_page cmd;
	gpu_stage_page dest;
	uint32 words[8];
	uint32 n;
	char what[80];
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	memset(&cmd, 0, sizeof(cmd));
	memset(&dest, 0, sizeof(dest));
	if (oldWord != NULL)
		*oldWord = 0;
	if (newWord != NULL)
		*newWord = 0;
	if (!gpu_stage_map(info, &a, x, true)
		|| !gpu_stage_map(info, &cmd, gpu_stage_fresh(), false)
		|| !gpu_stage_map(info, &b, gpu_stage_fresh(), false)
		|| !gpu_stage_map(info, &dest, y, false)) {
		gpu_stage_r1_line("r1 map", x, y);
		gpu_stage_release(&dest);
		gpu_stage_release(&b);
		gpu_stage_release(&cmd);
		gpu_stage_release(&a);
		return;
	}
	gpu_stage_unbind(&b);
	memset(a.cpu, 0, 4096);
	memset(b.cpu, 0, 4096);
	memset(dest.cpu, 0, 4096);
	if (which != R5_GPU_R1_AB)
		goto r1c;
	n = r5_r1_write(words, 8, x, 0x11111111u);
	gpu_stage_r1_line("r1a gtt", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1a-gtt", &cmd, words, n)
			!= R5_GPU_P_OK) {
		gpu_stage_r1_line("r1a gtt job", x, y);
		goto out;
	}
	gpu_stage_unbind(&a);
	if (!gpu_stage_bind(&b, x))
		goto out;
	n = r5_r1_write(words, 8, x, 0x22222222u);
	gpu_stage_r1_line("r1a vram", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1a-vram", &cmd, words, n)
			!= R5_GPU_P_OK)
		goto out;
	snprintf(what, sizeof(what), "r1a gtt %08lx vram %08lx",
		(unsigned long)gpu_stage_r1_word(&a),
		(unsigned long)gpu_stage_r1_word(&b));
	gpu_stage_r1_line(what, x, y);
	if (oldWord != NULL)
		*oldWord = gpu_stage_r1_word(&a);
	if (newWord != NULL)
		*newWord = gpu_stage_r1_word(&b);

	gpu_stage_unbind(&b);
	if (!gpu_stage_bind(&a, x))
		goto out;
	memset(a.cpu, 0, 4);
	memset(b.cpu, 0, 4);
	n = r5_r1_release(words, 8, x, 0x11111111u);
	gpu_stage_r1_line("r1b gtt", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1b-gtt", &cmd, words, n)
			!= R5_GPU_P_OK)
		goto out;
	gpu_stage_unbind(&a);
	if (!gpu_stage_bind(&b, x))
		goto out;
	n = r5_r1_release(words, 8, x, 0x22222222u);
	gpu_stage_r1_line("r1b vram", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1b-vram", &cmd, words, n)
			!= R5_GPU_P_OK)
		goto out;
	snprintf(what, sizeof(what), "r1b gtt %08lx vram %08lx",
		(unsigned long)gpu_stage_r1_word(&a),
		(unsigned long)gpu_stage_r1_word(&b));
	gpu_stage_r1_line(what, x, y);
	goto out;

r1c:
	if (which != R5_GPU_R1_C)
		goto out;
	gpu_stage_r1_line("r1c before", x, y);
	gpu_stage_unbind(&b);
	if (!gpu_stage_bind(&a, x))
		goto out;
	n = r5_r1_write(words, 8, y, 0x33333333u);
	gpu_stage_r1_line("r1c gtt", x, y);
	if (n == 0 || gpu_stage_r1_job(info, "r1c-gtt", &a, words, n)
			!= R5_GPU_P_OK)
		goto out;
	gpu_stage_unbind(&a);
	if (!gpu_stage_bind(&b, x))
		goto out;
	n = r5_r1_write(words, 8, y, 0x44444444u);
	memcpy(b.cpu, words, n * 4);
	r5_gfx9_stage_bytes(b.cpu, n * 4);
	gpu_stage_r1_line("r1c vram", x, y);
	if (gpu_stage_r1_job(info, "r1c-vram", &b, words, n) != R5_GPU_P_OK)
		goto out;
	snprintf(what, sizeof(what), "r1c dest %08lx",
		(unsigned long)gpu_stage_r1_word(&dest));
	gpu_stage_r1_line(what, x, y);
out:
	gpu_stage_release(&dest);
	gpu_stage_release(&b);
	gpu_stage_release(&cmd);
	gpu_stage_r1_line("r1 free a", x, y);
	gpu_stage_release(&a);
}


static status_t
gpu_stage_u(radeon_info* info)
{
	if (!gpu_render_enabled() || !gpu_selftest_enabled())
		return B_NOT_ALLOWED;
	sStageFresh = 0;
	gpu_stage_mtrr("before");
	gpu_stage_hdp(info);
	if (!r5_gfx9_vmid1_setup()) {
		dprintf("radeon_hd: u1 skip vmid\n");
		return B_NO_INIT;
	}
	bool stop = false;
	/* The HDP probe ran first and does not use the GPU. Group B then
	 * writes only, with the indirect buffer at a fresh low address.
	 * Group A fetches a VRAM indirect buffer. C1 is last. */
	if (!stop && !gpu_stage_pair(info, "b1", gpu_stage_fresh(), false,
			false, 0xffff800000041000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "b2", gpu_stage_fresh(), false,
			false, 0xffff800000041000ull, true, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "b3", gpu_stage_fresh(), false,
			false, 0xffff800000001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_dma(info, "b5", 0xffff800000041000ull, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a2", 0xffff800100000000ull, false,
			false, 0xffff800100001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a1", R5_GPU_HIGH_VA_MAX - 0x1000ull, false,
			false, R5_GPU_HIGH_VA_MAX - 0x2000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a3", 0xffff808000000000ull, false,
			false, 0xffff808000001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a8", 0x01000000ull, false,
			false, 0x01001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a7", 0x00100000ull, false,
			false, 0x00101000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a4", 0xffff8000c0000000ull, false,
			false, 0xffff8000c0001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a5", 0xffff800040000000ull, false,
			false, 0xffff800040001000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "a6", 0xffff800000200000ull, false,
			false, 0xffff800000201000ull, false, false, true, true, 0, 8, false))
		stop = true;
	if (!stop && !gpu_stage_pair(info, "c1",
			R5_GPU_HIGH_VA_OFFSET + 0x1000ull, false, false,
			R5_GPU_HIGH_VA_OFFSET + 0x2000ull, false, false,
			true, true, 0, 8, false))
		stop = true;
	gpu_stage_mtrr("after");
	return B_OK;
}


static status_t
gpu_open(const char* name, uint32 flags, void** cookie)
{
	(void)name;
	(void)flags;
	if (!sReady || gDeviceInfo[0] == NULL)
		return B_ERROR;
	gpu_client* client = (gpu_client*)malloc(sizeof(gpu_client));
	if (client == NULL)
		return B_NO_MEMORY;
	memset(client, 0, sizeof(*client));
	client->info = gDeviceInfo[0];
	gpu_lock();
	int pasid = r5_pasid_alloc(&sPasids, 16);
	if (pasid < 0) {
		gpu_unlock();
		free(client);
		return B_NO_MEMORY;
	}
	client->pasid = (uint32)pasid;
	client->next = sClientList;
	sClientList = client;
	/* The file tree is published before the cookie leaves the lock. */
	status_t root = gpu_file_root(client);
	if (root != B_OK) {
		sClientList = client->next;
		r5_pasid_free(&sPasids, client->pasid);
		gpu_unlock();
		free(client);
		return root;
	}
	gpu_unlock();
	*cookie = client;
	return B_OK;
}


static status_t
gpu_close(void* cookie)
{
	(void)cookie;
	return B_OK;
}


static status_t
gpu_free(void* cookie)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL)
		return B_OK;
	gpu_lock();
	status_t status = B_OK;
	uint64 seen = r5_gfx9_user_seen();
	r5_gpu_eop();
	for (uint32 i = 0; i < kMaxJob; i++) {
		if (!sJobs[i].live)
			continue;
		for (uint32 n = 0; n < sJobs[i].count; n++) {
			gpu_sync* sync = sJobs[i].syncs[n];
			if (sync != NULL && sync->owner == client)
				sJobs[i].syncs[n] = NULL;
		}
	}
	for (uint32 i = 0; i < client->handleCount; i++) {
		uint32 handle = client->handles[i];
		if (handle == 0 || handle > kMaxBo)
			continue;
		gpu_bo* bo = &sBo[handle - 1u];
		if (!bo->live || bo->owner != client)
			continue;
		uint64 sequence = client->lastSequence;
		if (!sLost && sequence != 0 && seen < sequence) {
			bo->owner = NULL;
			bo->parked = true;
			bo->parkSequence = sequence;
			gpu_bo_hold_tree(bo);
		} else
			gpu_bo_release(bo);
	}
	for (uint32 i = 0; i < kMaxSync; i++) {
		client->syncs[i].live = false;
		client->syncs[i].owner = NULL;
	}
	if (client->pasid != 0)
		r5_pasid_free(&sPasids, client->pasid);
	if (client->tree != NULL) {
		r5_file_tree* tree = client->tree;
		client->tree = NULL;
		tree->seen = seen;
		r5_file_tree_drop(tree);
	}
	if (sClientList == client)
		sClientList = client->next;
	else {
		for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
			if (scan->next == client) {
				scan->next = client->next;
				break;
			}
		}
	}
	if (client->contexts > 0) {
		sContexts -= client->contexts;
		if (sContexts < 0)
			sContexts = 0;
	}
	gpu_unlock();
	free(client);
	return status;
}


static status_t
gpu_read(void* cookie, off_t position, void* buffer, size_t* numBytes)
{
	(void)cookie;
	(void)position;
	(void)buffer;
	*numBytes = 0;
	return B_ERROR;
}


static status_t
gpu_write(void* cookie, off_t position, const void* buffer, size_t* numBytes)
{
	(void)cookie;
	(void)position;
	(void)buffer;
	*numBytes = 0;
	return B_ERROR;
}


static status_t
gpu_debug_switch(void* buffer, size_t length)
{
	r5_gpu_debug_switch in;
	status_t status = gpu_copy_in(buffer, length, &in, sizeof(in));
	if (status != B_OK)
		return status;
	if (!gpu_debug_enabled())
		return B_NOT_ALLOWED;
	if ((in.which & R5_GPU_SWITCH_VMID_PER_FILE) != 0)
		return B_NOT_ALLOWED;
	if ((in.which & R5_GPU_SWITCH_CE_META) != 0) {
		dprintf("radeon_hd: gfx9_ce_meta is a boot setting\n");
		return B_NOT_ALLOWED;
	}
	/* Idle first. The flush is after the registers move. */
	bigtime_t deadline = system_time() + 5000000;
	while (r5_gfx9_user_seen() < sHighestSequence) {
		if (system_time() > deadline)
			return B_TIMED_OUT;
		snooze(1000);
	}
	r5_gfx9_switch_registers(
		(in.which & R5_GPU_SWITCH_INV_RANGE) != 0 ? (in.on ? 1 : 0) : -1,
		(in.which & R5_GPU_SWITCH_HUB_LINUX) != 0 ? (in.on ? 1 : 0) : -1,
		(in.which & R5_GPU_SWITCH_VMIDS) != 0 ? (in.on ? 1 : 0) : -1);
	if ((in.which & R5_GPU_SWITCH_FLUSH_FENCE) != 0)
		r5_gfx9_set_flush_fence(in.on ? 1 : 0);
	if ((in.which & R5_GPU_SWITCH_COND_EXEC) != 0)
		r5_gfx9_set_cond_exec(in.on ? 1 : 0);
	if ((in.which & R5_GPU_SWITCH_GDS) != 0)
		r5_gfx9_set_gds_switch(in.on ? 1 : 0);
	if ((in.which & R5_GPU_SWITCH_DEFERRED) != 0) {
		sDeferred = in.on ? 1 : 0;
		r5_gfx9_set_defer_stamp(sDeferred ? gpu_defer_stamp : 0);
	}
	if (!r5_gfx9_vmid1_flush())
		return B_ERROR;
	dprintf("radeon_hd: t %lld switch which %lu on %lu "
		"inv %d hub %d vmids %d fence %d cond %d gds %d defer %d pasid %d\n",
		(long long)system_time(), (unsigned long)in.which,
		(unsigned long)in.on, r5_gfx9_inv_range_on(),
		r5_gfx9_hub_linux_on(), r5_gfx9_vmids_on(),
		r5_gfx9_flush_fence_on(), r5_gfx9_cond_exec_on(),
		r5_gfx9_gds_switch_on(), sDeferred, r5_gfx9_pasid_map_on());
	return B_OK;
}


static status_t
gpu_file_root(gpu_client* client)
{
	if (!r5_gfx9_vmid_per_file() || client == NULL)
		return B_OK;
	if (client->tree != NULL)
		return B_OK;
	if (!r5_gfx9_vmid1_setup())
		return B_NO_INIT;
	r5_file_tree* tree = (r5_file_tree*)malloc(sizeof(r5_file_tree));
	if (tree == NULL)
		return B_NO_MEMORY;
	memset(tree, 0, sizeof(*tree));
	tree->pagesN = 256;
	tree->pages = (r5_vm_page*)malloc(sizeof(r5_vm_page) * tree->pagesN);
	if (tree->pages == NULL) {
		free(tree);
		return B_NO_MEMORY;
	}
	if (!r5_gfx9_file_vm_init(&tree->vm, tree->pages, tree->pagesN)) {
		free(tree->pages);
		free(tree);
		return B_NO_MEMORY;
	}
	if (!sFileVmidsReady) {
		r5_vmid_init_files(&sFileVmids);
		sFileVmidsReady = true;
	}
	tree->owner = sNextFileOwner;
	sNextFileOwner++;
	if (sNextFileOwner == 0)
		sNextFileOwner = 1;
	r5_file_count_open(&tree->count, (int32)tree->pagesN);
	client->tree = tree;
	return B_OK;
}


static status_t
gpu_get_counters(gpu_client* client, void* buffer, size_t length)
{
	if (client == NULL || buffer == NULL || length < sizeof(r5_gpu_counters))
		return B_BAD_VALUE;
	if (!is_user_address_range(buffer, sizeof(r5_gpu_counters)))
		return B_BAD_ADDRESS;
	r5_gpu_counters out;
	memset(&out, 0, sizeof(out));
	out.size = (uint32)sizeof(out);
	out.version = 1;
	gpu_lock();
	r5_vm* shared = r5_gfx9_shared_vm();
	uint32 sharedPages = shared != NULL ? shared->used : 0;
	uint32 trees = 0;
	uint32 treePages = 0;
	uint32 treeUsed = 0;
	for (gpu_client* scan = sClientList; scan != NULL; scan = scan->next) {
		if (scan->tree == NULL)
			continue;
		trees++;
		treePages += scan->tree->pagesN;
		treeUsed += scan->tree->vm.used;
	}
	for (uint32 i = 0; i < kMaxBo; i++) {
		if (!sBo[i].live)
			continue;
		out.bo_count++;
		if (sBo[i].owner == client)
			out.bo_count_space++;
	}
	out.pt_pages = sharedPages + treeUsed;
	out.vmids = 1u + trees;
	out.trees = trees;
	out.tree_pages = treePages;
	if (client->tree != NULL) {
		out.pt_pages_space = client->tree->vm.used;
		out.vmids_space = 1;
		out.trees_space = 1;
		out.tree_pages_space = client->tree->pagesN;
	} else
		out.pt_pages_space = sharedPages;
	gpu_unlock();
	if (user_memcpy(buffer, &out, sizeof(out)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
gpu_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	gpu_client* client = (gpu_client*)cookie;
	if (client == NULL || client->info == NULL)
		return B_BAD_VALUE;
	if (op == R5_GPU_GET_INFO)
		return gpu_get_info(client, buffer, length);
	if (op == R5_GPU_GET_COUNTERS)
		return gpu_get_counters(client, buffer, length);
	if (!gpu_render_enabled())
		return B_NOT_ALLOWED;
	if (op == R5_GPU_BO_EXPORT || op == R5_GPU_BO_IMPORT)
		return B_UNSUPPORTED;

	gpu_lock();
	sInfo = client->info;
	if (r5_gfx9_vmid_per_file()) {
		status_t root = gpu_file_root(client);
		if (root != B_OK) {
			gpu_unlock();
			return root;
		}
	}
	status_t status = B_BAD_VALUE;
	if (op == R5_GPU_READ_REG) {
		r5_gpu_read_reg in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.offset != kGbAddrConfig)
			status = B_NOT_ALLOWED;
		else {
			in.value = gpu_mmio(client->info, kGcSegment, in.offset);
			status = gpu_copy_out(buffer, length, &in, sizeof(in));
		}
	} else if (op == R5_GPU_CTX_CREATE) {
		r5_gpu_ctx in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (!r5_gfx9_vmid1_setup())
			status = B_NO_INIT;
		else {
			if (sContexts > 0)
				if (gpu_debug_enabled())
				dprintf("radeon_hd: t %lld vmid1 reused, clients %ld\n",
					(long long)system_time(), (long)sContexts);
			sContexts++;
			if (client->contexts == 0)
				client->fenceCtx = r5_ctx_handout(&sCtxCounter);
			client->contexts++;
			in.context = 1;
			status = gpu_copy_out(buffer, length, &in, sizeof(in));
		}
	} else if (op == R5_GPU_CTX_DESTROY) {
		r5_gpu_ctx in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.context != 1 || client->contexts <= 0)
			status = B_BAD_VALUE;
		else {
			client->contexts--;
			sContexts--;
			status = B_OK;
		}
	} else if (op == R5_GPU_BO_CREATE)
		status = gpu_bo_create(client, buffer, length);
	else if (op == R5_GPU_BO_FREE)
		status = gpu_bo_free(client, buffer, length);
	else if (op == R5_GPU_BO_MAP)
		status = gpu_bo_map(client, buffer, length);
	else if (op == R5_GPU_VA_MAP)
		status = gpu_va_map(client, buffer, length);
	else if (op == R5_GPU_VA_UNMAP)
		status = gpu_va_unmap(client, buffer, length);
	else if (op == R5_GPU_VA_REPLACE)
		status = gpu_va_replace(client, buffer, length, false);
	else if (op == R5_GPU_STAGE_P) {
		r5_gpu_stage_p in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status == B_OK) {
			status = gpu_stage_p(client->info, &in);
			status_t copied = gpu_copy_out(buffer, length, &in, sizeof(in));
			if (copied != B_OK)
				status = copied;
		}
	} else if (op == R5_GPU_DEBUG_SWITCH)
		status = gpu_debug_switch(buffer, length);
	else if (op == R5_GPU_LOG_REGS) {
		r5_gpu_log_regs in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.version != 1)
			status = B_BAD_VALUE;
		else {
			r5_gfx9_log_state("selftest", in.line, sizeof(in.line));
			in.length = (uint32)strlen(in.line);
			status = gpu_copy_out(buffer, length, &in, sizeof(in));
		}
	} else if (op == R5_GPU_STAGE_R1) {
		r5_gpu_stage_r1 in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status != B_OK)
			;
		else if (in.version != 1
			|| (in.which != R5_GPU_R1_AB && in.which != R5_GPU_R1_C)
			|| r5_vm_va_fits(in.x, 4096) == 0
			|| in.y != in.x + 0x1000ull
			|| r5_vm_va_fits(in.y, 4096) == 0)
			status = B_BAD_VALUE;
		else if (sLost) {
			dprintf("radeon_hd: skipped, gpu lost\n");
			status = ECANCELED;
		} else if (!gpu_render_enabled() || !gpu_selftest_enabled())
			status = B_NOT_ALLOWED;
		else if (!r5_gfx9_vmid1_setup()) {
			dprintf("radeon_hd: r1 skip vmid\n");
			status = B_NO_INIT;
		} else {
			gpu_stage_r1(client->info, in.which, in.x, in.y,
				&in.old_word, &in.new_word);
			status = sLost ? ECANCELED : B_OK;
			status_t copied = gpu_copy_out(buffer, length, &in, sizeof(in));
			if (copied != B_OK)
				status = copied;
		}
	}
	else if (op == R5_GPU_SELFTEST) {
		if (sLost) {
			dprintf("radeon_hd: skipped, gpu lost\n");
			status = ECANCELED;
		} else if (!gpu_selftest_enabled())
			status = B_NOT_ALLOWED;
		else
			status = gpu_stage_u(client->info);
	} else if (op == R5_GPU_VA_CLEAR)
		status = gpu_va_replace(client, buffer, length, true);
	else if (op == R5_GPU_CS_SUBMIT)
		status = gpu_submit(client, buffer, length);
	else if (op == R5_GPU_FENCE_WAIT)
		status = gpu_fence_wait(client->info, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_CREATE)
		status = gpu_sync_create(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_DESTROY)
		status = gpu_sync_destroy(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_WAIT)
		status = gpu_sync_wait(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_WAIT_LIST)
		status = gpu_sync_wait_list(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_SIGNAL)
		status = gpu_sync_signal(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_TRANSFER)
		status = gpu_sync_transfer(client, buffer, length);
	else if (op == R5_GPU_SYNCOBJ_RESET || op == R5_GPU_SYNCOBJ_QUERY) {
		r5_gpu_syncobj in;
		status = gpu_copy_in(buffer, length, &in, sizeof(in));
		if (status == B_OK) {
			gpu_sync* sync = gpu_sync_get(client, in.handle);
			if (sync == NULL)
				status = B_BAD_VALUE;
			else if (op == R5_GPU_SYNCOBJ_RESET) {
				sync->point = 0;
				sync->submitted = 0;
				status = B_OK;
			} else {
				in.point = sync->point;
				status = gpu_copy_out(buffer, length, &in, sizeof(in));
			}
		}
	} 	else
		status = B_UNSUPPORTED;
	gpu_unlock();
	return status;
}


device_hooks gR5GpuHooks = {
	gpu_open,
	gpu_close,
	gpu_free,
	gpu_control,
	gpu_read,
	gpu_write,
	NULL,
	NULL,
	NULL,
	NULL
};
