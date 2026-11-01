# R5OS desktop style

How preference windows and account cards should look. Haiku controls, panel gray, one reading line per field. Window chrome (round tabs, buttons, Deskbar, shadow) is `docs/LOOK.md`.

## Forms

- The window background is the normal panel (`B_PANEL_BACKGROUND_COLOR`). Do not put a white document card behind a form.
- Labels share one column and are right-aligned. Values share the next column, start on the same vertical line, and are left-aligned.
- A label and its field sit on one row and share that row's vertical center. A text field is one line high.
- Do not let a grid row grow with the window or with a tall neighbor (an avatar, a list). Extra space is glue outside the form. Pin the text view's max height to one line.
- Buttons that belong together sit on one row. The machine lock sits at the bottom right.
- Hide a column with the layout item's `SetVisible`, after the window is shown. `BView::IsHidden()` before `Show()` follows the window, so a `Hide()` in the constructor never runs.

## People list

The Users list stays two lines: the name in bold, then the username and the role (`administrator` or `standard`). That list is worth keeping.

## Whose data

- The signed-in person can edit their own name, email, picture, and password while the machine lock is on. The lock is for the computer and for other people.
- A standard account does not see the people list, Add, Disable, or Delete until Unlock. An administrator sees them immediately.
- Sign in still lists every account that can sign in.
