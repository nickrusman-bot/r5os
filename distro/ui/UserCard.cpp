/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "UserCard.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Window.h>

#include <new>


namespace BAppearanceKit {


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


struct BUserCard::Data {
	BString		login;
	BString		displayName;
	BString		role;
	BBitmap*	avatar;
	bool		selected;
	bool		hover;

	Data()
		:
		avatar(NULL),
		selected(false),
		hover(false)
	{
	}

	~Data()
	{
		delete avatar;
	}
};


static const char* kUserCardClassName = "BAppearanceKit::BUserCard";


BUserCard::BUserCard(const char* name, const char* login,
	const char* displayName, const char* role, const BBitmap* avatar,
	BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_NAVIGABLE),
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


BUserCard::BUserCard(BMessage* archive)
	:
	BControl(archive),
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
	archive->FindBool("r5:selected", &fData->selected);
}


BUserCard::~BUserCard()
{
	delete fData;
}


BArchivable*
BUserCard::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, kUserCardClassName))
		return new(std::nothrow) BUserCard(archive);
	return NULL;
}


status_t
BUserCard::Archive(BMessage* archive, bool deep) const
{
	status_t status = BControl::Archive(archive, deep);
	if (status != B_OK || fData == NULL)
		return status;

	status = archive->AddString("class", kUserCardClassName);
	if (status == B_OK)
		status = archive->AddString("r5:login", fData->login);
	if (status == B_OK)
		status = archive->AddString("r5:displayName", fData->displayName);
	if (status == B_OK)
		status = archive->AddString("r5:role", fData->role);
	if (status == B_OK)
		status = archive->AddBool("r5:selected", fData->selected);

	return status;
}


const char*
BUserCard::Login() const
{
	return fData != NULL ? fData->login.String() : "";
}


const char*
BUserCard::DisplayName() const
{
	return fData != NULL ? fData->displayName.String() : "";
}


const char*
BUserCard::Role() const
{
	return fData != NULL ? fData->role.String() : "";
}


const BBitmap*
BUserCard::Avatar() const
{
	return fData != NULL ? fData->avatar : NULL;
}


void
BUserCard::SetDisplayName(const char* displayName)
{
	if (fData == NULL)
		return;
	fData->displayName = displayName != NULL ? displayName : "";
	Invalidate();
}


void
BUserCard::SetRole(const char* role)
{
	if (fData == NULL)
		return;
	fData->role = role != NULL ? role : "";
	Invalidate();
}


void
BUserCard::SetAvatar(const BBitmap* avatar)
{
	if (fData == NULL)
		return;
	delete fData->avatar;
	fData->avatar = avatar != NULL ? new(std::nothrow) BBitmap(avatar) : NULL;
	Invalidate();
}


bool
BUserCard::IsSelected() const
{
	return fData != NULL && fData->selected;
}


void
BUserCard::SetSelected(bool selected)
{
	if (fData == NULL || fData->selected == selected)
		return;
	fData->selected = selected;
	SetValue(selected ? B_CONTROL_ON : B_CONTROL_OFF);
	Invalidate();
}


void
BUserCard::Draw(BRect updateRect)
{
	(void)updateRect;
	if (fData == NULL)
		return;

	BRect bounds = Bounds();
	float u = unit();
	float radius = 5.0f * u;

	rgb_color bg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);

	if (fData->selected) {
		bg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		border = accent;
	} else if (fData->hover) {
		bg = mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			ui_color(B_PANEL_BACKGROUND_COLOR), 40);
	} else {
		bg = mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			ui_color(B_PANEL_BACKGROUND_COLOR), 20);
	}

	SetHighColor(bg);
	FillRoundRect(bounds, radius, radius);

	// Border
	SetHighColor(border);
	StrokeRoundRect(bounds, radius, radius);

	if (fData->selected) {
		BRect inner = bounds;
		inner.InsetBy(1.0f, 1.0f);
		rgb_color glow = accent;
		glow.alpha = 60;
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(glow);
		StrokeRoundRect(inner, radius - 1.0f, radius - 1.0f);
		SetDrawingMode(B_OP_COPY);
	}

	// Avatar circle
	float avSize = 34.0f * u;
	float pad = 8.0f * u;
	BRect avRect(bounds.left + pad, bounds.top + (bounds.Height() - avSize) / 2.0f,
		bounds.left + pad + avSize,
		bounds.top + (bounds.Height() - avSize) / 2.0f + avSize);

	rgb_color avBg = mix_color(accent, ui_color(B_DOCUMENT_BACKGROUND_COLOR), 40);
	SetHighColor(avBg);
	FillEllipse(avRect);

	if (fData->avatar != NULL && fData->avatar->IsValid()) {
		SetDrawingMode(B_OP_ALPHA);
		DrawBitmap(fData->avatar, fData->avatar->Bounds(), avRect);
		SetDrawingMode(B_OP_COPY);
	} else {
		// Simple silhouette fallback
		SetHighColor(tint_color(avBg, B_DARKEN_2_TINT));
		BRect head(avRect.left + avSize * 0.32f, avRect.top + avSize * 0.18f,
			avRect.right - avSize * 0.32f, avRect.top + avSize * 0.52f);
		FillEllipse(head);
		BRect body(avRect.left + avSize * 0.2f, avRect.top + avSize * 0.58f,
			avRect.right - avSize * 0.2f, avRect.bottom);
		FillRoundRect(body, 4.0f * u, 4.0f * u);
	}

	// Text area
	float textLeft = avRect.right + 10.0f * u;
	float textRight = bounds.right - pad;
	if (textLeft < textRight) {
		BFont boldFont(be_bold_font);
		boldFont.SetSize(be_plain_font->Size() * 1.05f);
		SetFont(&boldFont);
		SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));

		font_height fh;
		boldFont.GetHeight(&fh);
		float nameY = bounds.top + bounds.Height() * 0.45f;
		DrawString(fData->displayName.String(), BPoint(textLeft, nameY));

		// Role text
		BFont plainFont(be_plain_font);
		plainFont.SetSize(be_plain_font->Size() * 0.88f);
		SetFont(&plainFont);

		if (fData->role.ICompare("Administrator") == 0)
			SetHighColor(accent);
		else {
			SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
				ui_color(B_DOCUMENT_BACKGROUND_COLOR), 140));
		}

		plainFont.GetHeight(&fh);
		float roleY = nameY + fh.ascent + 4.0f * u;
		DrawString(fData->role.String(), BPoint(textLeft, roleY));
	}
}


void
BUserCard::MouseDown(BPoint where)
{
	(void)where;
	if (!IsEnabled())
		return;

	SetSelected(true);
	Invoke();
}


void
BUserCard::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && (bytes[0] == B_SPACE || bytes[0] == B_ENTER)) {
		SetSelected(true);
		Invoke();
		return;
	}
	BControl::KeyDown(bytes, numBytes);
}


void
BUserCard::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	(void)where;
	(void)dragMessage;
	if (fData == NULL)
		return;

	bool hover = (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW);
	if (fData->hover != hover) {
		fData->hover = hover;
		Invalidate();
	}
}


void
BUserCard::GetPreferredSize(float* width, float* height)
{
	float u = unit();
	if (width != NULL)
		*width = 170.0f * u;
	if (height != NULL)
		*height = 54.0f * u;
}


BSize
BUserCard::MinSize()
{
	float u = unit();
	return BSize(130.0f * u, 50.0f * u);
}


BSize
BUserCard::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height + 8.0f * unit());
}


BSize
BUserCard::PreferredSize()
{
	float w, h;
	GetPreferredSize(&w, &h);
	return BSize(w, h);
}


} // namespace BAppearanceKit
