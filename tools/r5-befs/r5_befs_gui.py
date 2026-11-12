#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT
"""Read-only BeFS GUI reader for Windows.

Built with Python and tkinter only (no external GUI libraries).
Imports r5_befs.py as the underlying BeFS parsing engine.
"""

from __future__ import annotations

import ctypes
import datetime
import os
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import Any

# Ensure directory containing r5_befs is on path
_DIR = os.path.dirname(os.path.abspath(__file__))
if _DIR not in sys.path:
    sys.path.insert(0, _DIR)

import r5_befs


def is_admin() -> bool:
    """Return True if running with Windows Administrator privileges."""
    if sys.platform != "win32":
        return True
    try:
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except Exception:
        return False


def request_admin_elevation(target_script: str, args: list[str]) -> bool:
    """Request UAC elevation on Windows using ShellExecute runas."""
    if sys.platform != "win32":
        return False
    try:
        quoted_args = " ".join(f'"{a}"' if " " in a else a for a in [target_script] + args)
        ret = ctypes.windll.shell32.ShellExecuteW(
            None, "runas", sys.executable, quoted_args, None, 1
        )
        return ret > 32
    except Exception:
        return False


def format_size(size_bytes: int) -> str:
    """Format bytes into readable units."""
    if size_bytes < 0:
        return "-"
    if size_bytes < 1024:
        return f"{size_bytes} B"
    for unit in ("KiB", "MiB", "GiB", "TiB"):
        size_bytes /= 1024.0
        if size_bytes < 1024.0 or unit == "TiB":
            return f"{size_bytes:.1f} {unit}"
    return f"{size_bytes:.1f} PiB"


def format_timestamp(epoch_sec: int) -> str:
    """Format unix epoch seconds to local datetime string."""
    if epoch_sec <= 0:
        return "-"
    try:
        dt = datetime.datetime.fromtimestamp(epoch_sec)
        return dt.strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return "-"


class BefsGuiApp:
    def __init__(self, root: tk.Tk, initial_image: str | None = None) -> None:
        self.root = root
        self.root.title("R5 BeFS Reader")
        self.root.geometry("1060x680")
        self.root.minsize(820, 500)

        self.volume: r5_befs.Volume | None = None
        self.volume_path: str = ""
        self.current_dir: str = "/"
        self.current_entries: list[tuple[str, r5_befs.Inode]] = []
        self.filtered_entries: list[tuple[str, r5_befs.Inode]] = []
        self.tree_node_to_path: dict[str, str] = {}
        self.sort_column = "name"
        self.sort_reverse = False

        self._build_ui()

        if initial_image and os.path.exists(initial_image):
            self.root.after(100, lambda: self.open_image_path(initial_image))

    def _build_ui(self) -> None:
        # Style configuration
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except Exception:
            pass

        # Top Toolbar
        toolbar = ttk.Frame(self.root, padding=(8, 6))
        toolbar.pack(side=tk.TOP, fill=tk.X)

        self.btn_open_img = ttk.Button(toolbar, text="📁 Open Image…", command=self.on_open_image)
        self.btn_open_img.pack(side=tk.LEFT, padx=3)

        self.btn_open_usb = ttk.Button(toolbar, text="💾 Open USB Drive…", command=self.on_open_usb)
        self.btn_open_usb.pack(side=tk.LEFT, padx=3)

        ttk.Separator(toolbar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)

        self.btn_extract = ttk.Button(toolbar, text="📥 Extract…", command=self.on_extract, state=tk.DISABLED)
        self.btn_extract.pack(side=tk.LEFT, padx=3)

        ttk.Separator(toolbar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)

        # Search / Filter
        ttk.Label(toolbar, text="🔍 Filter:").pack(side=tk.LEFT, padx=(4, 2))
        self.search_var = tk.StringVar()
        self.search_var.trace_add("write", lambda *_: self.on_search_changed())
        self.search_entry = ttk.Entry(toolbar, textvariable=self.search_var, width=22)
        self.search_entry.pack(side=tk.LEFT, padx=2)

        self.btn_clear_search = ttk.Button(toolbar, text="✕", width=3, command=lambda: self.search_var.set(""))
        self.btn_clear_search.pack(side=tk.LEFT, padx=1)

        # Path display bar
        path_bar = ttk.Frame(self.root, padding=(8, 2))
        path_bar.pack(side=tk.TOP, fill=tk.X)
        ttk.Label(path_bar, text="Location:").pack(side=tk.LEFT, padx=(2, 4))
        self.lbl_path = ttk.Label(path_bar, text="/", font=("Segoe UI", 9, "bold"))
        self.lbl_path.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # Main Paned Window (Horizontal: Folders Tree Left, Files & Preview Right)
        self.main_pane = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        self.main_pane.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=6, pady=4)

        # Left Pane: Folders Tree
        left_frame = ttk.Frame(self.main_pane, padding=2)
        self.main_pane.add(left_frame, weight=1)

        tree_header = ttk.Label(left_frame, text="Folders", font=("Segoe UI", 9, "bold"))
        tree_header.pack(side=tk.TOP, anchor=tk.W, pady=(0, 2))

        tree_scroll_y = ttk.Scrollbar(left_frame, orient=tk.VERTICAL)
        self.dir_tree = ttk.Treeview(
            left_frame,
            selectmode="browse",
            yscrollcommand=tree_scroll_y.set,
            show="tree",
        )
        tree_scroll_y.config(command=self.dir_tree.yview)
        tree_scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        self.dir_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.dir_tree.bind("<<TreeviewSelect>>", self.on_tree_select)
        self.dir_tree.bind("<<TreeviewOpen>>", self.on_tree_expand)

        # Right Pane: Vertical Split (Files List on top, Preview on bottom)
        right_frame = ttk.Frame(self.main_pane, padding=2)
        self.main_pane.add(right_frame, weight=3)

        self.right_pane = ttk.PanedWindow(right_frame, orient=tk.VERTICAL)
        self.right_pane.pack(fill=tk.BOTH, expand=True)

        # File List Subframe
        list_frame = ttk.Frame(self.right_pane)
        self.right_pane.add(list_frame, weight=3)

        list_scroll_y = ttk.Scrollbar(list_frame, orient=tk.VERTICAL)
        list_scroll_x = ttk.Scrollbar(list_frame, orient=tk.HORIZONTAL)

        cols = ("name", "size", "modified", "mime")
        self.file_list = ttk.Treeview(
            list_frame,
            columns=cols,
            show="headings",
            selectmode="extended",
            yscrollcommand=list_scroll_y.set,
            xscrollcommand=list_scroll_x.set,
        )
        list_scroll_y.config(command=self.file_list.yview)
        list_scroll_x.config(command=self.file_list.xview)

        list_scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        list_scroll_x.pack(side=tk.BOTTOM, fill=tk.X)
        self.file_list.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.file_list.heading("name", text="Name", command=lambda: self.on_sort_column("name"))
        self.file_list.heading("size", text="Size", command=lambda: self.on_sort_column("size"))
        self.file_list.heading("modified", text="Date Modified", command=lambda: self.on_sort_column("modified"))
        self.file_list.heading("mime", text="BeOS MIME Type", command=lambda: self.on_sort_column("mime"))

        self.file_list.column("name", width=260, minwidth=140)
        self.file_list.column("size", width=100, minwidth=70, anchor=tk.E)
        self.file_list.column("modified", width=160, minwidth=120)
        self.file_list.column("mime", width=200, minwidth=100)

        self.file_list.bind("<<TreeviewSelect>>", self.on_file_select)
        self.file_list.bind("<Double-1>", self.on_file_double_click)
        self.file_list.bind("<Return>", self.on_file_double_click)

        # Preview Subframe
        preview_frame = ttk.Frame(self.right_pane)
        self.right_pane.add(preview_frame, weight=2)

        self.lbl_preview_title = ttk.Label(
            preview_frame,
            text="Preview: (Select a file to inspect)",
            font=("Segoe UI", 9, "bold"),
            padding=(2, 2),
        )
        self.lbl_preview_title.pack(side=tk.TOP, anchor=tk.W)

        preview_scroll_y = ttk.Scrollbar(preview_frame, orient=tk.VERTICAL)
        self.txt_preview = tk.Text(
            preview_frame,
            wrap=tk.WORD,
            font=("Consolas", 9),
            yscrollcommand=preview_scroll_y.set,
            bg="#fcfdfe",
            fg="#0f172a",
            relief=tk.SOLID,
            borderwidth=1,
        )
        preview_scroll_y.config(command=self.txt_preview.yview)
        preview_scroll_y.pack(side=tk.RIGHT, fill=tk.Y)
        self.txt_preview.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.txt_preview.config(state=tk.DISABLED)

        # Status Bar
        status_bar = ttk.Frame(self.root, padding=(8, 4), relief=tk.SUNKEN)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

        self.lbl_status = ttk.Label(status_bar, text="Ready. Open a BeFS image or USB drive to start.")
        self.lbl_status.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.progress = ttk.Progressbar(status_bar, orient=tk.HORIZONTAL, length=180, mode="determinate")
        # Hidden initially; packed dynamically during extraction

    # -------------------------------------------------------------
    # Volume Opening (Image / USB Drive)
    # -------------------------------------------------------------
    def on_open_image(self) -> None:
        path = filedialog.askopenfilename(
            title="Open BeFS Disk Image",
            filetypes=[
                ("BeFS / Disk Images", "*.iso *.image *.img *.raw"),
                ("All files", "*.*"),
            ],
        )
        if path:
            self.open_image_path(path)

    def open_image_path(self, path: str) -> None:
        try:
            if self.volume:
                self.volume.close()
                self.volume = None

            self.volume = r5_befs.Volume(path, offset=None)
            self.volume_path = path
            self.root.title(f"R5 BeFS Reader — [{self.volume.name}] {os.path.basename(path)}")
            self.lbl_status.config(
                text=f"Volume: {self.volume.name} | Block size: {self.volume.block_size} B | Base: {self.volume.base}"
            )
            self._load_tree_root()
        except PermissionError as pe:
            messagebox.showerror("Access Denied", str(pe))
        except Exception as exc:
            messagebox.showerror("Error Opening Volume", f"Could not read BeFS volume:\n{exc}")

    def on_open_usb(self) -> None:
        if sys.platform != "win32":
            messagebox.showinfo("USB Drives", "Physical USB drive detection is available on Windows.")
            return

        if not is_admin():
            answer = messagebox.askyesno(
                "Administrator Rights Required",
                "Opening physical USB drives directly on Windows requires Administrator privileges.\n\n"
                "Would you like to restart R5 BeFS Reader as Administrator?",
            )
            if answer:
                if request_admin_elevation(__file__, []):
                    self.root.destroy()
                else:
                    messagebox.showwarning("Elevation", "Failed to elevate. Please run as Administrator manually.")
            return

        drives = r5_befs.list_removable_drives()
        if not drives:
            messagebox.showinfo(
                "No Removable Drives",
                "No removable USB drives were detected.\n\n"
                "Note: PhysicalDrive0 (Windows NVMe system disk) is strictly excluded for safety.",
            )
            return

        # Drive selector dialog
        diag = tk.Toplevel(self.root)
        diag.title("Select USB Drive")
        diag.geometry("520x280")
        diag.transient(self.root)
        diag.grab_set()

        ttk.Label(diag, text="Select removable drive to open (strictly read-only):", font=("Segoe UI", 9, "bold")).pack(
            anchor=tk.W, padx=12, pady=(12, 6)
        )

        frame = ttk.Frame(diag, padding=10)
        frame.pack(fill=tk.BOTH, expand=True)

        listbox = tk.Listbox(frame, font=("Segoe UI", 9), selectmode=tk.SINGLE)
        listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb = ttk.Scrollbar(frame, command=listbox.yview)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        listbox.config(yscrollcommand=sb.set)

        for d in drives:
            sz_str = format_size(d["size"])
            listbox.insert(tk.END, f"{d['device_id']} — {d['model']} ({sz_str}, {d['interface']})")

        listbox.selection_set(0)

        btn_frame = ttk.Frame(diag, padding=10)
        btn_frame.pack(side=tk.BOTTOM, fill=tk.X)

        def do_open():
            sel = listbox.curselection()
            if not sel:
                return
            chosen = drives[sel[0]]
            dev_id = chosen["device_id"]
            diag.destroy()
            self.open_image_path(dev_id)

        ttk.Button(btn_frame, text="Open Drive", command=do_open).pack(side=tk.RIGHT, padx=4)
        ttk.Button(btn_frame, text="Cancel", command=diag.destroy).pack(side=tk.RIGHT, padx=4)

    # -------------------------------------------------------------
    # Folder Tree & File List Loading
    # -------------------------------------------------------------
    def _load_tree_root(self) -> None:
        self.dir_tree.delete(*self.dir_tree.get_children())
        self.tree_node_to_path.clear()

        vol_name = self.volume.name if self.volume else "Root"
        root_id = self.dir_tree.insert("", tk.END, text=f"📁 {vol_name} (/)", open=True)
        self.tree_node_to_path[root_id] = "/"

        # Populate first level of subdirectories
        self._populate_tree_subdirs(root_id, "/")
        self.dir_tree.selection_set(root_id)
        self.load_directory("/")

    def _populate_tree_subdirs(self, parent_id: str, befs_path: str) -> None:
        if not self.volume:
            return
        try:
            entries = self.volume.list_path(befs_path)
        except Exception:
            return

        for name, inode in entries:
            if name in (".", ".."):
                continue
            if inode.kind() == "dir":
                sub_path = f"{befs_path.rstrip('/')}/{name}"
                node_id = self.dir_tree.insert(parent_id, tk.END, text=f"📁 {name}", open=False)
                self.tree_node_to_path[node_id] = sub_path
                # Add dummy child so expand icon shows
                self.dir_tree.insert(node_id, tk.END, text="…")

    def on_tree_expand(self, event: Any) -> None:
        item_id = self.dir_tree.focus()
        if not item_id:
            return
        children = self.dir_tree.get_children(item_id)
        # Check if dummy child exists
        if len(children) == 1 and self.dir_tree.item(children[0], "text") == "…":
            self.dir_tree.delete(children[0])
            befs_path = self.tree_node_to_path.get(item_id, "/")
            self._populate_tree_subdirs(item_id, befs_path)

    def on_tree_select(self, event: Any) -> None:
        selected = self.dir_tree.selection()
        if not selected:
            return
        path = self.tree_node_to_path.get(selected[0], "/")
        self.load_directory(path)

    def load_directory(self, befs_path: str) -> None:
        if not self.volume:
            return
        self.current_dir = befs_path
        self.lbl_path.config(text=befs_path)

        try:
            self.current_entries = self.volume.list_path(befs_path)
        except Exception as exc:
            messagebox.showerror("Error Reading Folder", f"Failed to list {befs_path}:\n{exc}")
            self.current_entries = []

        self._filter_and_display()

    def on_search_changed(self) -> None:
        self._filter_and_display()

    def _filter_and_display(self) -> None:
        query = self.search_var.get().strip().lower()
        if not query:
            self.filtered_entries = [
                (name, inode) for name, inode in self.current_entries if name not in (".", "..")
            ]
        else:
            self.filtered_entries = [
                (name, inode)
                for name, inode in self.current_entries
                if name not in (".", "..") and query in name.lower()
            ]

        self._sort_filtered_entries()
        self._render_file_list()

    def on_sort_column(self, col: str) -> None:
        if self.sort_column == col:
            self.sort_reverse = not self.sort_reverse
        else:
            self.sort_column = col
            self.sort_reverse = False
        self._sort_filtered_entries()
        self._render_file_list()

    def _sort_filtered_entries(self) -> None:
        # Directories first, then files
        def key_func(item: tuple[str, r5_befs.Inode]) -> Any:
            name, inode = item
            is_dir = inode.kind() == "dir"
            if self.sort_column == "name":
                val = name.lower()
            elif self.sort_column == "size":
                val = -1 if is_dir else inode.size
            elif self.sort_column == "modified":
                val = inode.modified_time
            elif self.sort_column == "mime":
                val = inode.mime_type.lower()
            else:
                val = name.lower()
            return (not is_dir, val)

        self.filtered_entries.sort(key=key_func, reverse=self.sort_reverse)

    def _render_file_list(self) -> None:
        self.file_list.delete(*self.file_list.get_children())
        for name, inode in self.filtered_entries:
            is_dir = inode.kind() == "dir"
            icon = "📁 " if is_dir else ("🔗 " if inode.kind() == "link" else "📄 ")
            display_name = f"{icon}{name}"
            size_str = "-" if is_dir else format_size(inode.size)
            mod_str = format_timestamp(inode.modified_time)
            mime_str = inode.mime_type or ("Directory" if is_dir else "-")

            self.file_list.insert(
                "",
                tk.END,
                values=(display_name, size_str, mod_str, mime_str),
                tags=(name,),
            )

        self.btn_extract.config(state=tk.DISABLED)
        items_count = len(self.filtered_entries)
        vol_info = f"Volume: {self.volume.name}" if self.volume else ""
        self.lbl_status.config(text=f"{vol_info} | Folder: {self.current_dir} | {items_count} item(s)")

    # -------------------------------------------------------------
    # Selection & Preview
    # -------------------------------------------------------------
    def on_file_select(self, event: Any) -> None:
        selected = self.file_list.selection()
        if not selected:
            self.btn_extract.config(state=tk.DISABLED)
            return

        self.btn_extract.config(state=tk.NORMAL)
        if len(selected) == 1:
            item = self.file_list.item(selected[0])
            raw_name = item["tags"][0] if item["tags"] else ""
            match = next((ino for name, ino in self.filtered_entries if name == raw_name), None)
            if match:
                self.show_preview(raw_name, match)
        else:
            self.clear_preview(f"Multiple items selected ({len(selected)} items)")

    def on_file_double_click(self, event: Any) -> None:
        selected = self.file_list.selection()
        if not selected:
            return
        item = self.file_list.item(selected[0])
        raw_name = item["tags"][0] if item["tags"] else ""
        match = next((ino for name, ino in self.filtered_entries if name == raw_name), None)
        if match and match.kind() == "dir":
            new_path = f"{self.current_dir.rstrip('/')}/{raw_name}"
            self.load_directory(new_path)
            self._sync_tree_selection(new_path)

    def _sync_tree_selection(self, target_path: str) -> None:
        for node_id, path in self.tree_node_to_path.items():
            if path == target_path:
                self.dir_tree.selection_set(node_id)
                self.dir_tree.see(node_id)
                break

    def show_preview(self, name: str, inode: r5_befs.Inode) -> None:
        self.txt_preview.config(state=tk.NORMAL)
        self.txt_preview.delete("1.0", tk.END)

        if inode.kind() == "dir":
            self.lbl_preview_title.config(text=f"Preview: Folder '{name}'")
            self.txt_preview.insert(tk.END, f"Folder: {name}\nDouble-click to navigate into this directory.")
            self.txt_preview.config(state=tk.DISABLED)
            return

        size_str = format_size(inode.size)
        mime_str = f" | MIME: {inode.mime_type}" if inode.mime_type else ""
        self.lbl_preview_title.config(text=f"Preview: {name} ({size_str}{mime_str}) — First 64 KB")

        if inode.size == 0:
            self.txt_preview.insert(tk.END, "[Empty file]")
            self.txt_preview.config(state=tk.DISABLED)
            return

        try:
            # Read at most 64 KB for preview
            data = self.volume.read_file(inode)[: 64 * 1024]
            # Try decoding text
            is_text = False
            for enc in ("utf-8", "latin-1", "ascii"):
                try:
                    text_content = data.decode(enc)
                    # Check for high ratio of printable chars
                    null_count = text_content.count("\x00")
                    if null_count == 0 or (len(text_content) > 0 and null_count / len(text_content) < 0.01):
                        is_text = True
                        self.txt_preview.insert(tk.END, text_content)
                        if inode.size > 64 * 1024:
                            self.txt_preview.insert(
                                tk.END,
                                f"\n\n--- [Preview truncated at 64 KB. Total file size: {format_size(inode.size)}] ---",
                            )
                        break
                except UnicodeDecodeError:
                    continue

            if not is_text:
                # Binary hex preview (first 1 KB)
                hex_dump = []
                sample = data[:1024]
                for i in range(0, len(sample), 16):
                    chunk = sample[i : i + 16]
                    hex_bytes = " ".join(f"{b:02x}" for b in chunk).ljust(48)
                    ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                    hex_dump.append(f"{i:08x}  {hex_bytes}  |{ascii_str}|")
                self.txt_preview.insert(
                    tk.END,
                    f"Binary file ({format_size(inode.size)}). Hex dump of first {len(sample)} bytes:\n\n"
                    + "\n".join(hex_dump),
                )
        except Exception as exc:
            self.txt_preview.insert(tk.END, f"[Error reading file preview: {exc}]")

        self.txt_preview.config(state=tk.DISABLED)

    def clear_preview(self, msg: str = "") -> None:
        self.lbl_preview_title.config(text="Preview:")
        self.txt_preview.config(state=tk.NORMAL)
        self.txt_preview.delete("1.0", tk.END)
        if msg:
            self.txt_preview.insert(tk.END, msg)
        self.txt_preview.config(state=tk.DISABLED)

    # -------------------------------------------------------------
    # Extraction
    # -------------------------------------------------------------
    def on_extract(self) -> None:
        selected = self.file_list.selection()
        if not selected or not self.volume:
            return

        dest_dir = filedialog.askdirectory(title="Select Destination Folder on Windows")
        if not dest_dir:
            return

        selected_items = [
            self.file_list.item(s)["tags"][0]
            for s in selected
            if self.file_list.item(s)["tags"]
        ]

        # Launch extraction in background thread with progress
        self.progress.pack(side=tk.RIGHT, padx=8)
        self.progress["value"] = 0
        self.progress["maximum"] = 100
        self.btn_extract.config(state=tk.DISABLED)

        def worker():
            total_summary = r5_befs.ExportSummary()
            for idx, item_name in enumerate(selected_items):
                befs_path = f"{self.current_dir.rstrip('/')}/{item_name}"
                target_dest = os.path.join(dest_dir, item_name)

                def update_prog(cur_file: str, bytes_written: int):
                    self.root.after(0, lambda: self.lbl_status.config(text=f"Extracting: {cur_file}"))

                sub_sum = r5_befs.export_path(
                    self.volume,
                    befs_path,
                    target_dest,
                    recursive=True,
                    progress_callback=update_prog,
                )
                total_summary.files_count += sub_sum.files_count
                total_summary.bytes_count += sub_sum.bytes_count
                total_summary.dirs_count += sub_sum.dirs_count
                total_summary.failures.extend(sub_sum.failures)
                total_summary.renamed.extend(sub_sum.renamed)

                pct = int(((idx + 1) / len(selected_items)) * 100)
                self.root.after(0, lambda p=pct: self.progress.config(value=p))

            self.root.after(0, lambda: self._on_extract_finished(total_summary, dest_dir))

        threading.Thread(target=worker, daemon=True).start()

    def _on_extract_finished(self, summary: r5_befs.ExportSummary, dest_dir: str) -> None:
        self.progress.pack_forget()
        self.btn_extract.config(state=tk.NORMAL)
        self.lbl_status.config(text="Extraction complete.")

        lines = [
            f"Extracted to: {dest_dir}",
            f"Files extracted: {summary.files_count}",
            f"Directories created: {summary.dirs_count}",
            f"Total size: {format_size(summary.bytes_count)} ({summary.bytes_count} bytes)",
        ]

        if summary.renamed:
            lines.append("")
            lines.append(f"Windows name sanitizations ({len(summary.renamed)}):")
            for orig, safe in summary.renamed[:15]:
                lines.append(f"  • {orig} -> {safe}")
            if len(summary.renamed) > 15:
                lines.append(f"  … and {len(summary.renamed) - 15} more.")

        if summary.failures:
            lines.append("")
            lines.append(f"Failures ({len(summary.failures)}):")
            for fail in summary.failures[:10]:
                lines.append(f"  ⚠ {fail}")
            messagebox.showwarning("Extraction Finished with Errors", "\n".join(lines))
        else:
            messagebox.showinfo("Extraction Summary", "\n".join(lines))


def main() -> int:
    initial_img = sys.argv[1] if len(sys.argv) > 1 else None
    root = tk.Tk()
    app = BefsGuiApp(root, initial_image=initial_img)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
