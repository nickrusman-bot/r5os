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
   - 1px border (`#8c8c8c`) and subtle modern drop shadow.
   - Softened bottom corners (6px radius).

3. **Logo & Header**:
   - Free-standing R5 wave symbol (`r5-symbol.svg`) without any artificial circular wrapper.
   - Bold "Welcome to R5" header and subtle subtitle.

4. **Multi-User Scenarios (Interactive Preview)**:
   - **1 User (Single-user workstation)**: The user selector row is completely hidden (matching `LoginView.cpp`'s `fUserList->CountItems() <= 1`). The user profile banner and password field are directly presented.
   - **2–4 Users (Small team / home)**: Compact horizontal account cards side-by-side with avatars and badges.
   - **> 4 Users (Enterprise / multi-user)**: Two-line scrollable user list with authentic Haiku scrollbar, displaying avatar, bold name, username, and role badge (`docs/STYLE.md`).

5. **Desktop & System Environment**:
   - Fullscreen scalable R5 wave desktop wallpaper (`100vw` x `100vh`).
   - No Deskbar or Tracker HDD icon, reflecting real OS boot state (Tracker and Deskbar launch only *after* successful sign-in).

6. **Credential Input**:
   - Inset password field with subtle shadow and crisp border.
   - Password reveal eye toggle button (interactive show/hide with `eye.svg` and `eye-off.svg`).
   - High-contrast blue primary action button (`#0084ff`).
   - Status feedback line (validates empty password, simulated authentication).

7. **Bottom Status & Power Bar**:
   - Live system clock on the left (updating every second).
   - BeOS-styled "Restart" and "Shut down" buttons with vector icons and bevels.
   - No divider line above the buttons, keeping the panel cohesive and uncluttered.

## How to View

Open `sign-in.html` directly in any modern web browser (Edge, Chrome, Firefox, Safari). Use the top pill toolbar to switch between **1 User**, **2 Users**, and **> 4 Users** scenarios.
