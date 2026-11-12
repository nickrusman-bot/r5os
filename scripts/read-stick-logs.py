#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Copy the boot logs off the USB stick (the General UDisk). Read only.

Run it from Windows Python, after the 5625U has been shut down and the
stick is plugged back in:

    py scripts\\read-stick-logs.py
    py scripts\\read-stick-logs.py --boot 33

It asks for elevation itself when it is not elevated. It never writes the
stick, and it refuses disk 0. The files land in generated/boot-5625u-<n>/.

--image PATH reads a raw image instead of the stick. That is for checking
the reader. It does not need elevation.
"""
import argparse
import os
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STICK_NAME = "General UDisk"

# Paths on the boot volume. The first hit wins for each output name.
WANTED = (
    ("home/config/cache/r5-gpu-selftest.txt", "r5-gpu-selftest.txt"),
    ("home/config/cache/r5-gpu-selftest.old.txt", "r5-gpu-selftest.old.txt"),
    ("home/r5-session.log", "r5-session.log"),
    ("system/var/log/syslog", "syslog"),
    ("system/var/log/syslog.old", "syslog.old"),
    ("common/var/log/syslog", "syslog"),
    ("common/var/log/syslog.old", "syslog.old"),
    ("system/settings/kernel/drivers/radeon_hd", "radeon_hd"),
    ("home/config/settings/kernel/drivers/radeon_hd", "radeon_hd-home"),
)


def die(message):
    print(message, file=sys.stderr)
    sys.exit(1)


class Slice:
    """A window of a larger image. offset is the BFS partition start."""

    def __init__(self, raw, offset):
        self.raw = raw
        self.offset = offset

    def read(self, at, size):
        return self.raw.read(self.offset + at, size)


class FileRaw:
    def __init__(self, path):
        self.file = open(path, "rb")

    def read(self, at, size):
        self.file.seek(at)
        data = self.file.read(size)
        if len(data) != size:
            die("short read at %u" % at)
        return data


def find_super(raw, limit):
    """Return the byte offset of the superblock, or None."""
    step = 1024 * 1024
    previous = b""
    at = 0
    while at < limit:
        chunk = raw.read(at, min(step + 64, limit - at))
        blob = previous + chunk
        base = at - len(previous)
        for magic in (b"1SFB", b"BFS1"):
            start = 0
            while True:
                hit = blob.find(magic, start)
                if hit < 0:
                    break
                start = hit + 1
                super_at = base + hit - 32
                if super_at < 512:
                    continue
                if (super_at - 512) % 512 != 0:
                    continue
                block = raw.read(super_at, 128)
                if block[32:36] != magic:
                    continue
                order = "<" if magic == b"1SFB" else ">"
                magic2 = struct.unpack_from(order + "I", block, 68)[0]
                if magic2 == 0xDD121031:
                    return super_at, order
        previous = chunk[-64:]
        at += step
    return None


class Bfs:
    def __init__(self, raw, super_at, order):
        self.raw = raw
        self.order = order
        self.super_at = super_at
        self.part = super_at - 512
        head = raw.read(super_at, 140)
        self.block_size = self.u32(head, 40)
        self.blocks_per_ag = self.u32(head, 72)
        self.ag_shift = self.u32(head, 76)
        self.root = self.run(head, 116)
        if self.block_size < 512 or self.block_size > 8192:
            die("unexpected block size %u" % self.block_size)

    def u16(self, data, at):
        return struct.unpack_from(self.order + "H", data, at)[0]

    def u32(self, data, at):
        return struct.unpack_from(self.order + "I", data, at)[0]

    def i32(self, data, at):
        return struct.unpack_from(self.order + "i", data, at)[0]

    def i64(self, data, at):
        return struct.unpack_from(self.order + "q", data, at)[0]

    def run(self, data, at):
        return (self.i32(data, at), self.u16(data, at + 4), self.u16(data, at + 6))

    def block_number(self, ag, start):
        return (ag << self.ag_shift) + start

    def read_block(self, number, count=1):
        return self.raw.read(self.part + number * self.block_size,
                             count * self.block_size)

    def read_inode(self, block):
        return self.read_block(block, 1)[:self.block_size]

    def runs_of(self, inode, size):
        runs = []
        covered = 0
        for index in range(12):
            ag, start, length = self.run(inode, 72 + index * 8)
            if length == 0:
                break
            runs.append((self.block_number(ag, start), length))
            covered += length * self.block_size
            if covered >= size:
                return runs
        ag, start, length = self.run(inode, 176)
        if length == 0:
            return runs
        table = self.read_block(self.block_number(ag, start), length)
        count = (length * self.block_size) // 8
        for index in range(count):
            rag, rstart, rlength = self.run(table, index * 8)
            if rlength == 0:
                break
            runs.append((self.block_number(rag, rstart), rlength))
            covered += rlength * self.block_size
            if covered >= size:
                break
        return runs

    def read_file(self, inode):
        size = self.i64(inode, 208)
        if size < 0 or size > 64 * 1024 * 1024:
            die("file size %d is not readable here" % size)
        if size == 0:
            return b""
        out = bytearray()
        for number, length in self.runs_of(inode, size):
            out += self.read_block(number, length)
            if len(out) >= size:
                break
        return bytes(out[:size])

    def inode_name(self, inode):
        at = 232
        while at + 8 <= len(inode):
            kind = self.u32(inode, at)
            name_size = self.u16(inode, at + 4)
            data_size = self.u16(inode, at + 6)
            if kind == 0 or name_size == 0:
                break
            name = inode[at + 8:at + 8 + name_size]
            data = inode[at + 8 + name_size:at + 8 + name_size + data_size]
            if kind == 0x52545343 and name[:1] == b"\x13":
                return data.split(b"\x00", 1)[0].decode("utf-8", "replace")
            at += 8 + name_size + data_size
            at = (at + 7) & ~7
        return ""

    def mode(self, inode):
        return self.u32(inode, 20)

    def list_dir(self, inode):
        data = self.read_file(inode)
        if len(data) < 40:
            return []
        if self.u32(data, 0) != 0x69F6C2E8:
            return []
        node_size = self.u32(data, 4)
        root = self.i64(data, 16)
        found = []

        def walk(offset):
            if offset < 0 or offset + 28 > len(data):
                return
            node = data[offset:offset + node_size]
            if len(node) < 28:
                return
            overflow = self.i64(node, 16)
            keys = self.u16(node, 24)
            key_bytes = self.u16(node, 26)
            leaf = overflow == -1
            lengths_at = (28 + key_bytes + 7) & ~7
            values_at = lengths_at + keys * 2
            for index in range(keys):
                if lengths_at + index * 2 + 2 > len(node):
                    return
                end = self.u16(node, lengths_at + index * 2)
                start = 0 if index == 0 else self.u16(node, lengths_at + (index - 1) * 2)
                if end < start or 28 + end > len(node):
                    return
                raw = node[28 + start:28 + end]
                if values_at + (index + 1) * 8 > len(node):
                    return
                value = self.i64(node, values_at + index * 8)
                if value < 0:
                    continue
                if (value >> 62) != 0:
                    continue
                name = raw.split(b"\x00", 1)[0].decode("utf-8", "replace")
                if leaf:
                    found.append((name, value))
                else:
                    walk(value)

        walk(root)
        return found

    def lookup(self, directory, name):
        for entry, block in self.list_dir(directory):
            if entry == name:
                return self.read_inode(block)
        return None

    def walk(self, path):
        inode = self.read_inode(self.block_number(*self.root[:2]))
        for part in path.split("/"):
            if part == "":
                continue
            nxt = self.lookup(inode, part)
            if nxt is None:
                return None
            kind = self.mode(nxt) & 0o170000
            if kind == 0o120000:
                raw = nxt[72:72 + 144].split(b"\x00", 1)[0]
                if not raw:
                    raw = self.read_file(nxt).split(b"\x00", 1)[0]
                text = raw.decode("utf-8", "replace")
                rest = path.split(part, 1)[1].lstrip("/")
                if text.startswith("/"):
                    text = text[1:]
                joined = text if rest == "" else text.rstrip("/") + "/" + rest
                return self.walk(joined)
            inode = nxt
        return inode


def next_boot_dir():
    folder = ROOT / "generated"
    highest = 0
    if folder.is_dir():
        for child in folder.iterdir():
            if child.name.startswith("boot-5625u-"):
                tail = child.name[len("boot-5625u-"):]
                if tail.isdigit():
                    highest = max(highest, int(tail))
    return folder / ("boot-5625u-%u" % (highest + 1))


def copy_tree(bfs, path, dest, depth=0):
    """Copy a directory of the volume below dest. Files already there stay."""
    inode = bfs.walk(path)
    if inode is None:
        return 0
    if (bfs.mode(inode) & 0o170000) != 0o040000:
        return 0
    if depth > 4:
        return 0
    count = 0
    for name, block in bfs.list_dir(inode):
        if name in (".", "..") or "/" in name:
            continue
        child = bfs.read_inode(block)
        kind = bfs.mode(child) & 0o170000
        target = dest / name
        if kind == 0o040000:
            count += copy_tree(bfs, path + "/" + name, target, depth + 1)
        elif kind == 0o100000:
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                continue
            if bfs.i64(child, 208) > 64 * 1024 * 1024:
                print("skipped %s, too large" % (path + "/" + name))
                continue
            target.write_bytes(bfs.read_file(child))
            count += 1
    return count


def copy_m10(bfs, dest):
    """The M10 stick tools keep their files in ~/m10, and the standard
    account's in its own home (check-user)."""
    total = copy_tree(bfs, "home/m10", dest / "m10")
    users = bfs.walk("home/users")
    if users is not None and (bfs.mode(users) & 0o170000) == 0o040000:
        for name, _block in bfs.list_dir(users):
            total += copy_tree(bfs, "home/users/%s/m10" % name,
                               dest / "users" / name / "m10")
    if total:
        print("copied %u files from /boot/home/m10 into %s" % (total, dest / "m10"))
    else:
        print("missing home/m10")


def copy_logs(bfs, dest, list_root):
    if list_root:
        root = bfs.read_inode(bfs.block_number(*bfs.root[:2]))
        for name, _block in bfs.list_dir(root):
            print(name)
        return 0
    dest.mkdir(parents=True, exist_ok=True)
    written = {}
    for path, name in WANTED:
        if name in written:
            continue
        inode = bfs.walk(path)
        if inode is None:
            print("missing %s" % path)
            continue
        data = bfs.read_file(inode)
        (dest / name).write_bytes(data)
        written[name] = path
        print("copied %s (%u bytes) from %s" % (name, len(data), path))
    copy_m10(bfs, dest)
    if "syslog" not in written and "radeon_hd" not in written:
        die("neither the syslog nor the radeon_hd settings were on the volume")
    print("logs: %s" % dest)
    return 0


def open_image(path):
    raw = FileRaw(path)
    size = os.path.getsize(path)
    found = find_super(raw, min(size, 64 * 1024 * 1024))
    if found is None:
        found = find_super(raw, size)
    if found is None:
        die("no BFS superblock in %s" % path)
    return Bfs(raw, found[0], found[1])


def main_image(path, boot, list_root):
    bfs = open_image(path)
    dest = ROOT / "generated" / ("boot-5625u-%u" % boot) if boot else next_boot_dir()
    return copy_logs(bfs, dest, list_root)


def main_stick(boot, list_root):
    if sys.platform != "win32":
        die("the stick is read from Windows Python; use --image elsewhere")
    import ctypes
    import json
    import subprocess
    from ctypes import wintypes

    kernel32 = ctypes.windll.kernel32
    shell32 = ctypes.windll.shell32
    GENERIC_READ = 0x80000000
    FILE_SHARE_READ = 1
    FILE_SHARE_WRITE = 2
    OPEN_EXISTING = 3
    INVALID = ctypes.c_void_p(-1).value
    IOCTL_STORAGE_QUERY_PROPERTY = 0x2D1400

    class ShellExecuteInfo(ctypes.Structure):
        _fields_ = [
            ("cbSize", wintypes.DWORD), ("fMask", ctypes.c_ulong),
            ("hwnd", wintypes.HWND), ("lpVerb", wintypes.LPCWSTR),
            ("lpFile", wintypes.LPCWSTR), ("lpParameters", wintypes.LPCWSTR),
            ("lpDirectory", wintypes.LPCWSTR), ("nShow", ctypes.c_int),
            ("hInstApp", wintypes.HINSTANCE), ("lpIDList", ctypes.c_void_p),
            ("lpClass", wintypes.LPCWSTR), ("hKeyClass", wintypes.HKEY),
            ("dwHotKey", wintypes.DWORD), ("hIcon", wintypes.HANDLE),
            ("hProcess", wintypes.HANDLE),
        ]

    if not shell32.IsUserAnAdmin():
        log_path = str(ROOT / "generated" / "read-stick-logs.log")
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        # ShellExecute has no shell, so "> file" would reach argparse as
        # arguments. The elevated run writes its own log instead.
        params = subprocess.list2cmdline(
            [os.path.abspath(__file__)] + sys.argv[1:] + ["--log", log_path])
        info = ShellExecuteInfo()
        info.cbSize = ctypes.sizeof(info)
        info.fMask = 0x40
        info.lpVerb = "runas"
        info.lpFile = sys.executable
        info.lpParameters = params
        info.lpDirectory = os.getcwd()
        info.nShow = 0
        if not shell32.ShellExecuteExW(ctypes.byref(info)):
            die("elevation was refused or failed")
        kernel32.WaitForSingleObject(info.hProcess, 0xFFFFFFFF)
        code = wintypes.DWORD()
        kernel32.GetExitCodeProcess(info.hProcess, ctypes.byref(code))
        if os.path.exists(log_path):
            sys.stdout.write(open(log_path, encoding="utf-8", errors="replace").read())
        sys.exit(code.value)

    script = ("Get-Disk | Select-Object Number,FriendlyName,BusType,Size "
              "| ConvertTo-Json -Compress")
    disks = json.loads(subprocess.check_output(
        ["powershell", "-NoProfile", "-Command", script], text=True))
    if isinstance(disks, dict):
        disks = [disks]
    chosen = []
    for disk in disks:
        number = int(disk["Number"])
        print("disk %u: %s %s" % (number, disk["FriendlyName"], disk["BusType"]))
        if number == 0:
            continue
        if str(disk["FriendlyName"]) == STICK_NAME and str(disk["BusType"]) == "USB":
            chosen.append(number)
    if len(chosen) != 1:
        die("need exactly one USB disk named %r, found %u" % (STICK_NAME, len(chosen)))
    number = chosen[0]
    if number == 0:
        die("refusing disk 0")
    path = "\\\\.\\PhysicalDrive%u" % number
    if "PhysicalDrive0" in path.replace(" ", ""):
        die("refusing disk 0")
    handle = kernel32.CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        None, OPEN_EXISTING, 0, None)
    if handle == INVALID:
        die("open %s failed %u" % (path, ctypes.get_last_error()))

    class StickRaw:
        def read(self, at, size):
            # A raw disk only reads whole sectors: read the covering range
            # and cut the asked bytes out of it.
            start = at - at % 512
            end = at + size
            length = end - start
            length += (-length) % 512
            moved = ctypes.c_longlong()
            kernel32.SetFilePointerEx(handle, ctypes.c_longlong(start), ctypes.byref(moved), 0)
            buf = ctypes.create_string_buffer(length)
            got = wintypes.DWORD()
            if not kernel32.ReadFile(handle, buf, length, ctypes.byref(got), None):
                die("read failed %u at %u" % (kernel32.GetLastError(), at))
            if got.value != length:
                die("short read at %u" % at)
            return buf.raw[at - start:at - start + size]

    query = (ctypes.c_uint32 * 3)(0, 0, 0)
    out = ctypes.create_string_buffer(512)
    got = wintypes.DWORD()
    if not kernel32.DeviceIoControl(
            handle, IOCTL_STORAGE_QUERY_PROPERTY, query, ctypes.sizeof(query),
            out, 512, ctypes.byref(got), None):
        die("the stick did not answer a property query")
    bus = int.from_bytes(out.raw[28:32], "little")
    if bus != 7:
        die("the chosen disk is not on USB")
    raw = StickRaw()
    size = next(int(disk["Size"]) for disk in disks if int(disk["Number"]) == number)
    found = find_super(raw, min(size, 64 * 1024 * 1024))
    if found is None:
        die("no BFS superblock on the stick")
    bfs = Bfs(raw, found[0], found[1])
    dest = ROOT / "generated" / ("boot-5625u-%u" % boot) if boot else next_boot_dir()
    try:
        return copy_logs(bfs, dest, list_root)
    finally:
        kernel32.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description="Read R5 boot logs from the stick")
    parser.add_argument("--image", help="raw image instead of the General UDisk")
    parser.add_argument("--boot", type=int, help="write generated/boot-5625u-N")
    parser.add_argument("--list-root", action="store_true",
                        help="print the volume root and copy nothing")
    parser.add_argument("--log", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.log:
        log = open(args.log, "w", encoding="utf-8", buffering=1)
        sys.stdout = sys.stderr = log
    if args.image:
        return main_image(args.image, args.boot, args.list_root)
    return main_stick(args.boot, args.list_root)


if __name__ == "__main__":
    sys.exit(main() or 0)
