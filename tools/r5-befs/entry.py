#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Dual-mode entry point for standalone r5-befs binary.

Without arguments: launches the BeFS GUI reader.
With arguments: runs the r5_befs command-line interface.
"""

from __future__ import annotations

import os
import sys

_DIR = os.path.dirname(os.path.abspath(__file__))
if _DIR not in sys.path:
    sys.path.insert(0, _DIR)


def main() -> int:
    if len(sys.argv) <= 1:
        import r5_befs_gui
        return r5_befs_gui.main()
    else:
        import r5_befs
        return r5_befs.main()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (FileNotFoundError, NotADirectoryError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
