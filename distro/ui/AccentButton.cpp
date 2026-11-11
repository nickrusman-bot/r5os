/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "AccentButton.h"

#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <Window.h>

#include <math.h>
#include <new>


namespace BAppearanceKit {


static const char* kAccentButtonClassName = "BAppearanceKit::BAccentButton";


// One mockup pixel. The design is drawn for a 12 point system font.
static float
unit()
{
	return be_plain_font->Size() / 12.0f;
}


static rgb_color
accent_label_color(rgb_color fill)
{
	// White on a dark fill, black on a light one.
	int32 luma = (fill.red * 299 + fill.green * 587 + fill.blue * 114) / 1000;
	return luma > 170 ? make_color(20, 20, 20) : make_color(255, 255, 255);
}


BAccentButton::BAccentButton(const char* name, const char* label,
	BMessage* message)
	:
	BButton(name, label, message, B_WILL_DRAW | B_NAVIGABLE
		| B_FULL_UPDATE_ON_RESIZE | B_TRANSPARENT_BACKGROUND),
	fHover(false)
{
	_Init();
}


BAccentButton::BAccentButton(BMessage* archive)
	:
	BButton(archive),
	fHover(false)
{
	_Init();
}


BAccentButton::~BAccentButton()
{
}


BArchivable*
BAccentButton::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kAccentButtonClassName))
		return NULL;
	return new(std::nothrow) BAccentButton(archive);
}


status_t
BAccentButton::Archive(BMessage* archive, bool deep) const
{
	status_t status = BButton::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kAccentButtonClassName) != B_OK)
		status = archive->AddString("class", kAccentButtonClassName);
	return status;
}


void
BAccentButton::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float u = unit();
	float radius = 4.0f * u;

	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color fill = accent;
	if (!IsEnabled())
		fill = mix_color(accent, panel, 110);
	else if (Value() == B_CONTROL_ON)
		fill = tint_color(accent, 1.16f);
	else if (fHover)
		fill = tint_color(accent, 1.07f);
	rgb_color border = tint_color(fill, 1.12f);

	// The corners show the parent: the view has a transparent background.
	SetDrawingMode(B_OP_COPY);
	SetHighColor(fill);
	FillRoundRect(bounds, radius, radius);
	SetHighColor(border);
	StrokeRoundRect(bounds, radius, radius);

	if (IsEnabled() && Value() != B_CONTROL_ON) {
		// The soft light along the top edge.
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(255, 255, 255, 64);
		StrokeLine(BPoint(bounds.left + radius, bounds.top + 1),
			BPoint(bounds.right - radius, bounds.top + 1));
		SetDrawingMode(B_OP_COPY);
	}

	if (IsFocus() && Window() != NULL && Window()->IsActive()) {
		BRect ring = bounds.InsetByCopy(2.0f * u, 2.0f * u);
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(255, 255, 255, 150);
		StrokeRoundRect(ring, radius - 1, radius - 1);
		SetDrawingMode(B_OP_COPY);
	}

	const char* label = Label();
	if (label == NULL || label[0] == '\0')
		return;

	BFont font;
	GetFont(&font);
	font_height height;
	font.GetHeight(&height);
	float width = font.StringWidth(label);
	BPoint where(floorf(bounds.left + (bounds.Width() - width) / 2 + 0.5f),
		floorf(bounds.top + (bounds.Height() - height.ascent
			- height.descent) / 2 + height.ascent + 0.5f));

	rgb_color text = accent_label_color(accent);
	if (!IsEnabled())
		text = mix_color(text, fill, 140);
	SetHighColor(text);
	SetLowColor(fill);
	SetDrawingMode(B_OP_OVER);
	DrawString(label, where);
	SetDrawingMode(B_OP_COPY);
}


void
BAccentButton::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	bool hover = transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW;
	if (hover != fHover) {
		fHover = hover;
		Invalidate();
	}
	BButton::MouseMoved(where, transit, dragMessage);
}


void
BAccentButton::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED)
		Invalidate();
	else if (message->what == B_FONTS_UPDATED) {
		_Init();
		InvalidateLayout();
		Invalidate();
	}
	BButton::MessageReceived(message);
}


void
BAccentButton::GetPreferredSize(float* width, float* height)
{
	float u = unit();
	BFont font;
	GetFont(&font);
	const char* label = Label();
	float labelWidth = label != NULL ? font.StringWidth(label) : 0;
	if (width != NULL)
		*width = ceilf(labelWidth + 40.0f * u);
	if (height != NULL)
		*height = floorf(38.0f * u) - 1;
}


BSize
BAccentButton::MinSize()
{
	float width;
	float height;
	GetPreferredSize(&width, &height);
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), BSize(width, height));
}


BSize
BAccentButton::MaxSize()
{
	float width;
	float height;
	GetPreferredSize(&width, &height);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), BSize(width, height));
}


BSize
BAccentButton::PreferredSize()
{
	float width;
	float height;
	GetPreferredSize(&width, &height);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(width, height));
}


void
BAccentButton::_Init()
{
	SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
	SetViewColor(B_TRANSPARENT_COLOR);
	// The label is bold, one step larger than body text (14 on 12).
	BFont font(be_bold_font);
	font.SetSize(floorf(be_plain_font->Size() * 14.0f / 12.0f + 0.5f));
	SetFont(&font);
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void
BAccentButton::_ReservedAccentButton1()
{
}


void
BAccentButton::_ReservedAccentButton2()
{
}


void
BAccentButton::_ReservedAccentButton3()
{
}


void
BAccentButton::_ReservedAccentButton4()
{
}


}	// namespace BAppearanceKit
