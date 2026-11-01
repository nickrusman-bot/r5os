/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "HomeView.h"

#include "SettingsMessages.h"

#include <Control.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <string.h>


class CategoryCard : public BControl {
public:
								CategoryCard(const char* categoryId,
									const char* name, const char* summary);
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				GetPreferredSize(float* width, float* height);

private:
			BString				fName;
			BString				fSummary;
};


CategoryCard::CategoryCard(const char* categoryId, const char* name,
	const char* summary)
	:
	BControl(name, NULL, new BMessage(kMsgOpenCategory), B_WILL_DRAW),
	fName(name),
	fSummary(summary)
{
	Message()->AddString("category", categoryId);
	SetExplicitMinSize(BSize(220, 84));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 84));
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

	font_height height;
	be_bold_font->GetHeight(&height);
	float top = rect.top + 12 + height.ascent;
	SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	SetFont(be_bold_font);
	DrawString(fName.String(), BPoint(rect.left + 12, top));

	SetFont(be_plain_font);
	be_plain_font->GetHeight(&height);
	SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR), 0.7f));
	DrawString(fSummary.String(),
		BPoint(rect.left + 12, top + height.ascent + height.descent + 6));
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
		*width = 240;
	if (height != NULL)
		*height = 84;
}


HomeView::HomeView(const SettingsHomeItem* items, int32 count)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fCount(0)
{
	memset(fCards, 0, sizeof(fCards));
	BStringView* intro = new BStringView("intro",
		"Choose a category. Existing preference programs open in their own window.");
	intro->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BGroupView* grid = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	BGroupLayout* row = grid->GroupLayout();
	row->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BGroupView* left = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BGroupView* right = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	if (count > 16)
		count = 16;
	for (int32 i = 0; i < count; i++) {
		CategoryCard* card = new CategoryCard(items[i].id, items[i].name,
			items[i].summary);
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

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(intro)
		.Add(grid)
		.AddGlue();
}


HomeView::~HomeView()
{
}
