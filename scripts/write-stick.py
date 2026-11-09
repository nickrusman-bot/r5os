#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write an R5 image to the USB stick (the General UDisk) and read it back.

The one tool for writing a stick on Windows. Run it from Windows Python:

    py scripts\\write-stick.py generated\\r5-os-m5b-stick.img
    py scripts\\write-stick.py IMAGE --expect "gfx9_ring true" --expect "gpu_render true"

It asks for elevation itself (UAC) when it is not elevated, waits, and
prints the log. The log is also written next to the image
(<image>.write.log).

Safety:
- It writes only to a disk whose FriendlyName is the stick name (default
  "General UDisk") with BusType USB, and only when exactly one such disk
  exists. Disk 0 is refused everywhere, also by path.
- The image must fit on the stick. Its size is read from the file.
- After writing, it reads the whole image size back from the stick and
  compares the SHA-256 with the image. A mismatch is an error.

When this script needs a change (a larger image, another stick, a new
Windows quirk), change this file. Do not write a new writer, and do not
use PowerShell, dd or another tool to write a stick.
"""
import argparse
import ctypes
import hashlib
import json
import os
import subprocess
import sys
import time
from ctypes import wintypes
from datetime import datetime

CHUNK = 1024 * 1024
SAMPLE = 65536

FSCTL_LOCK_VOLUME = 0x00090018
FSCTL_DISMOUNT_VOLUME = 0x00090020
IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS = 0x00560000
IOCTL_STORAGE_QUERY_PROPERTY = 0x002D1400
IOCTL_DISK_UPDATE_PROPERTIES = 0x00070140
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ = 0x1
FILE_SHARE_WRITE = 0x2
OPEN_EXISTING = 3
BUS_TYPE_USB = 7
ERROR_ACCESS_DENIED = 5
SEE_MASK_NOCLOSEPROCESS = 0x40
INFINITE = 0xFFFFFFFF

if os.name != "nt":
	raise SystemExit("write-stick.py runs on Windows only (Windows Python, not WSL)")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
shell32 = ctypes.WinDLL("shell32", use_last_error=True)
INVALID = wintypes.HANDLE(-1).value

kernel32.CreateFileW.argtypes = [
	wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
	wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.DeviceIoControl.argtypes = [
	wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
	wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
	wintypes.LPVOID,
]
kernel32.DeviceIoControl.restype = wintypes.BOOL
kernel32.ReadFile.argtypes = [
	wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
	ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
]
kernel32.WriteFile.argtypes = [
	wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
	ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
]
kernel32.SetFilePointerEx.argtypes = [
	wintypes.HANDLE, ctypes.c_longlong, ctypes.POINTER(ctypes.c_longlong),
	wintypes.DWORD,
]
kernel32.FindFirstVolumeW.argtypes = [wintypes.LPWSTR, wintypes.DWORD]
kernel32.FindFirstVolumeW.restype = wintypes.HANDLE
kernel32.FindNextVolumeW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD]
kernel32.FindNextVolumeW.restype = wintypes.BOOL
kernel32.FindVolumeClose.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.FlushFileBuffers.argtypes = [wintypes.HANDLE]
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]


class SHELLEXECUTEINFOW(ctypes.Structure):
	_fields_ = [
		("cbSize", wintypes.DWORD), ("fMask", wintypes.ULONG),
		("hwnd", wintypes.HWND), ("lpVerb", wintypes.LPCWSTR),
		("lpFile", wintypes.LPCWSTR), ("lpParameters", wintypes.LPCWSTR),
		("lpDirectory", wintypes.LPCWSTR), ("nShow", ctypes.c_int),
		("hInstApp", wintypes.HINSTANCE), ("lpIDList", wintypes.LPVOID),
		("lpClass", wintypes.LPCWSTR), ("hkeyClass", wintypes.HKEY),
		("dwHotKey", wintypes.DWORD), ("hIconOrMonitor", wintypes.HANDLE),
		("hProcess", wintypes.HANDLE),
	]


def die(message):
	raise SystemExit("ERROR: " + message)


def is_admin():
	try:
		return bool(shell32.IsUserAnAdmin())
	except OSError:
		return False


def relaunch_elevated(log_path):
	"""Run this script again elevated, wait for it, print its log."""
	params = subprocess.list2cmdline([os.path.abspath(__file__)] + sys.argv[1:])
	info = SHELLEXECUTEINFOW()
	info.cbSize = ctypes.sizeof(info)
	info.fMask = SEE_MASK_NOCLOSEPROCESS
	info.lpVerb = "runas"
	info.lpFile = sys.executable
	info.lpParameters = params
	info.lpDirectory = os.getcwd()
	info.nShow = 0
	if not shell32.ShellExecuteExW(ctypes.byref(info)):
		die(f"elevation was refused or failed ({ctypes.get_last_error()})")
	kernel32.WaitForSingleObject(info.hProcess, INFINITE)
	code = wintypes.DWORD()
	kernel32.GetExitCodeProcess(info.hProcess, ctypes.byref(code))
	kernel32.CloseHandle(info.hProcess)
	if os.path.exists(log_path):
		with open(log_path, encoding="utf-8") as log:
			sys.stdout.write(log.read())
	sys.exit(code.value)


def open_path(path, write):
	if "PhysicalDrive0" in path.replace(" ", ""):
		die("refusing disk 0")
	access = GENERIC_READ | GENERIC_WRITE if write else GENERIC_READ
	handle = kernel32.CreateFileW(path, access, 0, None, OPEN_EXISTING, 0, None)
	if handle == INVALID:
		handle = kernel32.CreateFileW(
			path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
			None, OPEN_EXISTING, 0, None)
	if handle == INVALID:
		die(f"open {path} failed {ctypes.get_last_error()}")
	return handle


def ioctl(handle, code, in_buf, out_size):
	out = ctypes.create_string_buffer(out_size)
	got = wintypes.DWORD()
	ok = kernel32.DeviceIoControl(
		handle, code, in_buf, ctypes.sizeof(in_buf) if in_buf is not None else 0,
		out, out_size, ctypes.byref(got), None)
	if not ok:
		return None
	return out.raw[:got.value]


def find_stick(name, image_size):
	script = ("Get-Disk | Select-Object Number,FriendlyName,BusType,Size "
		"| ConvertTo-Json -Compress")
	raw = subprocess.check_output(
		["powershell", "-NoProfile", "-Command", script], text=True)
	disks = json.loads(raw)
	if isinstance(disks, dict):
		disks = [disks]
	chosen = []
	for disk in disks:
		number = int(disk["Number"])
		print(f"disk {number}: {disk['FriendlyName']} {disk['BusType']} {disk['Size']}")
		if number == 0:
			continue
		if str(disk["FriendlyName"]) == name and str(disk["BusType"]) == "USB":
			chosen.append((number, int(disk["Size"])))
	if not chosen:
		die(f"no USB disk named {name!r}")
	if len(chosen) > 1:
		die(f"more than one USB disk named {name!r}")
	number, size = chosen[0]
	if number == 0:
		die("refusing disk 0")
	if image_size > size:
		die(f"image is {image_size} bytes, the stick holds {size}")
	drive = "\\\\.\\PhysicalDrive%d" % number
	# Ask the device itself as well, not only Get-Disk.
	handle = open_path(drive, False)
	query = (ctypes.c_uint32 * 3)(0, 0, 0)
	prop = ioctl(handle, IOCTL_STORAGE_QUERY_PROPERTY, query, 512)
	kernel32.CloseHandle(handle)
	if not prop or len(prop) < 32:
		die("the stick did not answer a property query")
	bus = int.from_bytes(prop[28:32], "little")
	text = b""
	for at in (int.from_bytes(prop[12:16], "little"), int.from_bytes(prop[16:20], "little")):
		if 0 < at < len(prop):
			text += prop[at:prop.find(b"\x00", at)]
	print(f"stick: disk {number}, bus {bus}, device {text.decode('ascii', 'replace')!r}, {size} bytes")
	if bus != BUS_TYPE_USB:
		die("the chosen disk is not on USB")
	return number, drive


def volume_names():
	name = ctypes.create_unicode_buffer(256)
	found = kernel32.FindFirstVolumeW(name, 256)
	if found == INVALID:
		die(f"FindFirstVolume failed {ctypes.get_last_error()}")
	names = []
	while True:
		names.append(name.value)
		if not kernel32.FindNextVolumeW(found, name, 256):
			break
	kernel32.FindVolumeClose(found)
	return names


def volume_disks(path):
	handle = kernel32.CreateFileW(
		path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		None, OPEN_EXISTING, 0, None)
	if handle == INVALID:
		return []
	raw = ioctl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, None, 256)
	kernel32.CloseHandle(handle)
	if not raw or len(raw) < 8:
		return []
	count = int.from_bytes(raw[0:4], "little")
	return [int.from_bytes(raw[8 + i * 24:12 + i * 24], "little")
		for i in range(count) if 8 + i * 24 + 24 <= len(raw)]


def lock_stick_volumes(number):
	held = []
	for name in volume_names():
		path = name[:-1] if name.endswith("\\") else name
		disks = volume_disks(path)
		if not disks or any(disk != number for disk in disks):
			continue
		handle = open_path(path, True)
		got = wintypes.DWORD()
		locked = kernel32.DeviceIoControl(
			handle, FSCTL_LOCK_VOLUME, None, 0, None, 0, ctypes.byref(got), None)
		dismounted = kernel32.DeviceIoControl(
			handle, FSCTL_DISMOUNT_VOLUME, None, 0, None, 0, ctypes.byref(got), None)
		print(f"volume {path}: lock {bool(locked)} dismount {bool(dismounted)}")
		held.append(handle)
	return held


def seek(handle, offset):
	new = ctypes.c_longlong()
	if not kernel32.SetFilePointerEx(handle, offset, ctypes.byref(new), 0):
		die(f"seek {offset} failed {ctypes.get_last_error()}")


def read_exact(handle, size):
	buf = ctypes.create_string_buffer(size)
	got = wintypes.DWORD()
	if not kernel32.ReadFile(handle, buf, size, ctypes.byref(got), None):
		die(f"read failed {ctypes.get_last_error()}")
	if got.value != size:
		die(f"short read {got.value}")
	return buf.raw


def write_block(handle, block):
	buf = ctypes.create_string_buffer(block)
	got = wintypes.DWORD()
	ok = kernel32.WriteFile(handle, buf, len(block), ctypes.byref(got), None)
	return ok and got.value == len(block), ctypes.get_last_error()


def hash_image(image_path, expects):
	digest = hashlib.sha256()
	wanted = [e.encode() for e in expects]
	found = set()
	keep = max((len(w) for w in wanted), default=0)
	tail = b""
	with open(image_path, "rb") as image:
		while True:
			block = image.read(CHUNK)
			if not block:
				break
			digest.update(block)
			if wanted:
				window = tail + block
				for w in wanted:
					if w in window:
						found.add(w)
				tail = window[-keep:]
	missing = [w.decode() for w in wanted if w not in found]
	if missing:
		die("not in the image: " + ", ".join(missing))
	return digest.hexdigest()


def clear_partition_table(handle):
	"""Windows mounts the stick's partitions while we write and then refuses
	writes there (error 5). Clear the MBR table, let Windows drop them, go on."""
	seek(handle, 0)
	sector = bytearray(read_exact(handle, 512))
	sector[446:510] = b"\x00" * 64
	seek(handle, 0)
	ok, err = write_block(handle, bytes(sector))
	if not ok:
		die(f"clear partition table failed {err}")
	kernel32.FlushFileBuffers(handle)
	got = wintypes.DWORD()
	kernel32.DeviceIoControl(
		handle, IOCTL_DISK_UPDATE_PROPERTIES, None, 0, None, 0, ctypes.byref(got), None)
	print("partition table cleared, writing on")
	time.sleep(2)


def write_and_check(drive, image_path, size, expected):
	samples = {}
	with open(image_path, "rb") as image:
		for mark in sorted({0, min(6 * CHUNK, size - SAMPLE), size - SAMPLE}):
			image.seek(mark)
			samples[mark] = image.read(SAMPLE)
	handle = open_path(drive, True)
	cleared = False
	try:
		written = 0
		with open(image_path, "rb") as image:
			while True:
				block = image.read(CHUNK)
				if not block:
					break
				ok, err = write_block(handle, block)
				if not ok and err == ERROR_ACCESS_DENIED and written > 0 and not cleared:
					clear_partition_table(handle)
					cleared = True
					seek(handle, written)
					ok, err = write_block(handle, block)
				if not ok:
					die(f"write at {written} failed {err}")
				written += len(block)
				if written % (256 * CHUNK) == 0 or written == size:
					print(f"wrote {written} of {size} at {datetime.now():%H:%M:%S}")
			if cleared:
				image.seek(0)
				seek(handle, 0)
				ok, err = write_block(handle, image.read(512))
				if not ok:
					die(f"restore boot sector failed {err}")
				print("boot sector restored")
		if written != size:
			die(f"wrote {written} of {size}")
		kernel32.FlushFileBuffers(handle)
		for mark, sample in samples.items():
			seek(handle, mark)
			if read_exact(handle, len(sample)) != sample:
				die(f"read-back differs at {mark}")
		seek(handle, 0)
		check = hashlib.sha256()
		left = size
		while left:
			take = min(CHUNK, left)
			check.update(read_exact(handle, take))
			left -= take
		print("stick sha256", check.hexdigest())
		if check.hexdigest() != expected:
			die("the stick differs from the image")
		print("stick matches the image")
	finally:
		kernel32.CloseHandle(handle)


def main():
	parser = argparse.ArgumentParser(description="Write an R5 image to the USB stick.")
	parser.add_argument("image")
	parser.add_argument("--expect", action="append", default=[],
		help="text that must be in the image (repeatable), e.g. a settings line")
	parser.add_argument("--name", default="General UDisk",
		help="FriendlyName of the stick (default: General UDisk)")
	args = parser.parse_args()
	image_path = os.path.abspath(args.image)
	if not os.path.isfile(image_path):
		die(f"no image at {image_path}")
	log_path = image_path + ".write.log"
	if not is_admin():
		relaunch_elevated(log_path)

	log = open(log_path, "w", encoding="utf-8", buffering=1)
	sys.stdout = sys.stderr = log
	try:
		print("start", datetime.now().isoformat(timespec="seconds"))
		size = os.path.getsize(image_path)
		print(f"image {image_path}, {size} bytes")
		number, drive = find_stick(args.name, size)
		expected = hash_image(image_path, args.expect)
		print("image sha256", expected)
		held = lock_stick_volumes(number)
		try:
			write_and_check(drive, image_path, size, expected)
		finally:
			for handle in held:
				kernel32.CloseHandle(handle)
		print("done", datetime.now().isoformat(timespec="seconds"))
	except SystemExit as stop:
		if stop.code not in (None, 0):
			print(stop.code)
			sys.exit(1)
		raise


if __name__ == "__main__":
	main()
