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


def main() -> int:
    patch_tsc_div0()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
