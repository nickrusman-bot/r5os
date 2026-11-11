# R5 Sign-in Mockup

Interactive design mockup for the R5 Operating System Sign-in / Login window (`distro/login`), addressing GitHub issue [#71](https://github.com/r5-operating-system/r5os/issues/71).

This design adapts the visual language established in the Settings window mockup (`docs/design/settings-mockup/`) to the login experience, maintaining authentic BeOS / Haiku heritage while introducing clean, modern ergonomics.

## Key Design Elements

1. **BeOS Yellow Window Tab**:
   - Iconic `#f8cc26` background with `#a88200` border.
   - Traditional BeOS square buttons: close box on the left (`#ffe88a`), zoom box on the right.
   - Clean "Sign in" title in bold 13px Manrope.

2. **Window Body & Framing**:
   - Neutral panel gray (`#ededed`) with an authentic `inset 1px 1px 0 #ffffff` highlight.
   - 1px border (`#8c8c8c`) and subtle modern drop shadow (`box-shadow: 0 24px 48px rgba(15,23,42,0.22), 0 6px 16px rgba(15,23,42,0.1)`).
   - Softened bottom corners (6px radius).

3. **User Selection & Account Card**:
   - Multi-user selection cards displaying the user avatar (`people.svg`), display name, and role badge (`Administrator` / `Standard`).
   - Active account selection with clear blue focus indicator (`#0084ff`).

4. **Credential Input**:
   - Inset password field with subtle shadow and crisp border.
   - Password reveal eye toggle button (interactive show/hide with `eye.svg` and `eye-off.svg`).
   - High-contrast blue primary action button (`#0084ff` / `#0070db`).
   - Informative status line for feedback (empty password, authentication status, error messages).

5. **Bottom Status & Power Bar**:
   - Live system clock on the left (matches `LoginView.cpp`'s `fClockView`).
   - BeOS-styled "Restart" and "Shut down" buttons with vector icons and authentic bevels.

## How to View

Open `sign-in.html` directly in any modern web browser (Edge, Chrome, Firefox, Safari). No web server is required.
