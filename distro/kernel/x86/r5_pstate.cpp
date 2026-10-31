/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Read-only snapshot of AMD hardware P-states and Intel HWP.
 * This file is linked into the kernel, so it stays MIT.
 * It does not write an MSR.
 */


#include "r5_pstate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Drivers.h>

#include <cpu.h>
#include <debug.h>
#include <smp.h>

#include <fs/devfs.h>

#include <arch/x86/arch_cpu.h>
#include <arch/x86/timer.h>


static const uint32 kAmdPStateLimit = 0xc0010061;
static const uint32 kAmdPStateStatus = 0xc0010063;
static const size_t kReportBytes = 4096;


static void
append(char* buffer, size_t capacity, size_t* used, const char* format, ...)
{
	if (*used >= capacity)
		return;

	va_list args;
	va_start(args, format);
	int wrote = vsnprintf(buffer + *used, capacity - *used, format, args);
	va_end(args);
	if (wrote < 0)
		return;
	if (size_t(wrote) >= capacity - *used)
		*used = capacity - 1;
	else
		*used += size_t(wrote);
}


static bool
zen_pstate_mhz(uint64 value, uint32* mhz)
{
	uint32 denominator = uint32((value >> 8) & 0x3f);
	uint32 numerator = uint32(value & 0xff);
	if (denominator < 0x8 || denominator > 0x2c)
		return false;
	if (denominator > 0x1a && (denominator % 2) == 1)
		return false;
	if (numerator < 0x10)
		return false;

	*mhz = 200 * numerator / denominator;
	return true;
}


static void
append_amd(char* buffer, size_t capacity, size_t* used, uint32 family)
{
	bool hardware = x86_check_feature(IA32_FEATURE_AMD_HW_PSTATE,
		FEATURE_EXT_7_EDX);
	bool cppc = x86_check_feature(IA32_FEATURE_CPPC, FEATURE_EXT_8_EBX);
	append(buffer, capacity, used, "R5 CPU: amd_hw_pstate %u cppc %u\n",
		hardware ? 1u : 0u, cppc ? 1u : 0u);
	if (!hardware && !cppc)
		return;

	if (hardware) {
		uint64 limit = x86_read_msr(kAmdPStateLimit);
		uint64 status = x86_read_msr(kAmdPStateStatus);
		uint32 current = uint32(status & 0x7);
		append(buffer, capacity, used,
			"R5 CPU: pstate_limit 0x%" B_PRIx64
			" pstate_status 0x%" B_PRIx64 " current %" B_PRIu32 "\n",
			limit, status, current);

		bool zen = family == 0x17 || family == 0x19;
		for (uint32 index = 0; index < 8; index++) {
			uint64 defined = x86_read_msr(MSR_F10H_PSTATEDEF(index));
			if ((defined & PSTATEDEF_EN) == 0) {
				append(buffer, capacity, used,
					"R5 CPU: pstate %" B_PRIu32 " disabled raw 0x%"
					B_PRIx64 "\n", index, defined);
				break;
			}

			uint32 fid = uint32(defined & 0xff);
			uint32 did = uint32((defined >> 8) & 0x3f);
			uint32 mhz = 0;
			const char* mark = index == current ? " current" : "";
			if (zen && zen_pstate_mhz(defined, &mhz)) {
				append(buffer, capacity, used,
					"R5 CPU: pstate %" B_PRIu32 " fid %" B_PRIu32
					" did %" B_PRIu32 " mhz %" B_PRIu32
					" raw 0x%" B_PRIx64 "%s\n",
					index, fid, did, mhz, defined, mark);
			} else {
				append(buffer, capacity, used,
					"R5 CPU: pstate %" B_PRIu32 " fid %" B_PRIu32
					" did %" B_PRIu32 " mhz n/a raw 0x%" B_PRIx64 "%s\n",
					index, fid, did, defined, mark);
			}
		}
	}

	if (cppc) {
		uint64 cap = x86_read_msr(MSR_AMD_CPPC_CAP1);
		uint64 enable = x86_read_msr(MSR_AMD_CPPC_ENABLE);
		uint64 request = x86_read_msr(MSR_AMD_CPPC_REQ);
		uint64 cppcStatus = x86_read_msr(MSR_AMD_CPPC_STATUS);
		append(buffer, capacity, used,
			"R5 CPU: cppc_cap 0x%" B_PRIx64 " lowest %u low_nonlinear %u"
			" nominal %u highest %u\n",
			cap,
			unsigned(AMD_CPPC_LOWEST_PERF(cap)),
			unsigned(AMD_CPPC_LOWNONLIN_PERF(cap)),
			unsigned(AMD_CPPC_NOMINAL_PERF(cap)),
			unsigned(AMD_CPPC_HIGHEST_PERF(cap)));
		append(buffer, capacity, used,
			"R5 CPU: cppc_enable 0x%" B_PRIx64 " request 0x%" B_PRIx64
			" status 0x%" B_PRIx64 "\n",
			enable, request, cppcStatus);
		append(buffer, capacity, used,
			"R5 CPU: cppc_request max %u min %u desired %u epp %u\n",
			unsigned(request & 0xff),
			unsigned((request >> 8) & 0xff),
			unsigned((request >> 16) & 0xff),
			unsigned((request >> 24) & 0xff));
	}
}


static void
append_intel(char* buffer, size_t capacity, size_t* used)
{
	bool hwp = x86_check_feature(IA32_FEATURE_HWP, FEATURE_6_EAX);
	append(buffer, capacity, used, "R5 CPU: hwp %u\n", hwp ? 1u : 0u);
	if (!hwp)
		return;

	uint64 caps = x86_read_msr(IA32_MSR_HWP_CAPABILITIES);
	uint64 enable = x86_read_msr(IA32_MSR_PM_ENABLE);
	append(buffer, capacity, used,
		"R5 CPU: hwp_caps 0x%" B_PRIx64 " highest %u guaranteed %u"
		" efficient %u lowest %u\n",
		caps,
		unsigned(IA32_HWP_CAPS_HIGHEST_PERFORMANCE(caps)),
		unsigned(IA32_HWP_CAPS_GUARANTEED_PERFORMANCE(caps)),
		unsigned(IA32_HWP_CAPS_EFFICIENT_PERFORMANCE(caps)),
		unsigned(IA32_HWP_CAPS_LOWEST_PERFORMANCE(caps)));
	append(buffer, capacity, used,
		"R5 CPU: hwp_enable 0x%" B_PRIx64 "\n", enable);
	if ((enable & 1) == 0)
		return;

	uint64 request = x86_read_msr(IA32_MSR_HWP_REQUEST);
	append(buffer, capacity, used,
		"R5 CPU: hwp_request 0x%" B_PRIx64 "\n", request);
}


static size_t
format_report(char* buffer, size_t capacity)
{
	size_t used = 0;
	if (capacity == 0)
		return 0;
	buffer[0] = '\0';

	cpu_ent* cpu = get_cpu_struct();
	const char* vendor = "other";
	if (cpu->arch.vendor == VENDOR_AMD)
		vendor = "amd";
	else if (cpu->arch.vendor == VENDOR_HYGON)
		vendor = "hygon";
	else if (cpu->arch.vendor == VENDOR_INTEL)
		vendor = "intel";

	uint32 family = uint32(cpu->arch.family + cpu->arch.extended_family);
	uint32 model = uint32((cpu->arch.extended_model << 4) | cpu->arch.model);
	append(buffer, capacity, &used,
		"R5 CPU: cpu %" B_PRId32 " vendor %s family 0x%" B_PRIx32
		" model 0x%" B_PRIx32 " name %s\n",
		smp_get_current_cpu(), vendor, family, model, cpu->arch.model_name);
	append(buffer, capacity, &used,
		"R5 CPU: tsc_hz %" B_PRId64 "\n", gCPUClockSpeed);

	if (x86_check_feature(IA32_FEATURE_EXT_HYPERVISOR, FEATURE_EXT)) {
		append(buffer, capacity, &used,
			"R5 CPU: hypervisor bit set, hardware MSRs not read\n");
		append(buffer, capacity, &used, "R5 CPU: no msr write\n");
		return used;
	}

	if (cpu->arch.vendor == VENDOR_AMD || cpu->arch.vendor == VENDOR_HYGON)
		append_amd(buffer, capacity, &used, family);
	else if (cpu->arch.vendor == VENDOR_INTEL)
		append_intel(buffer, capacity, &used);
	else {
		append(buffer, capacity, &used,
			"R5 CPU: no AMD hardware P-state and no Intel HWP on this cpu\n");
	}

	if (x86_check_feature(IA32_FEATURE_APERFMPERF, FEATURE_6_ECX)) {
		uint64 mperf = x86_read_msr(IA32_MSR_MPERF);
		uint64 aperf = x86_read_msr(IA32_MSR_APERF);
		append(buffer, capacity, &used,
			"R5 CPU: mperf 0x%" B_PRIx64 " aperf 0x%" B_PRIx64 "\n",
			mperf, aperf);
	}

	append(buffer, capacity, &used, "R5 CPU: no msr write\n");
	return used;
}


static void
print_lines(const char* text)
{
	const char* cursor = text;
	while (cursor != NULL && cursor[0] != '\0') {
		const char* end = strchr(cursor, '\n');
		size_t length = end != NULL ? size_t(end - cursor) : strlen(cursor);
		char line[192];
		if (length >= sizeof(line))
			length = sizeof(line) - 1;
		memcpy(line, cursor, length);
		line[length] = '\0';
		dprintf("%s\n", line);
		if (end == NULL)
			break;
		cursor = end + 1;
	}
}


struct r5_pstate_cookie {
	char*	text;
	size_t	length;
};


extern "C" {


static status_t
r5_pstate_open(const char* name, uint32 flags, void** _cookie)
{
	(void)name;
	(void)flags;

	r5_pstate_cookie* cookie = static_cast<r5_pstate_cookie*>(
		malloc(sizeof(r5_pstate_cookie)));
	if (cookie == NULL)
		return B_NO_MEMORY;
	cookie->text = static_cast<char*>(malloc(kReportBytes));
	if (cookie->text == NULL) {
		free(cookie);
		return B_NO_MEMORY;
	}
	cookie->length = format_report(cookie->text, kReportBytes);
	*_cookie = cookie;
	return B_OK;
}


static status_t
r5_pstate_close(void* cookie)
{
	(void)cookie;
	return B_OK;
}


static status_t
r5_pstate_free(void* _cookie)
{
	r5_pstate_cookie* cookie = static_cast<r5_pstate_cookie*>(_cookie);
	if (cookie != NULL) {
		free(cookie->text);
		free(cookie);
	}
	return B_OK;
}


static status_t
r5_pstate_control(void* cookie, uint32 op, void* data, size_t length)
{
	(void)cookie;
	(void)op;
	(void)data;
	(void)length;
	return B_BAD_VALUE;
}


static status_t
r5_pstate_read(void* _cookie, off_t position, void* buffer, size_t* numBytes)
{
	r5_pstate_cookie* cookie = static_cast<r5_pstate_cookie*>(_cookie);
	if (cookie == NULL || position < 0
		|| uint64(position) >= cookie->length) {
		*numBytes = 0;
		return B_OK;
	}

	size_t left = cookie->length - size_t(position);
	if (*numBytes > left)
		*numBytes = left;
	memcpy(buffer, cookie->text + position, *numBytes);
	return B_OK;
}


static status_t
r5_pstate_write(void* cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	(void)cookie;
	(void)position;
	(void)buffer;
	*numBytes = 0;
	return B_NOT_ALLOWED;
}


}


static device_hooks sPstateHooks = {
	r5_pstate_open,
	r5_pstate_close,
	r5_pstate_free,
	r5_pstate_control,
	r5_pstate_read,
	r5_pstate_write,
	NULL,
	NULL,
	NULL,
	NULL
};


void
r5_pstate_publish()
{
	char buffer[kReportBytes];
	format_report(buffer, sizeof(buffer));
	print_lines(buffer);

	status_t status = devfs_publish_device("misc/r5_pstate", &sPstateHooks);
	dprintf("R5 CPU: publish misc/r5_pstate status %" B_PRId32 "\n", status);
}
