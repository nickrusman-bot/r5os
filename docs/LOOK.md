# R5OS window look

Plan only. Do not start this during a multi-user fix.

The reference picture (2026-10-29) is a lighter, rounder desktop: round window, soft shadow, round buttons and fields, more air in the Deskbar. The yellow tab in that picture is a **shape** example. Keep the current tab yellow (`B_WINDOW_TAB_COLOR`). Do not pale it.

The large “HAIKU” wordmark on that desktop is a wallpaper. It is not part of this work.

## Where the pixels are

| What you see | Where it is drawn | What one change covers |
|---|---|---|
| Yellow tab, title, close/zoom, outer frame, resize corner | `app_server` `DefaultDecorator` / `TabDecorator` | Every titled window |
| Buttons, text fields, lists, menus, scrollbars | `HaikuControlLook` (`src/kits/interface/HaikuControlLook.cpp`) | System controls. Apps that paint their own widgets stay square |
| Deskbar tray, leaf, clock, replicants | `src/apps/deskbar` (`BarView`, `ExpandoMenuBar`) | The Deskbar only. It is not a titled window, so the decorator does not round it |
| Panel gray, list white, tab yellow | Appearance `ui_color` values | Colors only. Shapes stay as they are |

`HaikuControlLook` already has a button radius argument. The default call passes `0`. The window tab does not. It is a beveled rectangle in `_DrawTab` / `_DrawFrame`.

There is no compositor. A shadow is either painted into an extra frame margin and blended onto what is already on screen, or it is skipped. Do not start Vulkan or a new compositor for this.

## Rules

- One default look. Do not add a second decorator that apps must opt into.
- Radius follows UI scale / font size. Do not hardcode a pixel radius that looks right only at one resolution.
- Menus, tooltips, and borderless windows stay square until a titled window looks right. They share the decorator and will pick up a sloppy radius if the tab path is not limited to titled looks.
- Hit testing must match the drawn outline. A round tab that still uses a square click rect misses the corners or steals clicks beside the tab.
- A shadow margin must not take mouse clicks. Desktop icons under the margin stay clickable.
- Form layout (labels, one line per field, own account card) stays in `docs/STYLE.md`. This file is chrome.

## Chunks

Each chunk is one bootable change. Stop after the chunk. Look at Tracker, one preference, an alert, and a menu before the next chunk.

1. **Tab shape.** `DefaultDecorator::_DrawTab` draws a round tab in the current yellow. Close and zoom still sit in the tab and still work. The window frame stays square. This is the proof that every titled window can change together.

2. **Window corners.** Round the outer frame, including the bottom corners. Inset the client area so the decorator owns the corner pixels and apps do not paint a square into the curve. Dragging and the resize corner still work.

3. **Buttons.** Default `HaikuControlLook` button radius, a flatter face, a little more padding. Check a preference, Tracker, and an alert. Leave checkboxes and menus for later.

4. **Fields and lists.** Same radius on text controls and on the border around a list or scroll view, so a form matches the window. Users & Accounts is the picture to compare.

5. **Deskbar.** More padding inside the tray. Its own rounded background, drawn by Deskbar, not by the window decorator. Leaf menu after the tray looks right.

6. **Shadow.** Extra frame margin, a short alpha gradient, clicks pass through. Only after chunks 1 and 2 are stable. If dragging leaves dirt on the desktop, drop the shadow and keep the rounds.

7. **Color pass.** Only if the gray still feels harsh after the shapes exist. Tune the existing Appearance colors (panel, border, list). Do not build a theme engine. Dark mode stays the separate roadmap item.

## Not in these chunks

- A Haiku wordmark wallpaper, or any new desktop background.
- Restyling one preference by hand so it looks round while other windows stay square.
- Hiding controls, changing multi-user behavior, or moving settings into one Preferences window.
