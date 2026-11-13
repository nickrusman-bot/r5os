/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * AMDGPU ioctl numbers become R5 GPU calls. The caller is libdrm.
 */
#ifndef R5_TRANSLATE_H
#define R5_TRANSLATE_H

#include <stddef.h>
#include <stdint.h>

struct _drmDevice;

/* 0 and -1 are drmIoctl results. 1 means this request is not translated.
 * On Haiku the translator does not return 1: an unknown number is ENOSYS. */
#define R5_DRM_PASS 1

int r5_drm_ioctl(int fd, unsigned long request, void *arg, size_t size);
int r5_drm_get_devices2(uint32_t flags, struct _drmDevice *devices[], int max_devices);
int r5_drm_get_device2(int fd, uint32_t flags, struct _drmDevice **device);
int r5_bo_cpu_map(int fd, unsigned int handle, void **cpu, int *area);
int r5_bo_cpu_unmap(int fd, int area);

/* M11 step 3b: buffers shared with app_server (docs/APP-SERVER-
 * COMPOSITOR.md, "The R5 DRM library and the fifth libdrm patch" and
 * "Decided in step 3b"). The number is 16 bytes, opaque. */
/* The program: the export number behind an fd from PRIME_HANDLE_TO_FD. */
int r5_drm_fd_to_token(int fd, uint8_t token[16]);
/* app_server: a local fd for a number that came in the message, for
 * VkImportMemoryFdInfoKHR, and an id of its own for r5_drm_mark_get. The
 * import happens when RADV sends PRIME_FD_TO_HANDLE with that fd. device_fd
 * is not used in version 1; pass -1. */
int r5_drm_token_to_fd(int device_fd, const uint8_t token[16],
	int32_t client_team, int *fd, uint32_t *id);
/* libdrm's amdgpu_bo_import (fifth R5 patch): the size of an imported fd
 * from BO_IMPORT, instead of lseek. -1 when the fd was not imported. */
int64_t r5_drm_import_bytes(int fd);
/* app_server: BO_PRESENT_MARK GET on the device fd and handle of that
 * import. Fails once GEM_CLOSE closed that handle, and for an id that
 * never imported. */
int r5_drm_mark_get(uint32_t id, uint64_t *sequence);

#endif
