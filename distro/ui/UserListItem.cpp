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


#include "UserListItem.h"

#include <Bitmap.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <String.h>
#include <View.h>

#include <new>


namespace BAppearanceKit {


static float
unit(const BFont* font)
{
	float size = font != NULL ? font->Size() : be_plain_font->Size();
	return size / 13.0f;
}


struct BUserListItem::Data {
	BString		login;
	BString		displayName;
	BString		role;
	BBitmap*	avatar;

	Data()
		:
		avatar(NULL)
	{
	}

	~Data()
	{
		delete avatar;
	}
};


static const char* kUserListItemClassName = "BAppearanceKit::BUserListItem";


BUserListItem::BUserListItem(const char* login, const char* displayName,
	const char* role, const BBitmap* avatar)
	:
	BListItem(),
	fData(NULL)
{
	_Init();
	if (fData == NULL)
		return;

	fData->login = login != NULL ? login : "";
	fData->displayName = displayName != NULL
		? displayName : fData->login.String();
	fData->role = role != NULL ? role : "";

	if (avatar != NULL)
		fData->avatar = new(std::nothrow) BBitmap(avatar);
}


BUserListItem::BUserListItem(BMessage* archive)
	:
	BListItem(archive),
	fData(NULL)
{
	_Init();
	if (fData == NULL || archive == NULL)
		return;

	const char* text = NULL;
	if (archive->FindString("r5:login", &text) == B_OK)
		fData->login = text;
	if (archive->FindString("r5:displayName", &text) == B_OK)
		fData->displayName = text;
	if (archive->FindString("r5:role", &text) == B_OK)
		fData->role = text;

	BMessage avatarMessage;
	if (archive->FindMessage("r5:avatar", &avatarMessage) == B_OK) {
		fData->avatar = dynamic_cast<BBitmap*>(
			BBitmap::Instantiate(&avatarMessage));
	}
}


BUserListItem::~BUserListItem()
{
	delete fData;
}


BArchivable*
BUserListItem::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kUserListItemClassName))
		return NULL;
	return new(std::nothrow) BUserListItem(archive);
}


status_t
BUserListItem::Archive(BMessage* archive, bool deep) const
{
	status_t status = BListItem::Archive(archive, deep);
	if (status != B_OK || fData == NULL)
		return status;

	if (archive->ReplaceString("class", kUserListItemClassName) != B_OK)
		status = archive->AddString("class", kUserListItemClassName);
	if (status == B_OK)
		status = archive->AddString("r5:login", fData->login);
	if (status == B_OK)
		status = archive->AddString("r5:displayName", fData->displayName);
	if (status == B_OK)
		status = archive->AddString("r5:role", fData->role);
	if (status == B_OK && fData->avatar != NULL) {
		BMessage avatarMessage;
		if (fData->avatar->Archive(&avatarMessage) == B_OK)
			status = archive->AddMessage("r5:avatar", &avatarMessage);
	}

	return status;
}


const char*
BUserListItem::Login() const
{
	return fData != NULL ? fData->login.String() : "";
}


const char*
BUserListItem::DisplayName() const
{
	return fData != NULL ? fData->displayName.String() : "";
}


const char*
BUserListItem::Role() const
{
	return fData != NULL ? fData->role.String() : "";
}


const BBitmap*
BUserListItem::Avatar() const
{
	return fData != NULL ? fData->avatar : NULL;
}


void
BUserListItem::SetDisplayName(const char* displayName)
{
	if (fData == NULL)
		return;
	fData->displayName = displayName != NULL ? displayName : "";
}


void
BUserListItem::SetRole(const char* role)
{
	if (fData == NULL)
		return;
	fData->role = role != NULL ? role : "";
}


void
BUserListItem::SetAvatar(const BBitmap* avatar)
{
	if (fData == NULL)
		return;
	delete fData->avatar;
	fData->avatar = avatar != NULL
		? new(std::nothrow) BBitmap(avatar) : NULL;
}


void
BUserListItem::DrawItem(BView* owner, BRect itemRect, bool complete)
{
	(void)complete;
	if (owner == NULL || fData == NULL)
		return;

	BFont savedFont;
	owner->GetFont(&savedFont);
	float u = unit(&savedFont);

	rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color textColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color background = owner->LowColor();

	if (IsSelected()) {
		background = mix_color(accent, document, 30);
		owner->SetHighColor(background);
		owner->FillRect(itemRect);

		BRect bar = itemRect;
		bar.right = bar.left + 3.0f * u;
		owner->SetHighColor(accent);
		owner->FillRect(bar);
	} else {
		owner->SetHighColor(background);
		owner->FillRect(itemRect);
	}

	owner->SetHighColor(mix_color(background, panel, 80));
	owner->StrokeLine(BPoint(itemRect.left, itemRect.bottom),
		BPoint(itemRect.right, itemRect.bottom));

	float avatarSize = 28.0f * u;
	float padding = 8.0f * u;
	float avatarTop = itemRect.top + (itemRect.Height() - avatarSize) / 2.0f;
	BRect avatarRect(itemRect.left + padding, avatarTop,
		itemRect.left + padding + avatarSize, avatarTop + avatarSize);

	rgb_color avatarBackground = mix_color(accent, document, 35);
	owner->SetHighColor(avatarBackground);
	owner->FillEllipse(avatarRect);

	if (fData->avatar != NULL && fData->avatar->IsValid()) {
		BRect iconRect = avatarRect.InsetByCopy(2.5f * u, 2.5f * u);
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		owner->DrawBitmap(fData->avatar, fData->avatar->Bounds(), iconRect);
		owner->SetDrawingMode(B_OP_COPY);
	} else {
		owner->SetHighColor(tint_color(avatarBackground, B_DARKEN_2_TINT));
		BRect head(avatarRect.left + avatarSize * 0.32f,
			avatarRect.top + avatarSize * 0.18f,
			avatarRect.right - avatarSize * 0.32f,
			avatarRect.top + avatarSize * 0.52f);
		owner->FillEllipse(head);

		BRect body(avatarRect.left + avatarSize * 0.2f,
			avatarRect.top + avatarSize * 0.58f,
			avatarRect.right - avatarSize * 0.2f, avatarRect.bottom);
		owner->FillRoundRect(body, 3.0f * u, 3.0f * u);
	}

	float textLeft = avatarRect.right + 10.0f * u;
	float maxWidth = itemRect.right - padding - textLeft;
	if (maxWidth > 4.0f) {
		BFont boldFont(be_bold_font);
		boldFont.SetSize(savedFont.Size());
		owner->SetFont(&boldFont);
		owner->SetHighColor(textColor);

		BString name(fData->displayName);
		boldFont.TruncateString(&name, B_TRUNCATE_END, maxWidth);

		font_height nameHeight;
		boldFont.GetHeight(&nameHeight);

		BFont plainFont(be_plain_font);
		plainFont.SetSize(savedFont.Size() * 0.88f);
		font_height subHeight;
		plainFont.GetHeight(&subHeight);

		float gap = 2.0f * u;
		float blockHeight = nameHeight.ascent + nameHeight.descent
			+ gap + subHeight.ascent + subHeight.descent;
		float nameY = itemRect.top + (itemRect.Height() - blockHeight) / 2.0f
			+ nameHeight.ascent;
		owner->DrawString(name.String(), BPoint(textLeft, nameY));

		owner->SetFont(&plainFont);
		if (fData->role.ICompare("Administrator") == 0)
			owner->SetHighColor(accent);
		else
			owner->SetHighColor(mix_color(textColor, document, 140));

		BString subtitle;
		subtitle.SetToFormat("@%s \xc2\xb7 %s", fData->login.String(),
			fData->role.String());
		plainFont.TruncateString(&subtitle, B_TRUNCATE_END, maxWidth);

		float roleY = nameY + nameHeight.descent + gap + subHeight.ascent;
		owner->DrawString(subtitle.String(), BPoint(textLeft, roleY));
	}

	owner->SetFont(&savedFont);
}


void
BUserListItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	SetHeight(44.0f * unit(font));
}


void
BUserListItem::_Init()
{
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


void
BUserListItem::_ReservedUserListItem1()
{
}


void
BUserListItem::_ReservedUserListItem2()
{
}


void
BUserListItem::_ReservedUserListItem3()
{
}


void
BUserListItem::_ReservedUserListItem4()
{
}


}	// namespace BAppearanceKit
