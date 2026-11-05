#!/usr/bin/env python3
"""Haiku edits for libdrm 2.4.124 xf86drm.c.

Haiku has no makedev. The render node is /dev/gpu/radeon_hd/<n>, not /dev/dri.
AMDGPU ioctl translation is not in this edit. Run it on a clean extract.
"""
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
old = """#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif
"""
new = """#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif
#if defined(__HAIKU__)
static inline unsigned int major(unsigned long dev)
{
    return (unsigned int)((dev >> 8) & 0xffu);
}
static inline unsigned int minor(unsigned long dev)
{
    return (unsigned int)(dev & 0xffu);
}
static inline unsigned long makedev(unsigned int maj, unsigned int minor_dev)
{
    return ((unsigned long)maj << 8) | minor_dev;
}
#endif
"""
if old not in text:
    raise SystemExit("include anchor missing")
text = text.replace(old, new, 1)
old = """    if (!dev_name)
        return -EINVAL;

    sprintf(buf, dev_name, DRM_DIR_NAME, minor);
"""
new = """    if (!dev_name)
        return -EINVAL;

#if defined(__HAIKU__)
    snprintf(buf, sizeof(buf), "/dev/gpu/radeon_hd/%d", minor);
    if ((fd = open(buf, O_RDWR | O_CLOEXEC)) >= 0)
        return fd;
#endif
    sprintf(buf, dev_name, DRM_DIR_NAME, minor);
"""
if old not in text:
    raise SystemExit("open anchor missing")
path.write_text(text.replace(old, new, 1))
print(f"patched {path}")
