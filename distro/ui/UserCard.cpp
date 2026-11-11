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


#include "UserCard.h"

#include <Bitmap.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <String.h>

#include <math.h>
#include <new>

#include "R5UI.h"


namespace BAppearanceKit {


static float
unit()
{
	// One mockup pixel. The mockup is drawn for a 12 point system font.
	return be_plain_font->Size() / 12.0f;
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
	BControl(name, NULL, message,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_NAVIGABLE),
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


BUserCard::BUserCard(BMessage* archive)
	:
	BControl(archive),
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
	archive->FindBool("r5:selected", &fData->selected);

	BMessage avatarMessage;
	if (archive->FindMessage("r5:avatar", &avatarMessage) == B_OK) {
		fData->avatar = dynamic_cast<BBitmap*>(
			BBitmap::Instantiate(&avatarMessage));
	}
}


BUserCard::~BUserCard()
{
	delete fData;
}


BArchivable*
BUserCard::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kUserCardClassName))
		return NULL;
	return new(std::nothrow) BUserCard(archive);
}


status_t
BUserCard::Archive(BMessage* archive, bool deep) const
{
	status_t status = BControl::Archive(archive, deep);
	if (status != B_OK || fData == NULL)
		return status;

	if (archive->ReplaceString("class", kUserCardClassName) != B_OK)
		status = archive->AddString("class", kUserCardClassName);
	if (status == B_OK)
		status = archive->AddString("r5:login", fData->login);
	if (status == B_OK)
		status = archive->AddString("r5:displayName", fData->displayName);
	if (status == B_OK)
		status = archive->AddString("r5:role", fData->role);
	if (status == B_OK)
		status = archive->AddBool("r5:selected", fData->selected);
	if (status == B_OK && fData->avatar != NULL) {
		BMessage avatarMessage;
		if (fData->avatar->Archive(&avatarMessage) == B_OK)
			status = archive->AddMessage("r5:avatar", &avatarMessage);
	}

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
	fData->avatar = avatar != NULL
		? new(std::nothrow) BBitmap(avatar) : NULL;
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

	rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color border = tint_color(panel, B_DARKEN_2_TINT);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color background = document;

	if (fData->selected) {
		border = accent;
	} else if (fData->hover) {
		background = mix_color(document, panel, 40);
	} else {
		background = mix_color(document, panel, 20);
	}

	if (IsFocus() && !fData->selected)
		border = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	SetHighColor(background);
	FillRoundRect(bounds, radius, radius);

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

	float avatarSize = floorf(38.0f * u);
	float padding = 10.0f * u;
	float avatarTop = bounds.top + (bounds.Height() - avatarSize) / 2.0f;
	BRect avatarRect(bounds.left + padding, avatarTop,
		bounds.left + padding + avatarSize, avatarTop + avatarSize);

	r5_avatar_draw(this, avatarRect, fData->avatar,
		fData->role.ICompare("Administrator") == 0);

	float textLeft = avatarRect.right + 10.0f * u;
	float maxWidth = bounds.right - padding - textLeft;
	if (maxWidth > 4.0f) {
		BFont boldFont(be_bold_font);
		boldFont.SetSize(13.0f * u);
		SetFont(&boldFont);
		SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));

		BString name(fData->displayName);
		boldFont.TruncateString(&name, B_TRUNCATE_END, maxWidth);

		font_height nameHeight;
		boldFont.GetHeight(&nameHeight);

		BFont plainFont(be_plain_font);
		plainFont.SetSize(11.0f * u);
		font_height roleHeight;
		plainFont.GetHeight(&roleHeight);

		float gap = 2.0f * u;
		float blockHeight = nameHeight.ascent + nameHeight.descent
			+ gap + roleHeight.ascent + roleHeight.descent;
		float nameY = bounds.top + (bounds.Height() - blockHeight) / 2.0f
			+ nameHeight.ascent;
		DrawString(name.String(), BPoint(textLeft, nameY));

		bool admin = fData->role.ICompare("Administrator") == 0;
		if (admin) {
			// The administrator role is bold and blue, as in the mockup.
			BFont roleFont(be_bold_font);
			roleFont.SetSize(plainFont.Size());
			plainFont = roleFont;
		}
		SetFont(&plainFont);
		if (admin)
			SetHighColor(accent);
		else {
			SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
				document, 140));
		}

		BString role(fData->role);
		plainFont.TruncateString(&role, B_TRUNCATE_END, maxWidth);
		float roleY = nameY + nameHeight.descent + gap + roleHeight.ascent;
		DrawString(role.String(), BPoint(textLeft, roleY));
	}
}


void
BUserCard::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED || message->what == B_FONTS_UPDATED)
		Invalidate();
	BControl::MessageReceived(message);
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
		if (IsEnabled()) {
			SetSelected(true);
			Invoke();
		}
		return;
	}
	BControl::KeyDown(bytes, numBytes);
}


void
BUserCard::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	(void)where;
	(void)dragMessage;
	if (fData == NULL)
		return;

	bool hover = transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW;
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
		*height = floorf(54.0f * u);
}


BSize
BUserCard::MinSize()
{
	float u = unit();
	return BSize(130.0f * u, floorf(54.0f * u));
}


BSize
BUserCard::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
BUserCard::PreferredSize()
{
	float width;
	float height;
	GetPreferredSize(&width, &height);
	return BSize(width, height);
}


void
BUserCard::_Init()
{
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
BUserCard::_ReservedUserCard1()
{
}


void
BUserCard::_ReservedUserCard2()
{
}


void
BUserCard::_ReservedUserCard3()
{
}


void
BUserCard::_ReservedUserCard4()
{
}


}	// namespace BAppearanceKit
