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

#include <InterfaceDefs.h>
#include <View.h>

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
