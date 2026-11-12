#!/usr/bin/env python3
"""Copy R5OS kernel sources over the Haiku tree. Jam and launch hooks stay."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def install_kernel_sources() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    files = (
        "src/system/kernel/arch/x86/arch_timer.cpp",
        "src/system/kernel/arch/x86/arch_cpu.cpp",
        "src/system/kernel/arch/x86/arch_vm.cpp",
        "headers/private/kernel/ksignal.h",
        "src/system/kernel/signal.cpp",
        "src/system/kernel/thread.cpp",
        "src/system/kernel/vm/vm.cpp",
        "headers/private/kernel/vm/vm.h",
        "headers/private/kernel/arch/vm.h",
        "src/system/runtime_loader/arch/x86_64/arch_relocate.cpp",
        "src/system/runtime_loader/export.cpp",
        "headers/private/runtime_loader/runtime_loader.h",
        "src/system/libroot/os/thread.c",
        "src/system/libroot/posix/pthread/pthread.cpp",
        "src/system/libroot/posix/time/timer_support.cpp",
        "src/add-ons/kernel/bus_managers/pci/pci.cpp",
        "src/add-ons/kernel/busses/pci/ecam/ECAMPCIControllerACPI.cpp",
        "src/add-ons/kernel/busses/pci/x86/X86PCIController.cpp",
        "src/add-ons/kernel/busses/pci/x86/X86PCIController.h",
        "src/add-ons/kernel/busses/pci/x86/kernel_interface.cpp",
        "src/system/kernel/vm/vm_page_writer.cpp",
        "src/system/kernel/device_manager/IORequest.cpp",
        "src/system/kernel/device_manager/IORequest.h",
        "src/system/kernel/debug/user_debugger.cpp",
        "src/system/kernel/scheduler/scheduler.cpp",
        "src/add-ons/kernel/file_systems/bfs/Volume.cpp",
        "src/add-ons/kernel/drivers/disk/usb/usb_disk/usb_disk.cpp",
        "src/add-ons/kernel/file_systems/bfs/kernel_interface.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/Driver.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/HIDDevice.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/HIDDevice.h",
        "src/add-ons/kernel/drivers/input/i2c_hid/InputReadyLine.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/InputReadyLine.h",
        "src/add-ons/kernel/drivers/input/i2c_hid/Jamfile",
        "src/add-ons/kernel/drivers/input/i2c_hid/ProtocolHandler.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/TouchpadProtocolHandler.cpp",
        "src/add-ons/kernel/drivers/input/i2c_hid/TouchpadProtocolHandler.h",
        "src/add-ons/kernel/drivers/input/hid_shared/HIDReportItem.h",
        "src/add-ons/kernel/drivers/input/i2c_elan/Driver.cpp",
        "src/add-ons/kernel/drivers/input/i2c_elan/ELANDevice.cpp",
        "src/add-ons/kernel/drivers/input/i2c_elan/ELANDevice.h",
        "src/add-ons/kernel/drivers/input/i2c_elan/Jamfile",
        "src/add-ons/kernel/bus_managers/ps2/ps2_dev.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_dev.h",
        "src/add-ons/kernel/bus_managers/ps2/ps2_service.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_service.h",
        "src/add-ons/kernel/bus_managers/ps2/ps2_standard_mouse.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_synaptics.cpp",
        "src/add-ons/kernel/bus_managers/ps2/ps2_synaptics.h",
        "src/add-ons/kernel/bus_managers/ps2/ps2_trackpoint.cpp",
    )
    for relative in files:
        install_file(
            root / "distro" / "kernel" / relative,
            HAIKU / relative,
            relative,
        )


def install_smp_boot() -> None:
    src = pathlib.Path(__file__).resolve().parents[1] / "distro/boot/bios_ia32/smp.cpp"
    dst = HAIKU / "src/system/boot/platform/bios_ia32/smp.cpp"
    if not src.is_file():
        raise SystemExit(f"missing {src}")
    if not dst.parent.is_dir():
        raise SystemExit(f"missing {dst.parent}")
    data = src.read_bytes().replace(b"\r\n", b"\n")
    if dst.is_file() and dst.read_bytes().replace(b"\r\n", b"\n") == data:
        print(f"already {dst.name} (smp boot timeout)")
        return
    dst.write_bytes(data)
    print(f"copied {dst} (smp boot timeout)")


def insert_once(path: pathlib.Path, old: str, new: str, marker: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if marker in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def install_file(src: pathlib.Path, dst: pathlib.Path, tag: str) -> None:
    if not src.is_file():
        raise SystemExit(f"missing {src}")
    if not dst.parent.is_dir():
        raise SystemExit(f"missing {dst.parent}")
    data = src.read_bytes().replace(b"\r\n", b"\n")
    if dst.is_file() and dst.read_bytes().replace(b"\r\n", b"\n") == data:
        print(f"already {dst.name} ({tag})")
        return
    dst.write_bytes(data)
    print(f"copied {dst} ({tag})")


def install_pstate() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    install_file(
        root / "distro/kernel/x86/r5_pstate.cpp",
        HAIKU / "src/system/kernel/arch/x86/r5_pstate.cpp",
        "cpu readout",
    )
    install_file(
        root / "distro/kernel/x86/r5_pstate.h",
        HAIKU / "src/system/kernel/arch/x86/r5_pstate.h",
        "cpu readout",
    )
    install_file(
        root / "distro/cpu/r5_pstate_log.cpp",
        HAIKU / "src/bin/r5_pstate_log.cpp",
        "cpu log",
    )

    insert_once(
        HAIKU / "src/system/kernel/arch/x86/Jamfile",
        "\tarch_timer.cpp\n",
        "\tr5_pstate.cpp\n\tarch_timer.cpp\n",
        "\tr5_pstate.cpp\n",
        "kernel Jamfile",
    )
    insert_once(
        HAIKU / "src/bin/Jamfile",
        "\tsystem_time.cpp\n",
        "\tsystem_time.cpp\n\tr5_pstate_log.cpp\n",
        "\tr5_pstate_log.cpp\n",
        "bin Jamfile",
    )
    insert_once(
        HAIKU / "build/jam/images/definitions/minimum",
        "\tsystem_time\n",
        "\tsystem_time\n\tr5_pstate_log\n",
        "\tr5_pstate_log\n",
        "image bins",
    )
    insert_once(
        HAIKU / "data/launch/system",
        """service x-vnd.Haiku-SystemLogger {
	launch /system/servers/syslog_daemon
	port logger {
		capacity 256
	}
}
""",
        """service x-vnd.Haiku-SystemLogger {
	launch /system/servers/syslog_daemon
	port logger {
		capacity 256
	}
}

job x-vnd.R5OS-cpu-log {
	launch /system/bin/r5_pstate_log
	on initial_volumes_mounted
}
""",
        "job x-vnd.R5OS-cpu-log",
        "cpu log job",
    )


def install_usb_stability() -> None:
    root = pathlib.Path(__file__).resolve().parents[1]
    source_root = root / "distro" / "kernel"
    files = (
        "src/add-ons/kernel/busses/usb/ehci.cpp",
        "src/add-ons/kernel/busses/usb/ohci.h",
        "src/add-ons/kernel/busses/usb/ehci_hardware.h",
        "src/add-ons/kernel/busses/usb/ehci.h",
        "src/add-ons/kernel/busses/usb/uhci.h",
        "src/add-ons/kernel/busses/usb/uhci.cpp",
        "src/add-ons/kernel/busses/usb/ohci.cpp",
        "src/add-ons/kernel/busses/usb/xhci.cpp",
        "src/add-ons/kernel/busses/usb/xhci.h",
        "src/add-ons/kernel/busses/usb/xhci_hardware.h",
        "src/add-ons/kernel/bus_managers/usb/Device.cpp",
        "src/add-ons/kernel/bus_managers/usb/Hub.cpp",
        "src/add-ons/kernel/bus_managers/usb/usb_private.h",
        "src/add-ons/kernel/bus_managers/usb/Object.cpp",
        "src/add-ons/kernel/bus_managers/usb/BusManager.cpp",
        "src/add-ons/kernel/bus_managers/usb/Stack.cpp",
        "src/add-ons/kernel/drivers/network/ether/usb_ecm/ECMDevice.cpp",
    )
    for relative in files:
        source = source_root / relative
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        destination = HAIKU / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        print(f"copied {relative}")


def install_i2c_hid_package() -> None:
    # The driver is built and was not installed. RenkuOS b37a078925 adds it
    # next to i2c_elan.
    insert_once(
        HAIKU / "build/jam/packages/Haiku",
        "\ti2c_elan\n",
        "\ti2c_elan\n\ti2c_hid\n",
        "\ti2c_hid\n",
        "i2c_hid package",
    )


def main() -> int:
    install_smp_boot()
    install_kernel_sources()
    install_pstate()
    install_usb_stability()
    install_i2c_hid_package()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
