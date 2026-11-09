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


#include "ScreenArrangement.h"

#include <ControlLook.h>
#include <Deskbar.h>
#include <Message.h>
#include <R5UI.h>
#include <String.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <new>
#include <vector>


namespace BAppearanceKit {


static const char* kClassName = "BAppearanceKit::BScreenArrangement";


struct TileCopy {
	int32		id;
	BRect		frame;
	BString		name;
	bool		main;
	bool		enabled;
	BRect		drawn;
};


struct BScreenArrangement::Data {
	std::vector<TileCopy>	tiles;
	screen_tile				exposed;
	BString					exposedName;
	int32					selected;
	bool					draggable;
	BString					caption;
	BMessage*				modification;
	bool					dragging;
	int32					dragIndex;
	BPoint					dragStart;
	BRect					dragFrame;
	bool					placed;
};


static bool
tile_before(const TileCopy& left, const TileCopy& right)
{
	return left.id < right.id;
}


static float
unit_size()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 13.0f;
	if (size < 1.0f)
		size = 13.0f;
	return size / 13.0f;
}


static rgb_color
picture_fill()
{
	return mix_color(ui_color(B_LIST_BACKGROUND_COLOR),
		ui_color(B_CONTROL_HIGHLIGHT_COLOR), 18);
}


static rgb_color
picture_border()
{
	return mix_color(ui_color(B_LIST_BACKGROUND_COLOR),
		ui_color(B_CONTROL_HIGHLIGHT_COLOR), 48);
}


BScreenArrangement::BScreenArrangement(const char* name,
	BMessage* selectionMessage)
	:
	BControl(name, NULL, selectionMessage, B_WILL_DRAW | B_NAVIGABLE
		| B_FULL_UPDATE_ON_RESIZE),
	fData(NULL)
{
	_Init();
}


BScreenArrangement::BScreenArrangement(BMessage* archive)
	:
	BControl(archive),
	fData(NULL)
{
	_Init();
	if (archive == NULL)
		return;
	const char* caption = NULL;
	if (archive->FindString("r5:caption", &caption) == B_OK)
		fData->caption = caption;
	bool draggable = false;
	if (archive->FindBool("r5:draggable", &draggable) == B_OK)
		fData->draggable = draggable;
	int32 selected = -1;
	if (archive->FindInt32("r5:selected", &selected) == B_OK)
		fData->selected = selected;
	BMessage stored;
	for (int32 index = 0; archive->FindMessage("r5:screen", index, &stored)
			== B_OK; index++) {
		TileCopy tile;
		tile.id = 0;
		tile.main = false;
		tile.enabled = true;
		stored.FindInt32("id", &tile.id);
		stored.FindRect("frame", &tile.frame);
		const char* name = NULL;
		if (stored.FindString("name", &name) == B_OK)
			tile.name = name;
		stored.FindBool("main", &tile.main);
		stored.FindBool("enabled", &tile.enabled);
		fData->tiles.push_back(tile);
		stored.MakeEmpty();
	}
	BMessage modification;
	if (archive->FindMessage("r5:modification", &modification) == B_OK)
		fData->modification = new BMessage(modification);
}


BScreenArrangement::~BScreenArrangement()
{
	if (fData != NULL)
		delete fData->modification;
	delete fData;
}


BArchivable*
BScreenArrangement::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kClassName))
		return NULL;
	return new BScreenArrangement(archive);
}


status_t
BScreenArrangement::Archive(BMessage* archive, bool deep) const
{
	status_t status = BControl::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kClassName) != B_OK)
		status = archive->AddString("class", kClassName);
	if (status == B_OK && fData->caption.Length() > 0)
		status = archive->AddString("r5:caption", fData->caption);
	if (status == B_OK)
		status = archive->AddBool("r5:draggable", fData->draggable);
	if (status == B_OK)
		status = archive->AddInt32("r5:selected", fData->selected);
	for (uint32 index = 0; status == B_OK && index < fData->tiles.size();
			index++) {
		BMessage stored;
		const TileCopy& tile = fData->tiles[index];
		stored.AddInt32("id", tile.id);
		stored.AddRect("frame", tile.frame);
		stored.AddString("name", tile.name);
		stored.AddBool("main", tile.main);
		stored.AddBool("enabled", tile.enabled);
		status = archive->AddMessage("r5:screen", &stored);
	}
	if (status == B_OK && fData->modification != NULL)
		status = archive->AddMessage("r5:modification", fData->modification);
	return status;
}


void
BScreenArrangement::_Init()
{
	fData = new(std::nothrow) Data;
	if (fData == NULL)
		return;
	fData->selected = -1;
	fData->draggable = false;
	fData->modification = NULL;
	fData->dragging = false;
	fData->dragIndex = -1;
	fData->placed = false;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	SetViewColor(picture_fill());
	SetExplicitMinSize(MinSize());
	SetExplicitPreferredSize(PreferredSize());
}


float
BScreenArrangement::_Unit() const
{
	return unit_size();
}


void
BScreenArrangement::SetScreens(const screen_tile* tiles, int32 count)
{
	if (fData == NULL)
		return;
	fData->tiles.clear();
	if (tiles != NULL && count > 0) {
		for (int32 index = 0; index < count; index++) {
			TileCopy copy;
			copy.id = tiles[index].id;
			copy.frame = tiles[index].frame;
			copy.name = tiles[index].name != NULL ? tiles[index].name : "";
			copy.main = tiles[index].main;
			copy.enabled = tiles[index].enabled;
			fData->tiles.push_back(copy);
		}
		std::sort(fData->tiles.begin(), fData->tiles.end(), tile_before);
	}
	bool stillThere = false;
	for (uint32 index = 0; index < fData->tiles.size(); index++) {
		if (fData->tiles[index].id == fData->selected)
			stillThere = true;
	}
	if (!stillThere)
		fData->selected = fData->tiles.empty() ? -1 : fData->tiles[0].id;
	fData->placed = false;
	Invalidate();
}


int32
BScreenArrangement::CountScreens() const
{
	if (fData == NULL)
		return 0;
	return (int32)fData->tiles.size();
}


const screen_tile*
BScreenArrangement::ScreenAt(int32 index) const
{
	if (fData == NULL || index < 0 || index >= (int32)fData->tiles.size())
		return NULL;
	const TileCopy& tile = fData->tiles[index];
	fData->exposedName = tile.name;
	fData->exposed.id = tile.id;
	fData->exposed.frame = tile.frame;
	fData->exposed.name = fData->exposedName.String();
	fData->exposed.main = tile.main;
	fData->exposed.enabled = tile.enabled;
	return &fData->exposed;
}


void
BScreenArrangement::SetSelected(int32 id)
{
	if (fData == NULL || fData->selected == id)
		return;
	fData->selected = id;
	Invalidate();
}


int32
BScreenArrangement::Selected() const
{
	return fData != NULL ? fData->selected : -1;
}


void
BScreenArrangement::SetDraggable(bool draggable)
{
	if (fData != NULL)
		fData->draggable = draggable;
}


bool
BScreenArrangement::IsDraggable() const
{
	return fData != NULL && fData->draggable;
}


void
BScreenArrangement::SetModificationMessage(BMessage* message)
{
	if (fData == NULL) {
		delete message;
		return;
	}
	delete fData->modification;
	fData->modification = message;
}


void
BScreenArrangement::SetCaption(const char* caption)
{
	if (fData == NULL)
		return;
	fData->caption = caption != NULL ? caption : "";
	Invalidate();
}


const char*
BScreenArrangement::Caption() const
{
	if (fData == NULL || fData->caption.Length() == 0)
		return NULL;
	return fData->caption.String();
}


void
BScreenArrangement::_Place()
{
	if (fData == NULL)
		return;
	fData->placed = true;
	const int32 count = (int32)fData->tiles.size();
	for (int32 index = 0; index < count; index++)
		fData->tiles[index].drawn = BRect();
	if (count == 0)
		return;

	float inset = be_control_look != NULL
		? be_control_look->DefaultItemSpacing() : 8.0f;
	BRect area = Bounds();
	area.InsetBy(inset, inset);
	float nameRoom = _Unit() * 16.0f;
	if (area.Height() > nameRoom + 20.0f)
		area.bottom -= nameRoom;
	if (area.Width() < 4.0f || area.Height() < 4.0f)
		return;

	if (count == 1) {
		TileCopy& tile = fData->tiles[0];
		float frameWidth = tile.frame.Width();
		float frameHeight = tile.frame.Height();
		if (frameWidth < 1.0f)
			frameWidth = 4.0f;
		if (frameHeight < 1.0f)
			frameHeight = 3.0f;
		float height = Bounds().Height() * 0.60f;
		if (height > area.Height())
			height = area.Height();
		float width = height * (frameWidth / frameHeight);
		if (width > area.Width()) {
			width = area.Width();
			height = width * (frameHeight / frameWidth);
		}
		float left = area.left + (area.Width() - width) / 2.0f;
		float top = area.top + (area.Height() - height) / 2.0f;
		tile.drawn.Set(left, top, left + width, top + height);
		return;
	}

	BRect unionFrame = fData->tiles[0].frame;
	for (int32 index = 1; index < count; index++)
		unionFrame = unionFrame | fData->tiles[index].frame;
	float spanW = unionFrame.Width();
	float spanH = unionFrame.Height();
	if (spanW < 1.0f)
		spanW = 1.0f;
	if (spanH < 1.0f)
		spanH = 1.0f;
	float scale = area.Width() / spanW;
	if (spanH * scale > area.Height())
		scale = area.Height() / spanH;
	float usedW = spanW * scale;
	float usedH = spanH * scale;
	float originX = area.left + (area.Width() - usedW) / 2.0f;
	float originY = area.top + (area.Height() - usedH) / 2.0f;
	for (int32 index = 0; index < count; index++) {
		TileCopy& tile = fData->tiles[index];
		float left = originX + (tile.frame.left - unionFrame.left) * scale;
		float top = originY + (tile.frame.top - unionFrame.top) * scale;
		float right = left + tile.frame.Width() * scale;
		float bottom = top + tile.frame.Height() * scale;
		tile.drawn.Set(left, top, right, bottom);
	}
}


static void
deskbar_strip(BView* view, BRect content)
{
	BRect bar = content.InsetByCopy(3, 3);
	if (!bar.IsValid())
		return;
	float thickness = unit_size() * 6.0f;
	deskbar_location location = B_DESKBAR_TOP;
	BDeskbar deskbar;
	if (deskbar.IsRunning())
		location = deskbar.Location();
	bool expanded = deskbar.IsExpanded();
	switch (location) {
		case B_DESKBAR_TOP:
			bar.bottom = bar.top + thickness;
			break;
		case B_DESKBAR_BOTTOM:
			bar.top = bar.bottom - thickness;
			break;
		case B_DESKBAR_LEFT_TOP:
			bar.right = bar.left + thickness + 2;
			bar.bottom = bar.top + (expanded ? bar.Height() * 0.72f : 16);
			break;
		case B_DESKBAR_RIGHT_TOP:
			bar.left = bar.right - thickness - 2;
			bar.bottom = bar.top + (expanded ? bar.Height() * 0.72f : 16);
			break;
		case B_DESKBAR_LEFT_BOTTOM:
			bar.right = bar.left + thickness + 2;
			bar.top = bar.bottom - 16;
			break;
		case B_DESKBAR_RIGHT_BOTTOM:
			bar.left = bar.right - thickness - 2;
			bar.top = bar.bottom - 16;
			break;
		default:
			bar.bottom = bar.top + thickness;
			break;
	}
	view->SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	view->FillRect(bar);
}


void
BScreenArrangement::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fData == NULL)
		return;
	BRect bounds = Bounds();
	SetHighColor(picture_fill());
	FillRect(bounds);
	SetHighColor(picture_border());
	StrokeRect(bounds);

	if (!fData->placed)
		_Place();

	const float u = _Unit();
	rgb_color desktop = ui_color(B_DESKTOP_COLOR);
	rgb_color selectedColor = ui_color(B_STATUS_BAR_COLOR);
	for (uint32 index = 0; index < fData->tiles.size(); index++) {
		TileCopy& tile = fData->tiles[index];
		if (!tile.drawn.IsValid() || tile.drawn.Width() < 2.0f)
			continue;
		bool selected = tile.id == fData->selected;
		if (selected) {
			BRect ring = tile.drawn.InsetByCopy(-(u * 2.0f), -(u * 2.0f));
			SetHighColor(selectedColor);
			SetPenSize(u * 3.0f);
			StrokeRect(ring);
			SetPenSize(1.0f);
		}
		BRect content = r5_screen_draw(this, tile.drawn, desktop);
		if (tile.main)
			deskbar_strip(this, content);

		float disc = selected ? u * 26.0f : u * 22.0f;
		BPoint center((content.left + content.right) / 2.0f,
			(content.top + content.bottom) / 2.0f);
		BRect bubble(center.x - disc / 2.0f, center.y - disc / 2.0f,
			center.x + disc / 2.0f, center.y + disc / 2.0f);
		rgb_color bubbleColor = selected
			? selectedColor : ui_color(B_CONTROL_BORDER_COLOR);
		SetHighColor(bubbleColor);
		FillEllipse(bubble);
		int32 luminance = bubbleColor.red * 30 + bubbleColor.green * 59
			+ bubbleColor.blue * 11;
		SetHighColor(luminance < 14000
			? make_color(255, 255, 255) : make_color(0, 0, 0));
		BFont numberFont(be_bold_font);
		numberFont.SetSize(std::max(8.0f, disc * 0.55f));
		SetFont(&numberFont);
		BString number;
		number.SetToFormat("%" B_PRIu32, index + 1);
		float width = StringWidth(number.String());
		font_height height;
		numberFont.GetHeight(&height);
		DrawString(number.String(), BPoint(center.x - width / 2.0f,
			center.y + (height.ascent - height.descent) / 2.0f));

		if (tile.name.Length() > 0) {
			BFont nameFont(be_bold_font);
			nameFont.SetSize(u * 10.0f);
			SetFont(&nameFont);
			SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
			BString label(tile.name);
			float room = std::max(tile.drawn.Width(), u * 40.0f);
			TruncateString(&label, B_TRUNCATE_END, room);
			float labelWidth = StringWidth(label.String());
			DrawString(label.String(), BPoint(
				(tile.drawn.left + tile.drawn.right) / 2.0f - labelWidth / 2.0f,
				tile.drawn.bottom + u * 12.0f));
		}
	}

	if (fData->caption.Length() > 0) {
		BFont captionFont(be_plain_font);
		captionFont.SetSize(u * 11.0f);
		SetFont(&captionFont);
		SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			ui_color(B_DOCUMENT_TEXT_COLOR), 140));
		BString caption(fData->caption);
		TruncateString(&caption, B_TRUNCATE_END,
			std::max(1.0f, bounds.Width() - u * 24.0f));
		font_height height;
		captionFont.GetHeight(&height);
		DrawString(caption.String(), BPoint(bounds.left + u * 12.0f,
			bounds.bottom - u * 8.0f));
		(void)height;
	}

	if (IsFocus()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(bounds.InsetByCopy(1, 1));
	}
}


int32
BScreenArrangement::_IndexAt(BPoint where) const
{
	if (fData == NULL)
		return -1;
	for (int32 index = (int32)fData->tiles.size() - 1; index >= 0; index--) {
		if (fData->tiles[index].drawn.InsetByCopy(-4, -4).Contains(where))
			return index;
	}
	return -1;
}


void
BScreenArrangement::_SelectIndex(int32 index, bool invoke)
{
	if (fData == NULL || index < 0 || index >= (int32)fData->tiles.size())
		return;
	fData->selected = fData->tiles[index].id;
	Invalidate();
	if (!invoke)
		return;
	BMessage message(Message() != NULL ? *Message() : BMessage(B_CONTROL_INVOKED));
	message.RemoveName("be:screen");
	message.AddInt32("be:screen", fData->selected);
	Invoke(&message);
}


void
BScreenArrangement::MouseDown(BPoint where)
{
	MakeFocus(true);
	if (fData == NULL)
		return;
	if (!fData->placed)
		_Place();
	int32 index = _IndexAt(where);
	if (index < 0)
		return;
	_SelectIndex(index, true);
	if (fData->draggable && fData->tiles.size() > 1) {
		fData->dragging = true;
		fData->dragIndex = index;
		fData->dragStart = where;
		fData->dragFrame = fData->tiles[index].frame;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	}
}


void
BScreenArrangement::_Snap(int32 index, BRect proposed)
{
	if (fData == NULL || index < 0 || index >= (int32)fData->tiles.size())
		return;
	const float slack = 48.0f;
	for (int32 other = 0; other < (int32)fData->tiles.size(); other++) {
		if (other == index)
			continue;
		BRect neighbor = fData->tiles[other].frame;
		bool overlapX = proposed.left < neighbor.right
			&& proposed.right > neighbor.left;
		bool overlapY = proposed.top < neighbor.bottom
			&& proposed.bottom > neighbor.top;
		if (overlapX) {
			float below = proposed.top - neighbor.bottom;
			float above = neighbor.top - proposed.bottom;
			if (below >= -slack && below < slack && fabs(below) <= fabs(above))
				proposed.OffsetBy(0, -below);
			else if (above >= -slack && above < slack)
				proposed.OffsetBy(0, above);
		}
		if (overlapY) {
			float right = proposed.left - neighbor.right;
			float left = neighbor.left - proposed.right;
			if (right >= -slack && right < slack && fabs(right) <= fabs(left))
				proposed.OffsetBy(-right, 0);
			else if (left >= -slack && left < slack)
				proposed.OffsetBy(left, 0);
		}
	}
	fData->tiles[index].frame = proposed;
	fData->placed = false;
	Invalidate();

	if (fData->modification == NULL || Window() == NULL)
		return;
	BMessage message(*fData->modification);
	message.RemoveName("be:screen");
	message.RemoveName("be:origin");
	message.AddInt32("be:screen", fData->tiles[index].id);
	message.AddPoint("be:origin", proposed.LeftTop());
	BMessenger(Window()).SendMessage(&message);
}


void
BScreenArrangement::MouseUp(BPoint where)
{
	if (fData == NULL || !fData->dragging)
		return;
	int32 index = fData->dragIndex;
	fData->dragging = false;
	fData->dragIndex = -1;
	if (index < 0 || index >= (int32)fData->tiles.size())
		return;
	BRect drawn = fData->tiles[index].drawn;
	float scale = 1.0f;
	if (drawn.Width() > 1.0f && fData->dragFrame.Width() > 1.0f)
		scale = fData->dragFrame.Width() / drawn.Width();
	BPoint delta = where - fData->dragStart;
	BRect proposed = fData->dragFrame;
	proposed.OffsetBy(delta.x * scale, delta.y * scale);
	_Snap(index, proposed);
}


void
BScreenArrangement::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	(void)where;
	(void)transit;
	(void)dragMessage;
}


int32
BScreenArrangement::_Neighbor(int32 index, int32 dx, int32 dy) const
{
	if (fData == NULL || index < 0)
		return -1;
	BPoint origin = fData->tiles[index].frame.LeftTop();
	origin.x += fData->tiles[index].frame.Width() / 2.0f;
	origin.y += fData->tiles[index].frame.Height() / 2.0f;
	int32 best = -1;
	float bestDistance = 1.0e12f;
	for (int32 other = 0; other < (int32)fData->tiles.size(); other++) {
		if (other == index)
			continue;
		BPoint center = fData->tiles[other].frame.LeftTop();
		center.x += fData->tiles[other].frame.Width() / 2.0f;
		center.y += fData->tiles[other].frame.Height() / 2.0f;
		float vx = center.x - origin.x;
		float vy = center.y - origin.y;
		if (dx != 0 && ((dx > 0 && vx <= 0) || (dx < 0 && vx >= 0)))
			continue;
		if (dy != 0 && ((dy > 0 && vy <= 0) || (dy < 0 && vy >= 0)))
			continue;
		if (dx != 0 && fabs(vy) > fabs(vx))
			continue;
		if (dy != 0 && fabs(vx) > fabs(vy))
			continue;
		float distance = vx * vx + vy * vy;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = other;
		}
	}
	return best;
}


void
BScreenArrangement::KeyDown(const char* bytes, int32 numBytes)
{
	if (fData == NULL || numBytes < 1) {
		BControl::KeyDown(bytes, numBytes);
		return;
	}
	int32 current = -1;
	for (int32 index = 0; index < (int32)fData->tiles.size(); index++) {
		if (fData->tiles[index].id == fData->selected)
			current = index;
	}
	int32 next = -1;
	switch (bytes[0]) {
		case B_LEFT_ARROW:
			next = _Neighbor(current, -1, 0);
			break;
		case B_RIGHT_ARROW:
			next = _Neighbor(current, 1, 0);
			break;
		case B_UP_ARROW:
			next = _Neighbor(current, 0, -1);
			break;
		case B_DOWN_ARROW:
			next = _Neighbor(current, 0, 1);
			break;
		case B_SPACE:
		case B_ENTER:
			_SelectIndex(current, true);
			return;
		default:
			BControl::KeyDown(bytes, numBytes);
			return;
	}
	if (next >= 0)
		_SelectIndex(next, false);
}


void
BScreenArrangement::MakeFocus(bool focus)
{
	BControl::MakeFocus(focus);
	Invalidate();
}


void
BScreenArrangement::AttachedToWindow()
{
	BControl::AttachedToWindow();
	fData->placed = false;
}


void
BScreenArrangement::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED || message->what == B_FONTS_UPDATED)
		Invalidate();
	BControl::MessageReceived(message);
}


BSize
BScreenArrangement::MinSize()
{
	float u = unit_size();
	return BSize(u * 220.0f, u * 100.0f);
}


BSize
BScreenArrangement::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


BSize
BScreenArrangement::PreferredSize()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 12.0f;
	return BSize(size / 12.0f * 320.0f, size / 12.0f * 140.0f);
}


void
BScreenArrangement::_ReservedScreenArrangement1()
{
}


void
BScreenArrangement::_ReservedScreenArrangement2()
{
}


void
BScreenArrangement::_ReservedScreenArrangement3()
{
}


void
BScreenArrangement::_ReservedScreenArrangement4()
{
}


}	// namespace BAppearanceKit
