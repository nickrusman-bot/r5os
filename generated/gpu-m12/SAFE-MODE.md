# The CPU mode only in Safe mode (note, 13 Nov 2026)

Agreed with the user on 13 Nov 2026. Not a brief yet. A step after M12 (`generated/gpu-m12/BRIEF.md`). It builds on M11 (`generated/gpu-m11/BRIEF.md`, decisions 1, 11 and 14).

## The idea

The desktop is always composed: on RADV, otherwise on Lavapipe. The CPU mode (Painter, one back buffer, no compositor) is no longer an automatic fallback in a normal start. It stays installed and working, but it runs only in **Safe mode**. That keeps the `AGENTS.md` rule that the software path always stays installed, and still makes the composed desktop the only normal desktop.

## What others do (for reference, nothing copied)

| System | Normal | No GPU | GPU hangs | When the software path fails too |
|---|---|---|---|---|
| Windows 8 and later | always composed (DWM), no setting to turn it off | Basic Display Driver plus WARP, in software | the driver is reset (TDR), DWM creates its device again | Safe Mode with the basic display; after failed starts the boot menu and the recovery environment |
| macOS | always composed (WindowServer) | composed in software (for example in a VM) | the driver recovers; a WindowServer crash goes back to the login window | Safe Mode (Shift at start, only the essential extensions), then macOS Recovery |
| GNOME | always composed | llvmpipe in software; GNOME dropped its separate non-composited "fallback mode" in 3.8 (2013) for that reason | – | the "Oh no! Something has gone wrong" screen and log out; recovery mode from the boot menu |
| KDE Plasma (Wayland) | always composed (KWin) | KWin keeps a CPU back end (QPainter) besides OpenGL | – | – |
| Chrome / ChromeOS | GPU compositing | software | the GPU process is restarted; after repeated crashes software compositing | – |
| Haiku | never composed | VESA or the framebuffer | – | boot menu: Safe mode, the fail-safe graphics driver, an older system state |

The pattern: the big systems compose always, also in software, and keep the plain path only in a separate start (Safe mode or recovery). R5 does the same.

## The ladder in R5

1. **Normal start:** RADV, when the GPU is available (`R5_GPU_CAP_AVAILABLE`).
2. **Otherwise Lavapipe**, at session start.
3. **During a session:** RADV is lost and the restart after a reset fails (decision 14) → the compositor goes on **on Lavapipe**, without a freeze. This is new work: it replaces the switch to the CPU mode of decision 11.
4. **Lavapipe fails** (at start or during a session) → app_server tries once more. If that fails, the desktop start counts as failed.
5. **After a failed desktop start** the next boot shows the boot menu by itself, with **Safe mode** selected, and says in one line why ("The desktop could not start last time").
6. **Safe mode** = Haiku's safe mode, plus the fail-safe graphics driver, plus the CPU mode. No GPU driver, no compositor, no effects. A clear "Safe mode" mark on the desktop. From there the user can choose an **older system state** (Haiku's package states), because a broken Mesa or driver update is the most likely cause.

## Conditions before the automatic CPU fallback goes

- M12 is done: the GPU draws everything, also `BBitmap`s with views, so the GPU mode needs no Painter.
- `BDirectWindow` and `BWindowScreen` no longer need the CPU-mode pause (for example a buffer of their own in the compositor), or the pause stays as the only use of the CPU path outside Safe mode, written down as such.
- The switch from RADV to Lavapipe during a session (point 3) works and is tested.
- Lavapipe is proven on AMD64, ARM64 and RISC-V.
- A few releases without a `gpu compositor off` line in reports.
- `AGENTS.md` is updated by the user: the CPU mode is the Safe mode path.

## Open for the step

- How app_server tells the boot loader that the desktop start failed (a marker on the boot volume that the loader reads, removed after a good Sign in). Additive, MIT.
- Whether Safe mode also offers the older system state automatically, or only in the menu.
- Measuring that Lavapipe starts within the same time as the CPU mode on the slowest supported machine.
