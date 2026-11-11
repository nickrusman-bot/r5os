# Appearance Kit

The R5 interface classes: what exists, and when to use it. The exact calls, colors and sizes are in `docs/LIBR5.md`. Window chrome (tab, frame, control look) is `docs/LOOK.md`. Form layout is `docs/STYLE.md`.

## What it is

- C++ classes in BeOS style, in namespace `BAppearanceKit`, the same way as `BPackageKit`. A program writes `using BAppearanceKit::BCard;` or the full name.
- One class per header, named after the class (`<Card.h>`).
- Library `libr5ui.so`, source `distro/ui`, MIT. Any program may link it. A Haiku program that does not link it is unchanged.
- Sizes come from Haiku's spacing (`B_USE_*_SPACING`) and from `be_plain_font`. Colors come from `ui_color()`, so dark mode and font size work. No pixel values in a program that uses these classes.

## Classes

| Class | Header | Use it for | Do not use it for | First user |
|---|---|---|---|---|
| `BCard` | `<Card.h>` | A heading or a summary in its own area: the About heading (`B_CARD_DOCUMENT`), the system information (`B_CARD_PANEL`), an account summary | A form you fill in: that stays on the panel (`docs/STYLE.md`). A stack of pages: that is Haiku's `BCardView` / `BCardLayout`, a different thing | About R5 |
| `BSidebarItem` | `<SidebarItem.h>` | One row in a sidebar: a name, an optional icon, a count or a lock | A control the user edits. The icon is handed in; the row does not load one, except the small lock | Settings, in the list |
| `BSidebar` | `<Sidebar.h>` | A list of categories that fold, with one category open, and a filter that hides the rest | The contents of a settings page | Settings, beside the page |
| `BActionCard` | `<ActionCard.h>` | A card the user clicks, with a title, a short summary, and small labels | A form. The card does not contain other views | Settings home. The R5 Hub and Store start page is the second user |
| `BPageHeader` | `<PageHeader.h>` | The heading of a page: a small category line, a title, a description, and an icon | The controls under that heading | Settings. Users & Accounts is the second user, for the account card heading |
| `BScreenArrangement` | `<ScreenArrangement.h>` | The screens on a desk, each one drawn with `r5_screen_draw()`, with a number and a name | Changing the mode or the workspace count. The view only reports a click or a drag | Display. Desktop and Deskbar is the second user, when that page shows more than one screen |
| `BUserCard` | `<UserCard.h>` | A compact account card with an avatar, display name, and role badge | Large forms | Sign in. Users & Accounts is the second user |
| `BUserListItem` | `<UserListItem.h>` | A two-line user list item with avatar, bold display name, and username/role subtitle | Single-line plain text items | Sign in. Users & Accounts is the second user |
| `BPasswordControl` | `<PasswordControl.h>` | A password text field with an integrated show/hide eye toggle button | Plain text controls | Sign in. Users & Accounts and Network are future users |
| `BIconView` | `<R5IconView.h>` | One icon at its own pixel size, such as a logo beside a heading | An icon that has to grow or shrink with the view. Render a new bitmap instead | Sign in, for the R5 pebble and the clock |
| `BAccentButton` | `<AccentButton.h>` | The one main action of a window, in the accent blue | Every button in a row. Restart, Cancel and the like stay plain `BButton`s | Sign in, for Sign in |

## Adding a class

- Add it only when a program needs it and a second program will use it (`AGENTS.md`, LibR5). Do not add a class that only renames a `libbe` class.
- In the same change: the class in `distro/ui`, a section in `docs/LIBR5.md`, and a row in the table above.
- The ABI is fixed from the first release: a private data pointer, `_Reserved<Class>1`–`4` and `uint32 _reserved[4]`. New calls go at the end. A shipped call does not change.
- `Archive()` and `Instantiate()` work, so a replicant can use the class.
- Test it in the first program, in light and dark, at font size 12 and 18.
