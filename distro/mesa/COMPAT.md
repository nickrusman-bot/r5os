# Mesa 25.3.6 host build notes

Each row is a compile or configure error and the fix. Compat headers live in
`distro/mesa/compat/include` and are copied to `/home/nickr/r5os/r5-sysroot/include`.

| Error | Fix |
|---|---|
| `glslangValidator` not found | `distro/mesa/native.ini` points at the existing binary |
| Python mako missing for `/usr/bin/python3` | mako 1.3.10 unpacked into `~/.local/lib/python3.14/site-packages` |
| LLVM component `bitreader` missing | `mesa-build/bin/llvm-config` lists `bitreader` |
| `radv_physical_device.c:2143` `major`/`minor` undeclared | `sys/sysmacros.h` in the R5 sysroot. Meson then sets `MAJOR_IN_SYSMACROS` |
| `libsync.h:57` `_IOWR` undeclared | `r5/haiku_ioc.h` forced with `-include` from the cross file. The sync-file ioctls are not implemented |
| `libvulkan_radeon.so` undefined `drmGetVersion`, `drmGetDevice2`, `vk_drm_syncobj_*` | Haiku is not in Mesa's KMS list, so `dep_libdrm` was a null dependency and `vk_drm_syncobj.c` was not built. `patches/haiku-libdrm-dep.patch` looks up libdrm when RADV is enabled. After that, `ninja` exited 0 |
| Zink compiled to `libzink.a` and was not linked. After linking, `driParseConfigFiles` was undefined | Haiku EGL only linked `driver_swrast`. `patches/haiku-zink-link.patch` links `driver_zink` and `idep_xmlconfig`, and includes `zink_public.h` before `inline_sw_helper.h`. `GALLIUM_DRIVER=zink` then selects it. llvmpipe stays the default |
| `radv_amdgpu_winsys.c` treats `ac_drm_query_info` as `< 0` | `os_drm.h` returns `-errno`, which is positive on Haiku. `patches/haiku-errno-nonzero.patch` tests `!= 0`. Syncobj helpers that return `drmIoctl`'s `-1` are unchanged |
| RADV calls `drm_ioctl()` in `os_drm.h` | On Haiku that helper calls `drmIoctl`, so the Linux request reaches the R5 translation. `patches/haiku-os-drm-ioctl.patch`. The remaining `__ioctl` calls are `SYNC_IOC_MERGE` on a sync-file descriptor, not the render node |
| Zink frames never reached a window: `haiku_swap_buffers()` presents with `flush_frontbuffer()`, and `zink_flush_frontbuffer()` returns at once without a kopper swapchain. GLTeapot with Zink on Lavapipe showed a white window | `patches/haiku-zink-present.patch` (M9, package `mesa-25.3.6-3`): the colour buffers of a Zink screen have no `PIPE_BIND_DISPLAY_TARGET`, and `hgl_buffer.present` reads the back buffer into a `BBitmap` for `BitmapHook::SetBitmap()`. The same patch adds the driver choice (`gpu_opengl true`). `build-radv.sh` applies it with a grep guard. `docs/GPU-OPENGL.md` |
| `vulkaninfo` `mutex->owner == -1` in `radv_unregister_queue` | A zeroed `pthread_mutex_t` is not initialised on Haiku. `patches/haiku-sqtt-mutex.patch` initialises the SQTT mutexes for every device, because teardown locks them when RGP is off |
| Zink only with `gpu_opengl true`, RADV presenting only with `gpu_present true`, even on a GPU that passed its checks | M10: `patches/haiku-zink-available.patch` (package `mesa-25.3.6-4`) and `wsi/wsi_common_haiku.cpp` (`mesa_radv-25.3.6-6`) read `R5_GPU_CAP_AVAILABLE` from the first 32 bytes of `GET_INFO`. With the settings silent the bit decides; `false` still turns each off. `docs/GPU-OPENGL.md` |
