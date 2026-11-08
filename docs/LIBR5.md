# LibR5

Shared libraries that R5 adds beside `libbe`. `libbe` stays the Haiku Interface Kit. A program written for Haiku links `libbe` and does not need these libraries. Its ABI does not change because these libraries exist.

Do not move a call from here into `libbe`. Do not add a function here only to rename a `libbe` call. A helper belongs here when R5 wrote it and more than one program uses it.

The C++ is MIT, so any application can link these libraries. Code added to them must be MIT-compatible. A program that does not link it is unchanged. The icon pictures have their own notices in `docs/ICONS.md`.

## Libraries

| Library | Header | Source | What it is |
|---|---|---|---|
| `libr5icons.so` | `<R5Icons.h>` | `distro/icons` | Icons and icon buttons. `docs/ICONS.md` |
| `libr5ui.so` | `<R5UI.h>` | `distro/ui` | The screen picture |

`scripts/apply-icons.py` and `scripts/apply-r5ui.py` copy the sources into the Haiku tree, copy the header to `headers/os/interface/`, and add the library to the regular image and the minimum image. On a running system the files are `/system/lib/libr5icons.so` and `/system/lib/libr5ui.so`. The soname is the file name, with no version suffix, the same way `libbe.so` is named.

```
Application Example :
	Example.cpp
	: be libr5ui.so [ TargetLibstdc++ ]
	;
```

`SubInclude` for `src preferences r5ui` has to come before the program that links it.

Adding a function keeps old programs working. Changing a function's arguments, or removing a function, breaks programs already built against that soname. Do that only with a written note in this file and in `CHANGELOG.md`. The same rule is in `AGENTS.md`.

## Screen picture

`r5_screen_draw()` draws the frame Settings uses for a screen. Notifications, Desktop and Deskbar, and Display all call it. The caller draws anything that sits on the glass, and any label outside the frame.

```cpp
#include <R5UI.h>

void
ExampleView::Draw(BRect updateRect)
{
	(void)updateRect;
	rgb_color fill = ui_color(B_CONTROL_BACKGROUND_COLOR);
	BRect content = r5_screen_draw(this, Bounds(), fill);
	r5_screen_caption(this, content, "21 dpi",
		make_color(255, 255, 255));
	// Draw a bar, a card, or nothing else inside content.
}
```

`bounds` is the area the picture may use. The picture keeps a 4:3 shape and is centered in that area. `contentColor` is the fill inside the frame. The returned rectangle is that fill, in the view's coordinates.

`r5_screen_caption()` draws one line centered in that rectangle. A NULL or empty string draws nothing. Text that does not fit is skipped. It does not draw a label under the frame. The caller does that in the area below `bounds`.

Pass `NULL` for the view and either call returns without drawing.

## Card

`BAppearanceKit::BCard` is a view with a group layout inside it. The header is `<Card.h>`, installed next to `<R5UI.h>`. It is not Haiku's `BCardView`. Children are added through `GetLayout()`, the same way as a `BGroupView`.

`B_CARD_DOCUMENT` fills with `B_DOCUMENT_BACKGROUND_COLOR`. `B_CARD_PANEL` fills with `B_PANEL_BACKGROUND_COLOR` tinted by `B_DARKEN_1_TINT`. The border is one pixel, `B_PANEL_BACKGROUND_COLOR` tinted by `B_DARKEN_2_TINT`. The corner radius is a quarter of `be_plain_font->Size()` (3 px at 12 pt). The inner margin is `B_USE_BIG_SPACING`. Every color comes from `ui_color()`.

`Archive()` and `Instantiate()` keep the style. The archived class name is `BAppearanceKit::BCard`.

```cpp
#include <Card.h>
#include <LayoutBuilder.h>
#include <StringView.h>

BAppearanceKit::BCard* card = new BAppearanceKit::BCard("info",
	BAppearanceKit::B_CARD_PANEL);
BLayoutBuilder::Group<>((BGroupLayout*)card->GetLayout())
	.Add(new BStringView("title", "Processors"))
	.Add(new BStringView("value", "2"));
```
