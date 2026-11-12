# r5-befs

Read-only BeFS reader for Windows. It opens a disk image (`.iso`, `.image`, `.img`, `.raw`) or, with administrator rights, a physical removable drive. It lists directories, prints files, exports files and folders recursively, and provides a graphical explorer with text preview.

There is no separate R5 toolchain repository. This tool lives in the R5OS tree and is run with Python 3 or as a standalone Windows executable.

The on-disk layout is the Be File System: the superblock, inodes, small_data attributes, and directory B+ trees. This program is R5OS code under the MIT License. It does not copy the Haiku BFS sources. Those stay under the MIT license in a Haiku tree (`src/add-ons/kernel/file_systems/bfs`).

## Graphical User Interface (GUI)

Run the GUI with Python:

```text
py -3 tools/r5-befs/r5_befs_gui.py [IMAGE]
```

Or run the standalone binary without arguments:

```text
tools\r5-befs\r5-befs.exe
```

Features:
- **Open image…**: Browse and inspect `.iso`, `.image`, `.img`, or `.raw` files.
- **Open USB drive…**: Detects and lists only removable USB storage drives. Strictly excludes Disk 0 (Windows system NVMe disk). Prompts for administrator elevation if needed.
- **Folder Tree & File List**: Left pane displays hierarchical directory tree; right pane displays file names, human-readable sizes, modification timestamps, and BeOS MIME types.
- **Text Preview**: Preview pane displaying the first 64 KB of selected text files (e.g. `syslog`, scripts, settings).
- **Search Filter**: Real-time name search filtering the currently viewed folder.
- **Recursive Extraction**: Select one or more files or folders and click **Extract…** to copy them to a chosen Windows folder, complete with a progress bar and completion summary.
- **Windows Name Sanitization**: Replaces BeFS characters illegal on Windows (`:`, `*`, `?`, `"`, `<`, `>`, `|`, and trailing dots/spaces) with `_` and reports all renames in the summary.

## Command Line Interface (CLI)

```text
py -3 tools/r5-befs/r5_befs.py ls IMAGE [path]
py -3 tools/r5-befs/r5_befs.py cat IMAGE path
py -3 tools/r5-befs/r5_befs.py export [-r] IMAGE path DEST
py -3 tools/r5-befs/r5_befs.py drives
```

Or with the standalone `r5-befs.exe`:

```text
tools\r5-befs\r5-befs.exe ls IMAGE [path]
tools\r5-befs\r5-befs.exe cat IMAGE path
tools\r5-befs\r5-befs.exe export [-r] IMAGE path DEST
tools\r5-befs\r5-befs.exe drives
```

`path` is relative to the volume root, with `/` separators. A raw BeFS image is detected from the superblock. An anyboot image is detected from an MBR partition of type `eb`, or from a GPT entry whose type starts with the BeFS bytes. `--offset BYTES` skips that search.

```text
py -3 tools/r5-befs/r5_befs.py ls C:\Users\nickr\AppData\Local\Temp\r5-os-anyboot.iso /
py -3 tools/r5-befs/r5_befs.py export -r C:\Users\nickr\AppData\Local\Temp\r5-os-anyboot.iso home C:\Temp\r5-home
py -3 tools/r5-befs/r5_befs.py drives
```

## Building the Standalone Binary

Run the PowerShell build script:

```powershell
powershell -ExecutionPolicy Bypass -File tools/r5-befs/build-exe.ps1
```

This uses PyInstaller (GPL-2.0-or-later with bootloader exception) to package `entry.py`, `r5_befs.py`, and `r5_befs_gui.py` into a single standalone binary: `tools/r5-befs/r5-befs.exe`.
- Without arguments: opens the GUI.
- With arguments: runs the CLI commands (`ls`, `cat`, `export`, `drives`).

## Limits

- **Strictly read-only**: disks and images are opened with read access only (`open(..., "rb")`); no code path writes to the source.
- **Disk 0 Protection**: PhysicalDrive0 (the Windows NVMe drive on the reference machine) and the Windows system drive are rejected under all circumstances, even if explicitly requested on the command line.
- **Attributes**: attributes other than the BeOS MIME type (`BEOS:TYPE` from small_data), and drive letter mounting (Dokan/WinFsp), are out of scope.
- Directory indexes using duplicate-key fragments are not implemented; normal directory listings do not need them.

## Checks

- Against `r5-os-anyboot.iso`: `ls /` showed `home` and `system`; `cat home/config/settings/first_login` printed `1`; single-file `export` of `home/config/settings/first_login` wrote 2 bytes identical to previous output.
- Recursive export: `export -r` of `home` extracted 66 files and 56 directories (27,062 bytes) with automatic Windows filename sanitization.
- GUI check: opened `r5-os-anyboot.iso`, inspected `system/settings/bashrc` in preview pane (130 B, MIME `text/plain`), filtered for `bash`, and performed recursive extraction of `home/`.
- Protection check: `ls \\.\PhysicalDrive0` rejected with `PermissionError: PhysicalDrive0 (disk 0) is forbidden: protected system disk`.
