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


#include "PageHeader.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>

#include <new>
#include <string.h>


namespace BAppearanceKit {


struct BPageHeader::Data {
};


static const char* kHeaderClassName = "BAppearanceKit::BPageHeader";


BPageHeader::BPageHeader(const char* name)
	:
	BCard(name, B_CARD_DOCUMENT),
	fIcon(NULL),
	fData(NULL)
{
	_Init();
}


BPageHeader::BPageHeader(BMessage* archive)
	:
	BCard(archive),
	fIcon(NULL),
	fData(NULL)
{
	_Init();
	if (archive != NULL) {
		const char* text = NULL;
		if (archive->FindString("r5:eyebrow", &text) == B_OK)
			fEyebrow = text;
		if (archive->FindString("r5:title", &text) == B_OK)
			fTitle = text;
		if (archive->FindString("r5:subtitle", &text) == B_OK)
			fSubtitle = text;
		BMessage icon;
		if (archive->FindMessage("r5:icon", &icon) == B_OK)
			fIcon = dynamic_cast<BBitmap*>(BBitmap::Instantiate(&icon));
	}
}


BPageHeader::~BPageHeader()
{
	delete fIcon;
	delete fData;
}


BArchivable*
BPageHeader::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kHeaderClassName))
		return NULL;
	return new BPageHeader(archive);
}


status_t
BPageHeader::Archive(BMessage* archive, bool deep) const
{
	status_t status = BCard::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kHeaderClassName) != B_OK)
		status = archive->AddString("class", kHeaderClassName);
	if (status == B_OK)
		status = archive->AddString("r5:eyebrow", fEyebrow);
	if (status == B_OK)
		status = archive->AddString("r5:title", fTitle);
	if (status == B_OK)
		status = archive->AddString("r5:subtitle", fSubtitle);
	if (status == B_OK && fIcon != NULL) {
		BMessage icon;
		if (fIcon->Archive(&icon) == B_OK)
			status = archive->AddMessage("r5:icon", &icon);
	}
	return status;
}


void
BPageHeader::Draw(BRect updateRect)
{
	BCard::Draw(updateRect);
	BFont eyebrow;
	BFont title;
	BFont subtitle;
	_Fonts(eyebrow, title, subtitle);
	float u = be_plain_font->Size() / 13.0f;
	float margin = u * 16.0f;
	float padY = u * 12.0f;
	float iconSide = u * 48.0f;
	float right = Bounds().right - margin;
	if (fIcon != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		BRect dest(right - iconSide,
			(Bounds().Height() - iconSide) / 2, 0, 0);
		dest.right = dest.left + iconSide;
		dest.bottom = dest.top + iconSide;
		DrawBitmap(fIcon, fIcon->Bounds(), dest);
		SetDrawingMode(B_OP_COPY);
		right = dest.left - margin;
	}
	float room = right - margin;
	if (room < margin)
		room = margin;
	font_height eyebrowHeight;
	font_height titleHeight;
	font_height subtitleHeight;
	eyebrow.GetHeight(&eyebrowHeight);
	title.GetHeight(&titleHeight);
	subtitle.GetHeight(&subtitleHeight);
	float block = 0;
	if (fEyebrow.Length() > 0)
		block += eyebrowHeight.ascent + eyebrowHeight.descent;
	block += titleHeight.ascent + titleHeight.descent;
	if (fSubtitle.Length() > 0)
		block += subtitleHeight.ascent + subtitleHeight.descent;
	float y = padY;
	if (fEyebrow.Length() > 0) {
		BString line(fEyebrow);
		eyebrow.TruncateString(&line, B_TRUNCATE_END, room);
		SetFont(&eyebrow);
		SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			ui_color(B_DOCUMENT_TEXT_COLOR), 153));
		y += eyebrowHeight.ascent;
		DrawString(line.String(), BPoint(margin, y));
		y += eyebrowHeight.descent;
	}
	BString titleLine(fTitle);
	title.TruncateString(&titleLine, B_TRUNCATE_END, room);
	SetFont(&title);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	y += titleHeight.ascent;
	DrawString(titleLine.String(), BPoint(margin, y));
	y += titleHeight.descent;
	if (fSubtitle.Length() > 0) {
		BString line(fSubtitle);
		subtitle.TruncateString(&line, B_TRUNCATE_END, room);
		SetFont(&subtitle);
		SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			ui_color(B_DOCUMENT_TEXT_COLOR), 160));
		y += u * 2.0f + subtitleHeight.ascent;
		DrawString(line.String(), BPoint(margin, y));
	}
	SetFont(be_plain_font);
}


void
BPageHeader::GetPreferredSize(float* width, float* height)
{
	if (width != NULL)
		*width = 280;
	if (height != NULL)
		*height = _Height();
}


BSize
BPageHeader::MinSize()
{
	return BSize(0, _Height());
}


BSize
BPageHeader::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, _Height());
}


void
BPageHeader::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED || message->what == B_FONTS_UPDATED)
		Invalidate();
	BCard::MessageReceived(message);
}


void
BPageHeader::SetEyebrow(const char* text)
{
	fEyebrow = text;
	float height = _Height();
	SetExplicitMinSize(BSize(0, height));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
	Invalidate();
}


void
BPageHeader::SetTitle(const char* text)
{
	fTitle = text;
	Invalidate();
}


void
BPageHeader::SetSubtitle(const char* text)
{
	fSubtitle = text;
	float height = _Height();
	SetExplicitMinSize(BSize(0, height));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
	Invalidate();
}


void
BPageHeader::SetIcon(const BBitmap* icon)
{
	delete fIcon;
	fIcon = _Copy(icon);
	Invalidate();
}


void
BPageHeader::_Init()
{
	SetLayout(NULL);
	SetFlags(Flags() | B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	float height = _Height();
	SetExplicitMinSize(BSize(0, height));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
	SetExplicitPreferredSize(BSize(280, height));
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void
BPageHeader::_Fonts(BFont& eyebrow, BFont& title, BFont& subtitle) const
{
	eyebrow = *be_plain_font;
	eyebrow.SetSize(be_plain_font->Size() * 0.9f);
	font_family family;
	font_style style;
	be_plain_font->GetFamilyAndStyle(&family, &style);
	int32 styles = count_font_styles(family);
	for (int32 i = 0; i < styles; i++) {
		font_style candidate;
		if (get_font_style(family, i, &candidate) == B_OK
			&& strcasecmp(candidate, "SemiBold") == 0) {
			eyebrow.SetFamilyAndStyle(family, candidate);
			break;
		}
	}
	title = *be_bold_font;
	title.SetSize(be_plain_font->Size() / 13.0f * 22.0f);
	int32 titleStyles = count_font_styles(family);
	for (int32 i = 0; i < titleStyles; i++) {
		font_style candidate;
		if (get_font_style(family, i, &candidate) == B_OK
			&& (strcasecmp(candidate, "ExtraBold") == 0
				|| strcasecmp(candidate, "Black") == 0)) {
			title.SetFamilyAndStyle(family, candidate);
			title.SetSize(be_plain_font->Size() / 13.0f * 22.0f);
			break;
		}
	}
	subtitle = *be_plain_font;
	subtitle.SetSize(be_plain_font->Size() / 13.0f * 12.5f);
	(void)style;
}


float
BPageHeader::_Height() const
{
	BFont eyebrow;
	BFont title;
	BFont subtitle;
	_Fonts(eyebrow, title, subtitle);
	font_height eyebrowHeight;
	font_height titleHeight;
	font_height subtitleHeight;
	eyebrow.GetHeight(&eyebrowHeight);
	title.GetHeight(&titleHeight);
	subtitle.GetHeight(&subtitleHeight);
	float u = be_plain_font->Size() / 13.0f;
	float text = titleHeight.ascent + titleHeight.descent;
	if (fEyebrow.Length() > 0)
		text += eyebrowHeight.ascent + eyebrowHeight.descent;
	if (fSubtitle.Length() > 0)
		text += u * 2.0f + subtitleHeight.ascent + subtitleHeight.descent;
	float icon = u * 48.0f;
	float body = text > icon ? text : icon;
	return body + u * 12.0f * 2;
}


BBitmap*
BPageHeader::_Copy(const BBitmap* icon) const
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


void
BPageHeader::_ReservedPageHeader1()
{
}


void
BPageHeader::_ReservedPageHeader2()
{
}


void
BPageHeader::_ReservedPageHeader3()
{
}


void
BPageHeader::_ReservedPageHeader4()
{
}


}	// namespace BAppearanceKit
