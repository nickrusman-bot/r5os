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
#include <R5Icons.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>
#include <new>


namespace BAppearanceKit {


static float
unit()
{
	// One mockup pixel. The mockup is drawn for a 12 point system font.
	return be_plain_font->Size() / 12.0f;
}


// The field text is one step larger than body text, as in the mockup
// (14 on 12).
static BFont
field_font()
{
	BFont font(be_plain_font);
	font.SetSize(floorf(be_plain_font->Size() * 14.0f / 12.0f + 0.5f));
	return font;
}


// The edge of the blue focus ring around the field.
static float
ring_width()
{
	return floorf(2.0f * unit() + 0.5f);
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
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		rgb_color tc = ui_color(B_DOCUMENT_TEXT_COLOR);
		BFont font = field_font();
		SetFontAndColor(&font, B_FONT_ALL, &tc);
		SetWordWrap(false);
		HideTyping(true);
		MakeResizable(true);
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

	virtual void Draw(BRect updateRect)
	{
		BTextView::Draw(updateRect);
		if (TextLength() > 0 || fParent == NULL)
			return;
		const char* placeholder = fParent->Placeholder();
		if (placeholder == NULL || placeholder[0] == '\0')
			return;

		BFont font = field_font();
		font_height height;
		font.GetHeight(&height);
		BRect text = TextRect();
		PushState();
		SetFont(&font);
		SetDrawingMode(B_OP_OVER);
		SetHighColor(mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
			ui_color(B_DOCUMENT_BACKGROUND_COLOR), 120));
		DrawString(placeholder, BPoint(text.left + 1,
			text.top + height.ascent));
		PopState();
	}

	virtual void InsertText(const char* text, int32 length, int32 offset,
		const text_run_array* runs)
	{
		BTextView::InsertText(text, length, offset, runs);
		Invalidate();
	}

	virtual void DeleteText(int32 fromOffset, int32 toOffset)
	{
		BTextView::DeleteText(fromOffset, toOffset);
		Invalidate();
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
	BString				placeholder;

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
	BControl(name, label, message, B_WILL_DRAW | B_FRAME_EVENTS
		| B_SUPPORTS_LAYOUT | B_FULL_UPDATE_ON_RESIZE
		| B_TRANSPARENT_BACKGROUND),
	fData(new(std::nothrow) Data)
{
	SetViewColor(B_TRANSPARENT_COLOR);

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
	SetViewColor(B_TRANSPARENT_COLOR);

	BRect dummy(0, 0, 100, 20);
	fData->textView = new PasswordTextView(dummy, "password_tv", this);
	AddChild(fData->textView);

	if (fData == NULL || fData->textView == NULL)
		return;

	// The password is never taken from an archive.
	if (archive != NULL)
		archive->FindBool("r5:revealed", &fData->revealed);
	if (archive != NULL) {
		const char* placeholder = NULL;
		if (archive->FindString("r5:placeholder", &placeholder) == B_OK)
			fData->placeholder = placeholder;
	}
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
	// deep would archive the text view, and that view stores its text.
	(void)deep;
	status_t status = BControl::Archive(archive, false);
	if (status != B_OK || fData == NULL)
		return status;

	if (archive->ReplaceString("class", kPasswordControlClassName) != B_OK)
		status = archive->AddString("class", kPasswordControlClassName);
	if (status == B_OK)
		status = archive->AddBool("r5:revealed", fData->revealed);
	if (status == B_OK && fData->placeholder.Length() > 0)
		status = archive->AddString("r5:placeholder", fData->placeholder);

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
	if (fData->textView != NULL) {
		// HideTyping(true) deletes the text, and HideTyping(false) does not
		// redraw it, so the text is put back after the switch.
		BTextView* view = fData->textView;
		BString text(view->Text());
		int32 start;
		int32 end;
		view->GetSelection(&start, &end);
		view->HideTyping(!revealed);
		view->SetText(text.String());
		view->Select(start, end);
		text.SetTo('\0', text.Length());
	}

	Invalidate(_EyeButtonRect());
}


void
BPasswordControl::SetPlaceholder(const char* text)
{
	if (fData == NULL)
		return;
	fData->placeholder = text;
	if (fData->textView != NULL)
		fData->textView->Invalidate();
}


const char*
BPasswordControl::Placeholder() const
{
	return fData != NULL ? fData->placeholder.String() : NULL;
}


void
BPasswordControl::AttachedToWindow()
{
	BControl::AttachedToWindow();
	_LayoutChildren();
}


void
BPasswordControl::FrameResized(float newWidth, float newHeight)
{
	(void)newWidth;
	(void)newHeight;
	BControl::FrameResized(newWidth, newHeight);
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
	if (bounds.Width() <= 0 || bounds.Height() <= 0)
		return BRect();
	float u = unit();
	float btnSize = floorf(26.0f * u);
	float pad = ring_width() + 6.0f * u;

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
	if (bounds.Width() <= 0 || bounds.Height() <= 0)
		return;
	float u = unit();
	BRect eye = _EyeButtonRect();

	// Inset text view so text doesn't overlap the eye icon
	float leftPad = ring_width() + 10.0f * u;
	float rightPad = (bounds.right - eye.left) + 4.0f * u;

	font_height fh;
	BFont font = field_font();
	font.GetHeight(&fh);
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
	if (bounds.Width() <= 0 || bounds.Height() <= 0)
		return;
	float u = unit();
	float radius = 4.0f * u;
	float ring = ring_width();
	BRect field = bounds.InsetByCopy(ring, ring);

	rgb_color bg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_3_TINT);

	bool hasFocus = fData != NULL && fData->textView != NULL
		&& fData->textView->IsFocus() && Window() != NULL
		&& Window()->IsActive();

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	if (hasFocus && IsEnabled()) {
		// The soft blue ring outside the field.
		border = accent;
		rgb_color halo = accent;
		halo.alpha = 56;
		SetHighColor(halo);
		SetPenSize(ring);
		BRect haloRect = bounds.InsetByCopy(ring / 2, ring / 2);
		StrokeRoundRect(haloRect, radius + ring / 2, radius + ring / 2);
		SetPenSize(1.0f);
	}

	SetHighColor(IsEnabled() ? bg : mix_color(bg,
		ui_color(B_PANEL_BACKGROUND_COLOR), 128));
	FillRoundRect(field, radius, radius);

	// The faint inner shadow along the top.
	SetHighColor(0, 0, 0, 20);
	StrokeLine(BPoint(field.left + radius, field.top + 1),
		BPoint(field.right - radius, field.top + 1));

	SetHighColor(border);
	StrokeRoundRect(field, radius, radius);
	SetDrawingMode(B_OP_COPY);

	_DrawEyeButton();
}


void
BPasswordControl::_DrawEyeButton()
{
	BRect eye = _EyeButtonRect();
	if (!eye.IsValid())
		return;

	// The open eye means "show the password", as in the mockup. Once
	// the password is shown, the crossed-out eye hides it again.
	float side = floorf(18.0f * unit());
	BRect glyph(0, 0, side, side);
	glyph.OffsetTo(floorf(eye.left + (eye.Width() - side) / 2),
		floorf(eye.top + (eye.Height() - side) / 2));
	bool revealed = fData != NULL && fData->revealed;
	rgb_color color = mix_color(ui_color(B_DOCUMENT_TEXT_COLOR),
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 120);
	r5_glyph_draw(this, revealed ? R5_GLYPH_EYE_OFF : R5_GLYPH_EYE, glyph,
		color);
}


void
BPasswordControl::MouseDown(BPoint where)
{
	if (_EyeButtonRect().Contains(where)) {
		SetPasswordRevealed(!IsPasswordRevealed());
		return;
	}

	if (fData != NULL && fData->textView != NULL)
		fData->textView->MakeFocus(true);

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
		*height = floorf(38.0f * u) + 2 * ring_width() - 1;
}


BSize
BPasswordControl::MinSize()
{
	float u = unit();
	return BSize(140.0f * u, floorf(38.0f * u) + 2 * ring_width() - 1);
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


} // namespace BAppearanceKit
