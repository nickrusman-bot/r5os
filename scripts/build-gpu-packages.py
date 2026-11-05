#!/usr/bin/env python3
"""Pack the R5 libdrm, RADV, and the EGL library that contains Zink."""
import shutil
import subprocess
from pathlib import Path

HAIKU = Path("/home/nickr/r5os/haiku")
PKG = HAIKU / "generated.x86_64/objects/linux/x86_64/release/tools/package/package"
OUT = Path("/mnt/c/Users/nickr/Projects/R5OS/distro/packages/x86_64")
SYS = Path("/home/nickr/r5os/r5-sysroot/lib")
RADV = Path("/home/nickr/r5os/mesa-build/mesa-25.3.6/build-radv")
STAGE = Path("/tmp/r5-gpu-pkg")


def info(path, text):
    path.write_text(text)


def create(root, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        dest.unlink()
    subprocess.check_call([str(PKG), "create", "-C", str(root), str(dest)])
    print("created", dest)


def libdrm():
    root = STAGE / "libdrm"
    shutil.rmtree(root, ignore_errors=True)
    lib = root / "lib"
    lib.mkdir(parents=True)
    for name in ("libdrm.so.2.124.0", "libdrm_amdgpu.so.1.124.0"):
        shutil.copy2(SYS / name, lib / name)
    (lib / "libdrm.so.2").symlink_to("libdrm.so.2.124.0")
    (lib / "libdrm_amdgpu.so.1").symlink_to("libdrm_amdgpu.so.1.124.0")
    info(root / ".PackageInfo", """name\t\t\tlibdrm
version\t\t\t2.4.124-1
architecture\t\tx86_64
summary\t\t\t"R5 libdrm for the AMD render node"
description\t\t"Translates AMDGPU ioctls onto the R5 GPU interface."
packager\t\t"R5OS <r5os@localhost>"
vendor\t\t\t"R5OS"
copyrights\t\t"1999 Precision Insight, Inc."
licenses\t\t"MIT"
provides {
\tlibdrm = 2.4.124
\tlib:libdrm.so.2 = 2.4.124
\tlib:libdrm_amdgpu.so.1 = 2.4.124
}
requires {
\thaiku >= r1~beta5_hrev57937_5-1
}
""")
    create(root, OUT / "libdrm-2.4.124-1-x86_64.hpkg")


def radv():
    root = STAGE / "radv"
    shutil.rmtree(root, ignore_errors=True)
    lib = root / "lib"
    icd = root / "add-ons/vulkan/icd.d"
    lib.mkdir(parents=True)
    icd.mkdir(parents=True)
    shutil.copy2(RADV / "src/amd/vulkan/libvulkan_radeon.so", lib / "libvulkan_radeon.so")
    (icd / "radeon_icd.json").write_text(
        "{\n"
        '\t"file_format_version": "1.0.0",\n'
        '\t"ICD": {\n'
        '\t\t"library_path": "/boot/system/lib/libvulkan_radeon.so",\n'
        '\t\t"api_version": "1.4.328"\n'
        "\t}\n"
        "}\n"
    )
    info(root / ".PackageInfo", """name\t\t\tmesa_radv
version\t\t\t25.3.6-1
architecture\t\tx86_64
summary\t\t\t"Mesa RADV Vulkan driver"
description\t\t"AMD Vulkan driver. The loader uses it when the render node is present. Lavapipe stays installed."
packager\t\t"R5OS <r5os@localhost>"
vendor\t\t\t"R5OS"
copyrights\t\t"1999-2026 Brian Paul and others"
licenses\t\t"MIT"
provides {
\tmesa_radv = 25.3.6
}
requires {
\thaiku >= r1~beta5_hrev57937_5-1
\tlib:libdrm.so.2
\tlib:libdrm_amdgpu.so.1
\tlib:libllvm_21 >= 21
\tlib:libz >= 1.3.1
}
""")
    create(root, OUT / "mesa_radv-25.3.6-1-x86_64.hpkg")


def zink():
    root = STAGE / "zink"
    shutil.rmtree(root, ignore_errors=True)
    vendor = root / "add-ons/opengl/egl_vendor.d"
    vendor.mkdir(parents=True)
    src = RADV / "src/egl/libEGL_mesa.so.0.0.0"
    shutil.copy2(src, vendor / "libEGL_mesa.so.0.0.0")
    (vendor / "libEGL_mesa.so.0").symlink_to("libEGL_mesa.so.0.0.0")
    (vendor / "libEGL_mesa.so").symlink_to("libEGL_mesa.so.0")
    (vendor / "50_mesa.json").write_text(
        "{\n"
        '\t"file_format_version" : "1.0.0",\n'
        '\t"ICD" : {\n'
        '\t\t"library_path" : "./libEGL_mesa.so.0"\n'
        "\t}\n"
        "}\n"
    )
    info(root / ".PackageInfo", """name\t\t\tmesa
version\t\t\t25.3.6-2
architecture\t\tx86_64
summary\t\t\t"Mesa EGL with llvmpipe and Zink"
description\t\t"EGL vendor for libglvnd. llvmpipe is the default. GALLIUM_DRIVER=zink selects Zink."
packager\t\t"R5OS <r5os@localhost>"
vendor\t\t\t"R5OS"
copyrights\t\t"1999-2026 Brian Paul and others"
licenses\t\t"MIT"
provides {
\tmesa = 25.3.6
\tmesa_zink = 25.3.6
\tmesa_eglvnd = 25.3.6
}
requires {
\thaiku >= r1~beta5_hrev57937_5-1
\tlib:libllvm_21 >= 21
\tlib:libz >= 1.3.1
\tlib:libdrm.so.2
\tlib:libexpat >= 1
\tlib:libstdc++ >= 6
\tvulkan_loader >= 1.4.367
}
""")
    create(root, OUT / "mesa-25.3.6-2-x86_64.hpkg")


if __name__ == "__main__":
    libdrm()
    radv()
    zink()
