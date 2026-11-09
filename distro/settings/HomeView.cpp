/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "HomeView.h"

#include "SettingsFont.h"
#include "SettingsMessages.h"

#include <Card.h>
#include <Control.h>
#include <ControlLook.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <StringView.h>
#include <Window.h>

#include <string.h>


class PageLink : public BControl {
public:
								PageLink(const char* label, BMessage* message);
	virtual	void				Draw(BRect update);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();

private:
			bool				fPressed;
};


PageLink::PageLink(const char* label, BMessage* message)
	:
	BControl("link", label, message, B_WILL_DRAW | B_TRANSPARENT_BACKGROUND),
	fPressed(false)
{
	SetFont(r5_settings_plain_font());
}


void
PageLink::Draw(BRect update)
{
	(void)update;
	SetHighColor(ui_color(B_LINK_TEXT_COLOR));
	font_height height;
	GetFontHeight(&height);
	DrawString(Label(), BPoint(0, height.ascent));
}


void
PageLink::MouseDown(BPoint where)
{
	(void)where;
	fPressed = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}


void
PageLink::MouseUp(BPoint where)
{
	bool pressed = fPressed;
	fPressed = false;
	if (pressed && Bounds().Contains(where))
		Invoke();
}


void
PageLink::GetPreferredSize(float* width, float* height)
{
	font_height line;
	GetFontHeight(&line);
	if (width != NULL)
		*width = StringWidth(Label());
	if (height != NULL)
		*height = line.ascent + line.descent + line.leading;
}


BSize
PageLink::MinSize()
{
	float width = 0;
	float height = 0;
	GetPreferredSize(&width, &height);
	return BSize(width, height);
}


HomeView::HomeView(bool recent)
	:
	BView("home", B_WILL_DRAW),
	fGrid(NULL),
	fRecent(NULL),
	fCount(0),
	fWantsRecent(recent)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fGrid = new BGridLayout(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	SetLayout(fGrid);
}


HomeView::~HomeView()
{
}


void
HomeView::AddCard(BView* card)
{
	if (card == NULL || fGrid == NULL)
		return;
	int32 column = fCount % 2;
	int32 row = fCount / 2;
	fGrid->AddView(card, column, row);
	fCount++;
}


void
HomeView::SetRecent(const BString* names, const BString* ids, int32 count)
{
	if (!fWantsRecent)
		return;
	if (fRecent != NULL) {
		fRecent->RemoveSelf();
		delete fRecent;
		fRecent = NULL;
	}
	BAppearanceKit::BCard* card = new BAppearanceKit::BCard("recent",
		BAppearanceKit::B_CARD_PANEL);
	BStringView* title = new BStringView("recent-title", "Recently changed");
	title->SetFont(r5_settings_bold_font());
	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(card->GetLayout());
	if (layout != NULL) {
		layout->AddView(title);
		if (count <= 0 || names == NULL || ids == NULL) {
			layout->AddView(new BStringView("recent-empty",
				"Nothing changed yet"));
		} else {
			for (int32 i = 0; i < count; i++) {
				BMessage* message = new BMessage(kMsgOpenPage);
				message->AddString("module", ids[i].String());
				layout->AddView(new PageLink(names[i].String(), message));
			}
		}
	}
	fRecent = card;
	if (fGrid != NULL)
		fGrid->AddView(fRecent, fCount % 2, fCount / 2);
}
