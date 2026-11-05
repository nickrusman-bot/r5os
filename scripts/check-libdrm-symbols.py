#!/usr/bin/env python3
"""Compare undefined libdrm symbols in a Mesa binary with the R5 libdrm exports."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/nickr/r5os")
LIBS = [
    ROOT / "r5-sysroot/lib/libdrm.so.2",
    ROOT / "r5-sysroot/lib/libdrm_amdgpu.so.1",
]
BINARIES = [
    ROOT / "mesa-build/mesa-25.3.6/build-radv/src/amd/vulkan/libvulkan_radeon.so",
    ROOT / "mesa-build/mesa-25.3.6/build-radv/src/gallium/targets/lavapipe/libvulkan_lvp.so",
]


def dynamic(path):
    text = subprocess.check_output(["nm", "-D", "--defined-only", str(path)], text=True)
    names = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            names.add(parts[-1])
    return names


def undefined(path):
    text = subprocess.check_output(["nm", "-D", "-u", str(path)], text=True)
    names = []
    for line in text.splitlines():
        parts = line.split()
        if parts:
            names.append(parts[-1])
    return names


# Haiku's libc names the syscall __ioctl. A direct ioctl() on the DRM fd is
# rejected. The only allowed request is SYNC_IOC_MERGE (0xc0303e03) on a
# sync-file descriptor: radv_amdgpu_winsys_cs_submit, wsi_create_sync_for_image_syncobj,
# and vk_drm_syncobj_copy_payloads merge two sync files. That fd is not the
# render node, and drmIoctl would not translate it.
ALLOWED_IOCTL = {0xC0303E03}
REG_ALIAS = {
    "eax": "rax", "ebx": "rbx", "ecx": "rcx", "edx": "rdx",
    "esi": "rsi", "edi": "rdi", "ebp": "rbp", "esp": "rsp",
    "r8d": "r8", "r9d": "r9", "r10d": "r10", "r11d": "r11",
    "r12d": "r12", "r13d": "r13", "r14d": "r14", "r15d": "r15",
}


def canonical(name):
    return REG_ALIAS.get(name, name)


def ioctl_requests(binary):
    text = subprocess.check_output(["objdump", "-d", str(binary)], text=True, errors="replace")
    regs = {}
    found = []
    imm = re.compile(r"mov\s+\$(0x[0-9a-f]+),%([a-z0-9]+)")
    copy = re.compile(r"mov\s+%([a-z0-9]+),%([a-z0-9]+)")
    for line in text.splitlines():
        if re.search(r"<.*>:$", line):
            regs = {}
            continue
        match = imm.search(line)
        if match:
            regs[canonical(match.group(2))] = int(match.group(1), 16)
            continue
        match = copy.search(line)
        if match:
            src = canonical(match.group(1))
            dst = canonical(match.group(2))
            if src in regs:
                regs[dst] = regs[src]
            else:
                regs.pop(dst, None)
            continue
        if "__ioctl@plt" in line:
            found.append(regs.get("rsi"))
            continue
        written = re.search(r",%([a-z0-9]+)\s*$", line)
        if written:
            regs.pop(canonical(written.group(1)), None)
    return found


def main():
    exports = set()
    for lib in LIBS:
        exports |= dynamic(lib)
    missing = []
    for binary in BINARIES:
        if not binary.exists():
            print(f"absent {binary}")
            continue
        for name in undefined(binary):
            if name.startswith("drm") or name.startswith("amdgpu_"):
                if name not in exports:
                    missing.append(f"{binary.name} {name}")
    ioctl_hits = []
    for binary in BINARIES:
        if not binary.exists():
            continue
        names = set(undefined(binary))
        if "ioctl" in names:
            ioctl_hits.append(f"{binary.name} ioctl")
        if "__ioctl" not in names:
            continue
        requests = ioctl_requests(binary)
        if not requests or any(item not in ALLOWED_IOCTL for item in requests):
            ioctl_hits.append(f"{binary.name} __ioctl {requests}")
    if ioctl_hits:
        print("ioctl")
        print("\n".join(ioctl_hits))
        print("Mesa drm_ioctl must call drmIoctl on Haiku.")
        print("The only allow-list entry is __ioctl for SYNC_IOC_MERGE.")
        return 1
    if missing:
        print("missing")
        print("\n".join(missing))
        return 1
    print("ok libdrm symbols")
    return 0


if __name__ == "__main__":
    sys.exit(main())
