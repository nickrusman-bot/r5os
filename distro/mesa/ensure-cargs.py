#!/usr/bin/env python3
"""Keep VK_USE_PLATFORM_VIEW_RFIVE on an existing Mesa build directory.

Meson does not refresh c_args from the cross file once the build directory
exists. The cross file is still the source for a new directory.
"""
import pickle
import sys
from pathlib import Path

sys.path.insert(0, "/home/nickr/r5os/mesa-build/meson-1.7.0")
from mesonbuild.options import OptionKey

ARGS = [
    "-DVK_USE_PLATFORM_VIEW_RFIVE",
    "-I/home/nickr/r5os/r5-sysroot/include",
    "-include",
    "/home/nickr/r5os/r5-sysroot/include/r5/haiku_ioc.h",
]


def main() -> None:
    path = Path("/home/nickr/r5os/mesa-build/mesa-25.3.6/build-radv/meson-private/coredata.dat")
    data = pickle.loads(path.read_bytes())
    data.set_option(OptionKey("c_args"), ARGS)
    # M11 step 3b: the view-surface WSI (C++) uses AppServerLink and
    # ServerProtocol.h. Last in the search order, so they hide nothing of
    # Mesa's own headers.
    data.set_option(OptionKey("cpp_args"), ARGS + [
        "-idirafter", "/home/nickr/r5os/haiku/headers/private/app",
        "-idirafter", "/home/nickr/r5os/haiku/headers/private/shared",
    ])
    path.write_bytes(pickle.dumps(data))
    print("c_args", data.get_option(OptionKey("c_args")))


if __name__ == "__main__":
    main()
