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

## Adding a class

- Add it only when a program needs it and a second program will use it (`AGENTS.md`, LibR5). Do not add a class that only renames a `libbe` class.
- In the same change: the class in `distro/ui`, a section in `docs/LIBR5.md`, and a row in the table above.
- The ABI is fixed from the first release: a private data pointer, `_Reserved<Class>1`–`4` and `uint32 _reserved[4]`. New calls go at the end. A shipped call does not change.
- `Archive()` and `Instantiate()` work, so a replicant can use the class.
- Test it in the first program, in light and dark, at font size 12 and 18.
