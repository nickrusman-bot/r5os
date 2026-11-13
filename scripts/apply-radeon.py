#!/usr/bin/env python3
"""Copy the R5OS radeon_hd sources into the Haiku tree.

The files under distro/graphics/ keep the Haiku MIT header. R5OS changes are
marked with SPDX-Snippet and are MIT. They replace the same path
in the Haiku tree. A missing source file fails the apply.
"""
from __future__ import annotations

import hashlib
import pathlib
import shutil
import sys

# The hashes recorded in docs/LICENSING.md and THIRD_PARTY_NOTICES.md: the
# unmodified Linux v6.12 files with LF line ends. The check hashes with LF
# line ends, so a Windows checkout with core.autocrlf (CRLF) also passes.
CLEARSTATE_SHA256 = {
    "src/add-ons/accelerants/radeon_hd/clearstate_defs.h":
        "70eb9f9a57d2cacb2ee2d7b9f17485384019ac03c845ce4bea941acdae137285",
    "src/add-ons/accelerants/radeon_hd/clearstate_gfx9.h":
        "e885c7f6475efd74e7729688a70992ff9543979363eb029c628bd95627fb34b5",
}

FILES = (
    "src/add-ons/accelerants/radeon_hd/accelerant.cpp",
    "src/add-ons/accelerants/radeon_hd/connector.cpp",
    "src/add-ons/accelerants/radeon_hd/display.cpp",
    "src/add-ons/accelerants/radeon_hd/mode.cpp",
    "src/add-ons/accelerants/radeon_hd/gpu.cpp",
    "src/add-ons/accelerants/radeon_hd/gfx9_compute.s",
    "src/add-ons/accelerants/radeon_hd/gfx9_compute_shader.h",
    "src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h",
    "src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp",
    "src/add-ons/accelerants/radeon_hd/common/gfx9_start.h",
    "src/add-ons/accelerants/radeon_hd/common/gfx9_check.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_gfx9_firmware.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_vm.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_cs.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_log.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_hang_ib.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_stage_r1.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_gc9_regs.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_inv_range.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_vm_size.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_pasid.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_vmid.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_defer.h",
    "src/add-ons/accelerants/radeon_hd/common/r5_agp.h",
    "src/add-ons/accelerants/radeon_hd/Jamfile",
    "src/add-ons/accelerants/radeon_hd/clearstate_defs.h",
    "src/add-ons/accelerants/radeon_hd/clearstate_gfx9.h",
    "src/add-ons/accelerants/radeon_hd/pll.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/driver.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/device.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/r5_passport.cpp",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/r5_passport.h",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/r5_smu12.cpp",
    "src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_core.cpp",
    "src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_backend.h",
    "src/add-ons/kernel/drivers/graphics/r5gpu/r5_gpu_internal.h",
    "headers/private/r5gpu/r5_gpu.h",
    "headers/private/r5gpu/r5_span.h",
    "headers/private/r5gpu/r5_wait.h",
    "headers/private/r5gpu/r5_hang.h",
    "headers/private/r5gpu/r5_gpu_checks.h",
    "headers/private/r5gpu/r5_sha256.h",
    "headers/private/r5gpu/r5_vram_pool.h",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/Jamfile",
    "src/add-ons/kernel/drivers/graphics/radeon_hd/radeon_hd_private.h",
    "headers/private/graphics/radeon_hd/radeon_hd.h",
    "src/bin/gfxinfo/Jamfile",
    "src/bin/gfxinfo/gfxinfo.cpp",
    "src/bin/r5gpuinfo/Jamfile",
    "src/bin/r5gpuinfo/r5gpuinfo.cpp",
    "src/bin/r5-gpu-selftest/Jamfile",
    "src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
    "src/bin/r5-gpu-selftest/r5_selftest_ib.h",
    "src/bin/r5-gpu-isolation/r5_isolation_ib.h",
    "src/bin/r5-gpu-isolation/Jamfile",
    "src/bin/r5-gpu-isolation/r5-gpu-isolation.cpp",
    "src/bin/r5-wsi-test/Jamfile",
    "src/bin/r5-wsi-test/r5-wsi-test.cpp",
)


def hook_gfxinfo(haiku: pathlib.Path) -> None:
    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin gfxinfo ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin usb_audiotest ;\n"
        if anchor not in text:
            anchor = "SubInclude HAIKU_TOP src bin midi2test ;\n"
        if anchor not in text:
            anchor = "SubInclude HAIKU_TOP src bin writembr ;\n"
        if anchor not in text:
            raise SystemExit("gfxinfo anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile")
    else:
        print("already src/bin/Jamfile")

    regular = haiku / "build" / "jam" / "images" / "definitions" / "regular"
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += gfxinfo ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += usb_audiotest ;\n"
        if anchor not in text:
            anchor = "SYSTEM_BIN += midi2test ;\n"
        if anchor not in text:
            raise SystemExit("gfxinfo anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image")
    else:
        print("already regular image")

    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin r5gpuinfo ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin gfxinfo ;\n"
        if anchor not in text:
            raise SystemExit("r5gpuinfo anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile r5gpuinfo")
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += r5gpuinfo ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += gfxinfo ;\n"
        if anchor not in text:
            raise SystemExit("r5gpuinfo anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image r5gpuinfo")

    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin r5-gpu-selftest ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin r5gpuinfo ;\n"
        if anchor not in text:
            raise SystemExit("selftest anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile selftest")
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += r5-gpu-selftest ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += r5gpuinfo ;\n"
        if anchor not in text:
            raise SystemExit("selftest anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image selftest")

    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin r5-gpu-isolation ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin r5-gpu-selftest ;\n"
        if anchor not in text:
            raise SystemExit("isolation anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile isolation")
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += r5-gpu-isolation ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += r5-gpu-selftest ;\n"
        if anchor not in text:
            raise SystemExit("isolation anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image isolation")

    jam = haiku / "src" / "bin" / "Jamfile"
    text = jam.read_text(encoding="utf-8")
    line = "SubInclude HAIKU_TOP src bin r5-wsi-test ;\n"
    if line not in text:
        anchor = "SubInclude HAIKU_TOP src bin r5-gpu-isolation ;\n"
        if anchor not in text:
            raise SystemExit("wsi-test anchor missing in src/bin/Jamfile")
        jam.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched src/bin/Jamfile wsi-test")
    text = regular.read_text(encoding="utf-8")
    line = "SYSTEM_BIN += r5-wsi-test ;\n"
    if line not in text:
        anchor = "SYSTEM_BIN += r5-gpu-isolation ;\n"
        if anchor not in text:
            raise SystemExit("wsi-test anchor missing in regular image")
        regular.write_text(text.replace(anchor, anchor + line, 1), encoding="utf-8")
        print("patched regular image wsi-test")

    launch = haiku / "data" / "launch" / "user"
    text = launch.read_text(encoding="utf-8")
    job = (
        "\tjob x-vnd.R5OS-gpu-selftest {\n"
        "\t\tlaunch /system/bin/r5-gpu-selftest\n"
        "\t\tif setting ~/config/settings/kernel/drivers/radeon_hd gpu_selftest\n"
        "\t\trequires x-vnd.Be-TSKB\n"
        "\t\tno_safemode\n"
        "\t}\n"
    )
    if job in text:
        launch.write_text(text.replace(job + "\n", "", 1).replace(job, "", 1),
            encoding="utf-8")
        print("removed launch user selftest")


def main() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    haiku = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root / "haiku")
    source_root = root / "distro" / "graphics"
    if not haiku.is_dir():
        raise SystemExit(f"haiku tree not found: {haiku}")

    for relative in FILES:
        source = source_root / relative
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        expected = CLEARSTATE_SHA256.get(relative)
        if expected is not None:
            digest = hashlib.sha256(
                source.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
            print(f"sha256 {relative} {digest}")
            if digest != expected:
                raise SystemExit(
                    f"{relative} sha256 {digest} expected {expected}")
        destination = haiku / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")
    hook_gfxinfo(haiku)


if __name__ == "__main__":
    main()
