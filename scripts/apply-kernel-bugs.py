#!/usr/bin/env python3
"""Apply kernel boot fixes (#20400 TSC conversion divide-by-zero)."""
from __future__ import annotations

import pathlib
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text and old not in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def patch_tsc_div0() -> None:
    path = HAIKU / "src/system/kernel/arch/x86/arch_timer.cpp"

    text = path.read_text(encoding="utf-8")
    if "#include <debug.h>" not in text:
        if "#include <int.h>" in text:
            path.write_text(
                text.replace("#include <int.h>\n", "#include <debug.h>\n", 1),
                encoding="utf-8",
            )
        else:
            path.write_text(
                text.replace(
                    "#include <x86gprintrin.h>\n",
                    "#include <x86gprintrin.h>\n#include <debug.h>\n",
                    1,
                ),
                encoding="utf-8",
            )
        print(f"patched {path} (Haiku #20400 panic include)")
    else:
        print(f"already {path.name} (Haiku #20400 panic include)")
    if "#include <int.h>" in path.read_text(encoding="utf-8"):
        path.write_text(
            path.read_text(encoding="utf-8").replace(
                "#include <int.h>\n", "", 1
            ),
            encoding="utf-8",
        )

    replace_once(
        path,
        """	if (khz == 0)
		return;

	dprintf("CPU: using TSC frequency from CPUID\\n");
	// compute for microseconds as follows (1000000 << 32) / (tsc freq in Hz),
	// or (1000 << 32) / (tsc freq in kHz)
	*conversionFactor = (1000ULL << 32) / (khz * numerator / denominator);
}""",
        """	if (khz == 0)
		return;

	const uint64 tscKhz = uint64(khz) * numerator / denominator;
	if (tscKhz == 0)
		return;

	dprintf("CPU: using TSC frequency from CPUID\\n");
	// compute for microseconds as follows (1000000 << 32) / (tsc freq in Hz),
	// or (1000 << 32) / (tsc freq in kHz)
	*conversionFactor = (1000ULL << 32) / tscKhz;
}""",
        "Haiku #20400 CPUID TSC",
    )

    replace_once(
        path,
        """	dprintf("CPU: using TSC frequency from MSR %" B_PRIu64 "\\n", khz * numerator / denominator);
	// compute for microseconds as follows (1000000 << 32) / (tsc freq in Hz),
	// or (1000 << 32) / (tsc freq in kHz)
	*conversionFactor = (1000ULL << 32) / (khz * numerator / denominator);
}""",
        """	const uint64 tscKhz = khz * numerator / denominator;
	if (tscKhz == 0)
		return;

	dprintf("CPU: using TSC frequency from MSR %" B_PRIu64 "\\n", tscKhz);
	// compute for microseconds as follows (1000000 << 32) / (tsc freq in Hz),
	// or (1000 << 32) / (tsc freq in kHz)
	*conversionFactor = (1000ULL << 32) / tscKhz;
}""",
        "Haiku #20400 MSR TSC",
    )

    replace_once(
        path,
        """	if (maxVMM >= 0x40000010) {
		get_current_cpuid(&info, 0x40000010, 0);

		uint64 clockSpeed = uint64(info.regs.eax) * 1000;
		*conversionFactor = (uint64(1000) << 32) / info.regs.eax;

		gCPUClockSpeed = clockSpeed;

		dprintf("CPU: TSC frequency read from hypervisor CPUID leaf\\n");
		return;
	}""",
        """	if (maxVMM >= 0x40000010) {
		get_current_cpuid(&info, 0x40000010, 0);

		// Leaf present with EAX=0 still must not divide (Haiku #20400).
		if (info.regs.eax != 0) {
			uint64 clockSpeed = uint64(info.regs.eax) * 1000;
			*conversionFactor = (uint64(1000) << 32) / info.regs.eax;

			gCPUClockSpeed = clockSpeed;

			dprintf("CPU: TSC frequency read from hypervisor CPUID leaf\\n");
			return;
		}
	}""",
        "Haiku #20400 hypervisor TSC",
    )

    replace_once(
        path,
        """	// Scale the TSC delta to timer units
	tscDeltaSlow *= TIMER_CLKNUM_HZ;

	uint64 clockSpeed = tscDeltaSlow / expired;
	*conversionFactor = ((uint128(expired) * uint32(1000000)) << 32)
		/ uint128(tscDeltaSlow);
""",
        """	// Scale the TSC delta to timer units
	tscDeltaSlow *= TIMER_CLKNUM_HZ;

	if (expired == 0 || tscDeltaSlow == 0) {
		dprintf("CPU: PIT TSC calibration failed (expired %" B_PRIu16
			", delta %" B_PRIu64 ")\\n", expired, tscDeltaSlow);
		return;
	}

	uint64 clockSpeed = tscDeltaSlow / expired;
	*conversionFactor = ((uint128(expired) * uint32(1000000)) << 32)
		/ uint128(tscDeltaSlow);
""",
        "Haiku #20400 PIT TSC",
    )

    replace_once(
        path,
        """	if (conversionFactor == 0)
		calculate_cpu_conversion_factor(2, &conversionFactor);

	gSystemTimeConversionFactor = conversionFactor;
""",
        """	if (conversionFactor == 0)
		calculate_cpu_conversion_factor(2, &conversionFactor);

	if (conversionFactor == 0)
		panic("failed to calibrate TSC");

	gSystemTimeConversionFactor = conversionFactor;
""",
        "Haiku #20400 TSC panic",
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
        HAIKU / "src/system/kernel/arch/x86/arch_cpu.cpp",
        "#include <arch/x86/timer.h>\n",
        "#include <arch/x86/timer.h>\n\n#include \"r5_pstate.h\"\n",
        '#include "r5_pstate.h"\n',
        "cpu publish include",
    )
    insert_once(
        HAIKU / "src/system/kernel/arch/x86/arch_cpu.cpp",
        """	elf_add_memory_image_symbol(image, "commpage_thread_exit",
		threadExitPosition, threadExitLen, B_SYMBOL_TYPE_TEXT);

	return B_OK;
""",
        """	elf_add_memory_image_symbol(image, "commpage_thread_exit",
		threadExitPosition, threadExitLen, B_SYMBOL_TYPE_TEXT);

	r5_pstate_publish();
	return B_OK;
""",
        "r5_pstate_publish();",
        "cpu publish call",
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


def main() -> int:
    install_smp_boot()
    patch_tsc_div0()
    install_pstate()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
