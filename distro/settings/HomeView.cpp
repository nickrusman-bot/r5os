/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "HomeView.h"

#include "SettingsFont.h"

#include <Bitmap.h>
#include <Control.h>
#include <ControlLook.h>
#include <Font.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Mime.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <string.h>


class CategoryCard : public BControl {
public:
								CategoryCard(const char* name,
									const char* summary,
									const char* iconSignature,
									BMessage* message);
	virtual						~CategoryCard();
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				GetPreferredSize(float* width, float* height);

private:
			BString				fName;
			BString				fSummary;
			BBitmap*			fIcon;
};


CategoryCard::CategoryCard(const char* name, const char* summary,
	const char* iconSignature, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW),
	fName(name),
	fSummary(summary),
	fIcon(NULL)
{
	SetExplicitMinSize(BSize(300, 88));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 88));
	if (iconSignature != NULL && iconSignature[0] != '\0') {
		BRect bounds(BPoint(0, 0),
			be_control_look->ComposeIconSize(B_LARGE_ICON));
		fIcon = new BBitmap(bounds, B_RGBA32);
		if (BMimeType(iconSignature).GetIcon(fIcon, B_LARGE_ICON) != B_OK) {
			delete fIcon;
			fIcon = NULL;
		}
	}
}


CategoryCard::~CategoryCard()
{
	delete fIcon;
}


void
CategoryCard::Draw(BRect update)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color background = base;
	uint32 flags = 0;
	if (IsFocus() || Value() == B_CONTROL_ON)
		flags |= BControlLook::B_ACTIVATED;

	BRect rect = Bounds();
	be_control_look->DrawButtonFrame(this, rect, update, base, background,
		flags);
	be_control_look->DrawButtonBackground(this, rect, update, base, flags);

	float textLeft = rect.left + 12;
	if (fIcon != NULL) {
		BPoint where(rect.left + 10,
			rect.top + (rect.Height() - fIcon->Bounds().Height()) / 2);
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fIcon, where);
		SetDrawingMode(B_OP_COPY);
		textLeft = where.x + fIcon->Bounds().Width() + 12;
	}
	float textWidth = rect.right - textLeft - 8;
	if (textWidth < 20)
		textWidth = 20;

	const BFont* bold = r5_settings_bold_font();
	const BFont* plain = r5_settings_plain_font();
	font_height height;
	bold->GetHeight(&height);
	float top = rect.top + 16 + height.ascent;
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	SetFont(bold);
	BString name(fName);
	bold->TruncateString(&name, B_TRUNCATE_END, textWidth);
	DrawString(name.String(), BPoint(textLeft, top));

	SetFont(plain);
	plain->GetHeight(&height);
	SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR), 0.7f));
	BString summary(fSummary);
	plain->TruncateString(&summary, B_TRUNCATE_END, textWidth);
	DrawString(summary.String(),
		BPoint(textLeft, top + height.ascent + height.descent + 6));
}


void
CategoryCard::MouseDown(BPoint where)
{
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	BControl::MouseDown(where);
}


void
CategoryCard::MouseUp(BPoint where)
{
	BControl::MouseUp(where);
	if (Bounds().Contains(where))
		Invoke();
}


void
CategoryCard::GetPreferredSize(float* width, float* height)
{
	if (width != NULL)
		*width = 360;
	if (height != NULL)
		*height = 88;
}


HomeView::HomeView(const SettingsHomeItem* items, int32 count,
	const char* introText)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fCount(0)
{
	memset(fCards, 0, sizeof(fCards));
	BStringView* intro = NULL;
	if (introText != NULL && introText[0] != '\0') {
		intro = new BStringView("intro", introText);
		intro->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		intro->SetFont(r5_settings_plain_font());
	}

	BGroupView* grid = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	BGroupLayout* row = grid->GroupLayout();
	row->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BGroupView* left = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BGroupView* right = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	if (count > 16)
		count = 16;
	for (int32 i = 0; i < count; i++) {
		BMessage* message = new BMessage(items[i].command);
		if (items[i].field != NULL)
			message->AddString(items[i].field, items[i].id);
		CategoryCard* card = new CategoryCard(items[i].name, items[i].summary,
			items[i].iconSignature, message);
		fCards[fCount++] = card;
		if ((i % 2) == 0)
			left->AddChild(card);
		else
			right->AddChild(card);
	}
	left->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	right->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	grid->AddChild(left);
	grid->AddChild(right);

	if (intro != NULL) {
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.Add(intro)
			.Add(grid)
			.AddGlue();
	} else {
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.Add(grid)
			.AddGlue();
	}
	r5_settings_apply_plain(this);
}


HomeView::~HomeView()
{
}
