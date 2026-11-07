/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Kernel host for the shared GFX9 start, then the interrupt ring and the
 * GART. The packet bytes stay in gfx9_start.cpp. Register numbers are from
 * Linux v6.12 headers: vega10_ip_offset.h, osssys_4_0, nbio_7_0, gc_9_0.
 */

#include "gfx9_start.h"
#include "gfx9_check.h"
#include "r5_vm.h"
#include "r5_vmid.h"
#include "r5_limit.h"
#include "r5_gpu.h"
#include "r5_amd_device.h"
#include "r5_span.h"
#include "radeon_hd_private.h"

#include <PCI.h>
#include <KernelExport.h>
#include <OS.h>
#include <driver_settings.h>

// The kernel exports this with C linkage. The private vm.h declaration
// pulls in thread types before the mutex type is defined, so it is not
// included here.
extern "C" status_t vm_set_area_memory_type(area_id id,
	phys_addr_t physicalBase, uint32 type);
void r5_gpu_fill_passport(radeon_info* info);

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
static bool sVmCntlSavedValid = false;
static uint32 sVmCntlSaved = 0;
extern pci_module_info* gPCI;

static const uint32 kIhBytes = 64 * 1024;
static const uint32 kOssBase = 0x10A0;
static const uint32 kGcBase = 0x2000;
static const uint32 kNbioBase = 0x0D20;
static const uint32 kClientCp = R5_IH_CLIENT_GRBM_CP;
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
		/* Linux v6.12 drivers/gpu/drm/amd/include/soc15_ih_clientid.h:
		 * VMC is 0x12 (MMHUB), RESERVED is 0x1a, UTCL2 is 0x1b
		 * (GFXHUB). gmc_v9_0_process_interrupt handles VMC and UTCL2.
		 * Source 0 is the fault. vmid_src bit 31 clear is the GC hub.
		 * The address is src_data[0] and src_data[1]. */
		if ((entry.client == R5_IH_CLIENT_VMC
				|| entry.client == R5_IH_CLIENT_UTCL2)
			&& entry.source == 0) {
			const uint32* raw = (const uint32*)(sIhRing + rptr);
			uint32 vmid = (raw[0] >> 24) & 0xfu;
			uint32 hub = raw[0] >> 31;
			uint64 addr = r5_gfx9_fault_address(raw[4], raw[5]);
			dprintf("radeon_hd: vm fault %s vmid %lu addr %016llx\n",
				hub == 0 ? "gfxhub" : "mmhub",
				(unsigned long)vmid, (unsigned long long)addr);
			if (r5_gfx9_ih_gc_fault(sIhRing + rptr))
				r5_gfx9_as_fault(vmid, addr);
			r5_gpu_retire_wake();
			woke = true;
		} else if (entry.client == kClientCp && entry.source == kSourceEop
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
	*(volatile uint32*)(sInfo->registers + oss_byte(0x0083)) = rptr;
	return woke ? B_INVOKE_SCHEDULER : B_HANDLED_INTERRUPT;
}


static void
ih_off()
{
	if (sInfo == NULL)
		return;
	*(volatile uint32*)(sInfo->registers + oss_byte(0x0080)) = 0;
	*(volatile uint32*)(sInfo->registers + gc_byte(0x106a)) = 0;
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


void
r5_gfx9_kernel_stop()
{
	if (sInfo != NULL && sInfo->registers_area >= 0 && sInfo->registers != 0) {
		uint32 me = gc_byte(0x01b6);
		uint32 mec = gc_byte(0x008d);
		kwrite(sInfo, me, kread(sInfo, me)
			| (1u << 24) | (1u << 26) | (1u << 28));
		kwrite(sInfo, mec, kread(sInfo, mec) | (1u << 30) | (1u << 28));
		if (sVmCntlSavedValid)
			kwrite(sInfo, gc_byte(0x0880), sVmCntlSaved);
		const uint32 eng = 17;
		kwrite(sInfo, gc_byte(0x08a3 + eng), r5_gfx9_tlb_req(0, 0));
		for (int i = 0; i < 1000; i++) {
			if ((kread(sInfo, gc_byte(0x08b5 + eng)) & 1u) != 0)
				break;
			snooze(10);
		}
		*(volatile uint32*)(sInfo->registers + oss_byte(0x0080)) = 0;
		*(volatile uint32*)(sInfo->registers + gc_byte(0x106a)) = 0;
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
	uint32 location = kread(sInfo, gc_byte(0x0980));
	uint64 ringMc = ((uint64)(location & 0x00ffffffu) << 24) + ringOffset;
	uint64 wbMc = ringMc + kIhBytes;

	uint64 dummyOffset = 0;
	uint8* dummy = NULL;
	if (sHost.vram_alloc(sInfo, 4096, 4096, &dummyOffset, &dummy) != B_OK)
		return false;
	memset(dummy, 0, 4096);
	uint32 fb = kread(sInfo, gc_byte(0x096b));
	uint64 dummyPa = ((uint64)(fb & 0x00ffffffu) << 24) + dummyOffset;

	kwrite(sInfo, oss_byte(0x0080), 0);
	kwrite(sInfo, oss_byte(0x0088), 0);
	kwrite(sInfo, oss_byte(0x0090), 0);
	kwrite(sInfo, nbio_byte(0x00f2), (uint32)(dummyPa >> 8));
	uint32 interrupt = kread(sInfo, nbio_byte(0x00f1));
	interrupt &= ~(1u << 0);
	interrupt &= ~(1u << 3);
	kwrite(sInfo, nbio_byte(0x00f1), interrupt);

	kwrite(sInfo, oss_byte(0x0081), (uint32)(ringMc >> 8));
	kwrite(sInfo, oss_byte(0x0082), (uint32)((ringMc >> 40) & 0xffu));
	kwrite(sInfo, oss_byte(0x0086), (uint32)wbMc);
	kwrite(sInfo, oss_byte(0x0085), (uint32)(wbMc >> 32) & 0xffffu);
	kwrite(sInfo, oss_byte(0x0084), 0);
	kwrite(sInfo, oss_byte(0x0083), 0);
	kwrite(sInfo, oss_byte(0x0087), 0);

	uint32 cntl = kread(sInfo, oss_byte(0x0080));
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
	kwrite(sInfo, oss_byte(0x0080), cntl);

	uint32 cp = kread(sInfo, gc_byte(0x106a));
	/* gc_9_0_sh_mask.h CP_INT_CNTL_RING0. TIME_STAMP is the EOP
	 * (shift 0x1a). PRIV_INSTR 0x16, PRIV_REG 0x17 and OPCODE_ERROR
	 * 0x18 match gfx_v9_0_set_priv_inst_fault_state and
	 * gfx_v9_0_set_priv_reg_fault_state. Source ids 184 and 185 are
	 * CP_PRIV_REG_FAULT and CP_PRIV_INSTR_FAULT. */
	cp |= 1u << 0x16;
	cp |= 1u << 0x17;
	cp |= 1u << 0x18;
	cp |= 1u << 0x1a;
	kwrite(sInfo, gc_byte(0x106a), cp);
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
	uint32 rptr = kread(sInfo, oss_byte(0x0083));
	uint32 wptr = kread(sInfo, oss_byte(0x0084));
	uint32 wb = sIhWriteback != NULL ? *sIhWriteback : 0;
	knote("k1 fail %s irq %lu eop %lu client %lu src %lu ring %lu",
		sMsi ? "msi" : "legacy", (unsigned long)sIrq,
		(unsigned long)sEopCount, (unsigned long)sLastClient,
		(unsigned long)sLastSource, (unsigned long)sLastRing);
	knote("k1_wait_us %ld", (long)elapsed);
	knote("k1 rptr %08lx wptr %08lx wb %08lx intcntl %08lx cp %08lx status %08lx",
		(unsigned long)rptr, (unsigned long)wptr, (unsigned long)wb,
		(unsigned long)kread(sInfo, nbio_byte(0x00f1)),
		(unsigned long)kread(sInfo, gc_byte(0x106a)),
		(unsigned long)kread(sInfo, gc_byte(0x104a)));
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


static void
run_k2()
{
	uint32 location = kread(sInfo, gc_byte(0x0980));
	uint64 fbStart = (uint64)(location & 0x00ffffffu) << 24;
	uint32 aperLo = kread(sInfo, gc_byte(0x0985));
	uint32 aperHi = kread(sInfo, gc_byte(0x0986));
	uint64 aperStart = (uint64)aperLo << 18;
	uint64 aperEnd = (uint64)aperHi << 18;
	uint32 agpBot = kread(sInfo, gc_byte(0x0983));
	uint32 agpTop = kread(sInfo, gc_byte(0x0982));
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
	uint32 fb = kread(sInfo, gc_byte(0x096b));
	uint64 dummyPa = ((uint64)(fb & 0x00ffffffu) << 24) + dummyOffset;
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

	uint64 tablePa = ((uint64)(fb & 0x00ffffffu) << 24) + tableOffset;
	if (!r5_gfx9_pde_aligned(tablePa)) {
		knote("k2 fail table align %016llx", (unsigned long long)tablePa);
		return;
	}
	uint64 tableBase = r5_gfx9_page_table_base(fb, tableOffset);
	uint32 savedCntl = kread(sInfo, gc_byte(0x0880));
	sVmCntlSaved = savedCntl;
	sVmCntlSavedValid = true;
	kwrite(sInfo, gc_byte(0x08eb), (uint32)tableBase);
	kwrite(sInfo, gc_byte(0x08ec), (uint32)(tableBase >> 32));
	uint32 baseLo = kread(sInfo, gc_byte(0x08eb));
	uint32 baseHi = kread(sInfo, gc_byte(0x08ec));
	knote("k2 ptb %016llx got %08lx %08lx",
		(unsigned long long)tableBase, (unsigned long)baseLo,
		(unsigned long)baseHi);
	if (baseLo != (uint32)tableBase || baseHi != (uint32)(tableBase >> 32)) {
		knote("k2 fail ptb readback");
		return;
	}
	kwrite(sInfo, gc_byte(0x090b), (uint32)(gartStart >> 12));
	kwrite(sInfo, gc_byte(0x090c), (uint32)(gartStart >> 44));
	kwrite(sInfo, gc_byte(0x092b), (uint32)((gartEnd - 1) >> 12));
	kwrite(sInfo, gc_byte(0x092c), (uint32)((gartEnd - 1) >> 44));
	uint32 cntl = savedCntl;
	cntl |= 1u << 0;
	cntl &= ~0x6u;
	cntl &= ~(0xfu << 3);
	cntl &= ~(1u << 7);
	kwrite(sInfo, gc_byte(0x0880), cntl);
	kwrite(sInfo, gc_byte(0x084e), (uint32)(dummyPa >> 12));
	kwrite(sInfo, gc_byte(0x084f), (uint32)(dummyPa >> 44));

	const uint32 eng = 17;
	uint32 req = gc_byte(0x08a3 + eng);
	uint32 ack = gc_byte(0x08b5 + eng);
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
		uint32 faultStatus = kread(sInfo, gc_byte(0x084b));
		uint32 faultLo = kread(sInfo, gc_byte(0x084c));
		uint32 faultHi = kread(sInfo, gc_byte(0x084d));
		r5_gfx9_fault fault = r5_gfx9_fault_decode(faultStatus);
		knote("k2 fault %08lx MORE_FAULTS %lu WALKER_ERROR %lu "
			"PERMISSION_FAULTS %lu MAPPING_ERROR %lu CID %lu RW %lu "
			"addr %016llx cntl %08lx cp %08lx grbm %08lx",
			(unsigned long)faultStatus, (unsigned long)fault.more,
			(unsigned long)fault.walker, (unsigned long)fault.permission,
			(unsigned long)fault.mapping, (unsigned long)fault.cid,
			(unsigned long)fault.rw,
			(unsigned long long)r5_gfx9_fault_address(faultLo, faultHi),
			(unsigned long)kread(sInfo, gc_byte(0x0880)),
			(unsigned long)kread(sInfo, gc_byte(0x01a0)),
			(unsigned long)kread(sInfo, gc_byte(0x0004)));
		uint32 me = gc_byte(0x01b6);
		uint32 saved = kread(sInfo, me);
		kwrite(sInfo, me, saved | ((1u << 24) | (1u << 26) | (1u << 28)));
		kwrite(sInfo, gc_byte(0x0880), savedCntl);
	}
	(void)sequence;
	(void)flushed;
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

struct r5_as {
	uint32		id;
	r5_vm		vm;
	r5_vm_page	pages[1024];
	int			loaded;
	int			parked;
	uint64		last;
	uint32		vmid;
	uint64		tlb_seq;
	int			lost;
};

static r5_as* sSpaces[64];
static r5_vmid_mgr sVmids;
static int sVmidReady = 0;
static uint32 sNextAs = 1;
static r5_vm_page sFreePages[1024];
static uint32 sFreeCount = 0;



static r5_as*
as_from(void* cookie)
{
	return (r5_as*)cookie;
}


static int
vm_page_alloc(void* cookie, uint64_t* physical, uint64_t** cpu)
{
	(void)cookie;
	uint64 offset = 0;
	uint8* address = NULL;
	if (sFreeCount > 0) {
		sFreeCount--;
		*physical = sFreePages[sFreeCount].physical;
		*cpu = sFreePages[sFreeCount].cpu;
		return 1;
	}
	if (sHost.vram_alloc == NULL || sInfo == NULL)
		return 0;
	if (sHost.vram_alloc(sInfo, 4096, 4096, &offset, &address) != B_OK
		|| address == NULL)
		return 0;
	uint32 fb = kread(sInfo, gc_byte(0x096b));
	*physical = ((uint64)(fb & 0x00ffffffu) << 24) + offset;
	*cpu = (uint64_t*)address;
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
r5_gfx9_tlb_flush_vmid(uint32 vmid)
{
	if (sInfo == NULL || vmid == 0 || vmid > 15)
		return false;
	const uint32 eng = 0;
	uint32 inv = r5_gfx9_tlb_req(vmid, 0);
	kwrite(sInfo, gc_byte(0x08a3 + eng), inv);
	(void)kread(sInfo, gc_byte(0x08a3 + eng));
	const uint32 ackBit = r5_gfx9_tlb_ack_mask(vmid);
	for (int i = 0; i < 10000; i++) {
		if ((kread(sInfo, gc_byte(0x08b5 + eng)) & ackBit) != 0)
			return true;
		snooze(10);
	}
	return false;
}


bool
r5_gfx9_vmid1_flush()
{
	if (sInfo == NULL) {
		knote("vmid1 fail info");
		return false;
	}
	if (!r5_gfx9_tlb_flush_vmid(1)) {
		knote("vmid1 fail tlb");
		return false;
	}
	return true;
}


bool
r5_gfx9_sh_mem(uint32 vmid)
{
	/* gfx_v9_0_constants_init. ALIGNMENT_MODE unaligned and
	 * RETRY_DISABLE are the VMID 0 value 0x1018. PRIVATE_BASE and
	 * SHARED_BASE make SH_MEM_BASES 0x20001000. GRBM_GFX_CNTL is
	 * vmid << 4, then 0 so the next user is VMID 0 again. VMID 0
	 * itself stays the ring contract and is not written here. */
	if (sInfo == NULL || vmid == 0 || vmid > 15)
		return false;
	const uint32 shConfig = (3u << 3) | (1u << 12);
	const uint32 shBases = (0x2000u << 16) | 0x1000u;
	kwrite(sInfo, gc_byte(0x0022), vmid << 4);
	kwrite(sInfo, gc_byte(0x030d), shConfig);
	kwrite(sInfo, gc_byte(0x030a), shBases);
	kwrite(sInfo, gc_byte(0x0022), 0);
	return true;
}


bool
r5_gfx9_vmid1_setup()
{
	/* The boot root stays this shared table. A bound file maps through
	 * its own page table. These register writes stay the VMID 1 setup. */
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
		uint32 fb = kread(sInfo, gc_byte(0x096b));
		sK2DummyPa = ((uint64)(fb & 0x00ffffffu) << 24) + offset;
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
		if (!r5_vm_new_page(&sVm)) {
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
	kwrite(sInfo, gc_byte(0x0851), 0xffffffffu);
	kwrite(sInfo, gc_byte(0x0852), 0xfu);
	kwrite(sInfo, gc_byte(0x0853), 0);
	kwrite(sInfo, gc_byte(0x0854), 0);
	kwrite(sInfo, gc_byte(0x08ed), (uint32)tableBase);
	kwrite(sInfo, gc_byte(0x08ee), (uint32)(tableBase >> 32));
	kwrite(sInfo, gc_byte(0x090d), 0);
	kwrite(sInfo, gc_byte(0x090e), 0);
	kwrite(sInfo, gc_byte(0x092d), 0xffffffffu);
	kwrite(sInfo, gc_byte(0x092e), 0xfu);
	/* A faulting VMID 1 access lands on the K2 dummy page, not at
	 * physical 0. The page number is the MC address >> 12.
	 */
	kwrite(sInfo, gc_byte(0x084e), (uint32)(sK2DummyPa >> 12));
	kwrite(sInfo, gc_byte(0x084f), (uint32)(sK2DummyPa >> 44));
	uint32 faultCntl = kread(sInfo, gc_byte(0x0847));
	uint32 faultNext = (faultCntl | kFaultDefaultMask) & ~kFaultCrashMask;
	if (faultNext != faultCntl)
		kwrite(sInfo, gc_byte(0x0847), faultNext);
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
	kwrite(sInfo, gc_byte(0x0881), cntl);
	if (!r5_gfx9_vmid1_flush()) {
		/* CNTL is already enabled. Turn the context off so a later
		 * attempt does not run on the half-written state. The pages
		 * above stay for that attempt.
		 */
		kwrite(sInfo, gc_byte(0x0881), 0);
		return false;
	}
	/* gfx_v9_0_constants_init for VMID 1..15. ALIGNMENT_MODE unaligned
	 * and RETRY_DISABLE match VMID 0 (0x1018). PRIVATE_BASE is
	 * 0x1000000000000000 >> 48 and SHARED_BASE is 0x2000000000000000 >> 48,
	 * so SH_MEM_BASES is 0x20001000. VMID 0 stays the ring contract
	 * (bases 0) and is not written here. Callers hold gpu_lock, the same
	 * lock as the other GRBM_GFX_CNTL users. These are registers only.
	 */
	const uint32 shConfig = (3u << 3) | (1u << 12);
	const uint32 shBases = (0x2000u << 16) | 0x1000u;
	for (uint32 vmid = 1; vmid <= 15; vmid++) {
		kwrite(sInfo, gc_byte(0x0022), vmid << 4);
		kwrite(sInfo, gc_byte(0x030d), shConfig);
		kwrite(sInfo, gc_byte(0x030a), shBases);
	}
	kwrite(sInfo, gc_byte(0x0022), 1u << 4);
	uint32 shRead = kread(sInfo, gc_byte(0x030d));
	uint32 baseRead = kread(sInfo, gc_byte(0x030a));
	kwrite(sInfo, gc_byte(0x0022), 0);
	sVmid1 = true;
	knote("vmid1 sh_mem config %08lx bases %08lx",
		(unsigned long)shRead, (unsigned long)baseRead);
	knote("vmid1 ptb %016llx cntl %08lx pages %lu",
		(unsigned long long)tableBase, (unsigned long)cntl,
		(unsigned long)sVm.used);
	knote("vmid1 default %016llx fault %08lx",
		(unsigned long long)sK2DummyPa, (unsigned long)faultNext);
	knote("vmid1 identity low %08lx %08lx high %08lx %08lx",
		(unsigned long)kread(sInfo, gc_byte(0x0851)),
		(unsigned long)kread(sInfo, gc_byte(0x0852)),
		(unsigned long)kread(sInfo, gc_byte(0x0853)),
		(unsigned long)kread(sInfo, gc_byte(0x0854)));
	return true;
}


bool
r5_gfx9_vmid1_map(void* as, uint64 va, uint64 pte)
{
	r5_as* space = as_from(as);
	if (!sVmid1 || space == NULL || space->vm.used == 0)
		return false;
	if (!r5_vm_map_page(&space->vm, va, pte))
		return false;
	if (pte == 0) {
		space->tlb_seq++;
		if (space->tlb_seq == 0)
			space->tlb_seq = 1;
	}
	asm volatile("sfence" ::: "memory");
	return true;
}


bool
r5_gfx9_vmid1_lookup(void* as, uint64 va)
{
	r5_as* space = as_from(as);
	uint64 pte = 0;
	if (!sVmid1 || space == NULL)
		return false;
	if (r5_vm_lookup(&space->vm, va & ~0xfffull, &pte) == 0)
		return false;
	return (pte & (uint64)R5_GFX9_PTE_VALID) != 0;
}


bool
r5_gfx9_vmid1_pte(void* as, uint64 va, uint64* pte)
{
	r5_as* space = as_from(as);
	if (pte == NULL || !sVmid1 || space == NULL)
		return false;
	return r5_vm_lookup(&space->vm, va & ~0xfffull, pte) != 0;
}


/* The page comes from the VMID 1 table. A system page is mapped for the
 * copy and unmapped after it. A VRAM page is the pool window. Neither
 * is a user address (SMAP). */
bool
r5_gfx9_vmid1_read(uint64 va, uint32* words, uint32 count)
{
	uint64 pte = 0;
	uint32 space;
	int found = 0;
	if (words == NULL || count == 0)
		return false;
	for (space = 0; space < 64 && !found; space++) {
		if (sSpaces[space] != NULL
			&& r5_gfx9_vmid1_pte(sSpaces[space], va, &pte))
			found = 1;
	}
	if (!found)
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
	} else if (sInfo != NULL && sInfo->kernel_window != NULL
		&& sInfo->shared_info != NULL) {
		uint32 fb = kread(sInfo, gc_byte(0x096b)) & 0x00ffffffu;
		uint64 base = (uint64)fb << 24;
		uint64 pool = sInfo->shared_info->vram_pool_offset;
		uint64 bytes = sInfo->shared_info->vram_pool_bytes;
		if (page < base || page - base < pool
			|| page - base - pool >= bytes)
			return false;
		cpu = sInfo->kernel_window + (page - base - pool);
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
		unload_driver_settings(settings);
	}
	uint32 pci = info.pciID;
	if (!ring || !kernel)
		return;
	if (!r5_amd_gfx9_cp(pci))
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
	uint64 vramBytes = (uint64)info.shared_info->frame_buffer_size * 1024;
	memset(&sResults, 0, sizeof(sResults));
	info.shared_info->gfx9_owner = R5_GFX9_OWNER_KERNEL;
	r5_gfx9_prepare(&sHost, vramBytes);
	bool ok = r5_gfx9_start(&sHost, pci, vramBytes, workload, compute, &sResults);
	r5_gfx9_copy_firmware(info.fw_version, info.fw_feature, 32);
	knote("k0 %s fence", ok ? "ok" : "fail");
	if (ok && irq)
		run_k1();
	if (ok && gart)
		run_k2();
	r5_gpu_fill_passport(&info);
	publish();
}


static void
recycle_vm(r5_vm* vm)
{
	uint32 i;
	for (i = 0; i < vm->used && sFreeCount < 1024; i++) {
		if (vm->pages[i].physical == 0)
			continue;
		sFreePages[sFreeCount] = vm->pages[i];
		sFreeCount++;
	}
	vm->used = 0;
}


void*
r5_gfx9_as_open(void)
{
	r5_as* as;
	uint32 n;
	if (!sVmid1)
		return NULL;
	for (n = 0; n < 64; n++) {
		if (sSpaces[n] == NULL)
			break;
	}
	if (n == 64)
		return NULL;
	as = (r5_as*)malloc(sizeof(*as));
	if (as == NULL)
		return NULL;
	memset(as, 0, sizeof(*as));
	as->id = sNextAs++;
	if (as->id == 0)
		as->id = sNextAs++;
	as->vm.pages = as->pages;
	/* 1024 table pages cover a contiguous 1 GiB map (512 leaves plus
	 * the upper levels) and still fit under a quarter of RAM. */
	as->vm.capacity = 1024;
	as->vm.alloc = vm_page_alloc;
	if (!r5_vm_new_page(&as->vm)) {
		free(as);
		return NULL;
	}
	sSpaces[n] = as;
	return as;
}


void
r5_gfx9_as_close(void* cookie, uint64 seen)
{
	r5_as* as = (r5_as*)cookie;
	uint32 n;
	if (as == NULL)
		return;
	if (as->vmid != 0 && as->last > seen) {
		as->parked = 1;
		return;
	}
	if (as->vmid != 0 && sVmidReady)
		r5_vmid_release(&sVmids, as->id, seen);
	recycle_vm(&as->vm);
	for (n = 0; n < 64; n++) {
		if (sSpaces[n] == as)
			sSpaces[n] = NULL;
	}
	free(as);
}


void
r5_gfx9_as_bind(void* cookie)
{
	(void)cookie;
}


int
r5_gfx9_as_reserve(void* cookie, uint32* vmid, int* flush, uint64* pd)
{
	r5_as* as = as_from(cookie);
	uint64 seen;
	if (as == NULL || as->vm.used == 0 || vmid == NULL || flush == NULL)
		return 0;
	if (!sVmidReady) {
		r5_vmid_init(&sVmids);
		sVmidReady = 1;
	}
	seen = r5_gfx9_user_seen();
	/* gmc_v9_0_get_vm_pde adds no BFS flags when translate_further is
	 * off, which it is on this GPU. The register value is the root
	 * physical address with VALID, the same as the VMID 1 setup. */
	*vmid = r5_vmid_reserve(&sVmids, as->id, seen, as->tlb_seq, flush);
	if (*vmid == 0)
		return 0;
	if (pd != NULL)
		*pd = as->vm.pages[0].physical | (uint64)R5_GFX9_PTE_VALID;
	return 1;
}


void
r5_gfx9_as_cancel(void)
{
	if (sVmidReady)
		r5_vmid_cancel(&sVmids);
}


void
r5_gfx9_as_finish(void* cookie, uint32 vmid, uint64 sequence)
{
	r5_as* as = as_from(cookie);
	if (as == NULL)
		return;
	as->vmid = vmid;
	as->last = sequence;
	as->loaded = 1;
	r5_vmid_arm(&sVmids, sequence);
}


int
r5_gfx9_as_first(void* cookie)
{
	r5_as* as = as_from(cookie);
	if (as == NULL)
		return -1;
	return as->loaded ? 0 : 1;
}


uint32
r5_gfx9_as_vmid(void* cookie)
{
	r5_as* as = as_from(cookie);
	if (as == NULL)
		return 0;
	return as->vmid;
}


bool
r5_gfx9_as_lost(void* cookie)
{
	r5_as* as = as_from(cookie);
	return as != NULL && as->lost != 0;
}


void
r5_gfx9_as_fault(uint32 vmid, uint64 addr)
{
	uint32 n;
	if (vmid == 0 || vmid > 15)
		return;
	for (n = 0; n < 64; n++) {
		r5_as* as = sSpaces[n];
		if (as == NULL || as->vmid != vmid)
			continue;
		as->lost = 1;
		knote("vm fault vmid %lu addr %016llx",
			(unsigned long)vmid, (unsigned long long)addr);
		return;
	}
	knote("vm fault vmid %lu addr %016llx unmatched",
		(unsigned long)vmid, (unsigned long long)addr);
}


void
r5_gfx9_as_retired(uint64 seen)
{
	uint32 n;
	for (n = 0; n < 64; n++) {
		if (sSpaces[n] != NULL && sSpaces[n]->parked
			&& sSpaces[n]->last <= seen)
			r5_gfx9_as_close(sSpaces[n], seen);
	}
}


int
r5_gfx9_mem_limits(uint64* vramCap, uint64* gttCap)
{
	uint32 base;
	uint32 top;
	uint64 carve;
	uint64 ram;
	if (!sVmid1 || sInfo == NULL || vramCap == NULL || gttCap == NULL)
		return 0;
	base = kread(sInfo, gc_byte(0x096b)) & 0x00ffffffu;
	top = kread(sInfo, gc_byte(0x096c)) & 0x00ffffffu;
	if (top < base)
		return 0;
	carve = ((uint64)(top - base) + 1ull) << 24;
	system_info sys;
	if (get_system_info(&sys) != B_OK)
		return 0;
	ram = (uint64)sys.max_pages * 4096ull;
	*vramCap = r5_limit_vram(carve);
	*gttCap = r5_limit_gtt(ram);
	return *vramCap != 0 && *gttCap != 0;
}
