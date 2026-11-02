/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * The wrench itself is the unmodified Haiku file overlay-prefs.hvif.
 */


#include "SettingsIcon.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <File.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Roster.h>

#include <string.h>


static const void*
overlay_data(size_t* size)
{
	static const void* data = NULL;
	static size_t length = 0;
	static bool tried = false;
	if (tried) {
		*size = length;
		return data;
	}
	tried = true;

	app_info info;
	BFile file;
	BResources resources;
	if (be_app->GetAppInfo(&info) != B_OK
		|| file.SetTo(&info.ref, B_READ_ONLY) != B_OK
		|| resources.SetTo(&file) != B_OK) {
		*size = 0;
		return NULL;
	}

	const void* bytes = resources.LoadResource(B_VECTOR_ICON_TYPE,
		"prefs-overlay", &length);
	if (bytes == NULL || length == 0) {
		length = 0;
		*size = 0;
		return NULL;
	}
	uint8* copy = new uint8[length];
	memcpy(copy, bytes, length);
	data = copy;
	*size = length;
	return data;
}


static BBitmap*
overlay_bitmap(BRect bounds)
{
	static BBitmap* bitmap = NULL;
	static BRect cached;
	if (bitmap != NULL && cached == bounds)
		return bitmap;

	size_t size = 0;
	const void* data = overlay_data(&size);
	if (data == NULL || size == 0)
		return NULL;

	BBitmap* next = new BBitmap(bounds, B_RGBA32);
	if (BIconUtils::GetVectorIcon(static_cast<const uint8*>(data), size, next)
			!= B_OK) {
		delete next;
		return NULL;
	}
	delete bitmap;
	bitmap = next;
	cached = bounds;
	return bitmap;
}


static void
blend_over(BBitmap* base, const BBitmap* over)
{
	uint8* dstRow = static_cast<uint8*>(base->Bits());
	const uint8* srcRow = static_cast<const uint8*>(over->Bits());
	int32 height = base->Bounds().IntegerHeight() + 1;
	int32 width = base->Bounds().IntegerWidth() + 1;
	int32 dstStride = base->BytesPerRow();
	int32 srcStride = over->BytesPerRow();

	for (int32 y = 0; y < height; y++) {
		uint8* dst = dstRow;
		const uint8* src = srcRow;
		for (int32 x = 0; x < width; x++) {
			uint8 alpha = src[3];
			if (alpha == 255) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 255;
			} else if (alpha != 0) {
				dst[0] = (uint8)((src[0] * alpha + dst[0] * (255 - alpha)) / 255);
				dst[1] = (uint8)((src[1] * alpha + dst[1] * (255 - alpha)) / 255);
				dst[2] = (uint8)((src[2] * alpha + dst[2] * (255 - alpha)) / 255);
				dst[3] = (uint8)(alpha + (dst[3] * (255 - alpha)) / 255);
			}
			dst += 4;
			src += 4;
		}
		dstRow += dstStride;
		srcRow += srcStride;
	}
}


void
r5_settings_apply_prefs_overlay(BBitmap* icon)
{
	if (icon == NULL || icon->ColorSpace() != B_RGBA32)
		return;
	BBitmap* overlay = overlay_bitmap(icon->Bounds());
	if (overlay == NULL || overlay->Bounds() != icon->Bounds())
		return;
	blend_over(icon, overlay);
}
