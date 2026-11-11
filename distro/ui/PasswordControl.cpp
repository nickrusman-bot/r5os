/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "PasswordControl.h"

#include <Button.h>
#include <ControlLook.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <TextView.h>
#include <Window.h>

#include <new>


namespace BAppearanceKit {


static float
unit()
{
	return be_plain_font->Size() / 13.0f;
}


// Inner text view to handle Enter and navigation
class PasswordTextView : public BTextView {
public:
	PasswordTextView(BRect frame, const char* name, BPasswordControl* parent)
		:
		BTextView(frame, name, frame.OffsetToCopy(B_ORIGIN),
			B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE),
		fParent(parent)
	{
		HideTyping(true);
		MakeResizable(false);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && bytes[0] == B_ENTER) {
			if (fParent != NULL) {
				BMessage* msg = fParent->Message();
				if (msg != NULL)
					fParent->Invoke();
				else if (Window() != NULL && Window()->DefaultButton() != NULL)
					Window()->DefaultButton()->Invoke();
			}
			return;
		}
		BTextView::KeyDown(bytes, numBytes);
	}

	virtual void MakeFocus(bool focus = true)
	{
		BTextView::MakeFocus(focus);
		if (fParent != NULL)
			fParent->Invalidate();
	}

private:
	BPasswordControl* fParent;
};


struct BPasswordControl::Data {
	PasswordTextView*	textView;
	bool				revealed;

	Data()
		:
		textView(NULL),
		revealed(false)
	{
	}
};


static const char* kPasswordControlClassName = "BAppearanceKit::BPasswordControl";


BPasswordControl::BPasswordControl(const char* name, const char* label,
	const char* text, BMessage* message)
	:
	BControl(name, label, message, B_WILL_DRAW | B_FRAME_EVENTS),
	fData(new(std::nothrow) Data)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BRect dummy(0, 0, 100, 20);
	fData->textView = new PasswordTextView(dummy, "password_tv", this);
	if (text != NULL)
		fData->textView->SetText(text);
	AddChild(fData->textView);
}


BPasswordControl::BPasswordControl(BMessage* archive)
	:
	BControl(archive),
	fData(new(std::nothrow) Data)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BRect dummy(0, 0, 100, 20);
	fData->textView = new PasswordTextView(dummy, "password_tv", this);
	AddChild(fData->textView);

	const char* text = NULL;
	if (archive->FindString("r5:text", &text) == B_OK)
		fData->textView->SetText(text);

	archive->FindBool("r5:revealed", &fData->revealed);
	fData->textView->HideTyping(!fData->revealed);
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
	return (fData != NULL && fData->textView != NULL) ?
		fData->textView->Text() : "";
}


void
BPasswordControl::SetText(const char* text)
{
	if (fData != NULL && fData->textView != NULL) {
		fData->textView->SetText(text);
		fData->textView->SelectAll();
	}
}


BTextView*
BPasswordControl::TextView() const
{
	return fData != NULL ? fData->textView : NULL;
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
	if (fData->textView != NULL)
		fData->textView->HideTyping(!revealed);

	Invalidate(_EyeButtonRect());
}


void
BPasswordControl::AttachedToWindow()
{
	BControl::AttachedToWindow();
	_LayoutChildren();
}


void
BPasswordControl::WindowActivated(bool active)
{
	BControl::WindowActivated(active);
	Invalidate();
}


void
BPasswordControl::MakeFocus(bool focus)
{
	if (fData != NULL && fData->textView != NULL)
		fData->textView->MakeFocus(focus);
	else
		BControl::MakeFocus(focus);
}


void
BPasswordControl::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);
	if (fData != NULL && fData->textView != NULL)
		fData->textView->MakeEditable(enabled);
	Invalidate();
}


BRect
BPasswordControl::_EyeButtonRect() const
{
	BRect bounds = Bounds();
	float u = unit();
	float btnSize = 22.0f * u;
	float pad = 4.0f * u;

	return BRect(bounds.right - btnSize - pad,
		bounds.top + (bounds.Height() - btnSize) / 2.0f,
		bounds.right - pad,
		bounds.top + (bounds.Height() - btnSize) / 2.0f + btnSize);
}


void
BPasswordControl::_LayoutChildren()
{
	if (fData == NULL || fData->textView == NULL)
		return;

	BRect bounds = Bounds();
	float u = unit();
	BRect eye = _EyeButtonRect();

	// Inset text view so text doesn't overlap the eye icon
	float leftPad = 8.0f * u;
	float rightPad = (bounds.right - eye.left) + 4.0f * u;

	font_height fh;
	be_plain_font->GetHeight(&fh);
	float textHeight = fh.ascent + fh.descent + fh.leading;

	float topPad = (bounds.Height() - textHeight) / 2.0f;
	BRect textRect(leftPad, topPad, bounds.Width() - rightPad, topPad + textHeight);

	fData->textView->MoveTo(textRect.LeftTop());
	fData->textView->ResizeTo(textRect.Width(), textRect.Height());

	BRect textBounds(0, 0, textRect.Width(), textRect.Height());
	fData->textView->SetTextRect(textBounds);
}


void
BPasswordControl::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	float u = unit();
	float radius = 4.0f * u;

	rgb_color bg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT);

	bool hasFocus = fData != NULL && fData->textView != NULL
		&& fData->textView->IsFocus();

	if (hasFocus)
		border = ui_color(B_STATUS_BAR_COLOR);

	SetHighColor(bg);
	FillRoundRect(bounds, radius, radius);

	SetHighColor(border);
	StrokeRoundRect(bounds, radius, radius);

	// Eye icon
	_DrawEyeButton();
}


void
BPasswordControl::_DrawEyeButton()
{
	BRect eye = _EyeButtonRect();
	float u = unit();

	rgb_color iconColor = mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 140);
	SetHighColor(iconColor);

	// Outline of eye
	float cx = eye.left + eye.Width() / 2.0f;
	float cy = eye.top + eye.Height() / 2.0f;
	float rx = 7.0f * u;
	float ry = 4.5f * u;

	BRect eyeOval(cx - rx, cy - ry, cx + rx, cy + ry);
	StrokeEllipse(eyeOval);

	// Pupil
	BRect pupil(cx - 2.5f * u, cy - 2.5f * u, cx + 2.5f * u, cy + 2.5f * u);
	FillEllipse(pupil);

	// If slashed (not revealed), draw slash across eye
	if (fData == NULL || !fData->revealed) {
		SetPenSize(1.5f * u);
		StrokeLine(BPoint(eye.left + 3.0f * u, eye.bottom - 3.0f * u),
			BPoint(eye.right - 3.0f * u, eye.top + 3.0f * u));
		SetPenSize(1.0f);
	}
}


void
BPasswordControl::MouseDown(BPoint where)
{
	if (_EyeButtonRect().Contains(where)) {
		SetPasswordRevealed(!IsPasswordRevealed());
		return;
	}

	BControl::MouseDown(where);
}


void
BPasswordControl::MessageReceived(BMessage* message)
{
	BControl::MessageReceived(message);
}


void
BPasswordControl::GetPreferredSize(float* width, float* height)
{
	float u = unit();
	if (width != NULL)
		*width = 240.0f * u;
	if (height != NULL)
		*height = 36.0f * u;
}


BSize
BPasswordControl::MinSize()
{
	float u = unit();
	return BSize(140.0f * u, 34.0f * u);
}


BSize
BPasswordControl::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
BPasswordControl::PreferredSize()
{
	float w, h;
	GetPreferredSize(&w, &h);
	return BSize(w, h);
}


} // namespace BAppearanceKit
