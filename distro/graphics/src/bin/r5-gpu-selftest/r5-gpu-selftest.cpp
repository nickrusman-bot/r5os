/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Login starts this after Deskbar. The result file is written even when
 * the render node is absent.
 */


#include "r5_gpu.h"

#include <FindDirectory.h>
#include <Path.h>

#include <OS.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>


struct r5_bo_alloc {
	uint64_t	alloc_size;
	uint64_t	phys_alignment;
	uint32_t	preferred_heap;
	uint64_t	flags;
};

struct r5_cs_ib {
	uint64_t	flags;
	uint64_t	ib_mc_address;
	uint32_t	size;
};

struct r5_cs_fence_info {
	void*		handle;
	uint64_t	offset;
};

struct r5_cs_request {
	uint64_t	flags;
	unsigned	ip_type;
	unsigned	ip_instance;
	uint32_t	ring;
	void*		resources;
	uint32_t	number_of_dependencies;
	void*		dependencies;
	uint32_t	number_of_ibs;
	r5_cs_ib*	ibs;
	uint64_t	seq_no;
	r5_cs_fence_info	fence_info;
};

struct r5_cs_fence {
	void*		context;
	uint32_t	ip_type;
	uint32_t	ip_instance;
	uint32_t	ring;
	uint64_t	fence;
};


static void*
symbol(void* library, const char* name)
{
	return dlsym(library, name);
}


static void
stage_bo(FILE* out, uint64_t lowVa, uint64_t highVa, bool submitIb)
{
	fprintf(out, "stage bo\n");
	void* library = dlopen("libdrm_amdgpu.so.1", RTLD_NOW);
	if (library == NULL) {
		fprintf(out, "not available, %s\n", dlerror());
		return;
	}
	typedef int (*init_fn)(int, uint32_t*, uint32_t*, void**);
	typedef int (*deinit_fn)(void*);
	typedef int (*bo_alloc_fn)(void*, r5_bo_alloc*, void**);
	typedef int (*bo_free_fn)(void*);
	typedef int (*va_op_fn)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint32_t);
	typedef int (*map_fn)(void*, void**);
	typedef int (*unmap_fn)(void*);
	typedef int (*ctx_fn)(void*, void**);
	typedef int (*ctx_free_fn)(void*);
	typedef int (*submit_fn)(void*, uint64_t, r5_cs_request*, uint32_t);
	typedef int (*fence_fn)(r5_cs_fence*, uint64_t, uint64_t, uint32_t*);
	init_fn doInit = (init_fn)symbol(library, "amdgpu_device_initialize");
	deinit_fn doDeinit = (deinit_fn)symbol(library, "amdgpu_device_deinitialize");
	bo_alloc_fn doAlloc = (bo_alloc_fn)symbol(library, "amdgpu_bo_alloc");
	bo_free_fn doFree = (bo_free_fn)symbol(library, "amdgpu_bo_free");
	va_op_fn doVa = (va_op_fn)symbol(library, "amdgpu_bo_va_op");
	map_fn doMap = (map_fn)symbol(library, "amdgpu_bo_cpu_map");
	unmap_fn doUnmap = (unmap_fn)symbol(library, "amdgpu_bo_cpu_unmap");
	ctx_fn doCtx = (ctx_fn)symbol(library, "amdgpu_cs_ctx_create");
	ctx_free_fn doCtxFree = (ctx_free_fn)symbol(library, "amdgpu_cs_ctx_free");
	submit_fn doSubmit = (submit_fn)symbol(library, "amdgpu_cs_submit");
	fence_fn doFence = (fence_fn)symbol(library, "amdgpu_cs_query_fence_status");
	if (doInit == NULL || doAlloc == NULL || doVa == NULL || doMap == NULL
		|| doSubmit == NULL || doFence == NULL) {
		fprintf(out, "not available, libdrm symbols\n");
		dlclose(library);
		return;
	}

	int saved = dup(2);
	fflush(out);
	if (saved >= 0)
		dup2(fileno(out), 2);

	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	uint32_t major = 0;
	uint32_t minor = 0;
	void* device = NULL;
	int ret = fd < 0 ? -1 : doInit(fd, &major, &minor, &device);
	fprintf(out, "device %d\n", ret);
	void* vram = NULL;
	void* gtt = NULL;
	void* context = NULL;
	if (ret == 0) {
		r5_bo_alloc request;
		memset(&request, 0, sizeof(request));
		request.alloc_size = 4096;
		request.preferred_heap = 4;
		ret = doAlloc(device, &request, &vram);
		fprintf(out, "GEM_CREATE vram %d\n", ret);
		request.preferred_heap = 2;
		ret = doAlloc(device, &request, &gtt);
		fprintf(out, "GEM_CREATE gtt %d\n", ret);
		if (vram != NULL) {
			ret = doVa(vram, 0, 4096, lowVa, 0, 1);
			fprintf(out, "GEM_VA low %d\n", ret);
		} else
			fprintf(out, "GEM_VA low not called\n");
		if (gtt != NULL) {
			ret = doVa(gtt, 0, 4096, highVa, 0, 1);
			fprintf(out, "GEM_VA high %d\n", ret);
		} else
			fprintf(out, "GEM_VA high not called\n");
		void* cpu = NULL;
		if (gtt != NULL && doMap != NULL) {
			ret = doMap(gtt, &cpu);
			fprintf(out, "GEM_MMAP %d\n", ret);
			if (ret == 0 && cpu != NULL) {
				uint32_t written = 0x5a5a5a5au;
				((uint32_t*)cpu)[0] = written;
				uint32_t readBack = ((uint32_t*)cpu)[0];
				fprintf(out, "cpu write %08x read %08x\n", written, readBack);
				/* Same WRITE_DATA as r5_gfx9_k2_gart_ib, aimed at the
				 * VMID 1 address highVa + 64. Linux gfx_v9 .nop is
				 * PACKET3(PACKET3_NOP, 0x3FFF). Mesa pads a GFX IB to
				 * a multiple of 8 dwords (ib_pad_dw_mask 0x7). The ring
				 * align_mask 0xff is the ring pointer, not this IB:
				 * 256 dwords would cover the write at +64.
				 * The first call leaves this out. A hang here would
				 * mark the GPU lost before the teapot runs.
				 */
				if (!submitIb)
					fprintf(out, "IB skipped\n");
				uint64_t word = highVa + 64;
				uint32_t* dst = (uint32_t*)cpu;
				dst[16] = 0;
				if (submitIb) {
				fprintf(out, "stage ib\n");
				uint32_t packet[8];
				packet[0] = (3u << 30) | (3u << 16) | (0x37u << 8);
				packet[1] = (5u << 8) | (1u << 20);
				packet[2] = (uint32_t)word;
				packet[3] = (uint32_t)(word >> 32);
				packet[4] = 0xDEADBEEFu;
				uint32_t nop = (3u << 30) | (0x3fffu << 16) | (0x10u << 8);
				packet[5] = nop;
				packet[6] = nop;
				packet[7] = nop;
				memcpy(dst, packet, sizeof(packet));
				if (doCtx != NULL)
					ret = doCtx(device, &context);
				else
					ret = -1;
				fprintf(out, "context %d\n", ret);
				if (context != NULL) {
					r5_cs_ib ib;
					memset(&ib, 0, sizeof(ib));
					ib.ib_mc_address = highVa;
					ib.size = 8;
					r5_cs_request requestCs;
					memset(&requestCs, 0, sizeof(requestCs));
					requestCs.number_of_ibs = 1;
					requestCs.ibs = &ib;
					ret = doSubmit(context, 0, &requestCs, 1);
					fprintf(out, "IB %d\n", ret);
					r5_cs_fence fence;
					memset(&fence, 0, sizeof(fence));
					fence.context = context;
					fence.fence = requestCs.seq_no;
					uint32_t expired = 0;
					int waited = doFence(&fence, 1000000000ull, 0, &expired);
					if (waited == 0 && expired != 0)
						fprintf(out, "fence ok\n");
					else
						fprintf(out, "fence timeout\n");
					fprintf(out, "gpu read %08x\n", dst[16]);
				} else
					fprintf(out, "IB not called\n");
				}
			}
		} else
			fprintf(out, "GEM_MMAP not called\n");
		if (gtt != NULL) {
			ret = doVa(gtt, 0, 4096, highVa, 0, 2);
			fprintf(out, "GEM_VA unmap %d\n", ret);
			ret = doVa(gtt, 0, 4096, highVa, 0, 1);
			fprintf(out, "GEM_VA remap %d\n", ret);
		}
		if (cpu != NULL && doUnmap != NULL)
			doUnmap(gtt);
		if (context != NULL && doCtxFree != NULL)
			doCtxFree(context);
		if (vram != NULL && doFree != NULL)
			doFree(vram);
		if (gtt != NULL && doFree != NULL)
			doFree(gtt);
		if (doDeinit != NULL)
			doDeinit(device);
		fprintf(out, "close 0\n");
	}
	fflush(out);
	fflush(stderr);
	if (saved >= 0) {
		dup2(saved, 2);
		close(saved);
	}
	dlclose(library);
}


static void
run_stage(FILE* out, const char* title, const char* command, int seconds)
{
	fprintf(out, "stage %s\n", title);
	fflush(out);
	int fds[2];
	if (pipe(fds) != 0) {
		fprintf(out, "not available, %s\n", strerror(errno));
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		fprintf(out, "not available, %s\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		close(fds[0]);
		close(fds[1]);
		execl("/bin/sh", "sh", "-c", command, (char*)NULL);
		_exit(127);
	}
	setpgid(pid, pid);
	close(fds[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	bigtime_t deadline = system_time() + (bigtime_t)seconds * 1000000;
	int status = 0;
	bool ended = false;
	bool killed = false;
	char buf[256];
	while (!ended) {
		ssize_t got = read(fds[0], buf, sizeof(buf) - 1);
		if (got > 0) {
			buf[got] = 0;
			fputs(buf, out);
			fflush(out);
		}
		pid_t wait = waitpid(pid, &status, WNOHANG);
		if (wait == pid) {
			ended = true;
			break;
		}
		if (system_time() >= deadline) {
			/* kill() with a positive id signals the team. The shell's
			 * child is another team in this process group. */
			kill(-pid, SIGKILL);
			kill_team(pid);
			waitpid(pid, &status, 0);
			fprintf(out, "killed, it did not exit\n");
			fflush(out);
			killed = true;
			ended = true;
			break;
		}
		snooze(50000);
	}
	char drain[256];
	ssize_t rest;
	while ((rest = read(fds[0], drain, sizeof(drain) - 1)) > 0) {
		drain[rest] = 0;
		fputs(drain, out);
	}
	close(fds[0]);
	if (!killed && ended)
		fprintf(out, "exit %d\n", status);
	fflush(out);
}


static pid_t
spawn_command(const char* command, int* readFd)
{
	int fds[2];
	pid_t pid;
	if (pipe(fds) != 0)
		return -1;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		close(fds[0]);
		close(fds[1]);
		execl("/bin/sh", "sh", "-c", command, (char*)NULL);
		_exit(127);
	}
	setpgid(pid, pid);
	close(fds[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	*readFd = fds[0];
	return pid;
}


static void
run_pair(FILE* out, const char* title, const char* first, const char* second,
	int seconds)
{
	int fdA = -1;
	int fdB = -1;
	pid_t a;
	pid_t b;
	int statusA = 0;
	int statusB = 0;
	bool doneA = false;
	bool doneB = false;
	bigtime_t deadline = system_time() + (bigtime_t)seconds * 1000000;
	fprintf(out, "stage %s\n", title);
	fflush(out);
	a = spawn_command(first, &fdA);
	b = spawn_command(second, &fdB);
	if (a < 0 || b < 0) {
		fprintf(out, "not available, %s\n", strerror(errno));
		if (a > 0)
			kill_team(a);
		if (b > 0)
			kill_team(b);
		if (fdA >= 0)
			close(fdA);
		if (fdB >= 0)
			close(fdB);
		return;
	}
	while (!doneA || !doneB) {
		char buf[256];
		ssize_t got;
		if (!doneA && (got = read(fdA, buf, sizeof(buf) - 1)) > 0) {
			buf[got] = 0;
			fputs(buf, out);
		}
		if (!doneB && (got = read(fdB, buf, sizeof(buf) - 1)) > 0) {
			buf[got] = 0;
			fputs(buf, out);
		}
		if (!doneA && waitpid(a, &statusA, WNOHANG) == a)
			doneA = true;
		if (!doneB && waitpid(b, &statusB, WNOHANG) == b)
			doneB = true;
		if (system_time() >= deadline) {
			if (!doneA) {
				kill(-a, SIGKILL);
				kill_team(a);
				waitpid(a, &statusA, 0);
				doneA = true;
			}
			if (!doneB) {
				kill(-b, SIGKILL);
				kill_team(b);
				waitpid(b, &statusB, 0);
				doneB = true;
			}
			fprintf(out, "killed, it did not exit\n");
			break;
		}
		snooze(50000);
	}
	close(fdA);
	close(fdB);
	fprintf(out, "exit %d %d\n", statusA, statusB);
	fflush(out);
}


static void
write_command(FILE* out, const char* title, const char* command)
{
	fprintf(out, "stage %s\n", title);
	FILE* pipe = popen(command, "r");
	if (pipe == NULL) {
		fprintf(out, "not available, %s\n", strerror(errno));
		return;
	}
	char line[512];
	bool any = false;
	while (fgets(line, sizeof(line), pipe) != NULL) {
		fputs(line, out);
		any = true;
	}
	int status = pclose(pipe);
	if (!any)
		fprintf(out, "no output, status %d\n", status);
}


static bool
device_lost()
{
	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	r5_gpu_info info;
	if (fd < 0)
		return false;
	memset(&info, 0, sizeof(info));
	info.size = (uint32_t)sizeof(info);
	info.version = 1;
	if (ioctl(fd, R5_GPU_GET_INFO, &info, sizeof(info)) != 0) {
		close(fd);
		return false;
	}
	close(fd);
	return (info.capabilities & R5_GPU_CAP_LOST) != 0;
}


static void
skip_lost(FILE* out, const char* title)
{
	fprintf(out, "stage %s\nskipped, gpu lost\n", title);
	fflush(out);
}


static void
stage_p(FILE* out)
{
	fprintf(out, "stage P\n");
	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	r5_gpu_stage_p result;
	const char* how[] = {
		"ok", "timeout", "priv", "skipped, gpu lost", "room"
	};
	if (fd < 0) {
		fprintf(out, "not available, no supported GPU\n");
		return;
	}
	memset(&result, 0, sizeof(result));
	result.size = (uint32_t)sizeof(result);
	result.version = 1;
	int ret = ioctl(fd, R5_GPU_STAGE_P, &result, sizeof(result));
	close(fd);
	if (ret != 0 && result.count == 0) {
		fprintf(out, "R5_GPU_STAGE_P %d errno %d\n", ret, errno);
		return;
	}
	for (uint32_t i = 0; i < result.count && i < R5_GPU_P_STEPS; i++) {
		uint32_t status = result.status[i];
		if (status > R5_GPU_P_ROOM)
			status = R5_GPU_P_SKIP;
		fprintf(out, "p%u %s %s\n", i + 1, result.name[i], how[status]);
		if (status == R5_GPU_P_PRIV || status == R5_GPU_P_TIMEOUT
			|| status == R5_GPU_P_ROOM) {
			fprintf(out, "skipped, gpu lost\n");
			break;
		}
	}
	fflush(out);
}


static void
stage_selftest(FILE* out)
{
	fprintf(out, "stage selftest\n");
	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (fd < 0) {
		fprintf(out, "not available, no supported GPU\n");
		return;
	}
	int ret = ioctl(fd, R5_GPU_SELFTEST, NULL, 0);
	fprintf(out, "R5_GPU_SELFTEST %d errno %d\n", ret, errno);
	close(fd);
}


int
main()
{
	BPath cache;
	if (find_directory(B_USER_CACHE_DIRECTORY, &cache) != B_OK)
		return 1;
	cache.Append("r5-gpu-selftest.txt");
	FILE* out = fopen(cache.Path(), "w");
	if (out == NULL)
		return 1;

	int fd = open("/dev/gpu/radeon_hd/0", O_RDWR);
	if (fd < 0) {
		fprintf(out, "stage render node: not available, no supported GPU\n");
		fprintf(out, "stage bo: not available, no supported GPU\n");
		fprintf(out, "stage vulkaninfo: not available, no supported GPU\n");
		write_command(out, "VulkanTeapot",
			"/bin/VulkanTeapot --selftest 2>&1");
	} else {
		uint8_t bytes[sizeof(r5_gpu_info) + sizeof(r5_gpu_amd_info)];
		memset(bytes, 0, sizeof(bytes));
		r5_gpu_info* info = (r5_gpu_info*)bytes;
		info->size = (uint32_t)sizeof(bytes);
		info->version = 1;
		if (ioctl(fd, R5_GPU_GET_INFO, bytes, sizeof(bytes)) != 0) {
			fprintf(out, "stage render node: GET_INFO failed\n");
		} else {
			r5_gpu_amd_info* amd = (r5_gpu_amd_info*)(bytes + sizeof(r5_gpu_info));
			fprintf(out, "stage render node: api %u.%u device %04x cu %u\n",
				info->api_major, info->api_minor, info->device,
				amd->device.cu_active_number);
			fprintf(out, "counters vram %llu gtt %llu\n",
				(unsigned long long)info->vram_usage,
				(unsigned long long)info->gtt_usage);
			fprintf(out, "firmware me %u feature %u pfp %u mec %u\n",
				amd->firmware[0x04].ver, amd->firmware[0x04].feature,
				amd->firmware[0x05].ver, amd->firmware[0x08].ver);
			fprintf(out, "rb num %u mask %x\n",
				amd->device.num_rb_pipes,
				amd->device.enabled_rb_pipes_mask);
			r5_amd_device passport;
			memset(&passport, 0, sizeof(passport));
			passport.device_id = amd->device.device_id;
			passport.chip_rev = amd->device.chip_rev;
			passport.external_rev = amd->device.external_rev;
			passport.family = amd->device.family;
			passport.num_shader_engines = amd->device.num_shader_engines;
			passport.num_shader_arrays_per_engine =
				amd->device.num_shader_arrays_per_engine;
			passport.num_cu_per_sh = amd->device.num_cu_per_sh;
			passport.cu_active_number = amd->device.cu_active_number;
			passport.cu_bitmap = amd->device.cu_bitmap[0][0];
			passport.num_rb_pipes = amd->device.num_rb_pipes;
			passport.enabled_rb_pipes_mask = amd->device.enabled_rb_pipes_mask;
			passport.num_hw_gfx_contexts = amd->device.num_hw_gfx_contexts;
			passport.ce_ram_size = amd->device.ce_ram_size;
			passport.pte_fragment_size = amd->device.pte_fragment_size;
			passport.max_engine_clock = (uint32_t)amd->device.max_engine_clock;
			passport.min_engine_clock = (uint32_t)amd->device.min_engine_clock;
			passport.max_memory_clock = (uint32_t)amd->device.max_memory_clock;
			passport.min_memory_clock = (uint32_t)amd->device.min_memory_clock;
			passport.vram_type = amd->device.vram_type;
			passport.vram_bit_width = amd->device.vram_bit_width;
			passport.gpu_counter_freq = (uint32_t)amd->device.gpu_counter_freq;
			passport.gc_double_offchip_lds_buf =
				amd->device.gc_double_offchip_lds_buf;
			passport.num_shader_visible_vgprs =
				amd->device.num_shader_visible_vgprs;
			passport.num_tcc_blocks = amd->device.num_tcc_blocks;
			passport.gs_vgt_table_depth = amd->device.gs_vgt_table_depth;
			passport.gs_prim_buffer_depth = amd->device.gs_prim_buffer_depth;
			passport.max_gs_waves_per_vgt = amd->device.max_gs_waves_per_vgt;
			passport.hw_major =
				amd->hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_major;
			passport.hw_minor =
				amd->hw_ip[R5_AMDGPU_HW_IP_GFX].hw_ip_version_minor;
			passport.fw_ver[4] = amd->firmware[4].ver;
			passport.fw_feature[4] = amd->firmware[4].feature;
			char text[4096];
			r5_amd_passport_format(&passport, text, (int)sizeof(text));
			fputs(text, out);
		}
		close(fd);
		uint64_t lowVa = R5_GPU_VA_OFFSET;
		uint64_t highVa = R5_GPU_HIGH_VA_OFFSET;
		r5_gpu_info* reported = (r5_gpu_info*)bytes;
		if (reported->va_offset != 0)
			lowVa = reported->va_offset;
		if (reported->high_va_offset != 0)
			highVa = reported->high_va_offset;
		stage_bo(out, lowVa, highVa, false);
		stage_p(out);
		if (device_lost()) {
			skip_lost(out, "vulkaninfo");
			skip_lost(out, "VulkanTeapot (sam)");
			skip_lost(out, "VulkanTeapot");
			skip_lost(out, "selftest");
			skip_lost(out, "ib");
		} else {
		run_stage(out, "vulkaninfo", "/bin/vulkaninfo --summary 2>&1", 120);
		if (device_lost()) {
			skip_lost(out, "VulkanTeapot (sam)");
			skip_lost(out, "VulkanTeapot");
			skip_lost(out, "selftest");
			skip_lost(out, "ib");
		} else {
			run_stage(out, "VulkanTeapot (sam)",
				"RADV_PERFTEST=sam /bin/VulkanTeapot --selftest 2>&1", 120);
			if (device_lost()) {
				skip_lost(out, "VulkanTeapot");
				skip_lost(out, "selftest");
				skip_lost(out, "ib");
			} else {
				run_stage(out, "VulkanTeapot",
					"/bin/VulkanTeapot --selftest 2>&1", 120);
				run_pair(out, "VulkanTeapot pair",
					"/bin/VulkanTeapot --selftest 2>&1",
					"/bin/VulkanTeapot --selftest 2>&1", 120);
				run_stage(out, "isolation",
					"/bin/r5-gpu-isolation 2>&1", 60);
				run_stage(out, "kill",
					"/bin/r5-gpu-isolation kill 2>&1", 2);
				{
					int again = open("/dev/gpu/radeon_hd/0", O_RDWR);
					r5_gpu_info after;
					memset(&after, 0, sizeof(after));
					after.size = (uint32_t)sizeof(after);
					after.version = 1;
					if (again >= 0 && ioctl(again, R5_GPU_GET_INFO, &after,
							sizeof(after)) == 0) {
						fprintf(out, "counters after kill vram %llu gtt %llu\n",
							(unsigned long long)after.vram_usage,
							(unsigned long long)after.gtt_usage);
					}
					if (again >= 0)
						close(again);
				}
				if (device_lost()) {
					skip_lost(out, "selftest");
					skip_lost(out, "ib");
				} else {
					stage_selftest(out);
					if (device_lost())
						skip_lost(out, "ib");
					else
						stage_bo(out, lowVa, highVa, true);
				}
			}
		}
		}
	}
	fclose(out);
	return 0;
}
