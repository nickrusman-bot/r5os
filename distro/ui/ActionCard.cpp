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


#include "ActionCard.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Window.h>

#include <new>
#include <stdio.h>


namespace BAppearanceKit {


struct BActionCard::Data {
};


static const char* kActionClassName = "BAppearanceKit::BActionCard";


static void
wrap_two(const BFont& font, const char* text, float width, BString& one,
	BString& two)
{
	one = "";
	two = "";
	if (text == NULL || text[0] == '\0')
		return;
	BString rest(text);
	int32 space = rest.FindFirst(' ');
	BString trial;
	while (rest.Length() > 0) {
		int32 cut = rest.FindFirst(' ');
		BString word(rest);
		if (cut >= 0)
			word.Truncate(cut);
		BString next(one);
		if (next.Length() > 0)
			next << " ";
		next << word;
		if (one.Length() > 0 && font.StringWidth(next.String()) > width)
			break;
		one = next;
		if (cut < 0) {
			rest = "";
			break;
		}
		rest.Remove(0, cut + 1);
	}
	if (rest.Length() > 0) {
		two = rest;
		if (font.StringWidth(two.String()) > width)
			font.TruncateString(&two, B_TRUNCATE_END, width);
	}
	(void)trial;
	(void)space;
}


BActionCard::BActionCard(const char* name, BMessage* message)
	:
	BCard(name, B_CARD_DOCUMENT),
	BInvoker(message, NULL),
	fIcon(NULL),
	fTagMessage(NULL),
	fTagColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR)),
	fHover(false),
	fPressed(false),
	fHighlighted(false),
	fPressedTag(-1),
	fData(NULL)
{
	_Init();
}


BActionCard::BActionCard(BMessage* archive)
	:
	BCard(archive),
	BInvoker(),
	fIcon(NULL),
	fTagMessage(NULL),
	fTagColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR)),
	fHover(false),
	fPressed(false),
	fHighlighted(false),
	fPressedTag(-1),
	fData(NULL)
{
	_Init();
	if (archive != NULL) {
		const char* text = NULL;
		if (archive->FindString("r5:title", &text) == B_OK)
			fTitle = text;
		if (archive->FindString("r5:summary", &text) == B_OK)
			fSummary = text;
		BMessage icon;
		if (archive->FindMessage("r5:icon", &icon) == B_OK)
			fIcon = dynamic_cast<BBitmap*>(BBitmap::Instantiate(&icon));
		BMessage invoke;
		if (archive->FindMessage("r5:invoke", &invoke) == B_OK)
			SetMessage(new BMessage(invoke));
		BMessage tag;
		if (archive->FindMessage("r5:tag", &tag) == B_OK)
			fTagMessage = new BMessage(tag);
		rgb_color color;
		if (archive->FindColor("r5:tagcolor", &color) == B_OK)
			fTagColor = color;
		const char* label = NULL;
		for (int32 i = 0; archive->FindString("r5:tags", i, &label) == B_OK; i++)
			fTags.AddItem(new BString(label));
	}
}


BActionCard::~BActionCard()
{
	RemoveAllTags();
	delete fIcon;
	delete fTagMessage;
	delete fData;
}


BArchivable*
BActionCard::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kActionClassName))
		return NULL;
	return new BActionCard(archive);
}


status_t
BActionCard::Archive(BMessage* archive, bool deep) const
{
	status_t status = BCard::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kActionClassName) != B_OK)
		status = archive->AddString("class", kActionClassName);
	if (status == B_OK)
		status = archive->AddString("r5:title", fTitle);
	if (status == B_OK)
		status = archive->AddString("r5:summary", fSummary);
	if (status == B_OK)
		status = archive->AddColor("r5:tagcolor", fTagColor);
	if (status == B_OK && Message() != NULL)
		status = archive->AddMessage("r5:invoke", Message());
	if (status == B_OK && fTagMessage != NULL)
		status = archive->AddMessage("r5:tag", fTagMessage);
	for (int32 i = 0; status == B_OK && i < fTags.CountItems(); i++) {
		BString* label = (BString*)fTags.ItemAt(i);
		status = archive->AddString("r5:tags", label->String());
	}
	if (status == B_OK && fIcon != NULL) {
		BMessage icon;
		if (fIcon->Archive(&icon) == B_OK)
			status = archive->AddMessage("r5:icon", &icon);
	}
	return status;
}


void
BActionCard::Draw(BRect updateRect)
{
	(void)updateRect;
	_Layout(Bounds().Width());
	uint32 flags = 0;
	if (fHover || fHighlighted)
		flags |= BControlLook::B_HOVER;
	if (fPressed)
		flags |= BControlLook::B_ACTIVATED;
	if (IsFocus())
		flags |= BControlLook::B_FOCUSED;
	rgb_color base = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	if ((flags & BControlLook::B_ACTIVATED) != 0)
		base = mix_color(base, ui_color(B_CONTROL_HIGHLIGHT_COLOR), 31);
	else if ((flags & BControlLook::B_HOVER) != 0)
		base = mix_color(base, ui_color(B_CONTROL_HIGHLIGHT_COLOR), 13);
	SetLowColor(base);
	float radius = be_plain_font->Size() * 0.25f;
	SetHighColor(base);
	FillRoundRect(Bounds(), radius, radius);
	rgb_color border = tint_color(panel, B_DARKEN_2_TINT);
	if ((flags & BControlLook::B_HOVER) != 0)
		border = mix_color(border, ui_color(B_CONTROL_HIGHLIGHT_COLOR), 64);
	if ((flags & BControlLook::B_FOCUSED) != 0)
		border = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	SetHighColor(border);
	StrokeRoundRect(Bounds(), radius, radius);

	float margin = be_control_look->DefaultItemSpacing();
	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fIcon, BPoint(margin, margin));
		SetDrawingMode(B_OP_COPY);
	}
	BFont titleFont(be_bold_font);
	SetFont(&titleFont);
	font_height titleHeight;
	titleFont.GetHeight(&titleHeight);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	float textLeft = margin + be_control_look->ComposeIconSize(40).width
		+ margin;
	float y = margin + titleHeight.ascent;
	DrawString(fTitle.String(), BPoint(textLeft, y));

	BFont body(be_plain_font);
	SetFont(&body);
	font_height bodyHeight;
	body.GetHeight(&bodyHeight);
	SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 178));
	y = margin + titleHeight.ascent + titleHeight.descent + bodyHeight.ascent
		+ be_plain_font->Size() * 0.2f;
	if (fLineOne.Length() > 0)
		DrawString(fLineOne.String(), BPoint(textLeft, y));
	if (fLineTwo.Length() > 0) {
		y += bodyHeight.ascent + bodyHeight.descent + bodyHeight.leading;
		DrawString(fLineTwo.String(), BPoint(textLeft, y));
	}

	BFont tagFont(be_plain_font);
	tagFont.SetSize(be_plain_font->Size() * 0.85f);
	tagFont.SetFace(B_BOLD_FACE);
	SetFont(&tagFont);
	for (int32 i = 0; i < fTagFrames.CountItems(); i++) {
		BRect* frame = (BRect*)fTagFrames.ItemAt(i);
		BString* label = (BString*)fTags.ItemAt(i);
		SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			fTagColor, 30));
		FillRoundRect(*frame, frame->Height() / 2, frame->Height() / 2);
		SetHighColor(tint_color(fTagColor, B_DARKEN_3_TINT));
		font_height tagHeight;
		tagFont.GetHeight(&tagHeight);
		DrawString(label->String(), BPoint(frame->left + margin * 0.6f,
			frame->top + tagHeight.ascent + (frame->Height()
				- (tagHeight.ascent + tagHeight.descent)) / 2));
	}
	SetFont(be_plain_font);
}


void
BActionCard::MouseDown(BPoint where)
{
	if (!Bounds().Contains(where))
		return;
	MakeFocus(true);
	fPressed = true;
	fPressedTag = _TagAt(where);
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	Invalidate();
}


void
BActionCard::MouseUp(BPoint where)
{
	bool pressed = fPressed;
	int32 tag = fPressedTag;
	fPressed = false;
	fPressedTag = -1;
	Invalidate();
	if (!pressed || !Bounds().Contains(where))
		return;
	int32 at = _TagAt(where);
	if (tag >= 0 && at == tag && fTagMessage != NULL && Window() != NULL) {
		BMessage message(*fTagMessage);
		message.AddInt32("be:tag", tag);
		BString* label = (BString*)fTags.ItemAt(tag);
		if (label != NULL)
			message.AddString("be:label", label->String());
		BMessenger target = Messenger();
		if (!target.IsValid())
			target = BMessenger(Window());
		target.SendMessage(&message);
		return;
	}
	if (at < 0)
		Invoke();
}


void
BActionCard::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	(void)dragMessage;
	bool inside = transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW
		|| Bounds().Contains(where);
	if (fHover != inside) {
		fHover = inside;
		Invalidate();
	}
}


void
BActionCard::KeyDown(const char* bytes, int32 numBytes)
{
	if (bytes != NULL && (bytes[0] == B_ENTER || bytes[0] == B_SPACE)) {
		Invoke();
		return;
	}
	BView::KeyDown(bytes, numBytes);
}


void
BActionCard::MakeFocus(bool focus)
{
	BCard::MakeFocus(focus);
	Invalidate();
}


void
BActionCard::AttachedToWindow()
{
	BCard::AttachedToWindow();
	if (!Messenger().IsValid())
		SetTarget(Window());
}


void
BActionCard::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED || message->what == B_FONTS_UPDATED)
		Invalidate();
	BCard::MessageReceived(message);
}


void
BActionCard::GetPreferredSize(float* width, float* height)
{
	float wide = _Width();
	if (wide < 1)
		wide = 280;
	_Layout(wide);
	if (width != NULL)
		*width = wide;
	if (height != NULL) {
		float margin = be_control_look->DefaultItemSpacing();
		font_height titleHeight;
		be_bold_font->GetHeight(&titleHeight);
		font_height bodyHeight;
		be_plain_font->GetHeight(&bodyHeight);
		float text = titleHeight.ascent + titleHeight.descent
			+ be_plain_font->Size() * 0.2f;
		if (fLineOne.Length() > 0)
			text += bodyHeight.ascent + bodyHeight.descent;
		if (fLineTwo.Length() > 0)
			text += bodyHeight.ascent + bodyHeight.descent + bodyHeight.leading;
		float bottom = 0;
		if (fTagFrames.CountItems() > 0) {
			BRect* last = (BRect*)fTagFrames.ItemAt(fTagFrames.CountItems() - 1);
			bottom = last->bottom + margin;
		} else
			bottom = margin + text + margin;
		float icon = be_control_look->ComposeIconSize(40).height + margin * 2;
		*height = bottom > icon ? bottom : icon;
	}
}


BSize
BActionCard::MinSize()
{
	float width = 280;
	float height = 0;
	float saved = Bounds().Width();
	(void)saved;
	GetPreferredSize(&width, &height);
	_Layout(280);
	GetPreferredSize(NULL, &height);
	return BSize(280, height);
}


void
BActionCard::SetIcon(const BBitmap* icon)
{
	delete fIcon;
	fIcon = _Copy(icon);
	Invalidate();
}


void
BActionCard::SetTitle(const char* title)
{
	fTitle = title;
	Invalidate();
}


void
BActionCard::SetSummary(const char* summary)
{
	fSummary = summary;
	Invalidate();
}


int32
BActionCard::AddTag(const char* label)
{
	fTags.AddItem(new BString(label));
	Invalidate();
	return fTags.CountItems() - 1;
}


void
BActionCard::RemoveAllTags()
{
	for (int32 i = 0; i < fTags.CountItems(); i++)
		delete (BString*)fTags.ItemAt(i);
	fTags.MakeEmpty();
	for (int32 i = 0; i < fTagFrames.CountItems(); i++)
		delete (BRect*)fTagFrames.ItemAt(i);
	fTagFrames.MakeEmpty();
	Invalidate();
}


int32
BActionCard::CountTags() const
{
	return fTags.CountItems();
}


void
BActionCard::SetTagColor(rgb_color tint)
{
	fTagColor = tint;
	Invalidate();
}


void
BActionCard::SetTagMessage(BMessage* message)
{
	delete fTagMessage;
	fTagMessage = message;
}


void
BActionCard::SetHighlighted(bool on)
{
	fHighlighted = on;
	Invalidate();
}


void
BActionCard::_Init()
{
	SetLayout(NULL);
	SetFlags(Flags() | B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE);
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void
BActionCard::_Layout(float width)
{
	for (int32 i = 0; i < fTagFrames.CountItems(); i++)
		delete (BRect*)fTagFrames.ItemAt(i);
	fTagFrames.MakeEmpty();
	float margin = be_control_look->DefaultItemSpacing();
	float textLeft = margin + be_control_look->ComposeIconSize(40).width
		+ margin;
	float room = width - textLeft - margin;
	if (room < margin)
		room = margin;
	BFont body(be_plain_font);
	wrap_two(body, fSummary.String(), room, fLineOne, fLineTwo);
	BFont titleFont(be_bold_font);
	font_height titleHeight;
	titleFont.GetHeight(&titleHeight);
	font_height bodyHeight;
	body.GetHeight(&bodyHeight);
	float y = margin + titleHeight.ascent + titleHeight.descent
		+ be_plain_font->Size() * 0.2f;
	if (fLineOne.Length() > 0)
		y += bodyHeight.ascent + bodyHeight.descent;
	if (fLineTwo.Length() > 0)
		y += bodyHeight.ascent + bodyHeight.descent + bodyHeight.leading;
	y += margin * 0.5f;
	BFont tagFont(be_plain_font);
	tagFont.SetSize(be_plain_font->Size() * 0.85f);
	font_height tagHeight;
	tagFont.GetHeight(&tagHeight);
	float tagH = tagHeight.ascent + tagHeight.descent + margin * 0.4f;
	float x = textLeft;
	for (int32 i = 0; i < fTags.CountItems(); i++) {
		BString* label = (BString*)fTags.ItemAt(i);
		float tagW = tagFont.StringWidth(label->String()) + margin * 1.2f;
		if (x > textLeft && x + tagW > width - margin) {
			x = textLeft;
			y += tagH + margin * 0.3f;
		}
		fTagFrames.AddItem(new BRect(x, y, x + tagW, y + tagH));
		x += tagW + margin * 0.4f;
	}
}


int32
BActionCard::_TagAt(BPoint where) const
{
	for (int32 i = 0; i < fTagFrames.CountItems(); i++) {
		BRect* frame = (BRect*)fTagFrames.ItemAt(i);
		if (frame->Contains(where))
			return i;
	}
	return -1;
}


BBitmap*
BActionCard::_Copy(const BBitmap* icon) const
{
	if (icon == NULL)
		return NULL;
	BBitmap* copy = new(std::nothrow) BBitmap(icon->Bounds(),
		B_BITMAP_ACCEPTS_VIEWS, icon->ColorSpace());
	if (copy == NULL || !copy->IsValid()) {
		delete copy;
		return NULL;
	}
	memcpy(copy->Bits(), icon->Bits(), icon->BitsLength());
	return copy;
}


float
BActionCard::_Width() const
{
	return Bounds().Width();
}


void
BActionCard::_ReservedActionCard1()
{
}


void
BActionCard::_ReservedActionCard2()
{
}


void
BActionCard::_ReservedActionCard3()
{
}


void
BActionCard::_ReservedActionCard4()
{
}


}	// namespace BAppearanceKit
