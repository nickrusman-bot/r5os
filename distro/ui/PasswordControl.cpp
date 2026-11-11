/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
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


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


class EyeButton : public BButton {
public:
	EyeButton(BMessage* message)
		:
		BButton("eye", "", message),
		fRevealed(false)
	{
		float u = unit();
		float side = 28.0f * u;
		SetExplicitMinSize(BSize(side, side));
		SetExplicitMaxSize(BSize(side, side));
	}

	void SetRevealed(bool revealed)
	{
		if (fRevealed != revealed) {
			fRevealed = revealed;
			Invalidate();
		}
	}

	virtual void Draw(BRect updateRect)
	{
		BButton::Draw(updateRect);

		BRect bounds = Bounds();
		float u = unit();

		rgb_color iconColor = ui_color(B_CONTROL_TEXT_COLOR);
		if (!IsEnabled())
			iconColor = tint_color(iconColor, B_LIGHTEN_1_TINT);

		SetHighColor(iconColor);

		float cx = bounds.left + bounds.Width() / 2.0f;
		float cy = bounds.top + bounds.Height() / 2.0f;
		float rx = 6.5f * u;
		float ry = 4.0f * u;

		BRect eyeOval(cx - rx, cy - ry, cx + rx, cy + ry);
		StrokeEllipse(eyeOval);

		BRect pupil(cx - 2.2f * u, cy - 2.2f * u, cx + 2.2f * u, cy + 2.2f * u);
		FillEllipse(pupil);

		if (!fRevealed) {
			SetPenSize(1.5f * u);
			StrokeLine(BPoint(cx - rx, cy + ry + 1.5f * u),
				BPoint(cx + rx, cy - ry - 1.5f * u));
			SetPenSize(1.0f);
		}
	}

private:
	bool fRevealed;
};


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


static const char* kPasswordControlClassName = "BAppearanceKit::BPasswordControl";


BPasswordControl::BPasswordControl(const char* name, const char* label,
	const char* text, BMessage* message)
	:
	BControl(name, label, message, B_WILL_DRAW | B_SUPPORTS_LAYOUT),
	fData(new(std::nothrow) Data)
{
	_Init(text);
}


BPasswordControl::BPasswordControl(BMessage* archive)
	:
	BControl(archive),
	fData(new(std::nothrow) Data)
{
	const char* text = NULL;
	archive->FindString("r5:text", &text);
	_Init(text);

	bool revealed = false;
	if (archive->FindBool("r5:revealed", &revealed) == B_OK)
		SetPasswordRevealed(revealed);
}


void
BPasswordControl::_Init(const char* text)
{
	if (fData == NULL)
		return;

	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);

	fData->textControl = new BTextControl("password_input", Label(),
		text != NULL ? text : "", new BMessage(kMsgPasswordEnter));
	fData->textControl->TextView()->HideTyping(true);
	fData->textControl->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	if (Label() == NULL || Label()[0] == '\0')
		fData->textControl->SetDivider(0.0f);

	fData->eyeButton = new EyeButton(new BMessage(kMsgToggleReveal));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 4)
		.Add(fData->textControl)
		.Add(fData->eyeButton);
}


BPasswordControl::~BPasswordControl()
{
	delete fData;
}


BArchivable*
BPasswordControl::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, kPasswordControlClassName))
		return new(std::nothrow) BPasswordControl(archive);
	return NULL;
}


status_t
BPasswordControl::Archive(BMessage* archive, bool deep) const
{
	status_t status = BControl::Archive(archive, deep);
	if (status != B_OK || fData == NULL)
		return status;

	status = archive->AddString("class", kPasswordControlClassName);
	if (status == B_OK)
		status = archive->AddString("r5:text", Text());
	if (status == B_OK)
		status = archive->AddBool("r5:revealed", fData->revealed);

	return status;
}


const char*
BPasswordControl::Text() const
{
	return (fData != NULL && fData->textControl != NULL) ?
		fData->textControl->Text() : "";
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
	return (fData != NULL && fData->textControl != NULL) ?
		fData->textControl->TextView() : NULL;
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

	if (Parent() != NULL)
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


} // namespace BAppearanceKit
