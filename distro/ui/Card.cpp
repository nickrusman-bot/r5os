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


#include "Card.h"

#include <InterfaceDefs.h>
#include <Layout.h>
#include <Message.h>

#include <new>


namespace BAppearanceKit {


struct BCard::Data {
};


static const char* kCardClassName = "BAppearanceKit::BCard";


static rgb_color
card_fill(card_style style)
{
	if (style == B_CARD_PANEL) {
		return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_1_TINT);
	}
	return ui_color(B_DOCUMENT_BACKGROUND_COLOR);
}


// The view color is the card's own face, so a child that adopts its
// parent's colors (BStringView does) gets the card face, not gray. The
// B_TRANSPARENT_BACKGROUND flag keeps the rounded corners showing the
// parent.
static void
apply_card_view_color(BView* view, card_style style)
{
	if (style == B_CARD_PANEL) {
		view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
		view->SetLowUIColor(B_PANEL_BACKGROUND_COLOR, B_DARKEN_1_TINT);
	} else {
		view->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	}
}


static rgb_color
card_border()
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
}


BCard::BCard(const char* name, card_style style, orientation orientation)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
		| B_TRANSPARENT_BACKGROUND,
		new BGroupLayout(orientation, B_USE_DEFAULT_SPACING)),
	fStyle(style),
	fData(NULL)
{
	_Init();
}


BCard::BCard(BMessage* archive)
	:
	BView(archive),
	fStyle(B_CARD_DOCUMENT),
	fData(NULL)
{
	int32 style = B_CARD_DOCUMENT;
	if (archive != NULL && archive->FindInt32("r5:style", &style) == B_OK) {
		if (style == B_CARD_PANEL)
			fStyle = B_CARD_PANEL;
	}
	_Init();
}


BCard::~BCard()
{
	delete fData;
}


BArchivable*
BCard::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kCardClassName))
		return NULL;
	return new BCard(archive);
}


status_t
BCard::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kCardClassName) != B_OK)
		status = archive->AddString("class", kCardClassName);
	if (status == B_OK)
		status = archive->AddInt32("r5:style", (int32)fStyle);
	return status;
}


void
BCard::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float radius = be_plain_font->Size() * 0.25f;
	SetHighColor(card_fill(fStyle));
	FillRoundRect(bounds, radius, radius);
	SetHighColor(card_border());
	StrokeRoundRect(bounds, radius, radius);
}


void
BCard::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED)
		Invalidate();
	BView::MessageReceived(message);
}


void
BCard::SetStyle(card_style style)
{
	if (fStyle == style)
		return;
	fStyle = style;
	apply_card_view_color(this, fStyle);
	Invalidate();
}


card_style
BCard::Style() const
{
	return fStyle;
}


void
BCard::_Init()
{
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	apply_card_view_color(this, fStyle);
	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
	if (layout != NULL)
		layout->SetInsets(B_USE_BIG_SPACING);
}


void
BCard::_ReservedCard1()
{
}


void
BCard::_ReservedCard2()
{
}


void
BCard::_ReservedCard3()
{
}


void
BCard::_ReservedCard4()
{
}


}	// namespace BAppearanceKit
