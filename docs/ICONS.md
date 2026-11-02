# Icons

Public helpers for a bitmap or a button icon. The header is `distro/icons/R5Icons.h`. After apply it is also `headers/os/interface/R5Icons.h`, so a program includes `<R5Icons.h>`. The code is the static library `libr5icons.a`. Link that library. `scripts/apply-icons.py` copies the sources into `src/preferences/r5icons` and adds the Jam include. `scripts/apply-settings.py` does the same, because Settings links the library.

These calls do not open Tracker, and they do not read `libtracker.so`. Tracker’s resource ids are not a public interface. A program that needs an arrow or a home picture calls the function below and passes one of the names.

None of these pictures gets the preferences wrench. That overlay belongs to the Settings application and its own page icons and home cards. About R5 does not use this library.

## Predefined icons

| Name | Value | Picture |
|---|---|---|
| `R5_ICON_BACK` | 1 | Tracker’s back arrow (`R_ResBackNav`) |
| `R5_ICON_FORWARD` | 2 | Tracker’s forward arrow (`R_ResForwNav`) |
| `R5_ICON_UP` | 3 | Tracker’s up arrow (`R_ResUpNav`) |
| `R5_ICON_HOME` | 4 | The house Tracker draws on the home folder (`R_HomeDirIcon`) |

The numbers are part of the contract. Do not change a number that already shipped. A new predefined icon is added at the end of `enum r5_icon`, with the next number, the unmodified picture, and a row in this table.

The bytes are unmodified HVIF from `src/kits/tracker/TrackerIcons.rdef`, stored in `distro/icons/standard_icons.inc`. The Open Tracker notice is `distro/icons/TRACKER_ICON_NOTICE.txt`. Do not put a GPL header on that byte file. The C++ around the bytes is GPL-3.0-or-later.

```cpp
BBitmap* icon = r5_icon_bitmap(R5_ICON_BACK, 16);
if (icon != NULL) {
	// draw icon, then delete it
	delete icon;
}
```

`r5_icon_bitmap()` returns a new `B_RGBA32` bitmap, or `NULL` when the name is unknown or the side is below 1. The caller deletes the bitmap. `side` is the edge in pixels.

## Any other icon

Three calls take an icon the program already knows. Each returns a new bitmap of the requested side, or `NULL`. The caller deletes it.

- `r5_vector_icon_bitmap(data, size, side)` draws HVIF bytes. This is any vector icon the program loaded itself.
- `r5_file_icon_bitmap(path, side)` draws the icon of a file or folder through `BNodeInfo`. This is the public node API.
- `r5_mime_icon_bitmap(type, side)` draws the icon of a MIME type through `BMimeType`. Example: `"application/x-vnd.Haiku-About"`.

A file or type that has no icon returns `NULL`. The bitmap is not given the preferences wrench.

## Buttons

A text button such as Revert has a height that comes from the font. An icon button next to it has to use that same height or the row looks uneven.

Pass that height. The call makes the button a square of that height and draws the icon inside, one pixel clear of the border. It does not change the label. Leave the label empty when the button should show only the picture. `SetToolTip()` can still say “Back” or “Home”.

```cpp
float height = revert->PreferredSize().height;
r5_button_set_icon(back, R5_ICON_BACK, height);
r5_button_set_icon(home, R5_ICON_HOME, height);
```

The same height is accepted by:

- `r5_button_set_vector(button, data, size, height)`
- `r5_button_set_file_icon(button, path, height)`
- `r5_button_set_mime_icon(button, type, height)`

Each returns `B_OK` when the icon was set. The button is still resized to the square when the pointer is valid, including when the picture itself could not be loaded, so the row does not jump. A `NULL` button or a height below 1 returns `B_BAD_VALUE` and does not resize.

Settings uses `r5_button_set_icon()` for Back and Home, after the plain font is applied, so the height matches Revert and Save. The order on that row is Back, Home, Revert, Save.
