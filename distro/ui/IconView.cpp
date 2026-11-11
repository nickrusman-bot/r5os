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


#include "R5IconView.h"

#include <Bitmap.h>
#include <Message.h>

#include <new>


namespace BAppearanceKit {


static const char* kIconViewClassName = "BAppearanceKit::BIconView";


BIconView::BIconView(const char* name, const BBitmap* icon)
	:
	BView(name, B_WILL_DRAW),
	fIcon(NULL)
{
	_Init();
	SetIcon(icon);
}


BIconView::BIconView(BMessage* archive)
	:
	BView(archive),
	fIcon(NULL)
{
	_Init();
	BMessage iconArchive;
	if (archive != NULL
		&& archive->FindMessage("r5:icon", &iconArchive) == B_OK) {
		fIcon = dynamic_cast<BBitmap*>(BBitmap::Instantiate(&iconArchive));
	}
}


BIconView::~BIconView()
{
	delete fIcon;
}


BArchivable*
BIconView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kIconViewClassName))
		return NULL;
	return new(std::nothrow) BIconView(archive);
}


status_t
BIconView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kIconViewClassName) != B_OK)
		status = archive->AddString("class", kIconViewClassName);
	if (status == B_OK && fIcon != NULL) {
		BMessage iconArchive;
		if (fIcon->Archive(&iconArchive) == B_OK)
			status = archive->AddMessage("r5:icon", &iconArchive);
	}
	return status;
}


void
BIconView::SetIcon(const BBitmap* icon)
{
	delete fIcon;
	fIcon = NULL;
	if (icon != NULL && icon->IsValid())
		fIcon = new(std::nothrow) BBitmap(icon);
	InvalidateLayout();
	Invalidate();
}


const BBitmap*
BIconView::Icon() const
{
	return fIcon;
}


void
BIconView::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
}


void
BIconView::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fIcon == NULL || !fIcon->IsValid())
		return;

	// Whole pixels only: a half-pixel offset would blur the edges.
	BRect bounds = Bounds();
	BRect icon = fIcon->Bounds();
	BPoint where((int32)(bounds.left + (bounds.Width() - icon.Width()) / 2),
		(int32)(bounds.top + (bounds.Height() - icon.Height()) / 2));

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmap(fIcon, where);
	SetDrawingMode(B_OP_COPY);
}


BSize
BIconView::MinSize()
{
	if (fIcon == NULL)
		return BSize(0, 0);
	BRect icon = fIcon->Bounds();
	return BSize(icon.Width(), icon.Height());
}


BSize
BIconView::MaxSize()
{
	return MinSize();
}


BSize
BIconView::PreferredSize()
{
	return MinSize();
}


void
BIconView::_Init()
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void
BIconView::_ReservedIconView1()
{
}


void
BIconView::_ReservedIconView2()
{
}


void
BIconView::_ReservedIconView3()
{
}


void
BIconView::_ReservedIconView4()
{
}


}	// namespace BAppearanceKit
