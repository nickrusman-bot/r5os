#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only BeFS reader for disk images and physical drives.

The on-disk layout is the Be File System (superblock, inodes, B+ trees).
This file is new R5OS code. It does not copy the Haiku BFS implementation.
Haiku's BFS sources stay MIT; see headers/private and
src/add-ons/kernel/file_systems/bfs in a Haiku tree.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass


SUPER_MAGIC1 = 0x42465331  # BFS1
SUPER_MAGIC2 = 0xDD121031
SUPER_MAGIC3 = 0x15B6830E
INODE_MAGIC = 0x3BBE0AD9
TREE_MAGIC = 0x69F6C2E8
TREE_NULL = -1
S_IFMT = 0xF000
S_IFDIR = 0x4000
S_IFREG = 0x8000
S_IFLNK = 0xA000


def u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def i32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<i", buf, off)[0]


def u64(buf: bytes, off: int) -> int:
    return struct.unpack_from("<Q", buf, off)[0]


def i64(buf: bytes, off: int) -> int:
    return struct.unpack_from("<q", buf, off)[0]


def align8(value: int) -> int:
    return (value + 7) & ~7


@dataclass
class Run:
    group: int
    start: int
    length: int

    @staticmethod
    def parse(buf: bytes, off: int) -> "Run":
        return Run(i32(buf, off), u16(buf, off + 4), u16(buf, off + 6))


@dataclass
class Inode:
    mode: int
    size: int
    runs: list[Run]
    indirect: Run
    max_direct: int
    raw: bytes

    def kind(self) -> str:
        masked = self.mode & S_IFMT
        if masked == S_IFDIR:
            return "dir"
        if masked == S_IFLNK:
            return "link"
        return "file"


class Volume:
    def __init__(self, path: str, offset: int | None) -> None:
        self._file = open(path, "rb")
        self.base = 0 if offset is None else offset
        if offset is None:
            self.base = self._find_base()
        self._read_super()

    def close(self) -> None:
        self._file.close()

    def _read(self, offset: int, size: int) -> bytes:
        self._file.seek(self.base + offset)
        data = self._file.read(size)
        if len(data) != size:
            raise IOError(f"short read at {self.base + offset}")
        return data

    def _super_ok(self, offset: int) -> bool:
        try:
            block = self._read(offset, 512)
        except OSError:
            return False
        if len(block) < 96:
            return False
        return (
            u32(block, 32) == SUPER_MAGIC1
            and u32(block, 68) == SUPER_MAGIC2
            and u32(block, 112) == SUPER_MAGIC3
        )

    def _find_base(self) -> int:
        saved = self.base
        self.base = 0
        if self._super_ok(512):
            return 0
        try:
            mbr = self._read(0, 512)
        except OSError:
            self.base = saved
            raise
        if mbr[510:512] == b"\x55\xaa":
            for index in range(4):
                entry = 446 + index * 16
                if mbr[entry + 4] != 0xEB:
                    continue
                start = u32(mbr, entry + 8) * 512
                self.base = start
                if self._super_ok(512):
                    return start
        # GUID partition table. The BeFS type begins with the bytes of "1SFB".
        self.base = 0
        try:
            header = self._read(512, 512)
        except OSError:
            pass
        else:
            if header[0:8] == b"EFI PART":
                entry_lba = u64(header, 72)
                count = u32(header, 80)
                size = u32(header, 84)
                blob = self._read(entry_lba * 512, count * size)
                for index in range(count):
                    entry = blob[index * size:(index + 1) * size]
                    if entry[0:4] != b"1SFB":
                        continue
                    start = u64(entry, 32) * 512
                    self.base = start
                    if self._super_ok(512):
                        return start
        self.base = saved
        raise SystemExit("no BeFS superblock (tried the file start, MBR type eb, and GPT)")

    def _read_super(self) -> None:
        block = self._read(512, 128)
        self.name = block[0:32].split(b"\x00", 1)[0].decode("utf-8", "replace")
        self.block_size = u32(block, 40)
        self.block_shift = u32(block, 44)
        self.ag_shift = u32(block, 76)
        self.root = Run.parse(block, 116)
        if self.block_size == 0 or self.block_size > 65536:
            raise SystemExit(f"unexpected block size {self.block_size}")

    def _block_number(self, run: Run) -> int:
        return (run.group << self.ag_shift) | run.start

    def _read_inode_block(self, block: int) -> Inode:
        raw = self._read(block << self.block_shift, self.block_size)
        if u32(raw, 0) != INODE_MAGIC:
            raise IOError("inode magic mismatch")
        mode = u32(raw, 20)
        # data_stream begins at byte 72.
        runs = [Run.parse(raw, 72 + index * 8) for index in range(12)]
        max_direct = i64(raw, 72 + 96)
        indirect = Run.parse(raw, 72 + 104)
        size = i64(raw, 72 + 136)
        return Inode(mode, size, runs, indirect, max_direct, raw)

    def _read_inode(self, run: Run) -> Inode:
        return self._read_inode_block(self._block_number(run))

    def _read_blocks(self, run: Run, limit: int) -> bytes:
        if run.length == 0 or limit <= 0:
            return b""
        start = self._block_number(run) << self.block_shift
        want = min(limit, run.length * self.block_size)
        return self._read(start, want)

    def read_file(self, inode: Inode) -> bytes:
        if inode.size < 0:
            raise IOError("negative file size")
        if inode.size == 0:
            return b""
        out = bytearray()
        remaining = inode.size
        for run in inode.runs:
            if remaining <= 0:
                break
            if run.length == 0:
                continue
            chunk = self._read_blocks(run, remaining)
            out += chunk
            remaining -= len(chunk)
        if remaining > 0 and inode.indirect.length:
            table = self._read_blocks(
                inode.indirect, inode.indirect.length * self.block_size
            )
            offset = 0
            while remaining > 0 and offset + 8 <= len(table):
                run = Run.parse(table, offset)
                offset += 8
                if run.length == 0:
                    continue
                chunk = self._read_blocks(run, remaining)
                out += chunk
                remaining -= len(chunk)
        return bytes(out[: inode.size])

    def _list(self, inode: Inode) -> list[tuple[str, int]]:
        if inode.kind() != "dir":
            raise NotADirectoryError("not a directory")
        blob = self.read_file(inode)
        if len(blob) < 40 or u32(blob, 0) != TREE_MAGIC:
            raise IOError("directory is not a B+ tree")
        node_size = u32(blob, 4)
        root = i64(blob, 16)
        found: list[tuple[str, int]] = []

        def walk(offset: int, depth: int) -> None:
            if depth > 16 or offset < 0 or offset + 28 > len(blob):
                return
            node = blob[offset:offset + node_size]
            if len(node) < 28:
                return
            overflow = i64(node, 16)
            count = u16(node, 24)
            key_bytes = u16(node, 26)
            if count > 4096 or key_bytes > node_size:
                return
            lengths_at = align8(28 + key_bytes)
            values_at = lengths_at + count * 2
            cursor = 28
            leaf = overflow == TREE_NULL
            previous = 0
            for index in range(count):
                if lengths_at + index * 2 + 2 > len(node):
                    return
                # The table stores the end offset of each key, not its length.
                end = u16(node, lengths_at + index * 2)
                length = end - previous
                previous = end
                if length > 256 or cursor + length > len(node):
                    return
                name = node[cursor:cursor + length].split(b"\x00", 1)[0]
                cursor += length
                value_at = values_at + index * 8
                if value_at + 8 > len(node):
                    return
                if leaf:
                    found.append((
                        name.decode("utf-8", "replace"),
                        i64(node, value_at),
                    ))
                else:
                    child = i64(node, value_at)
                    if child > 0:
                        walk(child, depth + 1)
            if not leaf and overflow > 0:
                walk(overflow, depth + 1)

        walk(root, 0)
        found.sort(key=lambda item: item[0])
        return found

    def lookup(self, path: str) -> Inode:
        inode = self._read_inode(self.root)
        parts = [part for part in path.replace("\\", "/").split("/") if part not in ("", ".")]
        for part in parts:
            if inode.kind() != "dir":
                raise NotADirectoryError(path)
            match = None
            for name, run in self._list(inode):
                if name == part:
                    match = run
                    break
            if match is None:
                raise FileNotFoundError(path)
            inode = self._read_inode_block(match)
        return inode

    def list_path(self, path: str) -> list[tuple[str, Inode]]:
        inode = self.lookup(path)
        return [
            (name, self._read_inode_block(block))
            for name, block in self._list(inode)
        ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read a BeFS volume from an image or a physical drive."
    )
    parser.add_argument("command", choices=("ls", "cat", "export"))
    parser.add_argument("image")
    parser.add_argument("path", nargs="?", default="/")
    parser.add_argument("dest", nargs="?")
    parser.add_argument("--offset", type=int, default=None)
    args = parser.parse_args()

    volume = Volume(args.image, args.offset)
    try:
        if args.command == "ls":
            print(f"volume {volume.name} block {volume.block_size} base {volume.base}")
            for name, inode in volume.list_path(args.path):
                print(f"{inode.kind():4} {inode.size:10} {name}")
        elif args.command == "cat":
            inode = volume.lookup(args.path)
            sys.stdout.buffer.write(volume.read_file(inode))
        else:
            if not args.dest:
                raise SystemExit("export needs a destination path")
            inode = volume.lookup(args.path)
            data = volume.read_file(inode)
            with open(args.dest, "wb") as output:
                output.write(data)
            print(f"wrote {len(data)} bytes to {args.dest}")
    finally:
        volume.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, NotADirectoryError, OSError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
