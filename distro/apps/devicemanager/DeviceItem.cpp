/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */


#include "DeviceItem.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <View.h>

#include <string.h>

#include "DeviceModel.h"


static const float kIconSize = 16;


DeviceItem::DeviceItem(const char* text, const BBitmap* icon, bool isGroup,
	int32 group, uint32 cookie, int32 status, bool dimmed)
	:
	BListItem(),
	fText(text),
	fIcon(icon),
	fIsGroup(isGroup),
	fGroup(group),
	fCookie(cookie),
	fStatus(status),
	fDimmed(dimmed)
{
}


void
DeviceItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);
	float text = ceilf(height.ascent + height.descent + height.leading);
	SetHeight(std::max(text, kIconSize) + 4);
}


void
DeviceItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color low = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
		: ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color text = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
		: ui_color(B_LIST_ITEM_TEXT_COLOR);
	if (fDimmed && !IsSelected())
		text = mix_color(text, low, 110);

	if (IsSelected() || complete) {
		owner->SetLowColor(low);
		owner->FillRect(frame, B_SOLID_LOW);
	} else
		owner->SetLowColor(low);

	BRect icon(frame.left + 2, 0, frame.left + 2 + kIconSize - 1, kIconSize - 1);
	icon.OffsetTo(icon.left, frame.top + floorf((frame.Height() - kIconSize) / 2));
	if (fIcon != NULL) {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		owner->DrawBitmap(fIcon, fIcon->Bounds(), icon);
		owner->SetDrawingMode(B_OP_COPY);
	}
	_DrawBadge(owner, icon);

	font_height height;
	owner->GetFontHeight(&height);
	float baseline = frame.top + floorf((frame.Height() + height.ascent
		- height.descent) / 2);
	owner->SetHighColor(text);
	owner->DrawString(fText.String(), BPoint(icon.right + 6, baseline));
}


// A warning triangle for "no driver", a gray down arrow for a driver that
// is turned off. Drawn at the lower left of the icon, as Windows does.
void
DeviceItem::_DrawBadge(BView* owner, BRect icon)
{
	if (fIsGroup)
		return;
	BRect badge(icon.left, icon.bottom - 8, icon.left + 8, icon.bottom);
	if (fStatus == DM_STATUS_NO_DRIVER) {
		owner->SetHighColor(255, 203, 0);
		owner->FillTriangle(BPoint(badge.left + 4, badge.top),
			BPoint(badge.left, badge.bottom), BPoint(badge.right, badge.bottom));
		owner->SetHighColor(80, 60, 0);
		owner->StrokeTriangle(BPoint(badge.left + 4, badge.top),
			BPoint(badge.left, badge.bottom), BPoint(badge.right, badge.bottom));
		owner->StrokeLine(BPoint(badge.left + 4, badge.top + 3),
			BPoint(badge.left + 4, badge.bottom - 3));
		owner->StrokeLine(BPoint(badge.left + 4, badge.bottom - 1),
			BPoint(badge.left + 4, badge.bottom - 1));
	} else if (fStatus == DM_STATUS_OFF_AFTER_RESTART) {
		owner->SetHighColor(250, 250, 250);
		owner->FillEllipse(badge);
		owner->SetHighColor(90, 90, 90);
		owner->StrokeEllipse(badge);
		owner->FillTriangle(BPoint(badge.left + 2, badge.top + 3),
			BPoint(badge.right - 2, badge.top + 3),
			BPoint(badge.left + 4, badge.bottom - 2));
	}
}


/*static*/ int
DeviceItem::Compare(const BListItem* a, const BListItem* b)
{
	const DeviceItem* first = dynamic_cast<const DeviceItem*>(a);
	const DeviceItem* second = dynamic_cast<const DeviceItem*>(b);
	if (first == NULL || second == NULL)
		return 0;
	return strcasecmp(first->Text(), second->Text());
}
