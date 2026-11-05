/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Kernel host for the shared GFX9 start, then the interrupt ring and the
 * GART. The packet bytes stay in gfx9_start.cpp. Register numbers are from
 * Linux v6.12 headers: vega10_ip_offset.h, osssys_4_0, nbio_7_0, gc_9_0.
 */

#include "gfx9_start.h"
#include "radeon_hd_private.h"

#include <PCI.h>
#include <KernelExport.h>
#include <driver_settings.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>


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
static uint32 sEopCount = 0;
static uint32 sLastClient = 0;
static uint32 sLastSource = 0;
static uint32 sLastRing = 0;
static uint8 sFirstRaw[4][32];
static area_id sSystemArea = -1;
extern pci_module_info* gPCI;

static const uint32 kIhBytes = 64 * 1024;
static const uint32 kOssBase = 0x10A0;
static const uint32 kGcBase = 0x2000;
static const uint32 kNbioBase = 0x0D20;
static const uint32 kClientCp = 0x14;
static const uint32 kSourceEop = 181;


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
	if (info->kernel_window == NULL)
		return B_NO_MEMORY;
	shared->vram_pool_cursor = aligned + bytes;
	*offset = aligned;
	if (address != NULL)
		*address = info->kernel_window + aligned;
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
			&& entry.ring == 0 && sEopSem >= 0) {
			release_sem_etc(sEopSem, 1, B_DO_NOT_RESCHEDULE);
			sEopCount++;
			woke = true;
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


void
r5_gfx9_kernel_stop()
{
	ih_off();
	if (sEopSem >= 0) {
		delete_sem(sEopSem);
		sEopSem = -1;
	}
	if (sSystemArea >= 0) {
		delete_area(sSystemArea);
		sSystemArea = -1;
	}
	if (sMsi && gPCI != NULL && sInfo != NULL && sInfo->pci != NULL) {
		gPCI->disable_msi(sInfo->pci->bus, sInfo->pci->device,
			sInfo->pci->function);
		gPCI->unconfigure_msi(sInfo->pci->bus, sInfo->pci->device,
			sInfo->pci->function);
		sMsi = false;
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
	knote("k2 agp bot %08lx top %08lx %s", (unsigned long)agpBot,
		(unsigned long)agpTop, agpBot > agpTop ? "off" : "on");
	uint64 gartStart = 0;
	uint64 gartEnd = 0;
	if (!r5_gfx9_gart_range(fbStart, aperStart, aperEnd, &gartStart, &gartEnd)) {
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
	uint64 flags = R5_GFX9_PTE_VALID | R5_GFX9_PTE_SYSTEM | R5_GFX9_PTE_SNOOPED
		| R5_GFX9_PTE_READABLE | R5_GFX9_PTE_WRITEABLE;
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
	int32 got = get_memory_map(system, 1024 * 1024, entries, 256);
	if (got <= 0) {
		knote("k2 fail memory map");
		return;
	}
	uint32 page = 0;
	for (int32 slot = 0; slot < got && page < 256; slot++) {
		uint64 address = entries[slot].address;
		uint64 bytes = entries[slot].size;
		while (bytes >= 4096 && page < 256) {
			pte[page] = r5_gfx9_gart_pte(address, flags);
			address += 4096;
			bytes -= 4096;
			page++;
		}
	}
	if (page != 256) {
		knote("k2 fail pages %lu", (unsigned long)page);
		return;
	}

	uint64 tableMc = ((uint64)(location & 0x00ffffffu) << 24) + tableOffset;
	uint32 savedCntl = kread(sInfo, gc_byte(0x0880));
	kwrite(sInfo, gc_byte(0x08eb), (uint32)tableMc);
	kwrite(sInfo, gc_byte(0x08ec), (uint32)(tableMc >> 32));
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
	uint32 inv = 1u | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21)
		| (1u << 22);
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

	uint32* words = (uint32*)system;
	for (uint32 i = 0; i < (1024 * 1024) / 4; i++)
		words[i] = 0xCAFEDEAD;
	uint32 sequence = 0;
	bool dma = r5_gfx9_k2_dma(gartStart, &sequence);
	uint32 bad = 0;
	if (dma) {
		for (uint32 i = 0; i < (1024 * 1024) / 4; i++) {
			if (words[i] != 0x5A5A5A5Au)
				bad++;
		}
	}
	uint32 ibSequence = 0;
	bool ib = false;
	if (dma && bad == 0)
		ib = r5_gfx9_k2_gart_ib(gartStart, (uint8*)system, &ibSequence);
	uint32 seenWord = r5_gfx9_last_word();
	uint64 seenSeq = r5_gfx9_last_sequence();
	knote("k2 %s gart %016llx-%016llx pte0 %016llx",
		(dma && bad == 0 && ib) ? "ok" : "fail",
		(unsigned long long)gartStart, (unsigned long long)gartEnd,
		(unsigned long long)pte[0]);
	knote("k2_dma_bad %lu", (unsigned long)(dma ? bad : 0xffffffffu));
	knote("k2_ib wb %08lx seq %lu seen %lu", (unsigned long)seenWord,
		(unsigned long)ibSequence, (unsigned long)seenSeq);
	if (!(dma && bad == 0 && ib)) {
		knote("k2 fault %08lx addr %08lx %08lx cntl %08lx cp %08lx grbm %08lx",
			(unsigned long)kread(sInfo, gc_byte(0x084b)),
			(unsigned long)kread(sInfo, gc_byte(0x084c)),
			(unsigned long)kread(sInfo, gc_byte(0x084d)),
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


void
r5_gfx9_kernel_start(radeon_info& info)
{
	sInfo = &info;
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
	if (pci != 0x15e7 && pci != 0x1636 && pci != 0x1638)
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
	knote("k0 %s fence", ok ? "ok" : "fail");
	if (ok && irq)
		run_k1();
	if (ok && gart)
		run_k2();
	publish();
}
