/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "NetworkPage.h"

#include "SettingsFont.h"

#include <ControlLook.h>
#include <TextView.h>


float
network_unit()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 13.0f;
	return size / 13.0f;
}


rgb_color
network_quiet_color(uint8 amount)
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), amount);
}


rgb_color
network_box_fill()
{
	return r5_settings_tile_fill();
}


BTextView*
network_hint(const char* text)
{
	BTextView* view = new BTextView("hint");
	view->MakeEditable(false);
	view->MakeSelectable(false);
	view->SetStylable(true);
	view->SetInsets(0, 0, 0, 0);
	view->SetViewColor(network_box_fill());
	view->SetLowColor(network_box_fill());
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.9f);
	rgb_color color = network_quiet_color(140);
	view->SetFontAndColor(&font, B_FONT_ALL, &color);
	view->SetText(text);
	return view;
}


// #pragma mark - FilledBox


FilledBox::FilledBox(const char* name)
	:
	BBox(name)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
FilledBox::Draw(BRect updateRect)
{
	BRect fill = InnerFrame().InsetByCopy(-1, -1);
	fill.top = TopBorderOffset() + 1;
	SetHighColor(network_box_fill());
	FillRect(fill);
	BBox::Draw(updateRect);
}


// #pragma mark - BoxTitle


BoxTitle::BoxTitle(const char* text)
	:
	BStringView("box title", text)
{
	SetFont(r5_settings_bold_font());
}


void
BoxTitle::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float line = bounds.top + floorf(bounds.Height() / 2.0f);
	BBox* box = dynamic_cast<BBox*>(Parent());
	if (box != NULL)
		line = box->TopBorderOffset() - Frame().top;
	BRect top = bounds;
	top.bottom = line;
	BRect bottom = bounds;
	bottom.top = line + 1;
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(top);
	SetHighColor(network_box_fill());
	FillRect(bottom);
	font_height height;
	GetFontHeight(&height);
	float y = (bounds.top + bounds.bottom - ceilf(height.ascent)
		- ceilf(height.descent)) / 2.0f + ceilf(height.ascent);
	SetDrawingMode(B_OP_OVER);
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	DrawString(Text(), BPoint(bounds.left, y));
	SetDrawingMode(B_OP_COPY);
}


// #pragma mark - BoxCheckBox


BoxCheckBox::BoxCheckBox(const char* name, const char* label,
	BMessage* message)
	:
	BCheckBox(name, label, message)
{
	SetFont(r5_settings_bold_font());
}


void
BoxCheckBox::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	float line = bounds.top + floorf(bounds.Height() / 2.0f);
	BBox* box = dynamic_cast<BBox*>(Parent());
	if (box != NULL)
		line = box->TopBorderOffset() - Frame().top;
	BRect top = bounds;
	top.bottom = line;
	BRect bottom = bounds;
	bottom.top = line + 1;
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(top);
	SetHighColor(network_box_fill());
	FillRect(bottom);
	SetLowColor(network_box_fill());
	BCheckBox::Draw(updateRect);
}


// #pragma mark - ValueField


ValueField::ValueField(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE)
{
	SetFont(be_fixed_font);
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
ValueField::SetText(const char* text)
{
	if (fText == text)
		return;
	fText = text;
	_UpdateTip();
	Invalidate();
}


void
ValueField::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color fill = mix_color(panel, ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		128);
	SetHighColor(fill);
	FillRect(bounds);
	SetHighColor(tint_color(panel, B_DARKEN_2_TINT));
	StrokeRect(bounds);

	float inset = be_control_look->DefaultLabelSpacing();
	BString text(fText);
	TruncateString(&text, B_TRUNCATE_END, bounds.Width() - inset * 2);
	font_height height;
	GetFontHeight(&height);
	float y = floorf((bounds.top + bounds.bottom + height.ascent
		- height.descent) / 2);
	SetHighColor(network_quiet_color(160));
	SetLowColor(fill);
	DrawString(text.String(), BPoint(bounds.left + inset, y));
}


void
ValueField::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_UpdateTip();
}


BSize
ValueField::MinSize()
{
	font_height height;
	GetFontHeight(&height);
	return BSize(StringWidth("0000:0000::"),
		ceilf(height.ascent + height.descent) + network_unit() * 8);
}


BSize
ValueField::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
ValueField::PreferredSize()
{
	return MinSize();
}


void
ValueField::_UpdateTip()
{
	float inset = be_control_look->DefaultLabelSpacing();
	if (StringWidth(fText.String()) > Bounds().Width() - inset * 2)
		SetToolTip(fText.String());
	else
		SetToolTip((const char*)NULL);
}


// #pragma mark - LinkControl


LinkControl::LinkControl(const char* name, const char* label,
	BMessage* message)
	:
	BControl(name, label, message, B_WILL_DRAW | B_NAVIGABLE)
{
	// always on a filled box
	SetViewColor(network_box_fill());
	SetLowColor(network_box_fill());
}


void
LinkControl::Draw(BRect updateRect)
{
	(void)updateRect;
	font_height height;
	GetFontHeight(&height);
	float y = ceilf(height.ascent);
	SetHighColor(IsEnabled() ? ui_color(B_LINK_TEXT_COLOR)
		: network_quiet_color(80));
	DrawString(Label(), BPoint(0, y));
	float width = StringWidth(Label());
	StrokeLine(BPoint(0, y + 1), BPoint(width, y + 1));
	if (IsFocus()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeLine(BPoint(0, y + 2), BPoint(width, y + 2));
	}
}


void
LinkControl::MouseDown(BPoint where)
{
	(void)where;
	if (IsEnabled())
		Invoke();
}


void
LinkControl::GetPreferredSize(float* width, float* height)
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	if (width != NULL)
		*width = StringWidth(Label()) + 1;
	if (height != NULL)
		*height = ceilf(fontHeight.ascent + fontHeight.descent) + 3;
}


BSize
LinkControl::MinSize()
{
	float width;
	float height;
	GetPreferredSize(&width, &height);
	return BSize(width, height);
}


BSize
LinkControl::MaxSize()
{
	return MinSize();
}
