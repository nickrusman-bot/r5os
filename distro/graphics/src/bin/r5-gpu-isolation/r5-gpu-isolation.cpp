/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Two processes. The first maps a page and sends only the GPU address
 * down a pipe. The second reads and writes that address in its own
 * address space. The owner's page stays 0x5a. The fault is the second
 * file's.
 */

#include "r5_gpu.h"
#include "r5_isolation_ib.h"

#include <OS.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>


static int
gpu_open(void)
{
	return open("/dev/gpu/radeon_hd/0", O_RDWR);
}


static int
ctx_create(int fd)
{
	r5_gpu_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.size = (uint32_t)sizeof(ctx);
	ctx.version = 1;
	return ioctl(fd, R5_GPU_CTX_CREATE, &ctx, sizeof(ctx));
}


static int
bo_create(int fd, uint32_t* handle, int32_t* area, void** cpu)
{
	r5_gpu_bo bo;
	area_info info;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32_t)sizeof(bo);
	bo.version = 1;
	bo.domain = R5_GPU_DOMAIN_GTT;
	bo.flags = R5_GPU_BO_WC;
	bo.bytes = 4096;
	if (ioctl(fd, R5_GPU_BO_CREATE, &bo, sizeof(bo)) != 0)
		return -1;
	*handle = bo.handle;
	memset(&bo, 0, sizeof(bo));
	bo.size = (uint32_t)sizeof(bo);
	bo.version = 1;
	bo.handle = *handle;
	if (ioctl(fd, R5_GPU_BO_MAP, &bo, sizeof(bo)) != 0)
		return -1;
	*area = bo.area;
	if (get_area_info(*area, &info) != B_OK)
		return -1;
	*cpu = info.address;
	return 0;
}


/* Stick 30 fault status 0x00800880 is VMID 8 and PERMISSION_FAULTS 8.
 * The address register 0x80002 is page 0x80002000, the owner's command
 * list. The command processor fetches that page, so it is executable.
 * The data page stays readable and writable only. */
static const uint32_t kCommandFlags = R5_GPU_VA_READ | R5_GPU_VA_WRITE
	| R5_GPU_VA_EXEC;


static int
va_map(int fd, uint32_t handle, uint64_t va, uint32_t flags)
{
	r5_gpu_va map;
	memset(&map, 0, sizeof(map));
	map.size = (uint32_t)sizeof(map);
	map.version = 1;
	map.context = 1;
	map.handle = handle;
	map.gpu_va = va;
	map.map_bytes = 4096;
	map.flags = flags;
	return ioctl(fd, R5_GPU_VA_MAP, &map, sizeof(map));
}


static int
submit_write(int fd, uint64_t ibVa, uint32_t dwords)
{
	r5_gpu_ib ib;
	r5_gpu_cs cs;
	r5_gpu_fence fence;
	memset(&ib, 0, sizeof(ib));
	ib.gpu_va = ibVa;
	ib.dwords = dwords;
	memset(&cs, 0, sizeof(cs));
	cs.size = (uint32_t)sizeof(cs);
	cs.version = 1;
	cs.context = 1;
	cs.ib_count = 1;
	cs.ibs = (uint64_t)(uintptr_t)&ib;
	if (ioctl(fd, R5_GPU_CS_SUBMIT, &cs, sizeof(cs)) != 0)
		return -1;
	memset(&fence, 0, sizeof(fence));
	fence.size = (uint32_t)sizeof(fence);
	fence.version = 1;
	fence.sequence = cs.sequence;
	fence.timeout_us = 5000000;
	return ioctl(fd, R5_GPU_FENCE_WAIT, &fence, sizeof(fence));
}


static void
fill_ib(uint32_t* words, uint64_t dest, uint64_t readBack)
{
	r5_isolation_fill_ib(words, dest, readBack);
}


static void
print_fault(uint64_t addr)
{
	char needle[32];
	char line[512];
	FILE* log;
	int found = 0;
	snprintf(needle, sizeof(needle), "%016llx", (unsigned long long)addr);
	log = fopen("/var/log/syslog", "r");
	if (log != NULL) {
		while (fgets(line, (int)sizeof(line), log) != NULL) {
			if (strstr(line, "vm fault") != NULL && strstr(line, needle) != NULL) {
				fputs(line, stdout);
				found = 1;
			}
		}
		fclose(log);
	}
	if (found)
		fprintf(stdout, "vm fault addr %s\n", needle);
	else
		fprintf(stdout, "vm fault not in syslog %s\n", needle);
	fflush(stdout);
}


static int
kill_loop(void)
{
	int fd = gpu_open();
	if (fd < 0) {
		fprintf(stderr, "render node not available\n");
		return 0;
	}
	for (;;) {
		uint32_t handle = 0;
		int32_t area = -1;
		void* cpu = NULL;
		if (bo_create(fd, &handle, &area, &cpu) != 0) {
			fprintf(stderr, "kill create errno %d\n", errno);
			snooze(100000);
			continue;
		}
		fprintf(stdout, "kill bo %u\n", handle);
		fflush(stdout);
		snooze(200000);
	}
}


static int
foreign(void)
{
	int fds[2];
	int fd;
	uint32_t handle = 0;
	int32_t area = -1;
	void* cpu = NULL;
	uint64_t va = 0x80000000ull;
	pid_t child;
	int status = 0;
	if (pipe(fds) != 0)
		return 1;
	fd = gpu_open();
	if (fd < 0) {
		fprintf(stderr, "render node not available\n");
		return 0;
	}
	if (ctx_create(fd) != 0 || bo_create(fd, &handle, &area, &cpu) != 0
		|| va_map(fd, handle, va, R5_GPU_VA_READ | R5_GPU_VA_WRITE) != 0) {
		fprintf(stderr, "owner map errno %d\n", errno);
		return 1;
	}
	memset(cpu, 0x5a, 4096);
	child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		uint64_t foreignVa = 0;
		int mine;
		uint32_t ibHandle = 0;
		int32_t ibArea = -1;
		void* ibCpu = NULL;
		uint64_t ibVa = 0x80001000ull;
		close(fds[1]);
		if (read(fds[0], &foreignVa, sizeof(foreignVa))
			!= (ssize_t)sizeof(foreignVa))
			_exit(2);
		mine = gpu_open();
		if (mine < 0)
			_exit(3);
		if (ctx_create(mine) != 0
			|| bo_create(mine, &ibHandle, &ibArea, &ibCpu) != 0
			|| va_map(mine, ibHandle, ibVa, kCommandFlags) != 0)
			_exit(4);
		fill_ib((uint32_t*)ibCpu, foreignVa, ibVa + 256ull);
		if (submit_write(mine, ibVa, 14) == 0)
			_exit(5);
		fprintf(stderr, "client errno %d\n", errno);
		// M10: the faulting job stalls the ring; the watchdog then resets
		// the GPU and marks this context lost (ECANCELED). The client's own
		// wait has the same 10 s limit and can return B_TIMED_OUT first
		// (M10 stick, boot 1). Either way the write was refused; the
		// owner's buffer is checked by the parent.
		_exit(errno == ECANCELED || errno == B_TIMED_OUT ? 0 : 6);
	}
	close(fds[0]);
	if (write(fds[1], &va, sizeof(va)) != (ssize_t)sizeof(va))
		return 1;
	close(fds[1]);
	{
		uint32_t ownHandle = 0;
		int32_t ownArea = -1;
		void* ownCpu = NULL;
		uint64_t ownIb = 0x80002000ull;
		if (bo_create(fd, &ownHandle, &ownArea, &ownCpu) != 0
			|| va_map(fd, ownHandle, ownIb, kCommandFlags) != 0)
			return 1;
		fill_ib((uint32_t*)ownCpu, ownIb + 256ull, ownIb + 512ull);
		if (submit_write(fd, ownIb, 14) != 0) {
			fprintf(stderr, "owner submit errno %d\n", errno);
			return 1;
		}
	}
	if (waitpid(child, &status, 0) != child)
		return 1;
	{
		const unsigned char* bytes = (const unsigned char*)cpu;
		for (uint32_t i = 0; i < 4096; i++) {
			if (bytes[i] != 0x5a) {
				fprintf(stderr, "owner changed at %u\n", i);
				return 1;
			}
		}
		fprintf(stdout, "owner unchanged\n");
	}
	print_fault(va);
	fprintf(stdout, "owner ok client %d\n",
		WIFEXITED(status) ? WEXITSTATUS(status) : status);
	fflush(stdout);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}


int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "kill") == 0)
		return kill_loop();
	return foreign();
}
