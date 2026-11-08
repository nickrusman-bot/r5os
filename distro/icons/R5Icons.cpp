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
#include <IconUtils.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>


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
