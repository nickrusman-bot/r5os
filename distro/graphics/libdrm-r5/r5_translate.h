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

#endif
