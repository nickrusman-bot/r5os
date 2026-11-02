/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
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
