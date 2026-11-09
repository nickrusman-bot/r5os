# Settings mockup (9 Nov 2026)

The design the user chose for the R5 Settings window. Settings is rebuilt to look like this. The brief for that work names the classes and the steps.

| File | What it shows |
|---|---|
| `home.png`, `home.html` | Home with the card view. All categories are folded in the list. |
| `information.png` | The Information page: a device card, six tiles, and a footer. |
| `notifications.png`, `notifications.html` | An open page (Notifications). Its category, System, is open in the list. |
| `icons/` | The icons the pages use (see "Icons" below) |

The HTML pages open in a browser without a server. The PNGs are those pages at 1190 by 770.

## What the mockup decides

- **Style:** the About R5 window and the website.
  - the square yellow tab, with the BeOS close box and zoom box;
  - a grey body with a white card for the heading and grey boxes for groups;
  - Manrope;
  - the light blue desktop.

  It is not the round, frosted Sign in reference in `sign-in-reference.jpg`.
- **Icons:** the real Haiku icons, not drawn symbols. Home is the R5 wave pebble.
- **The list:**
  - Home is at the top, with the R5 wave pebble.
  - Under it are the five categories, each with its Haiku icon and the number of pages.
  - Only the category of the open page is unfolded. Its pages are indented under it, on a light tint, and the open page is a blue row.
  - A page that needs the administrator shows a small lock.
  - On Home every category is folded. A short tip about Search sits at the bottom of the list.
- **Home:**
  - one card per category, with its Haiku icon plus the preferences wrench, its name, its summary, and its pages as small labels;
  - the card under the pointer lights up;
  - one more card lists the pages that were changed most recently.
- **Page heading:** the category name in small text above the title, then the title, the description, and the page icon with the wrench at the right.
- **Bottom row:** the lock at the left, then Revert and Defaults at the right. **There is no Back button and no Home button.** Home stays at the top of the list.
- **Unchanged:**
  - the window size (1030 by 610, smallest 910 by 560);
  - the list and Search are 200 wide;
  - Search sits under the list;
  - a home card is at least 280 wide;
  - the page order and the five fixed descriptions.

The category summaries for Devices, Network and Accounts in the mockup are new text:
- Devices: "Keyboard, mouse, touchpad, printers, and Bluetooth."
- Network: "Wired and Wi-Fi connections, and network shares."
- Accounts: "The people who use this computer, and how they sign in."

The summaries for Personalization and System stay as they are.

## Icons

The SVG files in `icons/` were converted from Haiku's vector icons, unmodified, for this picture only. They are not part of any image.

| File | Source | Notice |
|---|---|---|
| `appearance.svg`, `screen.svg`, `devices.svg`, `network.svg`, `people.svg`, `notifications.svg` | the `vector_icon` resource of each program in the Haiku tree | MIT, `distro/icons/HAIKU_ICON_NOTICE.txt` |
| `prefs-wrench.svg` | The wrench from `R_PrefsDirIcon` in `src/kits/tracker/TrackerIcons.rdef`, the same picture as `overlay-prefs` | Open Tracker license, `distro/icons/TRACKER_ICON_NOTICE.txt` |
| `unlocked.svg` | Haiku `Action_Login`, as in `distro/icons/lock_icons.inc` | MIT |
| `r5-symbol.svg` | The R5 wave pebble | R5, GPL-3.0-or-later |

The running system does not use these files. It draws the same icons from the programs' own resources and from `libr5icons.so`.
