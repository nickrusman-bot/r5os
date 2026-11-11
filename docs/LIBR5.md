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

`Update()` sets the height from `u = be_plain_font->Size() / 13`. Home is `u*8` of top padding plus a row of `u*34`. A separator is `u*13` (`u*6`, a 1 px line, `u*6`). Any other row is `u*36` plus a `u*2` gap.

`DrawItem()` draws the icon only on a top-level row, with `B_OP_ALPHA` and `B_PIXEL_ALPHA` / `B_ALPHA_OVERLAY`. The caller draws any wrench before `SetIcon()`. A child has no icon. Its text starts where the category name starts. Text that does not fit ends with an ellipsis.

Home and a selected row are bold. A category that is not selected uses a SemiBold face of the same family when that face exists, and bold otherwise. A child label is plain. The selection is a rounded rectangle in `B_STATUS_BAR_COLOR`, inset `u*6` from the left and right of the list, with corner radius `u*4` and text in `B_LIST_BACKGROUND_COLOR`. `B_LIST_SELECTED_BACKGROUND_COLOR` is gray in the default set (`{190, 190, 190}`), so it does not match the mockup. `B_STATUS_BAR_COLOR` is the R5 blue in the default set (`{50, 150, 255}`). A dark theme that changes the status bar changes this row too. That is accepted for now: the row follows one theme color, and that color is the one that already looks right. The row under the pointer gets the same rounded rectangle in 28/255 of `B_STATUS_BAR_COLOR` mixed into the list background; a separator, a disabled row and the selected row get no hover. The list redraws in full when its width changes (`B_FULL_UPDATE_ON_RESIZE`), so a scroll bar that hides at start leaves no old edge behind. A child that is not selected uses the list text mixed so that 80 percent of the color is the text and 20 percent is the list background. Category icons are `u*24`. Home draws a white circle of `u*24` (`B_LIST_BACKGROUND_COLOR`) and the pebble at `u*20` inside it. The circle is filled with `B_OP_COPY`, then the bitmap is drawn with alpha again, because the pebble's wave is a hole and a copy-mode blit turns that hole black or blue.

A folded category draws its count on the right at `u*11`, in `mix_color()` of the list background and the list text with amount 140 (the stand-in for `#64748b`). An unfolded category draws `B_DOWN_ARROW` there instead. A child with `B_SIDEBAR_BADGE_LOCKED` draws `R5_ICON_LOCKED` at `ComposeIconSize(B_MINI_ICON)`, with the bitmap alpha multiplied by 60 percent. The line under Home is `tint_color()` of the list background with `B_DARKEN_1_TINT` (the stand-in for `#e2e2e2`), inset `u*4`.

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

`Draw()` paints one round rectangle behind an unfolded category and its pages. The fill is `mix_color()` of `B_LIST_BACKGROUND_COLOR` and `B_CONTROL_HIGHLIGHT_COLOR` with amount 40. The corner radius is a quarter of `be_plain_font->Size()`. The rows themselves draw the icon, the text, and the selection. The outline list's expander triangle is not drawn. `SetFooter()` draws a panel at the bottom of the list when the text is not empty and the rows leave room for it. The panel is inset `u*4`, padded `u*10`, and filled with `mix_color()` of the list background and `B_CONTROL_HIGHLIGHT_COLOR` at amount 18 (the stand-in for `#f3f6fb`). Its border is the same mix at amount 48 (the stand-in for `#dbe3ef`). The word "Tip:" is bold in `B_DOCUMENT_TEXT_COLOR`. The rest of the footer is `u*11` in `mix_color()` of the list background and the list text at amount 150 (the stand-in for `#475569`). That same mix, at amount 160, is the muted text on a card and under a page title. Card and heading borders use `tint_color()` of `B_PANEL_BACKGROUND_COLOR` with `B_DARKEN_2_TINT` (the stand-in for `#c4c4c4` and for the dashed `#b9b9b9`).

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

The card follows the pointer, the pressed button, and the keyboard focus. Hover mixes 10/255 of `B_STATUS_BAR_COLOR` (the list's selection blue) into the document fill, draws the border in `B_STATUS_BAR_COLOR`, and adds a soft inner ring of 70/255 of that blue, as in the mockup. Pressed mixes 26/255 into the fill. Focus uses `B_KEYBOARD_NAVIGATION_COLOR` for the border. The corner radius is a quarter of `be_plain_font->Size()`.

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

## Screen arrangement

`BAppearanceKit::BScreenArrangement` draws one or more screens in their places on the desk, the way Display shows them. The header is `<ScreenArrangement.h>`. It is a `BView` and a `BInvoker`. A click, Space, or Enter sends the message from the constructor with `int32 "be:screen"` set to that screen's id. The target is the window unless `SetTarget()` names another. The view does not change the system. The caller applies a change.

```cpp
namespace BAppearanceKit {

struct screen_tile {
	int32		id;			// the caller's id for this screen
	BRect		frame;		// in desktop coordinates
	const char*	name;		// copied by SetScreens()
	bool		main;		// the Deskbar is on this screen
	bool		enabled;	// false draws it dimmed
};

class BScreenArrangement : public BView, public BInvoker {
public:
	BScreenArrangement(const char* name, BMessage* selectionMessage);

	status_t	SetScreens(const screen_tile* tiles, int32 count);
	int32		CountScreens() const;
	bool		ScreenAt(int32 index, screen_tile* tile) const;
	void		SetSelected(int32 id);
	int32		Selected() const;			// -1 when none
	void		SetDraggable(bool draggable);
	bool		IsDraggable() const;
	void		SetModificationMessage(BMessage* message);	// adopted
	BMessage*	ModificationMessage() const;
	void		SetCaption(const char* caption);	// NULL or "" hides it
	const char*	Caption() const;
};

}
```

`SetScreens()` copies the tiles and returns `B_BAD_VALUE` for a negative count or a NULL list with a count. Tiles are kept in id order. `ScreenAt()` fills the caller's tile. Its `name` stays valid until the next `SetScreens()`. Arrow keys move the selection to the nearest screen in that direction.

The picture is filled with the list background mixed toward `B_CONTROL_HIGHLIGHT_COLOR` by 18, and stroked with the same mix at 48. That is the Search tip blue (`#f3f6fb` and `#dbe3ef` on the light theme). Each screen is `r5_screen_draw()`, filled with `B_DESKTOP_COLOR`. A screen with `enabled` false is filled with the panel background tinted `B_DARKEN_1_TINT`. The main screen then draws the Deskbar strip the same way Desktop and Deskbar does, following the running Deskbar. A filled circle sits in the glass with the number, starting at 1 in id order. The selected circle uses `B_STATUS_BAR_COLOR`. The others use the control border. The name is one bold line under the frame, cut with an ellipsis. A selected screen has a ring in `B_STATUS_BAR_COLOR` just outside the frame. Keyboard focus strokes the view in `B_KEYBOARD_NAVIGATION_COLOR`. The caption is one line at the bottom left, in the document background mixed toward the document text by 140 (`#64748b` on the light theme).

One screen is centered and keeps its own shape. Its height is at most 60 percent of the view. It is not stretched across the width. More than one screen are scaled together so the gaps stay, and the group is centered. Dragging is off unless `SetDraggable(true)` and there are two or more screens. On release, a screen snaps so its edge meets the next one, with no gap and no overlap. While the mouse is down the screen follows it. The modification message, adopted by `SetModificationMessage()`, then goes to the target with `int32 "be:screen"` and `BPoint "be:origin"` (the new top left in desktop coordinates).

Sizes follow `u = be_plain_font->Size() / 13`. The circle is `u*22`, and `u*26` when selected. The ring is `u*3` thick and sits `u*2` outside the frame. The name is `u*10`. The caption is `u*11`, `u*12` in from the left and `u*8` up from the bottom. The minimum size is `u*220` by `u*100`. The preferred height is 140 at a 12 point font.

`Archive()` and `Instantiate()` keep the tiles, the selection, the caption, and whether it can be dragged. The archived class name is `BAppearanceKit::BScreenArrangement`.

```cpp
#include <ScreenArrangement.h>

BAppearanceKit::BScreenArrangement* screens
	= new BAppearanceKit::BScreenArrangement("screens",
		new BMessage('pick'));
BAppearanceKit::screen_tile tile;
tile.id = 0;
tile.frame.Set(0, 0, 1279, 799);
tile.name = "Screen";
tile.main = true;
tile.enabled = true;
screens->SetScreens(&tile, 1);
screens->SetDraggable(false);
screens->SetCaption(NULL);
```

## User card

`BAppearanceKit::BUserCard` is a compact account card with an avatar, display name, and role badge. The header is `<UserCard.h>`. It is a `BControl` that sends a message when clicked or activated with Space or Enter.

```cpp
#include <UserCard.h>

BAppearanceKit::BUserCard* card = new BAppearanceKit::BUserCard(
	"user", "user", "User", "Administrator", avatarBitmap,
	new BMessage('usel'));
```

## User list item

`BAppearanceKit::BUserListItem` is a two-line item for `BListView` adhering to `docs/STYLE.md`. The header is `<UserListItem.h>`. It draws a circular avatar, the display name in bold on the first line, and `@login · role` on the second line.

```cpp
#include <UserListItem.h>

listView->AddItem(new BAppearanceKit::BUserListItem(
	"user", "User", "Administrator", avatarBitmap));
```

## Password control

`BAppearanceKit::BPasswordControl` is a password field with an integrated show/hide eye toggle button. The header is `<PasswordControl.h>`. The eye is `R5_GLYPH_EYE` while the password is hidden (a click shows it) and `R5_GLYPH_EYE_OFF` while it is shown. `SetPlaceholder("Enter password")` sets the gray text shown while the field is empty. The field text is Manrope one step larger than body text (14 on 12), and the blue focus ring is drawn inside the control's own bounds. `Archive()` and `Instantiate()` keep whether the password is shown and the placeholder. They do not keep the password. The archived class name is `BAppearanceKit::BPasswordControl`.

```cpp
#include <PasswordControl.h>

BAppearanceKit::BPasswordControl* password = new BAppearanceKit::BPasswordControl(
	"password", "Password", "", new BMessage('sign'));
```

## Icon view

`BAppearanceKit::BIconView` shows one bitmap at its own pixel size. The header is `<IconView.h>`. The view is exactly as large as the bitmap and never scales it, so a vector icon rendered at the size it is shown stays sharp. Render the bitmap at that size, for example with `r5_vector_icon_bitmap()`, and do not stretch it with `DrawBitmap(bitmap, rect)`: a stretch of even one pixel makes the edges jagged. `SetIcon()` keeps a copy. The background is the parent's color.

```cpp
#include <IconView.h>

BBitmap* pebble = r5_vector_icon_bitmap(data, size, 44);
BAppearanceKit::BIconView* logo = new BAppearanceKit::BIconView("logo", pebble);
delete pebble;
```

## Accent button

`BAppearanceKit::BAccentButton` is the main action of a window: a button filled with the selection blue (`B_STATUS_BAR_COLOR`) with a bold label, white on a dark accent and black on a light one. The header is `<AccentButton.h>`. It is a `BButton`, so the message, the target, `MakeDefault()` and Enter work as before. It is 38 pixels high at a 12 point system font, and the label is one step larger than body text. It is as wide as its label; for the full row width set an explicit maximum width of `B_SIZE_UNLIMITED`. Use one per window. The other buttons stay plain `BButton`s.

```cpp
#include <AccentButton.h>

BAppearanceKit::BAccentButton* signIn = new BAppearanceKit::BAccentButton(
	"signin", "Sign in", new BMessage('sign'));
signIn->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
```

## Account picture

`r5_avatar_draw(view, circle, icon, highlighted)` in `<R5UI.h>` draws a round plate with a thin ring and the icon in the middle. `highlighted` gives the light accent plate (an administrator, as on Sign in); otherwise the plate is gray and the picture is drawn in gray. Without an icon it draws a plain person outline. `r5_avatar_icon_side(circleSide)` is the icon size that is drawn pixel for pixel; any other size is filtered. `BUserCard`, `BUserListItem` and the Sign in window use it.

```cpp
float side = r5_avatar_icon_side(44);
BBitmap* people = r5_mime_icon_bitmap("application/x-vnd.Be-PEPL", side);
r5_avatar_draw(this, BRect(0, 0, 43, 43), people, true);
```
