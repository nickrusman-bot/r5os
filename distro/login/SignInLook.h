/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Shared by the Sign in window and the lock screen, so both follow
 * docs/design/login-mockup/sign-in.html the same way.
 */
#ifndef SIGN_IN_LOOK_H
#define SIGN_IN_LOOK_H


#include <Bitmap.h>
#include <Font.h>
#include <R5Icons.h>
#include <R5UI.h>
#include <StringView.h>
#include <View.h>

#include <math.h>


static const char* const kRoleAdministrator = "Administrator";
static const char* const kRoleStandard = "Standard";


// One pixel of the mockup. The mockup is drawn for the 12 point system
// font; a larger system font scales the whole window.
static inline float
px(float value)
{
	return value * be_plain_font->Size() / 12.0f;
}


// A whole number of pixels for a square that should be `value` mockup
// pixels on a side. Layout sizes are one less than the pixel count.
static inline float
px_side(float value)
{
	return floorf(px(value) + 0.5f);
}


static inline BFont
font_px(const BFont* base, float size)
{
	BFont font(base);
	font.SetSize(floorf(px(size) * 2 + 0.5f) / 2);
	return font;
}


static inline BStringView*
make_label(const char* name, const char* text, const BFont& font,
	color_which background)
{
	BStringView* view = new BStringView(name, text);
	view->SetFont(&font);
	view->SetViewUIColor(background);
	view->SetLowUIColor(background);
	return view;
}


// The People icon, as in the mockup. The person file icon if People is
// not installed.
static inline BBitmap*
person_icon(float side)
{
	BBitmap* icon = r5_mime_icon_bitmap("application/x-vnd.Be-PEPL", side);
	if (icon == NULL)
		icon = r5_mime_icon_bitmap("application/x-person", side);
	return icon;
}


// The round account picture beside a name.
class AvatarView : public BView {
public:
	AvatarView(const BBitmap** icon, float side)
		:
		BView("avatar", B_WILL_DRAW),
		fIcon(icon),
		fHighlighted(true)
	{
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		BSize size(side - 1, side - 1);
		SetExplicitMinSize(size);
		SetExplicitMaxSize(size);
	}

	void SetHighlighted(bool highlighted)
	{
		fHighlighted = highlighted;
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		r5_avatar_draw(this, Bounds(), fIcon != NULL ? *fIcon : NULL,
			fHighlighted);
	}

private:
	const BBitmap**	fIcon;
	bool			fHighlighted;
};


#endif	// SIGN_IN_LOOK_H
