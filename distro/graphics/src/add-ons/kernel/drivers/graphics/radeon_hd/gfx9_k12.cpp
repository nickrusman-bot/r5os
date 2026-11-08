/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Kernel host for the shared GFX9 start, then the interrupt ring and the
 * GART. The packet bytes stay in gfx9_start.cpp. Register numbers are from
 * Linux v6.12 headers: vega10_ip_offset.h, osssys_4_0, nbio_7_0, gc_9_0.
 */

#include "gfx9_start.h"
#include "r5_agp.h"
#include "r5_inv_range.h"
#include "r5_vm_size.h"
#include "r5_vm.h"
#include "r5_gpu.h"
#include "r5_span.h"
#include "radeon_hd_private.h"

#include <PCI.h>
#include <KernelExport.h>
#include <driver_settings.h>

// The kernel exports this with C linkage. The private vm.h declaration
// pulls in thread types before the mutex type is defined, so it is not
// included here.
extern "C" status_t vm_set_area_memory_type(area_id id,
	phys_addr_t physicalBase, uint32 type);

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

extern "C" void r5_gpu_retire_wake();


static radeon_info* sInfo = NULL;
static r5_gfx9_host sHost;
static r5_gfx9_results sResults;
static bool sIhInstalled = false;
static bool sMsi = false;
static uint32 sIrq = 0;
static sem_id sEopSem = -1;
static uint8* sIhRing = NULL;
static volatile uint32* sIhWriteback = NULL;
static uint32 sIhRptr = 0;
static uint32 sIhEntries = 0;
static uint32 sIhOther = 0;
extern bool r5_gpu_log_error();
static uint32 sEopCount = 0;
static uint32 sLastClient = 0;
static uint32 sLastSource = 0;
static uint32 sLastRing = 0;
static uint8 sFirstRaw[4][32];
static area_id sSystemArea = -1;
static bool sGfx9Started = false;
static bool sGpuDebug = false;
static bool sInvRange = true;
static bool sHubLinux = false;
static bool sVmids = false;

void
r5_gfx9_set_inv_range(int on)
{
	sInvRange = on ? true : false;
}


void
r5_gfx9_set_hub_linux(int on)
{
	sHubLinux = on ? true : false;
}


void
r5_gfx9_set_vmids(int on)
{
	sVmids = on ? true : false;
}


int
r5_gfx9_inv_range_on(void)
{
	return sInvRange ? 1 : 0;
}


int
r5_gfx9_hub_linux_on(void)
{
	return sHubLinux ? 1 : 0;
}


int
r5_gfx9_vmids_on(void)
{
	return sVmids ? 1 : 0;
}


static bool sK2InvHave = false;
static bool sK2HubHave = false;
static bool sVmidBaseHave = false;
static uint32 sK2InvLo[18];
static uint32 sK2InvHi[18];
static uint32 sK2AgpBase = 0;
static uint32 sK2AgpBot = 0;
static uint32 sK2AgpTop = 0;
static uint32 sK2AperLow = 0;
static uint32 sK2AperHigh = 0;
static uint32 sK2IdentityLo = 0;
static uint32 sK2IdentityHi = 0;
static uint32 sK2Fault2 = 0;
static uint32 sVmidCntl[15];
static uint32 sVmidStartLo[15];
static uint32 sVmidStartHi[15];
static uint32 sVmidEndLo[15];
static uint32 sVmidEndHi[15];


/* The values before gfx9_inv_range and gfx9_hub_linux write. The first
 * call wins, so a later program does not replace the restore copy. */
static uint32 gc_byte(uint32 index);
static uint32 kread(void* cookie, uint32 offset);

static void
r5_gfx9_k2_store(void)
{
	uint32 engine;
	if (sInfo == NULL)
		return;
	if (!sK2InvHave) {
		for (engine = 0; engine < r5_gfx9_inv_engines() && engine < 18u;
			engine++) {
			sK2InvLo[engine] = kread(sInfo,
				gc_byte(r5_gfx9_inv_range_lo(engine)));
			sK2InvHi[engine] = kread(sInfo,
				gc_byte(r5_gfx9_inv_range_hi(engine)));
		}
		sK2InvHave = true;
	}
	if (!sK2HubHave) {
		sK2AgpBase = kread(sInfo, gc_byte(mmMC_VM_AGP_BASE));
		sK2AgpBot = kread(sInfo, gc_byte(mmMC_VM_AGP_BOT));
		sK2AgpTop = kread(sInfo, gc_byte(mmMC_VM_AGP_TOP));
		sK2AperLow = kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR));
		sK2AperHigh = kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR));
		sK2IdentityLo = kread(sInfo, gc_byte(
			mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32));
		sK2IdentityHi = kread(sInfo, gc_byte(
			mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32));
		sK2Fault2 = kread(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL2));
		sK2HubHave = true;
	}
}
static bool sVmCntlSavedValid = false;
static uint32 sVmCntlSaved = 0;
extern pci_module_info* gPCI;

static const uint32 kIhBytes = 64 * 1024;
static const uint32 kOssBase = OSSSYS_BASE__INST0_SEG0;
static const uint32 kGcBase = GC_BASE__INST0_SEG0;
static const uint32 kNbioBase = NBIO_BASE__INST0_SEG2;
static const uint32 kClientCp = 0x14;
static const uint32 kSourceEop = 181;
static r5_gfx9_ib_now sPrivIb1;
static r5_gfx9_ib_now sPrivIb2;
static int32 sPrivPending;
static volatile int32 sPrivCount;


static uint32
gc_byte(uint32 index)
{
	return (kGcBase + index) * 4u;
}


static uint32
oss_byte(uint32 index)
{
	return (kOssBase + index) * 4u;
}


static uint32
nbio_byte(uint32 index)
{
	return (kNbioBase + index) * 4u;
}


static uint32
kread(void* cookie, uint32 offset)
{
	radeon_info* info = (radeon_info*)cookie;
	return *(volatile uint32*)(info->registers + offset);
}


static void
kwrite(void* cookie, uint32 offset, uint32 value)
{
	radeon_info* info = (radeon_info*)cookie;
	*(volatile uint32*)(info->registers + offset) = value;
}


static bool
kfits(void* cookie, uint32 offset)
{
	radeon_info* info = (radeon_info*)cookie;
	area_info area;
	if (info->registers_area < 0)
		return false;
	if (get_area_info(info->registers_area, &area) != B_OK)
		return false;
	return (uint64)offset + sizeof(uint32) <= area.size;
}


static void
ksleep(void* cookie, uint32 micros)
{
	(void)cookie;
	snooze(micros);
}


static void
klog(void* cookie, const char* format, ...)
{
	(void)cookie;
	char line[640];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	dprintf("%s", line);
}


static status_t
kvram(void* cookie, uint64 bytes, uint64 align, uint64* offset, uint8** address)
{
	radeon_info* info = (radeon_info*)cookie;
	radeon_shared_info* shared = info->shared_info;
	if (offset == NULL || bytes == 0 || shared == NULL)
		return B_BAD_VALUE;
	if (align < 4096)
		align = 4096;
	uint64 start = shared->vram_pool_offset;
	uint64 end = start + shared->vram_pool_bytes;
	uint64 cursor = shared->vram_pool_cursor;
	if (cursor < start)
		cursor = start;
	uint64 aligned = (cursor + align - 1) & ~(align - 1);
	if (aligned < cursor || aligned > end || bytes > end - aligned)
		return B_NO_MEMORY;
	if (info->kernel_window == NULL || aligned < shared->vram_pool_offset)
		return B_NO_MEMORY;
	shared->vram_pool_cursor = aligned + bytes;
	*offset = aligned;
	if (address != NULL) {
		*address = info->kernel_window
			+ (aligned - shared->vram_pool_offset);
	}
	return B_OK;
}


static uint8*
kfirmware(void* cookie, const char* name, size_t* bytes)
{
	(void)cookie;
	char path[256];
	snprintf(path, sizeof(path),
		"/boot/system/non-packaged/data/firmware/amdgpu/%s", name);
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	off_t size = lseek(fd, 0, SEEK_END);
	if (size <= 0 || size > 1024 * 1024) {
		close(fd);
		return NULL;
	}
	if (lseek(fd, 0, SEEK_SET) != 0) {
		close(fd);
		return NULL;
	}
	uint8* blob = (uint8*)malloc((size_t)size);
	if (blob == NULL) {
		close(fd);
		return NULL;
	}
	size_t filled = 0;
	while (filled < (size_t)size) {
		ssize_t got = read(fd, blob + filled, (size_t)size - filled);
		if (got <= 0) {
			free(blob);
			close(fd);
			return NULL;
		}
		filled += (size_t)got;
	}
	close(fd);
	*bytes = filled;
	return blob;
}


static void
kfree_fw(void* cookie, uint8* blob)
{
	(void)cookie;
	free(blob);
}


static void
knote(const char* format, ...)
{
	char line[240];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	r5_gfx9_note(line);
}


static void
publish()
{
	if (sInfo == NULL || sInfo->shared_info == NULL)
		return;
	snprintf(sInfo->shared_info->gfx9_text, sizeof(sInfo->shared_info->gfx9_text),
		"%s", sResults.text);
	sInfo->shared_info->gfx9_text_used = sResults.used;
}


static int32
ih_handler(void* data)
{
	(void)data;
	if (sIhRing == NULL || sIhWriteback == NULL || sInfo == NULL)
		return B_UNHANDLED_INTERRUPT;
	uint32 wptr = *sIhWriteback & 0x0003fffc;
	uint32 rptr = sIhRptr;
	if (wptr == rptr)
		return B_UNHANDLED_INTERRUPT;
	bool woke = false;
	uint32 walked = 0;
	while (rptr != wptr && walked < (kIhBytes / 32)) {
		r5_gfx9_ih_entry entry = r5_gfx9_ih_parse(sIhRing + rptr);
		if (sIhEntries < 4)
			memcpy(sFirstRaw[sIhEntries], sIhRing + rptr, 32);
		sIhEntries++;
		sLastClient = entry.client;
		sLastSource = entry.source;
		sLastRing = entry.ring;
		if (entry.client == kClientCp && entry.source == kSourceEop
			&& entry.ring == 0) {
			if (sEopSem >= 0)
				release_sem_etc(sEopSem, 1, B_DO_NOT_RESCHEDULE);
			r5_gpu_retire_wake();
			sEopCount++;
			woke = true;
		} else {
			if (entry.client == kClientCp && (entry.source == 184
				|| entry.source == 185 || entry.source == 186))
				sPrivCount++;
			if (r5_gpu_log_error()) {
				const uint32* raw = (const uint32*)(sIhRing + rptr);
				uint32 vmid = (raw[0] >> 24) & 0xfu;
				sIhOther++;
				dprintf("radeon_hd: ih client %lu src %lu ring %lu vmid %lu "
					"%08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
					(unsigned long)entry.client, (unsigned long)entry.source,
					(unsigned long)entry.ring, (unsigned long)vmid,
					(unsigned long)raw[0], (unsigned long)raw[1],
					(unsigned long)raw[2], (unsigned long)raw[3],
					(unsigned long)raw[4], (unsigned long)raw[5],
					(unsigned long)raw[6], (unsigned long)raw[7]);
				/* Registers only. The dword dump runs on the retire thread,
				 * from this snapshot, so the handler does not map a page. */
				if (entry.client == kClientCp && sPrivPending == 0) {
					r5_gfx9_ib_regs(&sPrivIb1, &sPrivIb2);
					sPrivPending = 1;
					if (r5_gpu_log_error())
					dprintf("radeon_hd: ih ib1 %016llx sz %08lx off %08lx "
						"ib2 %016llx sz %08lx off %08lx\n",
						(unsigned long long)sPrivIb1.base,
						(unsigned long)sPrivIb1.size,
						(unsigned long)sPrivIb1.offset,
						(unsigned long long)sPrivIb2.base,
						(unsigned long)sPrivIb2.size,
						(unsigned long)sPrivIb2.offset);
					r5_gpu_retire_wake();
				}
			}
		}
		rptr = (rptr + 32) & (kIhBytes - 1);
		walked++;
	}
	sIhRptr = rptr;
	*(volatile uint32*)(sInfo->registers + oss_byte(mmIH_RB_RPTR)) = rptr;
	return woke ? B_INVOKE_SCHEDULER : B_HANDLED_INTERRUPT;
}


static void
ih_off()
{
	if (sInfo == NULL)
		return;
	*(volatile uint32*)(sInfo->registers + oss_byte(mmIH_RB_CNTL)) = 0;
	*(volatile uint32*)(sInfo->registers + gc_byte(mmCP_INT_CNTL_RING0)) = 0;
	if (sIhInstalled) {
		remove_io_interrupt_handler(sIrq, ih_handler, sInfo);
		sIhInstalled = false;
	}
}


static bool
map_pool(radeon_info& info)
{
	radeon_shared_info* shared = info.shared_info;
	uint64 bytes = shared->vram_pool_bytes;
	uint64 phys = shared->vram_window_phys + shared->vram_pool_offset;
	if (bytes == 0
		|| (shared->vram_pool_offset & 4095) != 0 || (bytes & 4095) != 0
		|| (shared->vram_window_phys & 4095) != 0
		|| phys < shared->vram_window_phys) {
		dprintf("radeon_hd: gfx9 pool refused\n");
		return false;
	}
	void* address = NULL;
	area_id area = map_physical_memory("radeon hd gfx9 pool",
		(phys_addr_t)phys, (size_t)bytes, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &address);
	if (area < B_OK || address == NULL) {
		dprintf("radeon_hd: gfx9 pool map failed\n");
		return false;
	}
	if (vm_set_area_memory_type(area, (phys_addr_t)phys,
		B_WRITE_COMBINING_MEMORY) != B_OK) {
		delete_area(area);
		dprintf("radeon_hd: gfx9 pool type failed\n");
		return false;
	}
	info.kernel_window_area = area;
	info.kernel_window = (uint8*)address;
	dprintf("radeon_hd: gfx9 pool 0x%" B_PRIx64 "+0x%" B_PRIx64 "\n",
		phys, bytes);
	return true;
}


/* Linux 6.12 amdgpu_ttm_init maps aper_base with ioremap_wc. On an APU
 * aper_base is get_mc_fb_offset(), not the PCI BAR. */
static bool
map_carveout(radeon_info& info)
{
	uint32 fb = kread(&info, gc_byte(mmMC_VM_FB_OFFSET));
	uint64 base = r5_gpu_carveout_base(fb);
	uint64 bytes = info.shared_info->vram_window_bytes;
	if (base == 0 || bytes == 0 || (bytes & 4095) != 0 || (base & 4095) != 0) {
		dprintf("radeon_hd: carveout refused\n");
		return false;
	}
	void* address = NULL;
	area_id area = map_physical_memory("radeon hd carveout",
		(phys_addr_t)base, (size_t)bytes, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &address);
	if (area < B_OK || address == NULL) {
		dprintf("radeon_hd: carveout map failed\n");
		return false;
	}
	if (vm_set_area_memory_type(area, (phys_addr_t)base,
		B_WRITE_COMBINING_MEMORY) != B_OK) {
		delete_area(area);
		dprintf("radeon_hd: carveout type failed\n");
		return false;
	}
	info.carveout_area = area;
	info.carveout = (uint8*)address;
	info.carveout_base = base;
	info.carveout_bytes = bytes;
	dprintf("radeon_hd: carveout 0x%" B_PRIx64 "+0x%" B_PRIx64 "\n",
		base, bytes);
	return true;
}


uint8*
r5_gfx9_vram_bar(uint64 offset)
{
	if (sInfo == NULL || sInfo->kernel_window == NULL
		|| sInfo->shared_info == NULL)
		return NULL;
	uint64 pool = sInfo->shared_info->vram_pool_offset;
	uint64 bytes = sInfo->shared_info->vram_pool_bytes;
	if (offset < pool || offset - pool >= bytes)
		return NULL;
	return sInfo->kernel_window + (offset - pool);
}


uint8*
r5_gfx9_vram_cpu(uint64 offset)
{
	if (sInfo == NULL || sInfo->carveout == NULL || offset >= sInfo->carveout_bytes)
		return NULL;
	return sInfo->carveout + offset;
}


area_id
r5_gfx9_map_vram(uint64 offset, uint64 bytes, void** address)
{
	if (address == NULL || sInfo == NULL || sInfo->carveout == NULL || bytes == 0)
		return B_BAD_VALUE;
	if (offset > sInfo->carveout_bytes
		|| bytes > sInfo->carveout_bytes - offset)
		return B_BAD_VALUE;
	uint64 phys = sInfo->carveout_base + offset;
	area_id area = map_physical_memory("r5 gpu vram", (phys_addr_t)phys,
		(size_t)bytes, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, address);
	if (area < B_OK || *address == NULL)
		return area < B_OK ? area : B_ERROR;
	if (vm_set_area_memory_type(area, (phys_addr_t)phys,
		B_WRITE_COMBINING_MEMORY) != B_OK) {
		delete_area(area);
		*address = NULL;
		return B_ERROR;
	}
	return area;
}


void
r5_gfx9_kernel_stop()
{
	if (sInfo != NULL && sInfo->registers_area >= 0 && sInfo->registers != 0) {
		uint32 me = gc_byte(mmCP_ME_CNTL);
		uint32 mec = gc_byte(mmCP_MEC_CNTL);
		kwrite(sInfo, me, kread(sInfo, me)
			| (1u << 24) | (1u << 26) | (1u << 28));
		kwrite(sInfo, mec, kread(sInfo, mec) | (1u << 30) | (1u << 28));
		if (sVmCntlSavedValid)
			kwrite(sInfo, gc_byte(mmVM_CONTEXT0_CNTL), sVmCntlSaved);
		const uint32 eng = 17;
		kwrite(sInfo, gc_byte(mmVM_INVALIDATE_ENG0_REQ + eng), r5_gfx9_tlb_req(0, 0));
		for (int i = 0; i < 1000; i++) {
			if ((kread(sInfo, gc_byte(mmVM_INVALIDATE_ENG0_ACK + eng)) & 1u) != 0)
				break;
			snooze(10);
		}
		*(volatile uint32*)(sInfo->registers + oss_byte(mmIH_RB_CNTL)) = 0;
		*(volatile uint32*)(sInfo->registers + gc_byte(mmCP_INT_CNTL_RING0)) = 0;
		if (sIhInstalled) {
			remove_io_interrupt_handler(sIrq, ih_handler, sInfo);
			sIhInstalled = false;
		}
	}
	if (sMsi && gPCI != NULL && sInfo != NULL && sInfo->pci != NULL) {
		gPCI->disable_msi(sInfo->pci->bus, sInfo->pci->device,
			sInfo->pci->function);
		gPCI->unconfigure_msi(sInfo->pci->bus, sInfo->pci->device,
			sInfo->pci->function);
		sMsi = false;
	}
	if (sEopSem >= 0) {
		delete_sem(sEopSem);
		sEopSem = -1;
	}
	if (sSystemArea >= 0) {
		delete_area(sSystemArea);
		sSystemArea = -1;
	}
	if (sInfo != NULL && sInfo->kernel_window_area >= 0) {
		delete_area(sInfo->kernel_window_area);
		sInfo->kernel_window_area = -1;
		sInfo->kernel_window = NULL;
	}
	if (sInfo != NULL && sInfo->carveout_area >= 0) {
		delete_area(sInfo->carveout_area);
		sInfo->carveout_area = -1;
		sInfo->carveout = NULL;
		sInfo->carveout_base = 0;
		sInfo->carveout_bytes = 0;
	}
}


static bool
ih_on()
{
	uint64 ringOffset = 0;
	uint8* ring = NULL;
	if (sHost.vram_alloc(sInfo, kIhBytes + 4, 4096, &ringOffset, &ring) != B_OK
		|| ring == NULL)
		return false;
	memset(ring, 0, kIhBytes + 4);
	sIhRing = ring;
	sIhWriteback = (volatile uint32*)(ring + kIhBytes);
	sIhRptr = 0;
	uint32 location = kread(sInfo, gc_byte(mmMC_VM_FB_LOCATION_BASE));
	uint64 ringMc = r5_gfx9_fb_start(location) + ringOffset;
	uint64 wbMc = ringMc + kIhBytes;

	uint64 dummyOffset = 0;
	uint8* dummy = NULL;
	if (sHost.vram_alloc(sInfo, 4096, 4096, &dummyOffset, &dummy) != B_OK)
		return false;
	memset(dummy, 0, 4096);
	uint32 fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
	uint64 dummyPa = r5_gfx9_fb_start(fb) + dummyOffset;

	kwrite(sInfo, oss_byte(mmIH_RB_CNTL), 0);
	kwrite(sInfo, oss_byte(mmIH_RB_CNTL_RING1), 0);
	kwrite(sInfo, oss_byte(mmIH_RB_CNTL_RING2), 0);
	kwrite(sInfo, nbio_byte(mmINTERRUPT_CNTL2), (uint32)(dummyPa >> 8));
	uint32 interrupt = kread(sInfo, nbio_byte(mmINTERRUPT_CNTL));
	interrupt &= ~(1u << 0);
	interrupt &= ~(1u << 3);
	kwrite(sInfo, nbio_byte(mmINTERRUPT_CNTL), interrupt);

	kwrite(sInfo, oss_byte(mmIH_RB_BASE), (uint32)(ringMc >> 8));
	kwrite(sInfo, oss_byte(mmIH_RB_BASE_HI), (uint32)((ringMc >> 40) & 0xffu));
	kwrite(sInfo, oss_byte(mmIH_RB_WPTR_ADDR_LO), (uint32)wbMc);
	kwrite(sInfo, oss_byte(mmIH_RB_WPTR_ADDR_HI), (uint32)(wbMc >> 32) & 0xffffu);
	kwrite(sInfo, oss_byte(mmIH_RB_WPTR), 0);
	kwrite(sInfo, oss_byte(mmIH_RB_RPTR), 0);
	kwrite(sInfo, oss_byte(mmIH_DOORBELL_RPTR), 0);

	uint32 cntl = kread(sInfo, oss_byte(mmIH_RB_CNTL));
	cntl &= ~0x3eu;
	cntl |= 14u << 1;
	cntl |= 1u << 7;
	cntl |= 1u << 8;
	cntl |= 1u << 16;
	cntl |= 1u << 31;
	cntl |= 1u << 20;
	cntl &= ~(1u << 22);
	cntl &= ~(0xfu << 24);
	cntl = (cntl & ~(7u << 28)) | (4u << 28);
	if (sMsi)
		cntl |= 1u << 21;
	else
		cntl &= ~(1u << 21);
	cntl |= 1u << 0;
	cntl |= 1u << 17;
	kwrite(sInfo, oss_byte(mmIH_RB_CNTL), cntl);

	uint32 cp = kread(sInfo, gc_byte(mmCP_INT_CNTL_RING0));
	/* gc_9_0_sh_mask.h CP_INT_CNTL_RING0. TIME_STAMP is the EOP
	 * (shift 0x1a). PRIV_INSTR 0x16, PRIV_REG 0x17 and OPCODE_ERROR
	 * 0x18 match gfx_v9_0_set_priv_inst_fault_state and
	 * gfx_v9_0_set_priv_reg_fault_state. Source ids 184 and 185 are
	 * CP_PRIV_REG_FAULT and CP_PRIV_INSTR_FAULT. */
	cp |= 1u << 0x16;
	cp |= 1u << 0x17;
	cp |= 1u << 0x18;
	cp |= 1u << 0x1a;
	kwrite(sInfo, gc_byte(mmCP_INT_CNTL_RING0), cp);
	return true;
}


static void
run_k1()
{
	sIrq = sInfo->pci->u.h0.interrupt_line;
	sMsi = false;
	if (gPCI != NULL && gPCI->get_msi_count(sInfo->pci->bus,
			sInfo->pci->device, sInfo->pci->function) >= 1) {
		uint32 vector = 0;
		if (gPCI->configure_msi(sInfo->pci->bus, sInfo->pci->device,
				sInfo->pci->function, 1, &vector) == B_OK
			&& gPCI->enable_msi(sInfo->pci->bus, sInfo->pci->device,
				sInfo->pci->function) == B_OK) {
			sIrq = vector;
			sMsi = true;
		}
	}
	sEopSem = create_sem(0, "r5 gfx9 eop");
	if (install_io_interrupt_handler(sIrq, ih_handler, sInfo, 0) != B_OK) {
		knote("k1 fail %s irq %lu install", sMsi ? "msi" : "legacy",
			(unsigned long)sIrq);
		return;
	}
	sIhInstalled = true;
	bigtime_t started = system_time();
	if (!ih_on()) {
		ih_off();
		knote("k1 fail %s irq %lu ih", sMsi ? "msi" : "legacy",
			(unsigned long)sIrq);
		return;
	}
	uint32 sequence = 0;
	bool armed = r5_gfx9_k1_submit(&sequence);
	status_t waited = B_ERROR;
	if (armed && sEopSem >= 0)
		waited = acquire_sem_etc(sEopSem, 1, B_RELATIVE_TIMEOUT, 200000);
	bigtime_t elapsed = system_time() - started;
	if (waited == B_OK && sEopCount > 0) {
		knote("k1 ok %s irq %lu eop %lu client %lu src %lu ring %lu",
			sMsi ? "msi" : "legacy", (unsigned long)sIrq,
			(unsigned long)sEopCount, (unsigned long)sLastClient,
			(unsigned long)sLastSource, (unsigned long)sLastRing);
		knote("k1_wait_us %ld", (long)elapsed);
		return;
	}
	uint32 rptr = kread(sInfo, oss_byte(mmIH_RB_RPTR));
	uint32 wptr = kread(sInfo, oss_byte(mmIH_RB_WPTR));
	uint32 wb = sIhWriteback != NULL ? *sIhWriteback : 0;
	knote("k1 fail %s irq %lu eop %lu client %lu src %lu ring %lu",
		sMsi ? "msi" : "legacy", (unsigned long)sIrq,
		(unsigned long)sEopCount, (unsigned long)sLastClient,
		(unsigned long)sLastSource, (unsigned long)sLastRing);
	knote("k1_wait_us %ld", (long)elapsed);
	knote("k1 rptr %08lx wptr %08lx wb %08lx intcntl %08lx cp %08lx status %08lx",
		(unsigned long)rptr, (unsigned long)wptr, (unsigned long)wb,
		(unsigned long)kread(sInfo, nbio_byte(mmINTERRUPT_CNTL)),
		(unsigned long)kread(sInfo, gc_byte(mmCP_INT_CNTL_RING0)),
		(unsigned long)kread(sInfo, gc_byte(mmCP_INT_STATUS)));
	for (uint32 i = 0; i < 4; i++) {
		const uint32* dwords = (const uint32*)sFirstRaw[i];
		knote("k1 raw%lu %08lx %08lx %08lx %08lx", (unsigned long)i,
			(unsigned long)dwords[0], (unsigned long)dwords[1],
			(unsigned long)dwords[2], (unsigned long)dwords[3]);
	}
	ih_off();
	(void)sequence;
}


static uint64 sK2GartMc = 0;
static uint8* sK2GartCpu = NULL;
static uint64 sK2DummyPa = 0;

/* VM_L2_PROTECTION_FAULT_CNTL. Bits 2–12 are the ENABLE_DEFAULT fields.
 * Bits 30 and 31 are CRASH_ON_NO_RETRY_FAULT and CRASH_ON_RETRY_FAULT.
 * gfxhub_v1_0_set_fault_enable_default turns the defaults on and the
 * crash bits off when amdgpu_vm_fault_stop stays at never. */
static const uint32 kFaultDefaultMask = 0x1ffcu;
static const uint32 kFaultCrashMask = 0xc0000000u;


/* Linux v6.12 gfxhub_v1_0.c:gfxhub_v1_0_program_invalidation.
 * The read is not behind gfx9_inv_range. The print is behind gpu_debug. */
static void
r5_gfx9_inv_range_read(const char* when)
{
	uint32 engine;
	if (!sGpuDebug || sInfo == NULL || when == NULL)
		return;
	for (engine = 0; engine < r5_gfx9_inv_engines(); engine += 17) {
		knote("inv range %s eng %lu lo %08lx hi %08lx", when,
			(unsigned long)engine,
			(unsigned long)kread(sInfo,
				gc_byte(r5_gfx9_inv_range_lo(engine))),
			(unsigned long)kread(sInfo,
				gc_byte(r5_gfx9_inv_range_hi(engine))));
	}
}


static void
r5_gfx9_inv_range_program(void)
{
	uint32 engine;
	if (!sInvRange || sInfo == NULL)
		return;
	for (engine = 0; engine < r5_gfx9_inv_engines(); engine++) {
		kwrite(sInfo, gc_byte(r5_gfx9_inv_range_lo(engine)),
			r5_gfx9_inv_range_lo_value());
		kwrite(sInfo, gc_byte(r5_gfx9_inv_range_hi(engine)),
			r5_gfx9_inv_range_hi_value());
	}
	r5_gfx9_inv_range_read("wrote");
}


void
r5_gfx9_log_state(const char* when, char* line, size_t bytes)
{
	if (line == NULL || bytes == 0)
		return;
	line[0] = 0;
	if (sInfo == NULL || when == NULL)
		return;
	snprintf(line, bytes,
		"radeon_hd: t %lld regs %s "
		"inv0 %08lx %08lx inv17 %08lx %08lx "
		"l1 %08lx agp %08lx %08lx aper %08lx %08lx "
		"inv %d hub %d vmids %d fence %d cond %d gds %d pasid %d ce %d\n",
		(long long)system_time(), when,
		(unsigned long)kread(sInfo, gc_byte(r5_gfx9_inv_range_lo(0))),
		(unsigned long)kread(sInfo, gc_byte(r5_gfx9_inv_range_hi(0))),
		(unsigned long)kread(sInfo, gc_byte(r5_gfx9_inv_range_lo(17))),
		(unsigned long)kread(sInfo, gc_byte(r5_gfx9_inv_range_hi(17))),
		(unsigned long)kread(sInfo, gc_byte(mmMC_VM_MX_L1_TLB_CNTL)),
		(unsigned long)kread(sInfo, gc_byte(mmMC_VM_AGP_BOT)),
		(unsigned long)kread(sInfo, gc_byte(mmMC_VM_AGP_TOP)),
		(unsigned long)kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR)),
		(unsigned long)kread(sInfo,
			gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR)),
		r5_gfx9_inv_range_on(), r5_gfx9_hub_linux_on(), r5_gfx9_vmids_on(),
		r5_gfx9_flush_fence_on(), r5_gfx9_cond_exec_on(),
		r5_gfx9_gds_switch_on(), r5_gfx9_pasid_map_on(),
		r5_gfx9_ce_meta_on());
	dprintf("%s", line);
}


static void
r5_gfx9_inv_range_restore(void)
{
	uint32 engine;
	if (!sK2InvHave || sInfo == NULL)
		return;
	for (engine = 0; engine < r5_gfx9_inv_engines() && engine < 18u;
		engine++) {
		kwrite(sInfo, gc_byte(r5_gfx9_inv_range_lo(engine)),
			sK2InvLo[engine]);
		kwrite(sInfo, gc_byte(r5_gfx9_inv_range_hi(engine)),
			sK2InvHi[engine]);
	}
}


/* Linux v6.12 gfxhub_v1_0.c gart_enable, the registers R5 does not
 * already write. VMID 0 stays the K2 contract: this only prints it. */
static void
r5_gfx9_hub_linux(void)
{
	if (sInfo == NULL)
		return;
	r5_gfx9_k2_store();
	if (sGpuDebug) {
		knote("hub l1 %08lx l2 %08lx %08lx %08lx %08lx",
			(unsigned long)kread(sInfo, gc_byte(mmMC_VM_MX_L1_TLB_CNTL)),
			(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CNTL)),
			(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CNTL2)),
			(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CNTL3)),
			(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CNTL4)));
		knote("hub aper %08lx %08lx agp %08lx %08lx %08lx",
			(unsigned long)kread(sInfo,
				gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR)),
			(unsigned long)kread(sInfo,
				gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR)),
			(unsigned long)kread(sInfo, gc_byte(mmMC_VM_AGP_BASE)),
			(unsigned long)kread(sInfo, gc_byte(mmMC_VM_AGP_BOT)),
			(unsigned long)kread(sInfo, gc_byte(mmMC_VM_AGP_TOP)));
		knote("hub identity %08lx %08lx fault2 %08lx vmid0 %08lx",
			(unsigned long)kread(sInfo, gc_byte(
				mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32)),
			(unsigned long)kread(sInfo, gc_byte(
				mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32)),
			(unsigned long)kread(sInfo,
				gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL2)),
			(unsigned long)kread(sInfo, gc_byte(mmVM_CONTEXT0_CNTL)));
	}
}


void
r5_gfx9_hub_program(uint64 fbStart, uint64 fbEnd, uint32 lowNow, uint32 highNow)
{
	struct r5_agp_aperture agp;
	uint32 fault;
	r5_gfx9_k2_store();
	if (!sHubLinux || sInfo == NULL)
		return;
	agp = r5_gfx9_agp_aperture(fbStart, fbEnd, sInfo->aperturePlus);
	knote("hub linux low now %08lx linux %08lx high now %08lx linux %08lx "
		"bot %08lx top %08lx",
		(unsigned long)lowNow, (unsigned long)agp.low,
		(unsigned long)highNow, (unsigned long)agp.high,
		(unsigned long)agp.bot, (unsigned long)agp.top);
	/* v6.18 gfxhub_v1_0_init_system_aperture_regs. AGP_BASE is 0.
	 * BOT and TOP are set_agp_default, not agp_location. */
	kwrite(sInfo, gc_byte(mmMC_VM_AGP_BASE), 0);
	kwrite(sInfo, gc_byte(mmMC_VM_AGP_BOT), agp.bot);
	kwrite(sInfo, gc_byte(mmMC_VM_AGP_TOP), agp.top);
	kwrite(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR), agp.low);
	kwrite(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR), agp.high);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32), 0);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32), 0);
	fault = kread(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL2));
	fault |= 1u << VM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY__SHIFT;
	kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL2), fault);
}


static void
run_k2()
{
	uint32 location = kread(sInfo, gc_byte(mmMC_VM_FB_LOCATION_BASE));
	uint64 fbStart = r5_gfx9_fb_start(location);
	uint32 aperLo = kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR));
	uint32 aperHi = kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR));
	uint64 aperStart = (uint64)aperLo << 18;
	uint64 aperEnd = (uint64)aperHi << 18;
	uint32 agpBot = kread(sInfo, gc_byte(mmMC_VM_AGP_BOT));
	uint32 agpTop = kread(sInfo, gc_byte(mmMC_VM_AGP_TOP));
	uint64 agpStart = 0;
	uint64 agpEnd = 0;
	int agpOn = r5_gfx9_agp_window(agpBot, agpTop, &agpStart, &agpEnd);
	if (agpOn) {
		knote("k2 agp bot %08lx top %08lx %016llx-%016llx",
			(unsigned long)agpBot, (unsigned long)agpTop,
			(unsigned long long)agpStart, (unsigned long long)agpEnd);
	} else {
		knote("k2 agp bot %08lx top %08lx off",
			(unsigned long)agpBot, (unsigned long)agpTop);
	}
	uint64 gartStart = 0;
	uint64 gartEnd = 0;
	if (!r5_gfx9_gart_range(fbStart, aperStart, aperEnd, agpOn, agpStart,
			agpEnd, &gartStart, &gartEnd)) {
		knote("k2 fail range");
		return;
	}

	uint64 tableOffset = 0;
	uint8* table = NULL;
	const uint64 tableBytes = 512ull * 1024ull;
	if (sHost.vram_alloc(sInfo, tableBytes, 4096, &tableOffset, &table) != B_OK
		|| table == NULL) {
		knote("k2 fail table");
		return;
	}
	uint64 dummyOffset = 0;
	uint8* dummy = NULL;
	if (sHost.vram_alloc(sInfo, 4096, 4096, &dummyOffset, &dummy) != B_OK) {
		knote("k2 fail dummy");
		return;
	}
	uint32 fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
	uint64 dummyPa = r5_gfx9_fb_start(fb) + dummyOffset;
	sK2DummyPa = dummyPa;
	uint64 flags = R5_GFX9_PTE_VALID | R5_GFX9_PTE_SYSTEM | R5_GFX9_PTE_SNOOPED
		| R5_GFX9_PTE_EXECUTABLE | R5_GFX9_PTE_READABLE | R5_GFX9_PTE_WRITEABLE;
	uint64 dummyPte = r5_gfx9_gart_pte(dummyPa, flags);
	uint64* pte = (uint64*)table;
	uint32 pteCount = (uint32)(tableBytes / sizeof(uint64));
	for (uint32 i = 0; i < pteCount; i++)
		pte[i] = dummyPte;

	void* system = NULL;
	sSystemArea = create_area("r5 gfx9 gart", &system, B_ANY_KERNEL_ADDRESS,
		1024 * 1024, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (sSystemArea < 0 || system == NULL) {
		knote("k2 fail system area");
		return;
	}
	physical_entry entries[256];
	memset(entries, 0, sizeof(entries));
	uint32 count = 256;
	status_t mapped = get_memory_map_etc(B_CURRENT_TEAM, system,
		1024 * 1024, entries, &count);
	if (mapped != B_OK) {
		knote("k2 fail memory map %ld count %lu", (long)mapped,
			(unsigned long)count);
		return;
	}
	uint32 page = 0;
	uint64 lastExpect = 0;
	for (uint32 slot = 0; slot < count; slot++) {
		uint64 address = entries[slot].address;
		uint64 bytes = entries[slot].size;
		while (bytes >= 4096 && page < 256) {
			lastExpect = r5_gfx9_gart_pte(address, flags);
			pte[page] = lastExpect;
			address += 4096;
			bytes -= 4096;
			page++;
		}
	}
	knote("k2 map count %lu pages %lu", (unsigned long)count,
		(unsigned long)page);
	if (page != 256) {
		knote("k2 fail pages %lu", (unsigned long)page);
		return;
	}

	// Same instruction as radeon_visible_barrier. That function is static
	// in the accelerant, so the kernel issues the fence itself.
	asm volatile("sfence" ::: "memory");
	uint64 lastGot = *(volatile uint64*)(pte + 255);
	uint64 tailGot = *(volatile uint64*)(pte + (pteCount - 1));
	knote("k2 pte last %016llx got %016llx tail %016llx",
		(unsigned long long)lastExpect, (unsigned long long)lastGot,
		(unsigned long long)tailGot);
	if (lastGot != lastExpect || tailGot != dummyPte) {
		knote("k2 fail pte readback");
		return;
	}

	uint64 tablePa = r5_gfx9_fb_start(fb) + tableOffset;
	if (!r5_gfx9_pde_aligned(tablePa)) {
		knote("k2 fail table align %016llx", (unsigned long long)tablePa);
		return;
	}
	uint64 tableBase = r5_gfx9_page_table_base(fb, tableOffset);
	uint32 savedCntl = kread(sInfo, gc_byte(mmVM_CONTEXT0_CNTL));
	sVmCntlSaved = savedCntl;
	sVmCntlSavedValid = true;
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32), (uint32)tableBase);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32), (uint32)(tableBase >> 32));
	uint32 baseLo = kread(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32));
	uint32 baseHi = kread(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32));
	knote("k2 ptb %016llx got %08lx %08lx",
		(unsigned long long)tableBase, (unsigned long)baseLo,
		(unsigned long)baseHi);
	if (baseLo != (uint32)tableBase || baseHi != (uint32)(tableBase >> 32)) {
		knote("k2 fail ptb readback");
		return;
	}
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32), (uint32)(gartStart >> 12));
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32), (uint32)(gartStart >> 44));
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32), (uint32)((gartEnd - 1) >> 12));
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32), (uint32)((gartEnd - 1) >> 44));
	uint32 cntl = savedCntl;
	cntl |= 1u << 0;
	cntl &= ~0x6u;
	cntl &= ~(0xfu << 3);
	cntl &= ~(1u << 7);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT0_CNTL), cntl);
	kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32), (uint32)(dummyPa >> 12));
	kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32), (uint32)(dummyPa >> 44));

	const uint32 eng = 17;
	uint32 req = gc_byte(mmVM_INVALIDATE_ENG0_REQ + eng);
	uint32 ack = gc_byte(mmVM_INVALIDATE_ENG0_ACK + eng);
	uint32 inv = r5_gfx9_tlb_req(0, 0);
	knote("k2 inv %08lx", (unsigned long)inv);
	kwrite(sInfo, req, inv);
	(void)kread(sInfo, req);
	bool flushed = false;
	for (int i = 0; i < 10000; i++) {
		if ((kread(sInfo, ack) & 1u) != 0) {
			flushed = true;
			break;
		}
		snooze(10);
	}
	if (!flushed) {
		knote("k2 fail tlb");
		return;
	}

	const uint32 dwordCount = (1024 * 1024) / 4;
	uint32* words = (uint32*)system;
	for (uint32 i = 0; i < dwordCount; i++)
		words[i] = 0xCAFEDEAD;
	uint32 sequence = 0;
	bool dma = r5_gfx9_k2_dma(gartStart, &sequence);
	uint32 patternCount = 0;
	uint32 cafe = 0;
	uint32 other = 0;
	uint32 otherIndex = 0;
	uint32 otherValue = 0;
	uint32 first[4] = { 0, 0, 0, 0 };
	for (uint32 i = 0; i < dwordCount; i++) {
		uint32 word = words[i];
		if (i < 4)
			first[i] = word;
		if (word == kK2Pattern)
			patternCount++;
		else if (word == 0xCAFEDEADu)
			cafe++;
		else {
			if (other == 0) {
				otherIndex = i;
				otherValue = word;
			}
			other++;
		}
	}
	bool dmaOk = dma && patternCount == dwordCount;
	uint32 ibSequence = 0;
	bool ib = false;
	if (dmaOk)
		ib = r5_gfx9_k2_gart_ib(gartStart, (uint8*)system, &ibSequence);
	uint32 seenWord = r5_gfx9_last_word();
	uint64 seenSeq = r5_gfx9_last_sequence();
	knote("k2 %s gart %016llx-%016llx pte0 %016llx",
		(dmaOk && ib) ? "ok" : "fail",
		(unsigned long long)gartStart, (unsigned long long)gartEnd,
		(unsigned long long)pte[0]);
	knote("k2 dma %08lx %08lx %08lx %08lx",
		(unsigned long)first[0], (unsigned long)first[1],
		(unsigned long)first[2], (unsigned long)first[3]);
	knote("k2 dma pattern %lu cafe %lu other %lu",
		(unsigned long)patternCount, (unsigned long)cafe,
		(unsigned long)other);
	if (other != 0) {
		knote("k2 dma other %lu %08lx", (unsigned long)otherIndex,
			(unsigned long)otherValue);
	}
	knote("k2_ib wb %08lx seq %lu seen %lu", (unsigned long)seenWord,
		(unsigned long)ibSequence, (unsigned long)seenSeq);
	if (dmaOk && ib) {
		sK2GartMc = gartStart;
		sK2GartCpu = (uint8*)system;
		r5_gfx9_cp_dump("k2 baseline", 0);
	}
	if (!(dmaOk && ib)) {
		uint32 faultStatus = kread(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_STATUS));
		uint32 faultLo = kread(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_ADDR_LO32));
		uint32 faultHi = kread(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_ADDR_HI32));
		r5_gfx9_fault fault = r5_gfx9_fault_decode(faultStatus);
		knote("k2 fault %08lx MORE_FAULTS %lu WALKER_ERROR %lu "
			"PERMISSION_FAULTS %lu MAPPING_ERROR %lu CID %lu RW %lu "
			"addr %016llx cntl %08lx cp %08lx grbm %08lx pasid %lu file %016llx",
			(unsigned long)faultStatus, (unsigned long)fault.more,
			(unsigned long)fault.walker, (unsigned long)fault.permission,
			(unsigned long)fault.mapping, (unsigned long)fault.cid,
			(unsigned long)fault.rw,
			(unsigned long long)r5_gfx9_fault_address(faultLo, faultHi),
			(unsigned long)kread(sInfo, gc_byte(mmVM_CONTEXT0_CNTL)),
			(unsigned long)kread(sInfo, gc_byte(mmCP_STAT)),
			(unsigned long)kread(sInfo, gc_byte(mmGRBM_STATUS)),
			(unsigned long)r5_gfx9_pasid_for_vmid(fault.cid),
			(unsigned long long)r5_gfx9_file_for_vmid(fault.cid));
		uint32 me = gc_byte(mmCP_ME_CNTL);
		uint32 saved = kread(sInfo, me);
		kwrite(sInfo, me, saved | ((1u << 24) | (1u << 26) | (1u << 28)));
		kwrite(sInfo, gc_byte(mmVM_CONTEXT0_CNTL), savedCntl);
	}
	(void)sequence;
	(void)flushed;
	/* The invalidation ranges are copied here, before
	 * r5_gfx9_vmid1_setup calls r5_gfx9_inv_range_program. kernel_start
	 * runs this first. A later copy does not replace it. */
	r5_gfx9_k2_store();
	r5_gfx9_inv_range_read("k2");
}


bool
r5_gfx9_k2_gart_page(uint64* mc, uint8** cpu)
{
	if (mc == NULL || cpu == NULL || sK2GartMc == 0 || sK2GartCpu == NULL)
		return false;
	*mc = sK2GartMc;
	*cpu = sK2GartCpu;
	return true;
}


static r5_vm sVm;
static r5_vm_page sVmPages[1024];
static bool sVmid1 = false;


static int
vm_page_alloc(void* cookie, uint64_t* physical, uint64_t** cpu)
{
	(void)cookie;
	uint64 offset = 0;
	uint8* address = NULL;
	if (sHost.vram_alloc == NULL || sInfo == NULL)
		return 0;
	if (sInfo->carveout == NULL)
		return 0;
	if (sHost.vram_alloc(sInfo, 4096, 4096, &offset, &address) != B_OK
		|| address == NULL)
		return 0;
	(void)address;
	uint8* cpuPage = r5_gfx9_vram_cpu(offset);
	if (cpuPage == NULL)
		return 0;
	uint32 fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
	*physical = r5_gpu_carveout_base(fb) + offset;
	*cpu = (uint64_t*)cpuPage;
	return 1;
}


int32
r5_gfx9_eop_sem()
{
	return sEopSem;
}


status_t
r5_gfx9_alloc_vram(uint64 bytes, uint64 align, uint64* offset, uint8** address)
{
	if (sHost.vram_alloc == NULL || sInfo == NULL)
		return B_NO_INIT;
	return sHost.vram_alloc(sInfo, bytes, align, offset, address);
}


static r5_span sUserSpans[64];


status_t
r5_gfx9_alloc_user_vram(uint64 bytes, uint64* offset)
{
	if (offset == NULL || bytes == 0)
		return B_BAD_VALUE;
	bytes = (bytes + 4095ull) & ~4095ull;
	if (r5_span_take(sUserSpans, 64, bytes, offset))
		return B_OK;
	uint8* address = NULL;
	uint64 bumped = 0;
	status_t status = r5_gfx9_alloc_vram(bytes, 4096, &bumped, &address);
	if (status != B_OK)
		return status;
	if (!r5_span_add(sUserSpans, 64, bumped, bytes))
		return B_NO_MEMORY;
	*offset = bumped;
	return B_OK;
}


void
r5_gfx9_free_user_vram(uint64 offset)
{
	r5_span_free(sUserSpans, 64, offset);
}


bool
r5_gfx9_vmid1_flush()
{
	if (sInfo == NULL) {
		knote("vmid1 fail info");
		return false;
	}
	/* Linux gmc_v9_0_flush_gpu_tlb uses engine 17 for an MMIO flush.
	 * Engine 0 belongs to the GFX ring. The K sequence already uses 17. */
	const uint32 eng = 17;
	uint32 inv = r5_gfx9_tlb_req(1, 0);
	kwrite(sInfo, gc_byte(mmVM_INVALIDATE_ENG0_REQ + eng), inv);
	(void)kread(sInfo, gc_byte(mmVM_INVALIDATE_ENG0_REQ + eng));
	const uint32 ackBit = r5_gfx9_tlb_ack_mask(1);
	for (int i = 0; i < 10000; i++) {
		if ((kread(sInfo, gc_byte(mmVM_INVALIDATE_ENG0_ACK + eng)) & ackBit) != 0)
			return true;
		snooze(10);
	}
	knote("vmid1 fail tlb");
	return false;
}


/* Linux v6.12 gfxhub_v1_0.c:gfxhub_v1_0_setup_vmid_config.
 * i runs 0..14, which is context i + 1. The page-table base is not
 * written here. */
static void
r5_gfx9_vmid_baseline(void)
{
	uint32 i;
	if (sVmidBaseHave || sInfo == NULL)
		return;
	for (i = 0; i <= 14u; i++) {
		sVmidCntl[i] = kread(sInfo, gc_byte(mmVM_CONTEXT1_CNTL
			+ i * R5_GC9_CTX_DISTANCE));
		sVmidStartLo[i] = kread(sInfo, gc_byte(
			mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32
			+ i * R5_GC9_CTX_ADDR_DISTANCE));
		sVmidStartHi[i] = kread(sInfo, gc_byte(
			mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32
			+ i * R5_GC9_CTX_ADDR_DISTANCE));
		sVmidEndLo[i] = kread(sInfo, gc_byte(
			mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32
			+ i * R5_GC9_CTX_ADDR_DISTANCE));
		sVmidEndHi[i] = kread(sInfo, gc_byte(
			mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32
			+ i * R5_GC9_CTX_ADDR_DISTANCE));
	}
	sVmidBaseHave = true;
}


static void
r5_gfx9_vmid_restore(void)
{
	uint32 i;
	if (!sVmidBaseHave || sInfo == NULL)
		return;
	for (i = 0; i <= 14u; i++) {
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_CNTL
			+ i * R5_GC9_CTX_DISTANCE), sVmidCntl[i]);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), sVmidStartLo[i]);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), sVmidStartHi[i]);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), sVmidEndLo[i]);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), sVmidEndHi[i]);
	}
}


static void
r5_gfx9_hub_restore(void)
{
	if (!sK2HubHave || sInfo == NULL)
		return;
	kwrite(sInfo, gc_byte(mmMC_VM_AGP_BASE), sK2AgpBase);
	kwrite(sInfo, gc_byte(mmMC_VM_AGP_BOT), sK2AgpBot);
	kwrite(sInfo, gc_byte(mmMC_VM_AGP_TOP), sK2AgpTop);
	kwrite(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR), sK2AperLow);
	kwrite(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR), sK2AperHigh);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32),
		sK2IdentityLo);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32),
		sK2IdentityHi);
	kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL2), sK2Fault2);
}


static void r5_gfx9_vmids_program(void);
static void r5_gfx9_csa_map(void);

/* inv, hub and vmids are -1 to leave the flag, 0 to restore the stored
 * registers, or 1 to program them. The VMID copy is taken at the first
 * program, after VMID 1's own setup write, so restoring it does not
 * turn that context off. */
void
r5_gfx9_switch_registers(int inv, int hub, int vmids)
{
	if (inv == 1) {
		r5_gfx9_k2_store();
		sInvRange = true;
		r5_gfx9_inv_range_program();
	} else if (inv == 0) {
		sInvRange = false;
		r5_gfx9_inv_range_restore();
	}
	if (hub == 1) {
		uint32 location = 0;
		uint64 fbStart = 0;
		uint64 fbEnd = 0;
		sHubLinux = true;
		r5_gfx9_set_hub_aperture(1);
		if (sInfo != NULL && sInfo->shared_info != NULL) {
			location = kread(sInfo, gc_byte(mmMC_VM_FB_LOCATION_BASE));
			r5_gfx9_fb_range(location,
				(uint64)sInfo->shared_info->frame_buffer_size * 1024ull,
				&fbStart, &fbEnd);
			r5_gfx9_hub_program(fbStart, fbEnd,
				kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_LOW_ADDR)),
				kread(sInfo, gc_byte(mmMC_VM_SYSTEM_APERTURE_HIGH_ADDR)));
		}
	} else if (hub == 0) {
		sHubLinux = false;
		r5_gfx9_set_hub_aperture(0);
		r5_gfx9_hub_restore();
	}
	if (vmids == 1) {
		sVmids = true;
		if (sVmid1)
			r5_gfx9_vmids_program();
	} else if (vmids == 0) {
		sVmids = false;
		r5_gfx9_vmid_restore();
	}
}


static void
r5_gfx9_vmids_program(void)
{
	struct r5_vm_size size;
	uint32 cntl;
	uint32 i;
	uint64 end;
	r5_gfx9_vmid_baseline();
	if (!sVmids || sInfo == NULL)
		return;
	size = r5_gfx9_vm_size();
	cntl = r5_gfx9_vmid_cntl(&size, 0);
	end = size.max_pfn - 1ull;
	for (i = 0; i <= 14u; i++) {
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_CNTL
			+ i * R5_GC9_CTX_DISTANCE), cntl);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), 0);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), 0);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), (uint32)end);
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32
			+ i * R5_GC9_CTX_ADDR_DISTANCE), (uint32)(end >> 32));
	}
	if (sGpuDebug)
		knote("vmids 1-15 cntl %08lx end %016llx",
			(unsigned long)cntl, (unsigned long long)end);
}


bool
r5_gfx9_vmid1_setup()
{
	/* One shared VMID 1 for every client. A later context reuses the
	 * tables. Closing the last client does not rebuild them. */
	if (sVmid1)
		return true;
	if (!r5_gfx9_ring_ready() || sInfo == NULL) {
		knote("vmid1 fail ring");
		return false;
	}
	if (sK2DummyPa == 0) {
		uint64 offset = 0;
		uint8* dummy = NULL;
		if (sHost.vram_alloc == NULL
			|| sHost.vram_alloc(sInfo, 4096, 4096, &offset, &dummy) != B_OK
			|| dummy == NULL) {
			knote("vmid1 fail default");
			return false;
		}
		memset(dummy, 0, 4096);
		uint32 fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
		sK2DummyPa = r5_gfx9_fb_start(fb) + offset;
	}
	/* The root page is kept after a failed flush. The next attempt
	 * rewrites the base from that same page. There is no free for
	 * vm_page_alloc, and the page stays valid.
	 */
	if (sVm.used == 0) {
		memset(&sVm, 0, sizeof(sVm));
		sVm.pages = sVmPages;
		sVm.capacity = 1024;
		sVm.alloc = vm_page_alloc;
		if (!r5_vm_new_page(&sVm, 0)) {
			knote("vmid1 fail page");
			memset(&sVm, 0, sizeof(sVm));
			return false;
		}
		asm volatile("sfence" ::: "memory");
		if (!r5_gfx9_pde_aligned(sVm.pages[0].physical)) {
			knote("vmid1 fail align");
			memset(&sVm, 0, sizeof(sVm));
			return false;
		}
	}
	uint64 rootPa = sVm.pages[0].physical;
	uint64 tableBase = rootPa | (uint64)R5_GFX9_PTE_VALID;
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32), 0xffffffffu);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32), 0xfu);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32), 0);
	kwrite(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32), 0);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32), (uint32)tableBase);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_HI32), (uint32)(tableBase >> 32));
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32), 0);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32), 0);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32), 0xffffffffu);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32), 0xfu);
	/* A faulting VMID 1 access lands on the K2 dummy page, not at
	 * physical 0. The page number is the MC address >> 12.
	 */
	kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32), (uint32)(sK2DummyPa >> 12));
	kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32), (uint32)(sK2DummyPa >> 44));
	uint32 faultCntl = kread(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL));
	uint32 faultNext = (faultCntl | kFaultDefaultMask) & ~kFaultCrashMask;
	if (faultNext != faultCntl)
		kwrite(sInfo, gc_byte(mmVM_L2_PROTECTION_FAULT_CNTL), faultNext);
	/* RETRY_PERMISSION_OR_INVALID_PAGE_FAULT is bit 7. Linux sets it
	 * (0x007ffe87). R5 has no retry-fault handler, so the bit stays off
	 * and the value stays 0x007ffe07. */
	uint32 cntl = 1u | (3u << 1);
	cntl |= (1u << 9) | (1u << 10);
	cntl |= (1u << 11) | (1u << 12);
	cntl |= (1u << 13) | (1u << 14);
	cntl |= (1u << 15) | (1u << 16);
	cntl |= (1u << 17) | (1u << 18);
	cntl |= (1u << 19) | (1u << 20);
	cntl |= (1u << 21) | (1u << 22);
	kwrite(sInfo, gc_byte(mmVM_CONTEXT1_CNTL), cntl);
	if (!r5_gfx9_vmid1_flush()) {
		/* CNTL is already enabled. Turn the context off so a later
		 * attempt does not run on the half-written state. The pages
		 * above stay for that attempt.
		 */
		kwrite(sInfo, gc_byte(mmVM_CONTEXT1_CNTL), 0);
		return false;
	}
	/* gfx_v9_0_constants_init for VMID 1..15. ALIGNMENT_MODE unaligned
	 * and RETRY_DISABLE match VMID 0 (0x1018). PRIVATE_BASE is
	 * 0x1000000000000000 >> 48 and SHARED_BASE is 0x2000000000000000 >> 48,
	 * so SH_MEM_BASES is 0x20001000. VMID 0 stays the ring contract
	 * (bases 0) and is not written here. Callers hold gpu_lock, the same
	 * lock as the other GRBM_GFX_CNTL users. These are registers only.
	 */
	const uint32 shConfig = r5_gfx9_sh_mem_config();
	const uint32 shBases = r5_gfx9_sh_mem_bases();
	for (uint32 vmid = 1; vmid <= 15; vmid++) {
		kwrite(sInfo, gc_byte(mmGRBM_GFX_CNTL), vmid << 4);
		kwrite(sInfo, gc_byte(mmSH_MEM_CONFIG), shConfig);
		kwrite(sInfo, gc_byte(mmSH_MEM_BASES), shBases);
	}
	kwrite(sInfo, gc_byte(mmGRBM_GFX_CNTL), 1u << 4);
	uint32 shRead = kread(sInfo, gc_byte(mmSH_MEM_CONFIG));
	uint32 baseRead = kread(sInfo, gc_byte(mmSH_MEM_BASES));
	kwrite(sInfo, gc_byte(mmGRBM_GFX_CNTL), 0);
	sVmid1 = true;
	knote("vmid1 sh_mem config %08lx bases %08lx",
		(unsigned long)shRead, (unsigned long)baseRead);
	knote("vmid1 ptb %016llx cntl %08lx pages %lu",
		(unsigned long long)tableBase, (unsigned long)cntl,
		(unsigned long)sVm.used);
	knote("vmid1 default %016llx fault %08lx",
		(unsigned long long)sK2DummyPa, (unsigned long)faultNext);
	knote("vmid1 identity low %08lx %08lx high %08lx %08lx",
		(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32)),
		(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32)),
		(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32)),
		(unsigned long)kread(sInfo, gc_byte(mmVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32)));
	r5_gfx9_vmid1_set_root(tableBase);
	/* Linux programs the engine ranges after setup_vmid_config. */
	r5_gfx9_vmids_program();
	r5_gfx9_inv_range_program();
	r5_gfx9_csa_map();
	return true;
}


/* amdgpu_allocate_static_csa plus amdgpu_map_static_csa. One 128 KiB
 * VRAM buffer, zeroed, mapped at r5_csa_vaddr in this VM. Off leaves
 * the page tables alone. Per-file trees map the same buffer. */
static bool sCsaMapped = false;


static void
r5_gfx9_csa_map(void)
{
	uint64 offset = 0;
	uint8* cpu = NULL;
	uint64 va;
	uint64 flags;
	uint32 fb;
	uint32 page;
	if (!r5_gfx9_ce_meta_on() || sCsaMapped || sInfo == NULL)
		return;
	if (r5_gfx9_alloc_vram(kR5CsaBytes, 4096, &offset, &cpu) != B_OK
		|| cpu == NULL) {
		knote("csa fail alloc");
		return;
	}
	memset(cpu, 0, (size_t)kR5CsaBytes);
	fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
	va = r5_csa_vaddr(r5_gfx9_vm_size().max_pfn);
	flags = (uint64)R5_GFX9_PTE_VALID | (uint64)R5_GFX9_PTE_READABLE
		| (uint64)R5_GFX9_PTE_WRITEABLE | (uint64)R5_GFX9_PTE_EXECUTABLE;
	for (page = 0; page < 32u; page++) {
		uint64 pa = r5_gpu_carveout_base(fb) + offset + (uint64)page * 4096ull;
		if (!r5_gfx9_vmid1_map(va + (uint64)page * 4096ull, pa | flags)) {
			knote("csa fail map");
			return;
		}
	}
	sCsaMapped = true;
	knote("csa %016llx", (unsigned long long)va);
}


bool
r5_gfx9_vmid1_map(uint64 va, uint64 pte)
{
	if (!sVmid1)
		return false;
	if (!r5_vm_map_page(&sVm, va, pte))
		return false;
	asm volatile("sfence" ::: "memory");
	/* A cleared PTE is an unmap. The next user job flushes that. */
	r5_gfx9_vmid1_touch();
	return true;
}


bool
r5_gfx9_vmid1_lookup(uint64 va)
{
	uint64 pte = 0;
	if (!sVmid1)
		return false;
	if (r5_vm_lookup(&sVm, va & ~0xfffull, &pte) == 0)
		return false;
	return (pte & (uint64)R5_GFX9_PTE_VALID) != 0;
}


void
r5_gfx9_vmid1_levels(const char* name, uint64 va)
{
	if (name == NULL || !sVmid1 || sVm.used == 0 || sInfo == NULL
		|| sInfo->shared_info == NULL)
		return;
	uint32 fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
	uint64 base = r5_gpu_carveout_base(fb);
	int index = 0;
	for (int level = 3; level >= 0; level--) {
		uint64 phys = sVm.pages[index].physical;
		uint64 off = phys - base;
		uint64* table = sVm.pages[index].cpu;
		uint64 word = 0;
		bool ok = table != NULL;
		if (ok) {
			uint32 slot = r5_vm_index(va, (uint32)level);
			word = table[slot];
		}
		dprintf("radeon_hd: %s L%d off %016llx word %016llx\n",
			name, level, (unsigned long long)off,
			(unsigned long long)word);
		if (level == 0 || !ok || !r5_vm_is_table(word))
			break;
		index = r5_vm_find(&sVm, word);
		if (index < 0)
			break;
	}
}


bool
r5_gfx9_vmid1_pte(uint64 va, uint64* pte)
{
	if (pte == NULL || !sVmid1)
		return false;
	return r5_vm_lookup(&sVm, va & ~0xfffull, pte) != 0;
}


/* The page comes from the VMID 1 table. A system page is mapped for the
 * copy and unmapped after it. A VRAM page is the pool window. Neither
 * is a user address (SMAP). */
bool
r5_gfx9_vmid1_read(uint64 va, uint32* words, uint32 count)
{
	uint64 pte = 0;
	if (words == NULL || count == 0 || !r5_gfx9_vmid1_pte(va, &pte))
		return false;
	if ((pte & (uint64)R5_GFX9_PTE_VALID) == 0)
		return false;
	uint64 page = pte & ((1ull << 57) - 1ull) & ~0xfffull;
	uint32 offset = (uint32)(va & 0xfffull);
	if (offset >= 4096 || count > 16)
		return false;
	if (offset + count * 4 > 4096)
		count = (4096 - offset) / 4;
	uint8* cpu = NULL;
	area_id mapped = -1;
	if ((pte & (uint64)R5_GFX9_PTE_SYSTEM) != 0) {
		void* address = NULL;
		mapped = map_physical_memory("radeon hd ib", (phys_addr_t)page,
			4096, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA, &address);
		if (mapped < B_OK || address == NULL)
			return false;
		cpu = (uint8*)address;
	} else if (sInfo != NULL && sInfo->carveout != NULL) {
		uint32 fb = kread(sInfo, gc_byte(mmMC_VM_FB_OFFSET));
		uint64 base = r5_gpu_carveout_base(fb);
		if (page < base || r5_gfx9_vram_cpu(page - base) == NULL)
			return false;
		cpu = r5_gfx9_vram_cpu(page - base);
	} else
		return false;
	memcpy(words, cpu + offset, count * 4);
	if (mapped >= 0)
		delete_area(mapped);
	return true;
}


uint32
r5_gfx9_vmid1_copy(uint64 va, uint32* words, uint32 count)
{
	uint32 done = 0;
	if (words == NULL || count == 0)
		return 0;
	if (count > 1024)
		count = 1024;
	while (done < count) {
		uint32 chunk = count - done;
		uint32 room = (4096u - (uint32)(va & 0xfffu)) / 4u;
		if (room == 0)
			break;
		if (chunk > room)
			chunk = room;
		if (chunk > 16)
			chunk = 16;
		if (!r5_gfx9_vmid1_read(va, words + done, chunk))
			break;
		done += chunk;
		va += (uint64)chunk * 4u;
	}
	return done;
}


int32
r5_gfx9_priv_count()
{
	return sPrivCount;
}


bool
r5_gfx9_take_priv(r5_gfx9_ib_now* ib1, r5_gfx9_ib_now* ib2)
{
	if (ib1 == NULL || ib2 == NULL || sPrivPending == 0)
		return false;
	*ib1 = sPrivIb1;
	*ib2 = sPrivIb2;
	sPrivPending = 0;
	return true;
}


void
r5_gfx9_kernel_start(radeon_info& info)
{
	sInfo = &info;
	if (sGfx9Started) {
		knote("GFX9 kernel start already done this boot");
		return;
	}
	info.kernel_window_area = -1;
	info.kernel_window = NULL;
	info.carveout_area = -1;
	info.carveout = NULL;
	info.carveout_base = 0;
	info.carveout_bytes = 0;
	if (info.shared_info == NULL || info.pci == NULL)
		return;
	info.shared_info->gfx9_owner = R5_GFX9_OWNER_NONE;
	info.shared_info->gfx9_text[0] = 0;
	info.shared_info->gfx9_text_used = 0;
	bool ring = false;
	bool workload = false;
	bool compute = false;
	bool kernel = false;
	bool irq = false;
	bool gart = false;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		ring = get_driver_boolean_parameter(settings, "gfx9_ring", false, true);
		workload = get_driver_boolean_parameter(settings, "gfx9_workload",
			false, true);
		compute = get_driver_boolean_parameter(settings, "gfx9_compute",
			false, true);
		kernel = get_driver_boolean_parameter(settings, "gfx9_kernel",
			false, true);
		irq = get_driver_boolean_parameter(settings, "gfx9_irq", false, true);
		gart = get_driver_boolean_parameter(settings, "gfx9_gart", false, true);
		sGpuDebug = get_driver_boolean_parameter(settings, "gpu_debug",
			false, true);
		sInvRange = get_driver_boolean_parameter(settings, "gfx9_inv_range",
			true, true);
		sHubLinux = get_driver_boolean_parameter(settings, "gfx9_hub_linux",
			false, true);
		sVmids = get_driver_boolean_parameter(settings, "gfx9_vmids",
			false, true);
		r5_gfx9_set_ce_meta(get_driver_boolean_parameter(settings,
			"gfx9_ce_meta", false, true) ? 1 : 0);
		unload_driver_settings(settings);
	}
	uint32 pci = info.pciID;
	if (!ring || !kernel)
		return;
	if (pci != 0x15e7 && pci != 0x1636 && pci != 0x1638)
		return;
	sGfx9Started = true;
	if (!map_pool(info))
		return;

	memset(&sHost, 0, sizeof(sHost));
	sHost.read32 = kread;
	sHost.write32 = kwrite;
	sHost.mmio_fits = kfits;
	sHost.sleep_us = ksleep;
	sHost.log = klog;
	sHost.vram_alloc = kvram;
	sHost.read_firmware = kfirmware;
	sHost.free_firmware = kfree_fw;
	sHost.doorbell = info.doorbell;
	sHost.doorbellBytes = info.shared_info->doorbell_bytes;
	sHost.cookie = &info;
	r5_gfx9_set_hub_aperture(sHubLinux ? 1 : 0);
	r5_gfx9_set_aperture_plus(info.aperturePlus);
	r5_gfx9_set_hub_program(r5_gfx9_hub_program);
	uint64 vramBytes = (uint64)info.shared_info->frame_buffer_size * 1024;
	memset(&sResults, 0, sizeof(sResults));
	info.shared_info->gfx9_owner = R5_GFX9_OWNER_KERNEL;
	r5_gfx9_prepare(&sHost, vramBytes);
	bool ok = r5_gfx9_start(&sHost, pci, vramBytes, workload, compute, &sResults);
	if (ok)
		r5_gfx9_hub_linux();
	r5_gfx9_copy_firmware(info.fw_version, info.fw_feature, 32);
	knote("k0 %s fence", ok ? "ok" : "fail");
	if (ok && irq)
		run_k1();
	if (ok && gart)
		run_k2();
	if (ok)
		map_carveout(info);
	publish();
}
