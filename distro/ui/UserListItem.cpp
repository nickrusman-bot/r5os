/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "UserListItem.h"

#include <Bitmap.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
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
	fData(new(std::nothrow) Data)
{
	if (fData == NULL)
		return;

	fData->login = login != NULL ? login : "";
	fData->displayName = displayName != NULL ? displayName : fData->login.String();
	fData->role = role != NULL ? role : "";

	if (avatar != NULL)
		fData->avatar = new(std::nothrow) BBitmap(avatar);
}


BUserListItem::BUserListItem(BMessage* archive)
	:
	BListItem(archive),
	fData(new(std::nothrow) Data)
{
	if (fData == NULL)
		return;

	const char* str = NULL;
	if (archive->FindString("r5:login", &str) == B_OK)
		fData->login = str;
	if (archive->FindString("r5:displayName", &str) == B_OK)
		fData->displayName = str;
	if (archive->FindString("r5:role", &str) == B_OK)
		fData->role = str;
}


BUserListItem::~BUserListItem()
{
	delete fData;
}


BArchivable*
BUserListItem::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, kUserListItemClassName))
		return new(std::nothrow) BUserListItem(archive);
	return NULL;
}


status_t
BUserListItem::Archive(BMessage* archive, bool deep) const
{
	status_t status = BListItem::Archive(archive, deep);
	if (status != B_OK || fData == NULL)
		return status;

	status = archive->AddString("class", kUserListItemClassName);
	if (status == B_OK)
		status = archive->AddString("r5:login", fData->login);
	if (status == B_OK)
		status = archive->AddString("r5:displayName", fData->displayName);
	if (status == B_OK)
		status = archive->AddString("r5:role", fData->role);

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
	fData->avatar = avatar != NULL ? new(std::nothrow) BBitmap(avatar) : NULL;
}


void
BUserListItem::DrawItem(BView* owner, BRect itemRect, bool complete)
{
	(void)complete;
	if (owner == NULL || fData == NULL)
		return;

	BFont font;
	owner->GetFont(&font);
	float u = unit(&font);

	rgb_color bg = owner->LowColor();
	rgb_color textColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);

	if (IsSelected()) {
		bg = mix_color(accent, ui_color(B_DOCUMENT_BACKGROUND_COLOR), 30);
		owner->SetHighColor(bg);
		owner->FillRect(itemRect);

		// Blue accent bar on the left
		BRect bar = itemRect;
		bar.right = bar.left + 3.0f * u;
		owner->SetHighColor(accent);
		owner->FillRect(bar);
	} else {
		owner->SetHighColor(bg);
		owner->FillRect(itemRect);
	}

	// Bottom divider line
	owner->SetHighColor(mix_color(bg, ui_color(B_PANEL_BACKGROUND_COLOR), 80));
	owner->StrokeLine(BPoint(itemRect.left, itemRect.bottom),
		BPoint(itemRect.right, itemRect.bottom));

	// Avatar
	float avSize = 28.0f * u;
	float pad = 8.0f * u;
	BRect avRect(itemRect.left + pad,
		itemRect.top + (itemRect.Height() - avSize) / 2.0f,
		itemRect.left + pad + avSize,
		itemRect.top + (itemRect.Height() - avSize) / 2.0f + avSize);

	rgb_color avBg = mix_color(accent, ui_color(B_DOCUMENT_BACKGROUND_COLOR), 35);
	owner->SetHighColor(avBg);
	owner->FillEllipse(avRect);

	if (fData->avatar != NULL && fData->avatar->IsValid()) {
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->DrawBitmap(fData->avatar, fData->avatar->Bounds(), avRect);
		owner->SetDrawingMode(B_OP_COPY);
	} else {
		owner->SetHighColor(tint_color(avBg, B_DARKEN_2_TINT));
		BRect head(avRect.left + avSize * 0.32f, avRect.top + avSize * 0.18f,
			avRect.right - avSize * 0.32f, avRect.top + avSize * 0.52f);
		owner->FillEllipse(head);
		BRect body(avRect.left + avSize * 0.2f, avRect.top + avSize * 0.58f,
			avRect.right - avSize * 0.2f, avRect.bottom);
		owner->FillRoundRect(body, 3.0f * u, 3.0f * u);
	}

	// Text
	float textLeft = avRect.right + 10.0f * u;
	BFont boldFont(be_bold_font);
	boldFont.SetSize(font.Size());
	owner->SetFont(&boldFont);
	owner->SetHighColor(textColor);

	font_height fh;
	boldFont.GetHeight(&fh);
	float nameY = itemRect.top + itemRect.Height() * 0.44f;
	owner->DrawString(fData->displayName.String(), BPoint(textLeft, nameY));

	// Subtitle: @login · role
	BFont plainFont(be_plain_font);
	plainFont.SetSize(font.Size() * 0.88f);
	owner->SetFont(&plainFont);

	BString sub;
	sub.SetToFormat("@%s · %s", fData->login.String(), fData->role.String());

	if (fData->role.ICompare("Administrator") == 0)
		owner->SetHighColor(accent);
	else {
		owner->SetHighColor(mix_color(textColor,
			ui_color(B_DOCUMENT_BACKGROUND_COLOR), 140));
	}

	plainFont.GetHeight(&fh);
	float roleY = nameY + fh.ascent + 3.0f * u;
	owner->DrawString(sub.String(), BPoint(textLeft, roleY));

	owner->SetFont(&font);
}


void
BUserListItem::Update(BView* owner, const BFont* font)
{
	(void)owner;
	float u = unit(font);
	SetHeight(44.0f * u);
}


} // namespace BAppearanceKit
