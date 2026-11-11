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


#include "R5UI.h"

#include <Bitmap.h>
#include <InterfaceDefs.h>
#include <View.h>

#include <math.h>
#include <new>
#include <string.h>


static const float kScreenRatio = 4.0f / 3.0f;


BRect
r5_screen_draw(BView* view, BRect bounds, rgb_color contentColor)
{
	if (view == NULL)
		return bounds;

	float width = bounds.Width();
	float height = bounds.Height();
	if (width < 2.0f || height < 2.0f)
		return bounds;

	if (width / kScreenRatio > height)
		width = height * kScreenRatio;
	else
		height = width / kScreenRatio;

	float left = bounds.left + (bounds.Width() - width) / 2.0f;
	float top = bounds.top + (bounds.Height() - height) / 2.0f;
	BRect outer(left, top, left + width, top + height);
	BRect inner = outer.InsetByCopy(5, 5);

	view->SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
	view->FillRoundRect(outer, 4, 4);
	view->SetHighColor(contentColor);
	view->FillRoundRect(inner, 2, 2);
	view->SetHighColor(ui_color(B_CONTROL_BORDER_COLOR));
	view->StrokeRoundRect(inner, 2, 2);
	return inner;
}


void
r5_screen_caption(BView* view, BRect content, const char* text,
	rgb_color textColor)
{
	if (view == NULL || text == NULL || text[0] == '\0')
		return;

	float textWidth = view->StringWidth(text);
	font_height fontHeight;
	view->GetFontHeight(&fontHeight);
	float height = fontHeight.ascent + fontHeight.descent;
	if (textWidth > content.Width() || height > content.Height())
		return;

	view->SetHighColor(textColor);
	view->DrawString(text, BPoint(
		content.left + (content.Width() - textWidth) / 2.0f,
		content.top + fontHeight.ascent
			+ (content.Height() - height) / 2.0f));
}


float
r5_avatar_icon_side(float circleSide)
{
	return floorf(circleSide * 0.78f + 0.5f);
}


void
r5_avatar_draw(BView* view, BRect circle, const BBitmap* icon,
	bool highlighted)
{
	if (view == NULL || !circle.IsValid())
		return;

	view->PushState();

	rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color plate;
	rgb_color ring;
	if (highlighted) {
		plate = mix_color(document, accent, 40);
		ring = mix_color(document, accent, 100);
	} else {
		plate = mix_color(panel, document, 90);
		ring = mix_color(panel, document, 200);
	}

	view->SetDrawingMode(B_OP_OVER);
	view->SetHighColor(plate);
	view->FillEllipse(circle);
	view->SetHighColor(ring);
	view->StrokeEllipse(circle);

	float side = r5_avatar_icon_side(circle.Width() + 1);
	BRect target(0, 0, side - 1, side - 1);
	target.OffsetTo(
		floorf(circle.left + (circle.Width() + 1 - side) / 2 + 0.5f),
		floorf(circle.top + (circle.Height() + 1 - side) / 2 + 0.5f));

	if (icon != NULL && icon->IsValid()) {
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BRect source = icon->Bounds();
		BBitmap* gray = NULL;
		if (!highlighted && icon->ColorSpace() == B_RGBA32) {
			// Anyone but an administrator gets a gray picture, as in the
			// mockup.
			gray = new(std::nothrow) BBitmap(icon);
			if (gray != NULL && gray->IsValid()) {
				uint8* bits = (uint8*)gray->Bits();
				int32 length = gray->BitsLength();
				for (int32 i = 0; i + 3 < length; i += 4) {
					uint8 luma = (uint8)((bits[i + 2] * 30 + bits[i + 1] * 59
						+ bits[i] * 11) / 100);
					bits[i + 0] = (uint8)((luma * 4 + bits[i + 0]) / 5);
					bits[i + 1] = (uint8)((luma * 4 + bits[i + 1]) / 5);
					bits[i + 2] = (uint8)((luma * 4 + bits[i + 2]) / 5);
				}
				icon = gray;
			}
		}
		if (source.Width() == target.Width()
			&& source.Height() == target.Height()) {
			view->DrawBitmap(icon, target.LeftTop());
		} else {
			view->DrawBitmap(icon, source, target,
				B_FILTER_BITMAP_BILINEAR);
		}
		delete gray;
	} else {
		float s = target.Width();
		view->SetHighColor(tint_color(plate, B_DARKEN_2_TINT));
		view->FillEllipse(BRect(target.left + s * 0.32f,
			target.top + s * 0.12f, target.right - s * 0.32f,
			target.top + s * 0.48f));
		view->FillRoundRect(BRect(target.left + s * 0.18f,
			target.top + s * 0.56f, target.right - s * 0.18f,
			target.bottom), s * 0.12f, s * 0.12f);
	}

	view->PopState();
}
