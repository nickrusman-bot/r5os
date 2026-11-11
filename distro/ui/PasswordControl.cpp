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


#include "PasswordControl.h"

#include <Button.h>
#include <Font.h>
#include <GroupLayout.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <new>


namespace BAppearanceKit {


static const uint32 kMsgToggleReveal = 'prvl';
static const uint32 kMsgPasswordEnter = 'pent';
static const char* kPasswordControlClassName
	= "BAppearanceKit::BPasswordControl";


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


class EyeButton : public BButton {
public:
								EyeButton(BMessage* message);

			void				SetRevealed(bool revealed);
	virtual	void				Draw(BRect updateRect);

private:
			bool				fRevealed;
};


EyeButton::EyeButton(BMessage* message)
	:
	BButton("eye", "", message),
	fRevealed(false)
{
	float side = 28.0f * unit();
	SetExplicitMinSize(BSize(side, side));
	SetExplicitMaxSize(BSize(side, side));
}


void
EyeButton::SetRevealed(bool revealed)
{
	if (fRevealed != revealed) {
		fRevealed = revealed;
		Invalidate();
	}
}


void
EyeButton::Draw(BRect updateRect)
{
	BButton::Draw(updateRect);

	BRect bounds = Bounds();
	float u = unit();

	rgb_color iconColor = ui_color(B_CONTROL_TEXT_COLOR);
	if (!IsEnabled())
		iconColor = tint_color(iconColor, B_LIGHTEN_1_TINT);

	SetHighColor(iconColor);

	float centerX = bounds.left + bounds.Width() / 2.0f;
	float centerY = bounds.top + bounds.Height() / 2.0f;
	float radiusX = 6.5f * u;
	float radiusY = 4.0f * u;

	BRect eyeOval(centerX - radiusX, centerY - radiusY,
		centerX + radiusX, centerY + radiusY);
	StrokeEllipse(eyeOval);

	BRect pupil(centerX - 2.2f * u, centerY - 2.2f * u,
		centerX + 2.2f * u, centerY + 2.2f * u);
	FillEllipse(pupil);

	if (!fRevealed) {
		SetPenSize(1.5f * u);
		StrokeLine(BPoint(centerX - radiusX, centerY + radiusY + 1.5f * u),
			BPoint(centerX + radiusX, centerY - radiusY - 1.5f * u));
		SetPenSize(1.0f);
	}
}


struct BPasswordControl::Data {
	BTextControl*	textControl;
	EyeButton*		eyeButton;
	bool			revealed;

	Data()
		:
		textControl(NULL),
		eyeButton(NULL),
		revealed(false)
	{
	}
};


BPasswordControl::BPasswordControl(const char* name, const char* label,
	const char* text, BMessage* message)
	:
	BControl(name, label, message, B_WILL_DRAW | B_SUPPORTS_LAYOUT),
	fData(new(std::nothrow) Data)
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	_Init(text);
}


BPasswordControl::BPasswordControl(BMessage* archive)
	:
	BControl(archive),
	fData(new(std::nothrow) Data)
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;

	bool revealed = false;
	if (archive != NULL)
		archive->FindBool("r5:revealed", &revealed);
	// The password is never taken from an archive.
	_Init(NULL);
	if (revealed)
		SetPasswordRevealed(true);
}


BPasswordControl::~BPasswordControl()
{
	delete fData;
}


BArchivable*
BPasswordControl::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kPasswordControlClassName))
		return NULL;
	return new(std::nothrow) BPasswordControl(archive);
}


status_t
BPasswordControl::Archive(BMessage* archive, bool deep) const
{
	// deep would archive the child text control, and that control stores
	// its text. The password stays out of every archive.
	(void)deep;
	status_t status = BControl::Archive(archive, false);
	if (status != B_OK || fData == NULL)
		return status;

	if (archive->ReplaceString("class", kPasswordControlClassName) != B_OK)
		status = archive->AddString("class", kPasswordControlClassName);
	if (status == B_OK)
		status = archive->AddBool("r5:revealed", fData->revealed);

	return status;
}


const char*
BPasswordControl::Text() const
{
	return (fData != NULL && fData->textControl != NULL)
		? fData->textControl->Text() : "";
}


void
BPasswordControl::SetText(const char* text)
{
	if (fData != NULL && fData->textControl != NULL)
		fData->textControl->SetText(text);
}


BTextView*
BPasswordControl::TextView() const
{
	return (fData != NULL && fData->textControl != NULL)
		? fData->textControl->TextView() : NULL;
}


bool
BPasswordControl::IsPasswordRevealed() const
{
	return fData != NULL && fData->revealed;
}


void
BPasswordControl::SetPasswordRevealed(bool revealed)
{
	if (fData == NULL || fData->revealed == revealed)
		return;

	fData->revealed = revealed;
	if (fData->textControl != NULL && fData->textControl->TextView() != NULL) {
		fData->textControl->TextView()->HideTyping(!revealed);
		fData->textControl->TextView()->Invalidate();
	}
	if (fData->eyeButton != NULL)
		fData->eyeButton->SetRevealed(revealed);
}


void
BPasswordControl::AttachedToWindow()
{
	BControl::AttachedToWindow();

	if (Parent() != NULL && Parent()->ViewColor() != B_TRANSPARENT_COLOR)
		SetViewColor(Parent()->ViewColor());

	if (fData != NULL) {
		if (fData->eyeButton != NULL)
			fData->eyeButton->SetTarget(this);
		if (fData->textControl != NULL)
			fData->textControl->SetTarget(this);
	}
}


void
BPasswordControl::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleReveal:
			SetPasswordRevealed(!IsPasswordRevealed());
			if (fData != NULL && fData->textControl != NULL)
				fData->textControl->MakeFocus(true);
			break;
		case kMsgPasswordEnter:
			if (Message() != NULL)
				Invoke();
			else if (Window() != NULL && Window()->DefaultButton() != NULL)
				Window()->DefaultButton()->Invoke();
			break;
		default:
			BControl::MessageReceived(message);
			break;
	}
}


void
BPasswordControl::MakeFocus(bool focus)
{
	if (fData != NULL && fData->textControl != NULL)
		fData->textControl->MakeFocus(focus);
	else
		BControl::MakeFocus(focus);
}


void
BPasswordControl::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);
	if (fData != NULL) {
		if (fData->textControl != NULL)
			fData->textControl->SetEnabled(enabled);
		if (fData->eyeButton != NULL)
			fData->eyeButton->SetEnabled(enabled);
	}
}


void
BPasswordControl::_Init(const char* text)
{
	if (fData == NULL)
		return;

	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	fData->textControl = dynamic_cast<BTextControl*>(
		FindView("password_input"));
	fData->eyeButton = dynamic_cast<EyeButton*>(FindView("eye"));

	if (fData->textControl == NULL) {
		fData->textControl = new BTextControl("password_input", Label(),
			text != NULL ? text : "", new BMessage(kMsgPasswordEnter));
		fData->textControl->SetExplicitMaxSize(
			BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		if (Label() == NULL || Label()[0] == '\0')
			fData->textControl->SetDivider(0.0f);

		if (fData->eyeButton == NULL)
			fData->eyeButton = new EyeButton(new BMessage(kMsgToggleReveal));

		BLayoutBuilder::Group<>(this, B_HORIZONTAL, 4)
			.Add(fData->textControl)
			.Add(fData->eyeButton);
	} else {
		fData->textControl->SetText("");
	}

	if (fData->textControl->TextView() != NULL)
		fData->textControl->TextView()->HideTyping(!fData->revealed);
}


void
BPasswordControl::_ReservedPasswordControl1()
{
}


void
BPasswordControl::_ReservedPasswordControl2()
{
}


void
BPasswordControl::_ReservedPasswordControl3()
{
}


void
BPasswordControl::_ReservedPasswordControl4()
{
}


}	// namespace BAppearanceKit
