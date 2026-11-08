#!/usr/bin/env python3
"""Generate r5_gc9_regs.h from the Linux v6.12 register headers.

The headers are read, not retyped. Pass the directory that holds the
v6.12 files named below, or set R5_LINUX_V612. renoir_reg_init.c is not
in the v6.12 tree. The segment bases Renoir uses are in
vega10_ip_offset.h.

  python3 scripts/gen-r5-gc9-regs.py /path/to/headers
  python3 scripts/gen-r5-gc9-regs.py /path/to/headers --replace
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = (
    ROOT
    / "distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_gc9_regs.h"
)
CHECK = ROOT / "scripts/r5-gc9-regs-check.inc"

SOURCES = [
    "gc_9_0_offset.h",
    "gc_9_0_sh_mask.h",
    "gc_9_1_offset.h",
    "osssys_4_0_offset.h",
    "nbio_7_0_offset.h",
    "vega10_ip_offset.h",
]

# Names the M4 code uses when one offset has more than one Linux define.
# The value still comes from the header.
PREFER = {
    0x0000: "mmGRBM_CNTL",
    0x0002: "mmGRBM_STATUS2",
    0x0004: "mmGRBM_STATUS",
    0x0022: "mmGRBM_GFX_CNTL",
    0x008D: "mmCP_MEC_CNTL",
    0x00C0: "mmRCC_DOORBELL_APER_EN",
    0x019C: "mmCP_STALLED_STAT3",
    0x019D: "mmCP_STALLED_STAT1",
    0x019E: "mmCP_STALLED_STAT2",
    0x01A0: "mmCP_STAT",
    0x01A1: "mmCP_ME_HEADER_DUMP",
    0x01A2: "mmCP_PFP_HEADER_DUMP",
    0x01A5: "mmCP_PFP_INSTR_PNTR",
    0x01A6: "mmCP_ME_INSTR_PNTR",
    0x01B6: "mmCP_ME_CNTL",
    0x01C0: "mmCP_RB0_RPTR",
    0x01E1: "mmCP_ROQ_IB1_STAT",
    0x030A: "mmSH_MEM_BASES",
    0x030D: "mmSH_MEM_CONFIG",
    0x070C: "mmGCEA_PROBE_MAP",
    0x1040: "mmCP_RB0_BASE",
    0x1041: "mmCP_RB0_CNTL",
    0x1043: "mmCP_RB0_RPTR_ADDR",
    0x1044: "mmCP_RB0_RPTR_ADDR_HI",
    0x1054: "mmCP_RB0_WPTR",
    0x1055: "mmCP_RB0_WPTR_HI",
    0x5819: "mmCP_CE_UCODE_DATA",
    0x5A04: "mmGRBM_CAM_INDEX",
    0x5A05: "mmGRBM_CAM_DATA",
}

BASES = [
    "GC_BASE__INST0_SEG0",
    "GC_BASE__INST0_SEG1",
    "OSSSYS_BASE__INST0_SEG0",
    "NBIO_BASE__INST0_SEG2",
]

# Extra names the distance formulas need, even before a call site uses them.
ALWAYS = [
    "mmVM_INVALIDATE_ENG0_ADDR_RANGE_LO32",
    "mmVM_INVALIDATE_ENG0_ADDR_RANGE_HI32",
    "mmVM_INVALIDATE_ENG1_ADDR_RANGE_LO32",
    "mmVM_INVALIDATE_ENG1_REQ",
    "mmVM_INVALIDATE_ENG0_REQ",
    "mmVM_INVALIDATE_ENG0_ACK",
    "mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32",
    "mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32",
    "mmVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32",
    "mmVM_CONTEXT1_CNTL",
    "mmVM_CONTEXT0_CNTL",
    "mmIH_VMID_0_LUT",
    "mmMC_VM_AGP_BASE",
    "mmVM_L2_PROTECTION_FAULT_CNTL2",
    "mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32",
    "mmVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32",
]

SHIFTS = [
    "VM_L2_PROTECTION_FAULT_CNTL2__ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY__SHIFT",
    "VM_CONTEXT0_CNTL__ENABLE_CONTEXT__SHIFT",
    "VM_CONTEXT0_CNTL__PAGE_TABLE_DEPTH__SHIFT",
    "VM_CONTEXT0_CNTL__PAGE_TABLE_BLOCK_SIZE__SHIFT",
    "VM_CONTEXT0_CNTL__RETRY_PERMISSION_OR_INVALID_PAGE_FAULT__SHIFT",
    "VM_CONTEXT0_CNTL__RANGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
    "VM_CONTEXT0_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
    "VM_CONTEXT0_CNTL__PDE0_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
    "VM_CONTEXT0_CNTL__VALID_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
    "VM_CONTEXT0_CNTL__READ_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
    "VM_CONTEXT0_CNTL__WRITE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
    "VM_CONTEXT0_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT__SHIFT",
]

TARGETS = [
    ROOT / "distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp",
    ROOT / "distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h",
    ROOT / "distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp",
]

CALL = re.compile(
    r"((?:gc_byte|oss_byte|nbio_byte|r5_cs_gc_reg)\(\s*"
    r"|radeon_gfx9_byte\(\s*kGfx9Seg[01]\s*,\s*)(0x[0-9a-fA-F]+)"
)
DEFINE = re.compile(
    r"^#define\s+([A-Za-z0-9_]+)\s+(0x[0-9a-fA-F]+)[uUlL]*\s*$",
    re.M,
)


def load(directory: Path) -> dict[str, tuple[int, str]]:
    found: dict[str, tuple[int, str]] = {}
    for name in SOURCES:
        path = directory / name
        if not path.is_file():
            raise SystemExit(f"missing {path}")
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in DEFINE.finditer(text):
            symbol = match.group(1)
            if symbol.endswith("_BASE_IDX"):
                continue
            value = int(match.group(2), 16)
            # gc_9_0 wins when a later header repeats a name. gc_9_1
            # still supplies names that gc_9_0 does not have, such as
            # mmGCEA_PROBE_MAP.
            if symbol not in found:
                found[symbol] = (value, name)
    return found


def by_value(table: dict[str, tuple[int, str]]) -> dict[str, dict[int, list[str]]]:
    groups = {
        "gc": ("gc_9_0_offset.h", "gc_9_1_offset.h"),
        "oss": ("osssys_4_0_offset.h",),
        "nbio": ("nbio_7_0_offset.h",),
    }
    out = {key: {} for key in groups}
    for symbol, (value, source) in table.items():
        if not symbol.startswith("mm"):
            continue
        for key, files in groups.items():
            if source in files:
                out[key].setdefault(value, []).append(symbol)
    return out


def choose(value: int, names: list[str]) -> str:
    preferred = PREFER.get(value)
    if preferred is not None and preferred in names:
        return preferred
    if len(names) != 1:
        raise SystemExit(f"ambiguous {value:#x}: {', '.join(names)}")
    return names[0]


def replace_calls(text: str, values: dict[str, dict[int, list[str]]],
        used: dict[str, int]) -> str:
    def one(match: re.Match[str]) -> str:
        raw = match.group(2)
        value = int(raw, 16)
        prefix = match.group(1)
        if prefix.startswith("oss_byte"):
            group = "oss"
        elif prefix.startswith("nbio_byte"):
            group = "nbio"
        else:
            group = "gc"
        names = values[group].get(value)
        if not names:
            raise SystemExit(f"no Linux register for {raw} in {match.group(0)}")
        symbol = choose(value, names)
        used[symbol] = value
        return f"{prefix}{symbol}"

    return CALL.sub(one, text)


def replace_constants(text: str, table: dict[str, tuple[int, str]], used: dict[str, int]) -> str:
    swaps = [
        ("static const uint32 kGfx9Seg0 = 0x2000;",
         "static const uint32 kGfx9Seg0 = GC_BASE__INST0_SEG0;"),
        ("static const uint32 kGfx9Seg1 = 0xA000;",
         "static const uint32 kGfx9Seg1 = GC_BASE__INST0_SEG1;"),
        ("static const uint32 kScratchReg0 = 0x2040;",
         "static const uint32 kScratchReg0 = mmSCRATCH_REG0;"),
        ("static const uint32 kGcBase = 0x2000;",
         "static const uint32 kGcBase = GC_BASE__INST0_SEG0;"),
        ("static const uint32 kOssBase = 0x10A0;",
         "static const uint32 kOssBase = OSSSYS_BASE__INST0_SEG0;"),
        ("static const uint32 kNbioBase = 0x0D20;",
         "static const uint32 kNbioBase = NBIO_BASE__INST0_SEG2;"),
        ("(0xD20u + 0x00C0u)",
         "(NBIO_BASE__INST0_SEG2 + mmRCC_DOORBELL_APER_EN)"),
        ("(0xD20u + 0x01D0u)",
         "(NBIO_BASE__INST0_SEG2 + mmBIF_SDMA0_DOORBELL_RANGE)"),
        ("(0xD20u + 0x01D1u)",
         "(NBIO_BASE__INST0_SEG2 + mmBIF_SDMA1_DOORBELL_RANGE)"),
        ("(0xD20u + 0x01D2u)",
         "(NBIO_BASE__INST0_SEG2 + mmBIF_IH_DOORBELL_RANGE)"),
        ("(0xD20u + 0x01D3u)",
         "(NBIO_BASE__INST0_SEG2 + mmBIF_MMSCH0_DOORBELL_RANGE)"),
        ("return 0x2000u + index;",
         "return GC_BASE__INST0_SEG0 + index;"),
        ("r5_cs_gc_reg(0x08ebu + vmid * 2u)",
         "r5_cs_gc_reg(mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32"
         " + vmid * R5_GC9_CTX_ADDR_DISTANCE)"),
        ("r5_cs_gc_reg(0x08a3u)",
         "r5_cs_gc_reg(mmVM_INVALIDATE_ENG0_REQ)"),
        ("r5_cs_gc_reg(0x08b5u)",
         "r5_cs_gc_reg(mmVM_INVALIDATE_ENG0_ACK)"),
        ("static_assert(kGfx9Seg0 == 0x2000 && kGfx9Seg1 == 0xA000,",
         "static_assert(kGfx9Seg0 == GC_BASE__INST0_SEG0"
         " && kGfx9Seg1 == GC_BASE__INST0_SEG1,"),
    ]
    for old, new in swaps:
        if old in text:
            text = text.replace(old, new, 1)
    for symbol in (
        "GC_BASE__INST0_SEG0",
        "GC_BASE__INST0_SEG1",
        "OSSSYS_BASE__INST0_SEG0",
        "NBIO_BASE__INST0_SEG2",
        "mmSCRATCH_REG0",
        "mmRCC_DOORBELL_APER_EN",
        "mmBIF_SDMA0_DOORBELL_RANGE",
        "mmBIF_SDMA1_DOORBELL_RANGE",
        "mmBIF_IH_DOORBELL_RANGE",
        "mmBIF_MMSCH0_DOORBELL_RANGE",
        "mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32",
        "mmVM_INVALIDATE_ENG0_REQ",
        "mmVM_INVALIDATE_ENG0_ACK",
    ):
        if symbol in text:
            used[symbol] = table[symbol][0]
    return text


def emit(table: dict[str, tuple[int, str]], used: dict[str, int]) -> None:
    symbols = sorted(set(used) | set(ALWAYS) | set(BASES))
    lines = [
        "/*",
        " * Generated by scripts/gen-r5-gc9-regs.py. Do not type values in.",
        " * Linux v6.12:",
        " *   drivers/gpu/drm/amd/include/asic_reg/gc/gc_9_0_offset.h",
        " *   drivers/gpu/drm/amd/include/asic_reg/gc/gc_9_1_offset.h",
        " *   drivers/gpu/drm/amd/include/asic_reg/oss/osssys_4_0_offset.h",
        " *   drivers/gpu/drm/amd/include/asic_reg/nbio/nbio_7_0_offset.h",
        " *   drivers/gpu/drm/amd/include/vega10_ip_offset.h",
        " * renoir_reg_init.c is not in Linux v6.12. Renoir's segment bases",
        " * are GC_BASE, OSSSYS_BASE and NBIO_BASE in vega10_ip_offset.h.",
        " *",
        " * Copyright (C) 2017, 2018 Advanced Micro Devices, Inc.",
        " *",
        " * Permission is hereby granted, free of charge, to any person obtaining a",
        " * copy of this software and associated documentation files (the \"Software\"),",
        " * to deal in the Software without restriction, including without limitation",
        " * the rights to use, copy, modify, merge, publish, distribute, sublicense,",
        " * and/or sell copies of the Software, and to permit persons to whom the",
        " * Software is furnished to do so, subject to the following conditions:",
        " *",
        " * The above copyright notice and this permission notice shall be included",
        " * in all copies or substantial portions of the Software.",
        " *",
        " * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS",
        " * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,",
        " * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL",
        " * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,",
        " * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF",
        " * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE",
        " * SOFTWARE.",
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        "#ifndef R5_GC9_REGS_H",
        "#define R5_GC9_REGS_H",
        "",
    ]
    current = ""
    for symbol in symbols:
        value, source = table[symbol]
        if source != current:
            lines.append(f"/* Linux v6.12 {source} */")
            current = source
        lines.append(f"#define {symbol} 0x{value:x}u")
    lines += [
        "/* Linux v6.12 gfxhub_v1_0.c:gfxhub_v1_0_init */",
        "#define R5_GC9_CTX_ADDR_DISTANCE (mmVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 \\",
        "	- mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32)",
        "#define R5_GC9_CTX_DISTANCE (mmVM_CONTEXT1_CNTL - mmVM_CONTEXT0_CNTL)",
        "#define R5_GC9_ENG_ADDR_DISTANCE (mmVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 \\",
        "	- mmVM_INVALIDATE_ENG0_ADDR_RANGE_LO32)",
        "#define R5_GC9_ENG_REQ_DISTANCE (mmVM_INVALIDATE_ENG1_REQ \\",
        "	- mmVM_INVALIDATE_ENG0_REQ)",
        "",
        "#endif",
        "",
    ]
    HEADER.write_bytes("\n".join(lines).encode("utf-8"))
    checks = ["/* Generated by scripts/gen-r5-gc9-regs.py. The second column is the number this code used before the name. */"]
    for symbol in sorted(used):
        checks.append(f"R5_GC9_WAS({symbol}, 0x{used[symbol]:x}u)")
    checks.append("R5_GC9_WAS(R5_GC9_ENG_ADDR_DISTANCE, 0x2u)")
    checks.append("R5_GC9_WAS(R5_GC9_CTX_ADDR_DISTANCE, 0x2u)")
    CHECK.write_bytes(("\n".join(checks) + "\n").encode("utf-8"))


def main() -> int:
    args = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
    replace = "--replace" in sys.argv
    directory = Path(args[0] if args else os.environ.get("R5_LINUX_V612", ""))
    if not directory.is_dir():
        raise SystemExit("pass the Linux v6.12 header directory")
    table = load(directory)
    values = by_value(table)
    used: dict[str, int] = {}
    ident = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")
    for path in TARGETS:
        if not path.is_file():
            continue
        for match in ident.finditer(path.read_text(encoding="utf-8")):
            symbol = match.group(1)
            if symbol in table:
                used[symbol] = table[symbol][0]
    if replace:
        for path in TARGETS:
            text = path.read_text(encoding="utf-8")
            text = text.replace("\r\n", "\n")
            updated = replace_constants(text, table, used)
            updated = replace_calls(updated, values, used)
            if updated != text:
                path.write_bytes(updated.encode("utf-8"))
                print(f"replaced {path.relative_to(ROOT).as_posix()}")
            else:
                print(f"unchanged {path.relative_to(ROOT).as_posix()}")
    else:
        for symbol in ALWAYS + BASES:
            used[symbol] = table[symbol][0]
    for symbol in ALWAYS + BASES + SHIFTS:
        used.setdefault(symbol, table[symbol][0])
    emit(table, used)
    print(f"wrote {HEADER.relative_to(ROOT).as_posix()} ({len(used)} names)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
