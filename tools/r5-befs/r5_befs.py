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
import datetime
import json
import os
import struct
import subprocess
import sys
from dataclasses import dataclass, field


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

WINDOWS_ILLEGAL_CHARS = set(':*?"<>|')


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


def check_device_allowed(path: str) -> None:
    """Refuse opening physical disk 0 (Windows NVMe system disk) under all circumstances."""
    norm = path.lower().replace("/", "\\")
    if "physicaldrive0" in norm:
        raise PermissionError("PhysicalDrive0 (disk 0) is forbidden: protected system disk")


def sanitize_filename(name: str) -> tuple[str, bool]:
    """Sanitize filename for Windows filesystem compatibility.
    Replaces ':', '*', '?', '"', '<', '>', '|' with '_' and strips trailing dots/spaces.
    Returns (safe_name, was_changed).
    """
    chars: list[str] = []
    changed = False
    for ch in name:
        if ch in WINDOWS_ILLEGAL_CHARS or ord(ch) < 32:
            chars.append("_")
            changed = True
        else:
            chars.append(ch)
    safe = "".join(chars)
    stripped = safe.rstrip(". ")
    if not stripped:
        safe = "_"
        changed = True
    elif stripped != safe:
        safe = stripped
        changed = True
    return safe, changed


def list_removable_drives() -> list[dict[str, object]]:
    """Query Windows for removable storage drives. Strictly excludes disk 0 and system disk."""
    if sys.platform != "win32":
        return []
    drives: list[dict[str, object]] = []
    try:
        cmd = [
            "powershell",
            "-NoProfile",
            "-Command",
            "Get-CimInstance Win32_DiskDrive | "
            "Select-Object DeviceID, Index, MediaType, Model, InterfaceType, Size | "
            "ConvertTo-Json -Compress",
        ]
        raw = subprocess.check_output(cmd, encoding="utf-8", errors="replace").strip()
        if not raw:
            return []
        data = json.loads(raw)
        if isinstance(data, dict):
            data = [data]
        for item in data:
            idx = item.get("Index")
            dev_id = str(item.get("DeviceID", ""))
            # NEVER allow disk 0 or PhysicalDrive0
            if idx == 0 or "physicaldrive0" in dev_id.lower():
                continue
            media_type = str(item.get("MediaType", "")).lower()
            interface_type = str(item.get("InterfaceType", "")).lower()
            if "removable" in media_type or "usb" in interface_type:
                drives.append({
                    "device_id": dev_id,
                    "index": idx,
                    "model": item.get("Model", "Removable Disk"),
                    "size": item.get("Size", 0),
                    "media_type": item.get("MediaType", ""),
                    "interface": item.get("InterfaceType", ""),
                })
    except Exception:
        pass
    return drives


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

    @property
    def modified_time(self) -> int:
        """Return unix epoch timestamp (seconds) from inode last_modified_time."""
        if len(self.raw) >= 44:
            return i64(self.raw, 36) >> 16
        return 0

    @property
    def mime_type(self) -> str:
        """Extract BEOS:TYPE attribute from small_data in raw inode if present."""
        idx = self.raw.find(b"BEOS:TYPE")
        if idx >= 8:
            try:
                name_size = u16(self.raw, idx - 4)
                data_size = u16(self.raw, idx - 2)
                data_off = idx + ((name_size + 3) & ~3)
                if data_off + data_size <= len(self.raw):
                    raw_mime = self.raw[data_off:data_off + data_size]
                    return raw_mime.split(b"\x00", 1)[0].decode("utf-8", "replace").strip()
            except Exception:
                pass
        return ""


@dataclass
class ExportSummary:
    files_count: int = 0
    bytes_count: int = 0
    dirs_count: int = 0
    failures: list[str] = field(default_factory=list)
    renamed: list[tuple[str, str]] = field(default_factory=list)


def export_path(
    volume: Volume,
    befs_path: str,
    dest_path: str,
    recursive: bool = False,
    progress_callback = None,
) -> ExportSummary:
    """Export file or directory from volume to Windows destination.
    Sanitizes illegal Windows characters and records any renames.
    """
    summary = ExportSummary()
    inode = volume.lookup(befs_path)

    if inode.kind() != "dir":
        try:
            data = volume.read_file(inode)
            dest_dir = os.path.dirname(dest_path)
            if dest_dir:
                os.makedirs(dest_dir, exist_ok=True)
            with open(dest_path, "wb") as output:
                output.write(data)
            summary.files_count += 1
            summary.bytes_count += len(data)
            if progress_callback:
                progress_callback(befs_path, len(data))
        except Exception as exc:
            summary.failures.append(f"{befs_path}: {exc}")
        return summary

    if not recursive:
        raise IsADirectoryError(f"'{befs_path}' is a directory (use -r / --recursive to export)")

    os.makedirs(dest_path, exist_ok=True)
    summary.dirs_count += 1

    def _walk(curr_path: str, curr_dest: str):
        try:
            entries = volume.list_path(curr_path)
        except Exception as exc:
            summary.failures.append(f"{curr_path}: {exc}")
            return

        for name, child_inode in entries:
            if name in (".", ".."):
                continue
            safe_name, changed = sanitize_filename(name)
            sub_befs = f"{curr_path}/{name}".lstrip("/")
            if changed:
                summary.renamed.append((sub_befs, safe_name))
            target_child = os.path.join(curr_dest, safe_name)

            if child_inode.kind() == "dir":
                try:
                    os.makedirs(target_child, exist_ok=True)
                    summary.dirs_count += 1
                    _walk(sub_befs, target_child)
                except Exception as exc:
                    summary.failures.append(f"{sub_befs}: {exc}")
            else:
                try:
                    file_data = volume.read_file(child_inode)
                    with open(target_child, "wb") as f_out:
                        f_out.write(file_data)
                    summary.files_count += 1
                    summary.bytes_count += len(file_data)
                    if progress_callback:
                        progress_callback(sub_befs, len(file_data))
                except Exception as exc:
                    summary.failures.append(f"{sub_befs}: {exc}")

    _walk(befs_path.rstrip("/"), dest_path)
    return summary


class Volume:
    def __init__(self, path: str, offset: int | None) -> None:
        check_device_allowed(path)
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
    parser.add_argument("command", choices=("ls", "cat", "export", "drives"))
    parser.add_argument("image", nargs="?")
    parser.add_argument("path", nargs="?", default="/")
    parser.add_argument("dest", nargs="?")
    parser.add_argument("-r", "--recursive", action="store_true", help="Recursively export directories")
    parser.add_argument("--offset", type=int, default=None)
    args = parser.parse_args()

    if args.command == "drives":
        drives = list_removable_drives()
        if not drives:
            print("No removable drives found.")
            return 0
        for d in drives:
            size_gib = d["size"] / (1024**3) if d["size"] else 0
            print(f"{d['device_id']}: {d['model']} ({size_gib:.1f} GiB, {d['interface']})")
        return 0

    if not args.image:
        parser.error(f"{args.command} requires an image argument")

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
            if inode.kind() == "dir":
                summary = export_path(volume, args.path, args.dest, recursive=args.recursive)
                print(f"exported {summary.files_count} files ({summary.bytes_count} bytes) to {args.dest}")
                if summary.renamed:
                    print("renamed on extract for Windows compatibility:")
                    for orig, safe in summary.renamed:
                        print(f"  {orig} -> {safe}")
                if summary.failures:
                    print(f"failures ({len(summary.failures)}):")
                    for f in summary.failures:
                        print(f"  {f}")
            else:
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
