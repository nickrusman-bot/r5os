#!/usr/bin/env python3
"""Check a hand-written PM4 stream against Mesa 25.3.6 sid.h.

The opcode names and the control-field macros come from
distro/graphics/third_party/mesa-25.3.6/src/amd/common/sid.h.
A few packets in that header name the opcode and not the bits. Those
bits are the ones Linux v6.12 and the R5 builders already use, and each
one is named below.

Run from the repository root:

    python3 scripts/check-pm4.py

The script builds scripts/check-pm4-emit.cpp, checks every stream it
prints, checks the brace lists of packet words in the self-test, the
isolation program and the kernel ring sources, and checks two failures
from 9 Nov 2026: COPY_DATA with register select and a GPU address, and
a command list mapped without the executable flag.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SID = ROOT / "distro/graphics/third_party/mesa-25.3.6/src/amd/common/sid.h"
EMIT = ROOT / "scripts/check-pm4-emit.cpp"

SOURCES = (
    "distro/graphics/src/bin/r5-gpu-selftest/r5-gpu-selftest.cpp",
    "distro/graphics/src/bin/r5-gpu-isolation/r5-gpu-isolation.cpp",
    "distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp",
    "distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/gfx9_k12.cpp",
    "distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp",
    "distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_cs.h",
    "distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_p.h",
    "distro/graphics/src/add-ons/accelerants/radeon_hd/common/r5_stage_r1.h",
    "distro/graphics/src/add-ons/accelerants/radeon_hd/gfx9_compute_ib.h",
    "distro/graphics/src/bin/r5-gpu-isolation/r5_isolation_ib.h",
    "distro/graphics/src/bin/r5-gpu-selftest/r5_selftest_ib.h",
)


# Mesa 25.3.6 sid.h drops these. Mesa 22.3 src/amd/common/sid.h still has
# PKT3_PREAMBLE_CNTL 0x4A and PKT3_SWITCH_BUFFER 0x8B. PACKET3_FRAME_CONTROL
# is 0x90 in Linux gfx_v9_0.c (the same file r5_cs.h follows).
EXTRA_OPCODES = {
    0x4A: "PKT3_PREAMBLE_CNTL",
    0x8B: "PKT3_SWITCH_BUFFER",
    0x90: "PKT3_FRAME_CONTROL",
}


def opcodes_from_sid(text):
    found = {}
    for match in re.finditer(
            r"^#define\s+(PKT3_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)\b",
            text, re.M):
        name = match.group(1)
        if name in ("PKT3_IT_OPCODE_S", "PKT3_IT_OPCODE_G", "PKT3_IT_OPCODE_C",
                    "PKT3_PREDICATE", "PKT3_SHADER_TYPE_S", "PKT3_SHADER_TYPE_G",
                    "PKT3_RESET_FILTER_CAM_S", "PKT3_RESET_FILTER_CAM_G"):
            continue
        value = int(match.group(2), 0)
        if value > 0xFF:
            continue
        found[value] = name
    for value, name in EXTRA_OPCODES.items():
        found.setdefault(value, name)
    return found


def fail(errors, where, detail):
    errors.append("%s: %s" % (where, detail))


def bits_outside(word, allowed):
    return word & ~allowed


def check_event(word, where, errors):
    """EVENT_TYPE, EVENT_INDEX and the action bits in sid.h."""
    action = ((1 << 12) | (1 << 13) | (1 << 15) | (1 << 16) | (1 << 17)
              | (1 << 19) | (1 << 20) | (1 << 21))
    if bits_outside(word, 0xFFFF | action):
        fail(errors, where, "event dword %08x" % word)


def check_release_sel(word, where, errors):
    """EOP_DST_SEL, EOP_INT_SEL and EOP_DATA_SEL. sid.h names data 0, 1, 2, 3
    and 5, and interrupt 0 and 3. Linux also uses interrupt 2 for the EOP
    fence (gfx_v9_0_ring_emit_fence)."""
    allowed = (3 << 16) | (7 << 24) | (7 << 29)
    if bits_outside(word, allowed):
        fail(errors, where, "RELEASE_MEM select %08x" % word)
        return
    data = (word >> 29) & 7
    intr = (word >> 24) & 7
    if data not in (0, 1, 2, 3, 5):
        fail(errors, where, "EOP_DATA_SEL %u" % data)
    if intr not in (0, 2, 3):
        fail(errors, where, "EOP_INT_SEL %u" % intr)


def check_copy(body, where, errors):
    """sid.h lines 128-145. Register select with a GPU address is the
    stick 31 control word."""
    if len(body) != 5:
        fail(errors, where, "COPY_DATA count")
        return
    ctrl = body[0]
    allowed = 0xF | (0xF << 8) | (1 << 16) | (1 << 20) | (1 << 30)
    if bits_outside(ctrl, allowed):
        fail(errors, where, "COPY_DATA reserved %08x" % ctrl)
    src = ctrl & 0xF
    dst = (ctrl >> 8) & 0xF
    if src not in (0, 1, 2, 3, 4, 5, 9):
        fail(errors, where, "COPY_DATA_SRC_SEL %u" % src)
    if dst not in (0, 1, 2, 3, 4, 5):
        fail(errors, where, "COPY_DATA_DST_SEL %u" % dst)

    def register_address(lo, hi):
        return hi == 0 and lo <= 0x3FFFF

    if src == 0 and not register_address(body[1], body[2]):
        fail(errors, where, "COPY_DATA source is a register but the address is not")
    if dst == 0 and not register_address(body[3], body[4]):
        fail(errors, where, "COPY_DATA destination is a register but the address is not")


def check_write(body, where, errors):
    """sid.h names PKT3_WRITE_DATA and not the control bits. The layout is
    the one gfx_v9_0_ring_emit_wreg and the CE meta packet use:
    DST_SEL in bits 8-11 (0 register, 1 memory sync, 2 L2, 3 GDS,
    5 memory async, 8 the CE meta destination), WR_ONE_ADDR bit 16,
    WR_CONFIRM bit 20, cache policy bits 25-26, ENGINE_SEL bits 30-31."""
    if len(body) < 4:
        fail(errors, where, "WRITE_DATA count")
        return
    ctrl = body[0]
    allowed = (0xF << 8) | (1 << 16) | (1 << 20) | (3 << 25) | (3 << 30)
    if bits_outside(ctrl, allowed):
        fail(errors, where, "WRITE_DATA reserved %08x" % ctrl)
    dst = (ctrl >> 8) & 0xF
    engine = ctrl >> 30
    if dst not in (0, 1, 2, 3, 5, 8):
        fail(errors, where, "WRITE_DATA_DST_SEL %u" % dst)
    if engine > 2:
        fail(errors, where, "WRITE_DATA_ENGINE_SEL %u" % engine)
    if dst == 0 and (body[2] != 0 or body[1] > 0x3FFFF):
        fail(errors, where, "WRITE_DATA register destination is not a register")
    if dst in (1, 5) and (ctrl & (1 << 20)) == 0:
        fail(errors, where, "WRITE_DATA to memory is missing WR_CONFIRM")


def check_wait(body, where, errors):
    """WAIT_REG_MEM_EQUAL 3, NOT_EQUAL 4, GREATER_OR_EQUAL 5, MEM_SPACE
    bits 4-5, PFP bit 8. Bits 6-7 are the operation field Linux uses for
    write-wait-write (0x143). sid.h 25.3.6 does not name that field."""
    if len(body) != 6:
        fail(errors, where, "WAIT_REG_MEM count")
        return
    ctrl = body[0]
    allowed = 0x7 | (3 << 4) | (3 << 6) | (1 << 8)
    if bits_outside(ctrl, allowed):
        fail(errors, where, "WAIT_REG_MEM reserved %08x" % ctrl)
    function = ctrl & 7
    if function not in (3, 4, 5):
        fail(errors, where, "WAIT_REG_MEM function %u" % function)


def check_ib(body, where, errors):
    """Length is bits 19:0 and VMID is bits 27:24, as gfx_v9_0_ring_emit_ib_gfx
    writes them. sid.h names bits 22, 23 and 30. Cache policy is bits 29:28."""
    if len(body) != 3:
        fail(errors, where, "INDIRECT_BUFFER count")
        return
    ctrl = body[2]
    allowed = 0xFFFFF | (1 << 22) | (1 << 23) | (0xF << 24) | (3 << 28) | (1 << 30)
    if bits_outside(ctrl, allowed):
        fail(errors, where, "INDIRECT_BUFFER control %08x" % ctrl)
    if (ctrl & 0xFFFFF) == 0:
        fail(errors, where, "INDIRECT_BUFFER length is 0")
    # The ring's own IB (VMID field 0) sets INDIRECT_BUFFER_VALID, bit 23.
    # A user IB carries a non-zero VMID and does not set that bit.
    vmid = (ctrl >> 24) & 0xF
    if vmid == 0 and (ctrl & (1 << 23)) == 0:
        fail(errors, where, "INDIRECT_BUFFER_VALID is required when the VMID field is 0")


def check_context(body, where, errors):
    """CC0 and CC1 load and shadow bits from sid.h."""
    if len(body) != 2:
        fail(errors, where, "CONTEXT_CONTROL count")
        return
    cc0 = (1 << 0) | (1 << 1) | (1 << 15) | (1 << 16) | (1 << 24) | (1 << 28) | (1 << 31)
    cc1 = (1 << 0) | (1 << 1) | (1 << 15) | (1 << 16) | (1 << 24) | (1 << 31)
    if bits_outside(body[0], cc0):
        fail(errors, where, "CONTEXT_CONTROL dw0 %08x" % body[0])
    if bits_outside(body[1], cc1):
        fail(errors, where, "CONTEXT_CONTROL dw1 %08x" % body[1])


def check_acquire(body, where, errors):
    """Count is 6 dwords in gfx_v9_0_emit_mem_sync. The coherency bits are
    the ones gfx9_compute_ib.h and the stage P packet use."""
    if len(body) != 6:
        fail(errors, where, "ACQUIRE_MEM count")
        return
    allowed = (1 << 18) | (1 << 22) | (1 << 23) | (1 << 27) | (1 << 29)
    if bits_outside(body[0], allowed):
        fail(errors, where, "ACQUIRE_MEM coherency %08x" % body[0])


def check_load(body, where, errors):
    """LOAD_SH_REG, LOAD_CONTEXT_REG and the two INDEX forms. The body
    starts with a 64-bit address, so it is not a SET offset. sid.h names
    the opcodes and not the rest of the body."""
    if len(body) < 3:
        fail(errors, where, "LOAD count")
        return
    if body[1] > 0xFFFF:
        fail(errors, where, "LOAD address high half %08x" % body[1])


def check_set(body, where, errors):
    if len(body) < 2:
        fail(errors, where, "SET register count")
        return
    offset = body[0]
    if bits_outside(offset, 0xFFFF | (0xF << 28)):
        fail(errors, where, "SET offset %08x" % offset)
    if (offset & 0xFFFF) >= 0x4000:
        fail(errors, where, "SET offset %08x is outside the window" % offset)


def check_packet(opcode, body, where, errors):
    if opcode == 0x10:
        return
    if opcode == 0x11:
        if len(body) != 3:
            fail(errors, where, "SET_BASE count")
    elif opcode == 0x12:
        if len(body) != 1 or body[0] != 0:
            fail(errors, where, "CLEAR_STATE")
    elif opcode == 0x15 or opcode == 0x16:
        if opcode == 0x15 and len(body) != 4:
            fail(errors, where, "DISPATCH_DIRECT count")
    elif opcode == 0x22:
        if len(body) != 4:
            fail(errors, where, "COND_EXEC count")
    elif opcode == 0x28:
        check_context(body, where, errors)
    elif opcode == 0x37:
        check_write(body, where, errors)
    elif opcode == 0x3C:
        check_wait(body, where, errors)
    elif opcode == 0x3F or opcode == 0x33:
        check_ib(body, where, errors)
    elif opcode == 0x40:
        check_copy(body, where, errors)
    elif opcode == 0x42:
        if len(body) != 1 or body[0] != 0:
            fail(errors, where, "PFP_SYNC_ME")
    elif opcode == 0x46:
        if len(body) not in (1, 3):
            fail(errors, where, "EVENT_WRITE count")
        else:
            check_event(body[0], where, errors)
    elif opcode == 0x49:
        if len(body) != 7:
            fail(errors, where, "RELEASE_MEM count")
        else:
            check_event(body[0], where, errors)
            check_release_sel(body[1], where, errors)
    elif opcode == 0x4A:
        if len(body) != 1 or body[0] not in (0x20000000, 0x30000000):
            fail(errors, where, "PREAMBLE_CNTL %s" % (
                "%08x" % body[0] if body else "short"))
    elif opcode == 0x50:
        if len(body) != 6:
            fail(errors, where, "DMA_DATA count")
        else:
            # Linux soc15d.h: SRC_SEL is bits 30:29, DST_SEL is bits 21:20
            # (0 DAS, 1 GDS, 3 L2; 2 is not a destination), the engine is
            # bit 27, and CP_SYNC is bit 31.
            allowed = (3 << 20) | (1 << 27) | (3 << 29) | (1 << 31)
            if bits_outside(body[0], allowed):
                fail(errors, where, "DMA_DATA control %08x" % body[0])
            dst = (body[0] >> 20) & 3
            if dst not in (0, 1, 3):
                fail(errors, where, "DMA_DATA DST_SEL %u" % dst)
    elif opcode == 0x58:
        check_acquire(body, where, errors)
    elif opcode in (0x5F, 0x61, 0x63, 0x9F):
        check_load(body, where, errors)
    elif opcode == 0x9B:
        check_set(body, where, errors)
    elif opcode in (0x68, 0x69, 0x76, 0x77, 0x79, 0x7A):
        check_set(body, where, errors)
    elif opcode == 0x8B:
        if len(body) != 1 or body[0] != 0:
            fail(errors, where, "SWITCH_BUFFER")
    elif opcode == 0x90:
        if len(body) != 1 or body[0] not in (0, 0x10000000):
            fail(errors, where, "FRAME_CONTROL")
    elif opcode == 0x95:
        return
    else:
        fail(errors, where, "opcode %02x is not checked" % opcode)


def check_stream(words, where, known, errors):
    i = 0
    while i < len(words):
        header = words[i]
        if header == 0xFFFF1000:
            i += 1
            continue
        if (header >> 30) == 2:
            fail(errors, where, "dword %u is type 2, which GFX9 does not use" % i)
            return
        if (header >> 30) != 3:
            fail(errors, where, "dword %u is %08x, not a packet" % (i, header))
            return
        if header & 0xF8:
            fail(errors, where, "header %08x has bits outside predicate and shader type" % header)
        opcode = (header >> 8) & 0xFF
        if opcode in (0x32, 0x3D) or 0xB8 <= opcode <= 0xBE:
            fail(errors, where, "opcode %02x at dword %u is not a GFX9 packet" % (opcode, i))
            return
        count = (header >> 16) & 0x3FFF
        body_len = count + 1
        if opcode not in known:
            fail(errors, where, "opcode %02x at dword %u is not in sid.h" % (opcode, i))
            return
        if i + 1 + body_len > len(words):
            fail(errors, where, "opcode %02x at dword %u is truncated" % (opcode, i))
            return
        body = words[i + 1:i + 1 + body_len]
        check_packet(opcode, body, "%s dword %u" % (where, i), errors)
        i += 1 + body_len


def parse_emit(text):
    streams = []
    sites = []
    name = None
    words = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            if name and words:
                streams.append((name, words))
            name = None
            words = []
            continue
        if line.startswith("# site "):
            sites.append(line[len("# site "):])
            continue
        if line.startswith("# "):
            if name and words:
                streams.append((name, words))
            name = line[2:]
            words = []
            continue
        words.extend(int(part, 16) for part in line.split())
    if name and words:
        streams.append((name, words))
    return sites, streams


def brace_lists(text):
    """Brace lists whose every element is a hex literal, and whose first
    word is a packet header. Lists that mix in variables are built by the
    emitter instead."""
    lists = []
    for match in re.finditer(r"\{([^{}]{0,8000})\}", text):
        body = match.group(1)
        if "0x" not in body:
            continue
        stripped = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        tokens = re.findall(r"0x[0-9A-Fa-f]+u?|\d+u?", stripped)
        rest = re.sub(r"0x[0-9A-Fa-f]+u?|\d+u?", "", stripped)
        if re.search(r"[A-Za-z_]", rest):
            continue
        if len(tokens) < 2:
            continue
        words = [int(item.rstrip("uU"), 0) for item in tokens]
        header = words[0]
        if header != 0xFFFF1000 and (header >> 30) != 3:
            continue
        lists.append(words)
    return lists


def compile_emit():
    include = ROOT / "distro/graphics/src/add-ons/accelerants/radeon_hd"
    common = include / "common"
    isolation = ROOT / "distro/graphics/src/bin/r5-gpu-isolation"
    selftest = ROOT / "distro/graphics/src/bin/r5-gpu-selftest"
    out = Path(tempfile.gettempdir()) / "check-pm4-emit"
    cmd = [
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I%s" % common, "-I%s" % include,
        "-I%s" % isolation, "-I%s" % selftest,
        "-I%s" % (ROOT / "scripts"),
        str(EMIT), "-o", str(out),
    ]
    subprocess.check_call(cmd)
    return subprocess.check_output([str(out)], text=True)


def command_exec(errors):
    """Text search, not a page-table walk. Stick 30 mapped the command
    list without the executable flag. The search looks for the flag name
    near kCommandFlags and in the kernel. It does not read a page table."""
    path = ROOT / "distro/graphics/src/bin/r5-gpu-isolation/r5-gpu-isolation.cpp"
    text = path.read_text(encoding="utf-8")
    if "R5_GPU_VA_EXEC" not in text.split("kCommandFlags", 1)[-1][:240]:
        fail(errors, "isolation", "command list is mapped without R5_GPU_VA_EXEC")
    kernel = (ROOT / "distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp").read_text(encoding="utf-8")
    if "R5_GPU_VA_EXEC" not in kernel or "R5_GFX9_PTE_EXECUTABLE" not in kernel:
        fail(errors, "r5_gpu.cpp", "executable flag does not reach the page table")


RING_CONSTANTS = (
    "kPreambleBegin", "kUconfigIndexType", "kCePartitionBase", "kGfx9StartDwords",
)

WORKLOAD_CONSTANTS = (
    "kWlWriteData", "kWlIndirectBuffer", "kWlReleaseMem", "kWlWriteDstMemory",
    "kWlWrConfirm", "kWlIbValid", "kWlCacheFlushTs", "kWlDmaData",
    "kWlDmaCpSync", "kWlDmaSrcData", "kWlDmaRawWait", "kWlDataSelCounter",
    "kFenceIbWord", "kFenceSequence", "kFenceTsStart", "kFenceTsEnd",
    "kDmaChunk", "kWlSubmitDwords",
)


def constant_expr(text, name):
    match = re.search(r"\b%s\s*=\s*([^;]+);" % name, text)
    if not match:
        return None
    return re.sub(r"\s+", "", match.group(1)).rstrip("uU")


def check_constants(errors):
    """The emitter's copies must use the same initializers as the kernel."""
    start = (ROOT / "distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.cpp").read_text(encoding="utf-8")
    start_h = (ROOT / "distro/graphics/src/add-ons/accelerants/radeon_hd/common/gfx9_start.h").read_text(encoding="utf-8")
    emit = (ROOT / "scripts/check-pm4-emit.cpp").read_text(encoding="utf-8")
    workload = (ROOT / "scripts/r5_pm4_workload.h").read_text(encoding="utf-8")
    kernel = (ROOT / "distro/graphics/src/add-ons/kernel/drivers/graphics/radeon_hd/r5_gpu.cpp").read_text(encoding="utf-8")
    for name in RING_CONSTANTS:
        left = constant_expr(start, name)
        right = constant_expr(emit, name)
        if left is None or left != right:
            fail(errors, "constants", "%s is %s in the ring and %s in the emitter" % (name, left, right))
    for name in WORKLOAD_CONSTANTS:
        left = constant_expr(start, name)
        right = constant_expr(workload, name)
        if left is None or left != right:
            fail(errors, "constants", "%s is %s in gfx9_start.cpp and %s in the copy" % (name, left, right))
    left = constant_expr(start_h, "kK2Pattern")
    right = constant_expr(workload, "kK2Pattern")
    if left is None or left != right:
        fail(errors, "constants", "kK2Pattern is %s and %s" % (left, right))
    ctrl = constant_expr(workload, "kStageDmaCtrl")
    if ctrl is None or ctrl.lower() not in kernel.lower().replace(" ", ""):
        fail(errors, "constants", "gpu_stage_dma does not use %s" % ctrl)


def blank_comments(text):
    def keep(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    return re.sub(r"/\*.*?\*/", keep, text, flags=re.S)


def is_packet_helper(line):
    if re.search(r"return\s+\(3u\s*<<\s*30\)", line):
        return True
    return re.search(
        r"\b(radeon_packet3|r5_cs_packet3|r5_packet3)\s*\(\s*uint32", line) is not None


def source_sites():
    """Every line that builds a type-3 header, except the helper itself
    and a comparison against one."""
    found = []
    call = re.compile(r"\b(radeon_packet3|r5_cs_packet3|r5_packet3)\s*\(")
    shift = re.compile(r"\(3u\s*<<\s*30\)")
    for rel in SOURCES:
        text = blank_comments((ROOT / rel).read_text(encoding="utf-8"))
        for number, line in enumerate(text.splitlines(), 1):
            code = line.split("//", 1)[0]
            if "!=" in code or is_packet_helper(code):
                continue
            hit = call.search(code) or shift.search(code)
            if not hit:
                # A raw header is eight hex digits, type 3, low byte 0,
                # and a non-zero opcode. 0xFFFF1000 is the NOP pad.
                for literal in re.findall(r"0x[0-9A-Fa-f]{8}(?![0-9A-Fa-f])", code):
                    value = int(literal, 16)
                    opcode = (value >> 8) & 0xFF
                    if ((value >> 30) == 3 and (value & 0xFF) == 0
                            and opcode != 0 and value != 0xFFFF1000):
                        hit = True
                        break
            if hit:
                found.append("%s:%u" % (rel, number))
    return found


def check_sites(printed, errors):
    found = source_sites()
    missing = sorted(set(found) - set(printed))
    extra = sorted(set(printed) - set(found))
    for site in missing:
        fail(errors, "coverage", "not emitted: %s" % site)
    for site in extra:
        fail(errors, "coverage", "emitter names a line that does not build a packet: %s" % site)


def self_check(known):
    errors = []
    bad = [
        0xC0044000, (1 << 16) | (1 << 20),
        0x80000000, 0, 0x80000100, 0,
    ]
    check_stream(bad, "stick-31", known, errors)
    if not any("register" in item for item in errors):
        return ["the stick 31 COPY_DATA was accepted"]
    good = []
    check_stream([
        0xC0044000,
        1 | (5 << 8) | (1 << 16) | (1 << 20),
        0x80000000, 0, 0x80000100, 0,
    ], "good-copy", known, good)
    if good:
        return good
    check_stream([0xFFFF1000, 0x80000000], "nop", known, good)
    if not any("type 2" in item for item in good):
        return ["a type 2 dword was accepted"]
    good.clear()
    return []


def main():
    sid = SID.read_text(encoding="utf-8")
    known = opcodes_from_sid(sid)
    if known.get(0x40) != "PKT3_COPY_DATA" or known.get(0x37) != "PKT3_WRITE_DATA":
        print("sid.h did not yield COPY_DATA and WRITE_DATA", file=sys.stderr)
        return 1
    errors = self_check(known)
    command_exec(errors)
    emitted = compile_emit()
    sites, streams = parse_emit(emitted)
    check_constants(errors)
    check_sites(sites, errors)
    if not streams:
        print("emitter printed no streams", file=sys.stderr)
        return 1
    names = [name for name, _words in streams]
    for need in ("kernel-ring", "isolation", "selftest-ib", "compute-submit", "vm-switch"):
        if need not in names:
            fail(errors, "emitter", "missing %s" % need)
    for name, words in streams:
        check_stream(words, name, known, errors)
    for rel in SOURCES:
        text = (ROOT / rel).read_text(encoding="utf-8")
        for index, words in enumerate(brace_lists(text)):
            check_stream(words, "%s list %u" % (rel, index), known, errors)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("ok pm4 %u streams, %u opcodes, text check "
          "(R5_GPU_VA_EXEC is a text search, not a page-table check)"
          % (len(streams), len(known)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
