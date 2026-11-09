# LibR5

Shared libraries that R5 adds beside `libbe`. `libbe` stays the Haiku Interface Kit. A program written for Haiku links `libbe` and does not need these libraries. Its ABI does not change because these libraries exist.

Do not move a call from here into `libbe`. Do not add a function here only to rename a `libbe` call. A helper belongs here when R5 wrote it and more than one program uses it.

The C++ is MIT, so any application can link these libraries. Code added to them must be MIT-compatible. A program that does not link it is unchanged. The icon pictures have their own notices in `docs/ICONS.md`.

## Libraries

| Library | Header | Source | What it is |
|---|---|---|---|
| `libr5icons.so` | `<R5Icons.h>` | `distro/icons` | Icons and icon buttons. `docs/ICONS.md` |
| `libr5ui.so` | `<R5UI.h>` | `distro/ui` | The screen picture, cards, the sidebar, and the page heading. It links `libr5icons.so` because a locked sidebar row draws `R5_ICON_LOCKED` |

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

## Sidebar item

`BAppearanceKit::BSidebarItem` is one row in the sidebar. The header is `<SidebarItem.h>`.

A row with a label and an id is a Home row, a category, or a page. An empty label and an empty id is a separator: it cannot be selected, and it draws one line in `B_LIST_BACKGROUND_COLOR` tinted by `B_DARKEN_2_TINT`.

`Update()` sets the height from the font and from `be_control_look->DefaultItemSpacing()`. A top-level row is at least `ComposeIconSize(24)` tall. A child row is one text line plus that spacing.

`DrawItem()` draws the icon only on a top-level row, with `B_OP_ALPHA`. The caller draws any wrench before `SetIcon()`. A child has no icon. Its text starts one icon width plus `DefaultItemSpacing()` from the left, which is where the category name starts. Text that does not fit ends with an ellipsis.

A top-level label is bold. A child label is plain. A selected label is bold. The selection is a round rectangle in `B_LIST_SELECTED_BACKGROUND_COLOR`, inset by half of `DefaultItemSpacing()`, with text in `B_LIST_SELECTED_ITEM_TEXT_COLOR`. The corner radius is a quarter of `be_plain_font->Size()`. A child that is not selected uses the list text mixed so that 80 percent of the color is the text and 20 percent is the list background.

A folded category with a count draws that count on the right, mixed so that 60 percent is the text and 40 percent is the background. An unfolded category draws `B_DOWN_ARROW` there instead. A child with `B_SIDEBAR_BADGE_LOCKED` draws `R5_ICON_LOCKED` at `ComposeIconSize(B_MINI_ICON)`, with the bitmap alpha multiplied by 60 percent.

`Archive()` and `Instantiate()` keep the label, id, keywords, count, badge, emphasis, and icon. The archived class name is `BAppearanceKit::BSidebarItem`.

```cpp
#include <SidebarItem.h>

BAppearanceKit::BSidebarItem* home = new BAppearanceKit::BSidebarItem(
	"Home", "home", pebble);
home->SetEmphasized(true);
```

## Sidebar

`BAppearanceKit::BSidebar` is an outline list of `BSidebarItem` rows. The header is `<Sidebar.h>`. Categories are top-level items. Pages are added with `AddUnder()`. The scroll bar stays outside: the caller wraps the sidebar in a `BScrollView`.

`SetSingleExpand(true)` is the default. Unfolding one category folds the others. `ExpandOnly(NULL)` folds every category. While a filter is active, more than one category may stay unfolded.

`Draw()` paints one round rectangle behind an unfolded category and its pages. The fill is `mix_color()` of `B_LIST_BACKGROUND_COLOR` and `B_CONTROL_HIGHLIGHT_COLOR` with amount 20. The corner radius is a quarter of `be_plain_font->Size()`. The rows themselves draw the icon, the text, and the selection. The outline list's expander triangle is not drawn.

`ItemForId()` finds a row by its id. `SelectId()` unfolds the parent when there is one, selects the row, and scrolls it into view. It returns false when the id is missing. While it changes what is unfolded it clears the selection message, then puts that message back, so an arrow key does not open a page.

`SetFilter()` hides children whose label and keywords do not contain the text, ignoring case. A category with no remaining match is hidden. An emphasized row and a separator stay. Every category that still has a match is unfolded. An empty filter shows every row again and unfolds the category that was open before the filter. Hidden rows are kept and put back. They are not deleted.

`MouseDown()` on a category selects it and calls `ExpandOnly()`. `MouseDown()` on a page or on Home leaves the click to the outline list. `KeyDown()` moves with Up and Down and skips a separator. Right unfolds the selected category. Left folds it. Enter posts the selection message.

`Archive()` and `Instantiate()` keep whether one category unfolds at a time. The archived class name is `BAppearanceKit::BSidebar`. The rows are archived with the outline list.

```cpp
#include <Sidebar.h>

BAppearanceKit::BSidebar* list = new BAppearanceKit::BSidebar("sidebar");
list->AddItem(home);
list->AddItem(system);
list->AddUnder(display, system);
list->SetFilter("display");
```

## Action card

`BAppearanceKit::BActionCard` is a document card that sends a message. The header is `<ActionCard.h>`. It is a `BCard` with style `B_CARD_DOCUMENT` and a `BInvoker`. It has no child views. It draws itself.

The icon sits at the top left at `ComposeIconSize(40)`. The caller draws any wrench before `SetIcon()`. The title uses `be_bold_font`. The summary uses `be_plain_font`, wraps to at most two lines, and the second line ends with an ellipsis. The gap and the inner margin are `be_control_look->DefaultItemSpacing()`.

A tag is a small round label. Its text is `be_plain_font` at 0.85 of that size, bold. The fill is `mix_color()` of `B_DOCUMENT_BACKGROUND_COLOR` and the tag color with amount 30. The text is the tag color tinted by `B_DARKEN_3_TINT`. The default tag color is `B_CONTROL_HIGHLIGHT_COLOR`. Tags wrap onto the next line.

The card follows the pointer, the pressed button, and the keyboard focus. Hover mixes 13/255 of `B_CONTROL_HIGHLIGHT_COLOR` into the document fill and 64/255 into the border. Pressed mixes 31/255 into the fill. Focus uses `B_KEYBOARD_NAVIGATION_COLOR` for the border. The corner radius is a quarter of `be_plain_font->Size()`.

A click on a tag sends the tag message with `int32` `"be:tag"` and string `"be:label"`. A click on the rest of the card calls `Invoke()`. Space and Enter do the same. The card is navigable. `AttachedToWindow()` aims the invoker at the window when no target was set.

`GetPreferredSize()` and `MinSize()` follow the text and the tags. A width of 0 is treated as 280. The caller may still set an explicit minimum, and Settings does that at 280 wide.

`Archive()` and `Instantiate()` keep the title, summary, tags, tag color, icon, invoke message (`"r5:invoke"`), and tag message. The archived class name is `BAppearanceKit::BActionCard`.

```cpp
#include <ActionCard.h>

BAppearanceKit::BActionCard* card = new BAppearanceKit::BActionCard(
	"system", new BMessage('open'));
card->SetTitle("System");
card->SetSummary("Display, sound, notifications, virtual memory, and this computer.");
card->SetIcon(icon);
card->AddTag("Display");
```

## Page header

`BAppearanceKit::BPageHeader` is the heading card at the top of a page. The header is `<PageHeader.h>`. It is a `BCard` with style `B_CARD_DOCUMENT`. It has no child views.

`SetEyebrow()` with NULL or an empty string hides that line and leaves no gap. The eyebrow uses `be_plain_font` at 0.9 of that size. When the font family has a style named SemiBold, that style is used. Otherwise the line stays plain. The color is the document text mixed so that 60 percent is the text and 40 percent is the document background.

The title uses `be_bold_font` at 8 points above that font's size. The subtitle uses `be_plain_font`, mixed so that 70 percent is the document text and 30 percent is the document background. Each line is cut with an ellipsis in the width left of the icon.

The icon is `ComposeIconSize(40)`, vertically centered, `DefaultItemSpacing()` from the right edge. The caller draws any wrench before `SetIcon()`. The preferred height follows the lines and the icon. The maximum width is unlimited and the maximum height is the preferred height.

`Invalidate()` runs on `B_COLORS_UPDATED` and `B_FONTS_UPDATED`. `Archive()` and `Instantiate()` keep the three lines and the icon. The archived class name is `BAppearanceKit::BPageHeader`.

```cpp
#include <PageHeader.h>

BAppearanceKit::BPageHeader* heading = new BAppearanceKit::BPageHeader("heading");
heading->SetEyebrow("System");
heading->SetTitle("Notifications");
heading->SetSubtitle("Manage alerts and notifications");
heading->SetIcon(icon);
```
