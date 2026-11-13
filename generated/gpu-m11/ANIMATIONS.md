# Window animations: note for a step after M11 (13 Nov 2026)

Agreed with the user on 13 Nov 2026. Not a brief yet. It builds on the M11 compositor (`generated/gpu-m11/BRIEF.md`, `docs/APP-SERVER-COMPOSITOR.md`) and comes after M11 step 4 (shadow and glass), because it uses the same compose pass.

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
- **A switch "Animations"** on the Look and feel tab of Appearance (Settings page and the separate Appearance program), default on; off is instant. A new control, not a reorder (`AGENTS.md`, "Settings window").
- **Never in the way:** an animation never delays input. A window is usable at once; the animation only changes how it is composed. A new event during an animation starts from where the old one is.
- **No change for programs:** no new API is needed; the compositor animates the window buffer. Programs with direct access to the screen (`BDirectWindow`, `BWindowScreen`) are not animated, because composing is paused for them.
- **Deskbar link:** for hide and show, app_server needs the screen rectangle of the window's line in Deskbar. How Deskbar gives it is designed in the step; it is additive.
- **Measured:** compose time during an animation on RADV and on Lavapipe (VirtualBox), recorded in `CHANGELOG.md`.
