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


#include "SidebarItem.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <Mime.h>
#include <Message.h>
#include <R5Icons.h>
#include <View.h>

#include <new>
#include <string.h>


namespace BAppearanceKit {


struct BSidebarItem::Data {
};


static const char* kItemClassName = "BAppearanceKit::BSidebarItem";


static rgb_color
fade(rgb_color color, rgb_color toward, uint8 keep)
{
	return mix_color(toward, color, keep);
}


BSidebarItem::BSidebarItem(const char* label, const char* id,
	const BBitmap* icon)
	:
	BListItem(),
	fLabel(label),
	fId(id),
	fIcon(NULL),
	fLock(NULL),
	fCount(-1),
	fBadge(B_SIDEBAR_BADGE_NONE),
	fEmphasized(false),
	fInGroup(false),
	fFirst(false),
	fLast(false),
	fData(NULL)
{
	fIcon = _Copy(icon);
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	if (_IsSeparator())
		SetEnabled(false);
}


BSidebarItem::BSidebarItem(BMessage* archive)
	:
	BListItem(archive),
	fIcon(NULL),
	fLock(NULL),
	fCount(-1),
	fBadge(B_SIDEBAR_BADGE_NONE),
	fEmphasized(false),
	fInGroup(false),
	fFirst(false),
	fLast(false),
	fData(NULL)
{
	if (archive != NULL) {
		const char* label = NULL;
		if (archive->FindString("r5:label", &label) == B_OK)
			fLabel = label;
		const char* id = NULL;
		if (archive->FindString("r5:id", &id) == B_OK)
			fId = id;
		const char* keywords = NULL;
		if (archive->FindString("r5:keywords", &keywords) == B_OK)
			fKeywords = keywords;
		archive->FindInt32("r5:count", &fCount);
		int32 badge = B_SIDEBAR_BADGE_NONE;
		if (archive->FindInt32("r5:badge", &badge) == B_OK)
			fBadge = (sidebar_badge)badge;
		bool emphasized = false;
		if (archive->FindBool("r5:emphasized", &emphasized) == B_OK)
			fEmphasized = emphasized;
		BMessage icon;
		if (archive->FindMessage("r5:icon", &icon) == B_OK)
			fIcon = dynamic_cast<BBitmap*>(BBitmap::Instantiate(&icon));
	}
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	if (_IsSeparator())
		SetEnabled(false);
}


BSidebarItem::~BSidebarItem()
{
	delete fIcon;
	delete fLock;
	delete fData;
}


BArchivable*
BSidebarItem::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kItemClassName))
		return NULL;
	return new BSidebarItem(archive);
}


status_t
BSidebarItem::Archive(BMessage* archive, bool deep) const
{
	status_t status = BListItem::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kItemClassName) != B_OK)
		status = archive->AddString("class", kItemClassName);
	if (status == B_OK)
		status = archive->AddString("r5:label", fLabel);
	if (status == B_OK)
		status = archive->AddString("r5:id", fId);
	if (status == B_OK)
		status = archive->AddString("r5:keywords", fKeywords);
	if (status == B_OK)
		status = archive->AddInt32("r5:count", fCount);
	if (status == B_OK)
		status = archive->AddInt32("r5:badge", (int32)fBadge);
	if (status == B_OK)
		status = archive->AddBool("r5:emphasized", fEmphasized);
	if (status == B_OK && fIcon != NULL) {
		BMessage icon;
		if (fIcon->Archive(&icon) == B_OK)
			status = archive->AddMessage("r5:icon", &icon);
	}
	return status;
}


void
BSidebarItem::Update(BView* owner, const BFont* font)
{
	(void)owner;
	font_height height;
	font->GetHeight(&height);
	float line = height.ascent + height.descent + height.leading;
	float spacing = be_control_look->DefaultItemSpacing();
	float row = line + spacing;
	if (OutlineLevel() == 0 && !_IsSeparator()) {
		float icon = be_control_look->ComposeIconSize(24).height;
		if (icon > line)
			row = icon + spacing;
	}
	SetHeight(row);
	if (fBadge == B_SIDEBAR_BADGE_LOCKED && fLock == NULL) {
		int32 side = (int32)be_control_look->ComposeIconSize(
			B_MINI_ICON).IntegerWidth() + 1;
		fLock = r5_icon_bitmap(R5_ICON_LOCKED, side);
		if (fLock != NULL) {
			uint8* bits = (uint8*)fLock->Bits();
			int32 length = fLock->BitsLength();
			for (int32 i = 3; i < length; i += 4)
				bits[i] = (uint8)((bits[i] * 60) / 100);
		}
	}
}


void
BSidebarItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	(void)complete;
	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color text = ui_color(B_LIST_ITEM_TEXT_COLOR);
	bool selected = IsSelected() && !_IsSeparator();
	if (!fInGroup) {
		owner->SetLowColor(background);
		owner->FillRect(frame, B_SOLID_LOW);
	}
	float spacing = be_control_look->DefaultItemSpacing();
	float radius = be_plain_font->Size() * 0.25f;
	if (selected) {
		BRect mark = frame.InsetByCopy(spacing * 0.5f, spacing * 0.5f);
		owner->SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
		owner->FillRoundRect(mark, radius, radius);
		text = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
	}

	if (_IsSeparator()) {
		owner->SetHighColor(tint_color(background, B_DARKEN_2_TINT));
		float y = (frame.top + frame.bottom) / 2;
		owner->StrokeLine(BPoint(frame.left + spacing, y),
			BPoint(frame.right - spacing, y));
		return;
	}

	BSize iconSize = be_control_look->ComposeIconSize(24);
	float textLeft = iconSize.width + spacing;
	if (OutlineLevel() == 0 && fIcon != NULL) {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BPoint where(frame.left,
			frame.top + (frame.Height() - fIcon->Bounds().Height()) / 2);
		owner->DrawBitmap(fIcon, where);
		owner->SetDrawingMode(B_OP_COPY);
	} else if (OutlineLevel() > 0)
		textLeft = iconSize.width + spacing;

	BFont font(be_plain_font);
	if (fEmphasized || selected || (OutlineLevel() == 0 && !selected)) {
		if (fEmphasized || selected || OutlineLevel() == 0)
			font.SetFace(B_BOLD_FACE);
	}
	if (OutlineLevel() == 0)
		font.SetFace(B_BOLD_FACE);
	if (!selected && OutlineLevel() > 0)
		text = fade(text, background, 204);
	owner->SetFont(&font);
	font_height height;
	font.GetHeight(&height);

	float right = frame.right - spacing;
	if (OutlineLevel() == 0 && fCount >= 0) {
		if (IsExpanded()) {
			BRect arrow(right - spacing, frame.top,
				right, frame.top + spacing);
			arrow.OffsetBy(0, (frame.Height() - arrow.Height()) / 2);
			be_control_look->DrawArrowShape(owner, arrow, frame,
				background, BControlLook::B_DOWN_ARROW, 0, B_DARKEN_2_TINT);
			right = arrow.left - spacing;
		} else {
			BString count;
			count << fCount;
			float width = font.StringWidth(count.String());
			owner->SetHighColor(fade(text, background, 153));
			owner->DrawString(count.String(), BPoint(right - width,
				frame.bottom - height.descent - (frame.Height()
					- (height.ascent + height.descent)) / 2));
			right -= width + spacing;
		}
	} else if (fBadge == B_SIDEBAR_BADGE_LOCKED && fLock != NULL) {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BPoint where(right - fLock->Bounds().Width(),
			frame.top + (frame.Height() - fLock->Bounds().Height()) / 2);
		owner->DrawBitmap(fLock, where);
		owner->SetDrawingMode(B_OP_COPY);
		right = where.x - spacing;
	}

	BString label(fLabel);
	float room = right - (frame.left + textLeft);
	if (room < spacing)
		room = spacing;
	font.TruncateString(&label, B_TRUNCATE_END, room);
	owner->SetHighColor(text);
	owner->DrawString(label.String(),
		BPoint(frame.left + textLeft,
			frame.bottom - height.descent
				- (frame.Height() - (height.ascent + height.descent)) / 2));
	owner->SetFont(be_plain_font);
}


const char*
BSidebarItem::Label() const
{
	return fLabel.String();
}


const char*
BSidebarItem::Id() const
{
	return fId.String();
}


void
BSidebarItem::SetIcon(const BBitmap* icon)
{
	delete fIcon;
	fIcon = _Copy(icon);
}


void
BSidebarItem::SetCount(int32 count)
{
	fCount = count;
}


int32
BSidebarItem::Count() const
{
	return fCount;
}


void
BSidebarItem::SetBadge(sidebar_badge badge)
{
	fBadge = badge;
}


sidebar_badge
BSidebarItem::Badge() const
{
	return fBadge;
}


void
BSidebarItem::SetEmphasized(bool bold)
{
	fEmphasized = bold;
}


bool
BSidebarItem::IsEmphasized() const
{
	return fEmphasized;
}


void
BSidebarItem::SetKeywords(const char* keywords)
{
	fKeywords = keywords;
}


const char*
BSidebarItem::Keywords() const
{
	return fKeywords.String();
}


void
BSidebarItem::_SetRole(bool inGroup, bool first, bool last)
{
	fInGroup = inGroup;
	fFirst = first;
	fLast = last;
	(void)fFirst;
	(void)fLast;
}


bool
BSidebarItem::_IsSeparator() const
{
	return fLabel.Length() == 0 && fId.Length() == 0;
}


BBitmap*
BSidebarItem::_Copy(const BBitmap* icon) const
{
	if (icon == NULL)
		return NULL;
	BBitmap* copy = new(std::nothrow) BBitmap(icon->Bounds(), 0,
		icon->ColorSpace());
	if (copy == NULL || !copy->IsValid()) {
		delete copy;
		return NULL;
	}
	memcpy(copy->Bits(), icon->Bits(), icon->BitsLength());
	return copy;
}


void
BSidebarItem::_ReservedSidebarItem1()
{
}


void
BSidebarItem::_ReservedSidebarItem2()
{
}


void
BSidebarItem::_ReservedSidebarItem3()
{
}


void
BSidebarItem::_ReservedSidebarItem4()
{
}


}	// namespace BAppearanceKit
