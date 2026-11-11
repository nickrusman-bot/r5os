# LibR5UI Elements for Multi-User & Sign-in

Specification of new UI classes for `libr5ui.so` (namespace `BAppearanceKit`) identified during the Sign-in window mockup design (issue [#71](https://github.com/r5-operating-system/r5os/issues/71)).

In accordance with `AGENTS.md` and `docs/APPEARANCE-KIT.md`, a component belongs in `libr5ui.so` when:
1. It is written for R5 and not part of standard Haiku `libbe`.
2. It is used by **more than one program** (here: **Sign in** in `distro/login` and **Users & Accounts** in `distro/settings`).
3. It uses `BAppearanceKit` conventions: sizes scale with `be_plain_font->Size()`, colors come from `ui_color()`, and C++ is MIT-licensed.

---

## 1. `BUserCard` (Account Tile)

A clickable, compact card representing a user account with an avatar, display name, and role badge.

- **Header**: `<UserCard.h>`
- **Source**: `distro/ui/UserCard.cpp`
- **First users**:
  - **Sign in** (`distro/login`): account selection when 2 to 4 accounts exist.
  - **Users & Accounts** (`distro/settings`): active account card and quick user switcher.

### Class Declaration

```cpp
namespace BAppearanceKit {

class BUserCard : public BControl {
public:
	BUserCard(const char* name, const char* login, const char* displayName,
		const char* role, const BBitmap* avatar, BMessage* message = NULL);
	virtual ~BUserCard();

	// Identification
	const char*		Login() const;
	const char*		DisplayName() const;
	const char*		Role() const;
	void			SetDisplayName(const char* name);
	void			SetRole(const char* role);
	void			SetAvatar(const BBitmap* avatar);
	const BBitmap*	Avatar() const;

	// Selection state
	void			SetSelected(bool selected);
	bool			IsSelected() const;

	// BControl overrides
	virtual void	Draw(BRect updateRect);
	virtual void	MouseDown(BPoint where);
	virtual void	KeyDown(const char* bytes, int32 numBytes);
	virtual void	GetPreferredSize(float* width, float* height);

	// Archiving
	static BArchivable* Instantiate(BMessage* archive);
	virtual status_t	Archive(BMessage* archive, bool deep = true) const;

private:
	// Reserved for ABI stability
	uint32			_reserved[4];
};

} // namespace BAppearanceKit
```

### Visual & Layout Contract
- **Dimensions**: Scales with `u = be_plain_font->Size() / 13`. Preferred height is `u * 54`, min width `u * 140`.
- **Avatar**: Circle of diameter `u * 38`, drawn with `B_OP_ALPHA`. Falls back to `people.svg` when NULL.
- **Text Layout**:
  - Top line: Display name in bold (`be_bold_font`, `u * 13`), color `B_DOCUMENT_TEXT_COLOR`.
  - Bottom line: Role (`Administrator` / `Standard user`), `u * 11`, color `B_STATUS_BAR_COLOR` for admin, muted list text for standard.
- **Selection**:
  - Inactive: Background `#f8fafc`, 1px border `tint_color(B_PANEL_BACKGROUND_COLOR, B_DARKEN_2_TINT)`.
  - Active: Background `B_DOCUMENT_BACKGROUND_COLOR`, 1px border `B_STATUS_BAR_COLOR` with subtle 2px outer glow (`rgba(0,132,255,0.18)`), and focus indicator.

---

## 2. `BUserListItem` (Two-Line User List Item)

A two-line item for `BListView` or `BOutlineListView`, implementing the design mandate from `docs/STYLE.md` (*"The Users list stays two lines: the name in bold, then the username and the role (administrator or standard). That list is worth keeping."*).

- **Header**: `<UserListItem.h>`
- **Source**: `distro/ui/UserListItem.cpp`
- **First users**:
  - **Sign in** (`distro/login`): scrollable user list when > 4 accounts exist.
  - **Users & Accounts** (`distro/settings`): user management list.

### Class Declaration

```cpp
namespace BAppearanceKit {

class BUserListItem : public BListItem {
public:
	BUserListItem(const char* login, const char* displayName,
		const char* role, const BBitmap* avatar = NULL);
	virtual ~BUserListItem();

	const char*		Login() const;
	const char*		DisplayName() const;
	const char*		Role() const;
	void			SetDisplayName(const char* name);
	void			SetRole(const char* role);
	void			SetAvatar(const BBitmap* avatar);
	const BBitmap*	Avatar() const;

	virtual void	DrawItem(BView* owner, BRect itemRect, bool complete = false);
	virtual void	Update(BView* owner, const BFont* font);

	static BArchivable* Instantiate(BMessage* archive);
	virtual status_t	Archive(BMessage* archive, bool deep = true) const;

private:
	uint32			_reserved[4];
};

} // namespace BAppearanceKit
```

### Visual & Layout Contract
- **Row Height**: `u * 44` (based on `u = be_plain_font->Size() / 13`).
- **Left Margin**: Circular avatar at `u * 30` diameter, centered vertically.
- **Text Area**: Inset `u * 44` from left:
  - Line 1: `DisplayName` in `be_bold_font` (`u * 13`).
  - Line 2: `@login · role` in `be_plain_font` (`u * 11`), role in bold for `Administrator`.
- **Selection**: Matches `BSidebarItem`: background in `B_STATUS_BAR_COLOR` with high-contrast text when selected; subtle hover highlight when pointed at.

---

## 3. `BPasswordControl` (Password Field with Eye Toggle)

A secure text input control with an integrated show/hide reveal button, satisfying `docs/design/README.md` (*"a large password field with a show/hide (eye) toggle"*).

- **Header**: `<PasswordControl.h>`
- **Source**: `distro/ui/PasswordControl.cpp`
- **First users**:
  - **Sign in** (`distro/login`): password entry.
  - **Network / Wi-Fi** (`distro/settings`): WPA2/WPA3 pre-shared key entry.
  - **Users & Accounts** (`distro/settings`): password creation and change dialog.

### Class Declaration

```cpp
namespace BAppearanceKit {

class BPasswordControl : public BView {
public:
	BPasswordControl(const char* name, const char* label,
		const char* text = NULL, BMessage* message = NULL);
	virtual ~BPasswordControl();

	// Password text access
	const char*		Text() const;
	void			SetText(const char* text);
	BTextView*		TextView() const;

	// Reveal state
	void			SetPasswordRevealed(bool revealed);
	bool			IsPasswordRevealed() const;

	// Modification message
	void			SetModificationMessage(BMessage* message);
	BMessage*		ModificationMessage() const;

	// View hooks
	virtual void	AttachedToWindow();
	virtual void	MakeFocus(bool focus = true);
	virtual void	SetEnabled(bool enabled);
	virtual bool	IsEnabled() const;
	virtual void	GetPreferredSize(float* width, float* height);

	static BArchivable* Instantiate(BMessage* archive);
	virtual status_t	Archive(BMessage* archive, bool deep = true) const;

private:
	uint32			_reserved[4];
};

} // namespace BAppearanceKit
```

### Visual & Layout Contract
- **Structure**: A compound view containing:
  - An inner `BTextView` configured with `HideTyping(true)` by default.
  - An inline eye icon button (`R5_ICON_EYE` / `R5_ICON_EYE_OFF` or vector glyph).
- **Behavior**:
  - Clicking the eye button toggles `TextView()->HideTyping(!revealed)` and swaps the icon.
  - Enter key invokes the `BMessage` (e.g. `kAttemptLogin`).
  - Styled with standard Haiku text field border and inset shadow `inset 1px 1px 2px rgba(0,0,0,0.08)`.

---

## Summary of Additions to LibR5UI

| Class | Header | Use Case | Multi-App Users |
|---|---|---|---|
| `BUserCard` | `<UserCard.h>` | Compact account tile with avatar, name & role | **Login** (picker), **Settings** (Users & Accounts active card) |
| `BUserListItem` | `<UserListItem.h>` | 2-line list item for user accounts | **Login** (>4 users), **Settings** (user list) |
| `BPasswordControl` | `<PasswordControl.h>` | Password text box with eye toggle | **Login**, **Network** (Wi-Fi PSK), **Settings** (Users & Accounts) |

All classes conform to the R5 shared library policy:
- Added to `libr5ui.so` alongside `BCard`, `BActionCard`, `BPageHeader`, `BSidebar`, and `BScreenArrangement`.
- MIT licensed.
- Documented in `docs/LIBR5.md` and `docs/APPEARANCE-KIT.md` upon landing.
