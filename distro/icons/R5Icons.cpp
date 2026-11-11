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
 *
 * The pictures in standard_icons.inc are unmodified Tracker HVIF.
 * Their license is distro/icons/TRACKER_ICON_NOTICE.txt, not this header.
 * The pictures in lock_icons.inc stay under distro/icons/HAIKU_ICON_NOTICE.txt.
 */


#include "R5Icons.h"

#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Font.h>
#include <IconUtils.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Shape.h>
#include <View.h>

#include <algorithm>
#include <math.h>
#include <new>
#include <string.h>


#include "standard_icons.inc"
#include "lock_icons.inc"


static int32
icon_pixels(float side)
{
	if (side < 1.0f)
		return 0;
	return (int32)side;
}


static BBitmap*
bitmap_from_vector(const uint8* data, size_t size, int32 pixels)
{
	if (data == NULL || size == 0 || pixels < 1)
		return NULL;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, pixels - 1, pixels - 1),
		B_RGBA32);
	if (bitmap->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon(data, size, bitmap) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


static float
icon_side_for_button(float buttonHeight)
{
	float left;
	float top;
	float right;
	float bottom;
	be_control_look->GetInsets(BControlLook::B_BUTTON_FRAME,
		BControlLook::B_BUTTON_BACKGROUND, 0, left, top, right, bottom);
	// One pixel of air so the glyph does not sit on the button border.
	return buttonHeight - top - bottom - 2.0f;
}


static status_t
apply_button_bitmap(BButton* button, BBitmap* icon, float buttonHeight)
{
	if (button == NULL || buttonHeight < 1.0f) {
		delete icon;
		return B_BAD_VALUE;
	}

	status_t status = B_ERROR;
	if (icon != NULL)
		status = button->SetIcon(icon);
	delete icon;

	BSize box(buttonHeight, buttonHeight);
	button->SetExplicitMinSize(box);
	button->SetExplicitMaxSize(box);
	button->SetExplicitPreferredSize(box);
	return status;
}


BBitmap*
r5_icon_bitmap(r5_icon which, float side)
{
	const uint8* data = NULL;
	size_t size = 0;
	switch (which) {
		case R5_ICON_BACK:
			data = kBackIcon;
			size = kBackIconSize;
			break;
		case R5_ICON_FORWARD:
			data = kForwardIcon;
			size = kForwardIconSize;
			break;
		case R5_ICON_UP:
			data = kUpIcon;
			size = kUpIconSize;
			break;
		case R5_ICON_HOME:
			data = kHomeIcon;
			size = kHomeIconSize;
			break;
		case R5_ICON_UNLOCKED:
			data = kUnlockedIcon;
			size = kUnlockedIconSize;
			break;
		case R5_ICON_LOCKED:
			data = kLockedIcon;
			size = kLockedIconSize;
			break;
		default:
			return NULL;
	}
	return bitmap_from_vector(data, size, icon_pixels(side));
}


BBitmap*
r5_vector_icon_bitmap(const uint8* data, size_t size, float side)
{
	return bitmap_from_vector(data, size, icon_pixels(side));
}


BBitmap*
r5_file_icon_bitmap(const char* path, float side)
{
	int32 pixels = icon_pixels(side);
	if (path == NULL || path[0] == '\0' || pixels < 1)
		return NULL;

	BNode node(path);
	if (node.InitCheck() != B_OK)
		return NULL;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, pixels - 1, pixels - 1),
		B_RGBA32);
	BNodeInfo info(&node);
	if (bitmap->InitCheck() != B_OK
		|| info.GetTrackerIcon(bitmap, static_cast<icon_size>(pixels))
			!= B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


BBitmap*
r5_mime_icon_bitmap(const char* type, float side)
{
	if (type == NULL || type[0] == '\0')
		return NULL;

	BMimeType mime(type);
	uint8* data = NULL;
	size_t size = 0;
	if (mime.InitCheck() != B_OK || mime.GetIcon(&data, &size) != B_OK
		|| data == NULL || size == 0) {
		delete[] data;
		return NULL;
	}

	BBitmap* bitmap = bitmap_from_vector(data, size, icon_pixels(side));
	delete[] data;
	return bitmap;
}


status_t
r5_button_set_icon(BButton* button, r5_icon which, float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button, r5_icon_bitmap(which, side),
		buttonHeight);
}


status_t
r5_button_set_vector(BButton* button, const uint8* data, size_t size,
	float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button,
		r5_vector_icon_bitmap(data, size, side), buttonHeight);
}


status_t
r5_button_set_file_icon(BButton* button, const char* path,
	float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button, r5_file_icon_bitmap(path, side),
		buttonHeight);
}


status_t
r5_button_set_mime_icon(BButton* button, const char* type,
	float buttonHeight)
{
	float side = icon_side_for_button(buttonHeight);
	return apply_button_bitmap(button, r5_mime_icon_bitmap(type, side),
		buttonHeight);
}


status_t
r5_button_set_lock(BButton* button, bool locked, float buttonHeight)
{
	return r5_button_set_icon(button,
		locked ? R5_ICON_LOCKED : R5_ICON_UNLOCKED, buttonHeight);
}


// #pragma mark - line glyphs


// The glyphs are drawn on a 24 x 24 grid, with a 2.2 unit pen and round
// ends. The shapes follow the common open line symbols for power,
// restart, clock, and show or hide password.
static const float kGlyphGrid = 24.0f;


struct glyph_space {
	BRect	frame;
	float	scale;

	BPoint Map(float x, float y) const
	{
		return BPoint(frame.left + x * scale, frame.top + y * scale);
	}

	float Size(float v) const
	{
		return v * scale;
	}
};


static void
glyph_arc(BView* view, const glyph_space& space, float cx, float cy,
	float radius, float startAngle, float arcAngle)
{
	view->StrokeArc(space.Map(cx, cy), space.Size(radius),
		space.Size(radius), startAngle, arcAngle);
}


static void
glyph_line(BView* view, const glyph_space& space, float x1, float y1,
	float x2, float y2)
{
	view->StrokeLine(space.Map(x1, y1), space.Map(x2, y2));
}


static void
glyph_eye(BView* view, const glyph_space& space)
{
	// The outline is two arcs that meet at the corners (1, 12) and
	// (23, 12): the upper lid reaches y 4.5, the lower lid y 19.5. Arcs,
	// because a stroked BShape is not drawn on every app_server path.
	glyph_arc(view, space, 12, 16.32f, 11.82f, 21.4f, 137.2f);
	glyph_arc(view, space, 12, 7.68f, 11.82f, 201.4f, 137.2f);

	glyph_arc(view, space, 12, 12, 3, 0, 360);
}


static void
glyph_paint(BView* view, r5_glyph which, BRect frame)
{
	glyph_space space;
	float side = std::min(frame.Width(), frame.Height());
	space.scale = side / kGlyphGrid;
	space.frame = BRect(frame.left + (frame.Width() - side) / 2,
		frame.top + (frame.Height() - side) / 2, 0, 0);
	space.frame.right = space.frame.left + side;
	space.frame.bottom = space.frame.top + side;

	view->SetPenSize(std::max(1.0f, space.Size(2.2f)));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	switch (which) {
		case R5_GLYPH_POWER:
			// An open ring with the gap at the top, and the stem.
			glyph_arc(view, space, 12, 13, 9, 135, 270);
			glyph_line(view, space, 12, 2, 12, 12);
			break;

		case R5_GLYPH_RESTART:
			// A ring that is open at the upper right, with the arrow
			// head on its upper end.
			glyph_arc(view, space, 12, 12, 9.5f, 30, 305);
			glyph_line(view, space, 20.5f, 3, 20.5f, 8.5f);
			glyph_line(view, space, 20.5f, 8.5f, 15, 8.5f);
			break;

		case R5_GLYPH_CLOCK:
			glyph_arc(view, space, 12, 12, 10, 0, 360);
			glyph_line(view, space, 12, 6, 12, 12);
			glyph_line(view, space, 12, 12, 16, 14);
			break;

		case R5_GLYPH_EYE:
			glyph_eye(view, space);
			break;

		case R5_GLYPH_EYE_OFF:
		{
			// The eye, with a band cut out around the slash, then the
			// slash itself.
			view->PushState();
			BShape band;
			float w = 2.4f;
			band.MoveTo(space.Map(1 - w, 1 + w));
			band.LineTo(space.Map(1 + w, 1 - w));
			band.LineTo(space.Map(23 + w, 23 - w));
			band.LineTo(space.Map(23 - w, 23 + w));
			band.Close();
			view->ClipToInverseShape(&band);
			glyph_eye(view, space);
			view->PopState();
			glyph_line(view, space, 2, 2, 22, 22);
			break;
		}

		default:
			break;
	}
}


void
r5_glyph_draw(BView* view, r5_glyph which, BRect frame, rgb_color color)
{
	if (view == NULL || !frame.IsValid())
		return;

	view->PushState();
	color.alpha = 255;
	view->SetHighColor(color);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	glyph_paint(view, which, frame);
	view->PopState();
}


BBitmap*
r5_glyph_bitmap(r5_glyph which, float side, rgb_color color)
{
	int32 pixels = icon_pixels(side);
	if (pixels < 1 || which < R5_GLYPH_POWER || which > R5_GLYPH_EYE_OFF)
		return NULL;

	BRect bounds(0, 0, pixels - 1, pixels - 1);
	BBitmap* bitmap = new(std::nothrow) BBitmap(bounds,
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	BView* view = new BView(bounds, "glyph", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);
	if (bitmap->Lock()) {
		color.alpha = 255;
		view->SetHighColor(color);
		// Composite keeps the coverage in the alpha channel, so the
		// edges stay smooth on any background.
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
		glyph_paint(view, which, BRect(0, 0, pixels, pixels));
		view->Sync();
		bitmap->RemoveChild(view);
		bitmap->Unlock();
	} else
		bitmap->RemoveChild(view);
	delete view;
	return bitmap;
}


status_t
r5_button_set_glyph(BButton* button, r5_glyph which)
{
	if (button == NULL)
		return B_BAD_VALUE;

	BFont font;
	button->GetFont(&font);
	float side = floorf(font.Size() * 1.15f + 0.5f);
	BBitmap* glyph = r5_glyph_bitmap(which, side,
		ui_color(B_CONTROL_TEXT_COLOR));
	if (glyph == NULL)
		return B_NO_MEMORY;

	status_t status = button->SetIcon(glyph);
	delete glyph;
	return status;
}
