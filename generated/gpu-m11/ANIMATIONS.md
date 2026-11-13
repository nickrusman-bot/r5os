# Window animations: M11 step 4b (13 Nov 2026)

Agreed with the user on 13 Nov 2026. Open, close and the workspace switch are **M11 step 4b** (`generated/gpu-m11/BRIEF.md`, the steps table and decision 8), right after step 4, on the same compose pass (decision 16). Hide and show into Deskbar come later, with Deskbar NextGen, because they need the Deskbar link below.

## What others do (for reference, nothing copied)

- Windows 11: open is a short zoom from about 95 % to 100 % with a fade; close is the reverse; minimise shrinks into the taskbar button; maximise and snapping resize smoothly.
- macOS: minimise with the "genie" or a scale effect into the Dock; new windows open with a short zoom and fade.
- iOS: an app zooms open from its icon with a spring motion and zooms back to it on close; the app switcher shows cards.
- All of them have a "reduce motion" setting.

## Proposal for R5

| Event | Animation | Length |
|---|---|---|
| Open | fade in, scale 95 % → 100 % | about 150 ms |
| Close | the reverse | a little shorter |
| Hide (Haiku's minimise) | the window shrinks into its line in Deskbar | about 200 ms |
| Show again | the reverse, out of Deskbar | about 200 ms |
| Workspace switch | the screen slides towards the new workspace | about 200 ms |

Lengths are starting values for the step's measurement, not fixed constants in code.

## Rules

- **GPU mode only.** In the CPU mode everything stays instant, as today.
- **A switch "Animations"** in the "Window effects" box on the Look and feel tab of Appearance (Settings page and the separate Appearance program), default on; off is instant. A new control, not a reorder (`AGENTS.md`, "Settings window"). It is the flag `B_WINDOW_ANIMATIONS` of `set_window_effects()`; no new call.
- **Never in the way:** an animation never delays input. A window is usable at once; the animation only changes how it is composed. A new event during an animation starts from where the old one is.
- **No change for programs:** no new API is needed; the compositor animates the window buffer. Programs with direct access to the screen (`BDirectWindow`, `BWindowScreen`) are not animated, because composing is paused for them.
- **Deskbar link (later, not in 4b):** for hide and show, app_server needs the screen rectangle of the window's line in Deskbar. How Deskbar gives it is designed in the step; it is additive.
- **Measured:** compose time during an animation on RADV and on Lavapipe (VirtualBox), recorded in `CHANGELOG.md`.
