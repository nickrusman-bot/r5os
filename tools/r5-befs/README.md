# r5-befs

Read-only BeFS reader for Windows. It opens a disk image or, with administrator rights, a physical drive. It lists directories, prints a file, and exports a file.

There is no separate R5 toolchain repository. This tool lives in the R5OS tree and is run with the Python 3 that is already used for the image scripts.

The on-disk layout is the Be File System: the superblock, inodes, and directory B+ trees. This program is new R5OS code under GPL-3.0-or-later. It does not copy the Haiku BFS sources. Those stay under the MIT license in a Haiku tree (`src/add-ons/kernel/file_systems/bfs`).

## Use

```text
py -3 tools/r5-befs/r5_befs.py ls IMAGE [path]
py -3 tools/r5-befs/r5_befs.py cat IMAGE path
py -3 tools/r5-befs/r5_befs.py export IMAGE path DEST
```

`path` is relative to the volume root, with `/` separators. A raw BeFS image is detected from the superblock. An anyboot image is detected from an MBR partition of type `eb`, or from a GPT entry whose type starts with the BeFS bytes. `--offset BYTES` skips that search.

```text
py -3 tools/r5-befs/r5_befs.py ls C:\Users\nickr\AppData\Local\Temp\r5-os-anyboot.iso /
py -3 tools/r5-befs/r5_befs.py cat \\.\PhysicalDrive1 home/r5-cpu.txt --offset 6291456
```

The physical-drive form needs an elevated prompt. Disk 0 on the reference machine is the Windows NVMe. Do not pass that disk.

Attributes and a drive letter (Dokan or WinFsp) are not implemented. Directory indexes that use duplicate-key fragments are not implemented; a normal directory listing does not need them.

## Check

Against the BeFS partition copied from the Ryzen 5 5625U stick on 3 Nov 2026: `ls /` showed `home` and `system`, `export` of `system/var/log/syslog` wrote 155889 bytes, and that file contains `device 15e7`. The same reader opened `r5-os-anyboot.iso` at byte offset 6291456 without `--offset`.
