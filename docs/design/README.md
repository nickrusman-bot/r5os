# Design references

Pictures of how R5 should eventually look. They are references, not specifications. The rules for forms and the account card are in `docs/STYLE.md`. The fixed layout of the Settings window is in `AGENTS.md`.

These files are not part of any image.

## `sign-in-reference.jpg`

The user's target for the look of a window, shared on 7 Nov 2026. It shows a Sign in window. **The logo in it is not the R5 logo.** Look at the window, not the logo. The text "R5OS" in the picture becomes "R5" in user-facing text (`AGENTS.md`, "Name").

What to take from it:
- **The BeOS yellow tab** with the title and a close box, with rounded corners and a lighter finish.
- **A window body with large rounded corners.**
- **A frosted, slightly translucent light surface** with a soft shadow over the desktop.
- **Controls:**
  - a large password field with a show/hide (eye) toggle;
  - one blue primary button;
  - secondary buttons with an icon and a label;
  - a small grey line of text at the bottom.

How R5 gets there:
1. **Before the GPU work:**
   - an R5 window decorator add-on (the rounded tab and frame);
   - an R5 control look (`BControlLook`: rounder controls, the primary button, icon buttons through `libr5icons`);
   - the eye toggle and this layout in the Sign in window (`distro/login`).

   The current Haiku decorator and control look stay available.
2. **After GPU milestone M8** (app_server on the GPU, `docs/GPU-ARCHITECTURE.md`): translucency, blur and soft shadows, which need a compositing app_server. Without a supported GPU the window keeps the same shape and layout with an opaque surface.
