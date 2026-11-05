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
