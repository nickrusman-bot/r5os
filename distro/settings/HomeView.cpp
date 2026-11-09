/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "HomeView.h"

#include "SettingsFont.h"
#include "SettingsMessages.h"

#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <GridLayout.h>
#include <String.h>
#include <Window.h>

#include <string.h>


static void
stroke_dashed(BView* view, BRect rect)
{
	float dash = 4.0f;
	float gap = 3.0f;
	for (float x = rect.left; x < rect.right; x += dash + gap) {
		float end = x + dash;
		if (end > rect.right)
			end = rect.right;
		view->StrokeLine(BPoint(x, rect.top), BPoint(end, rect.top));
		view->StrokeLine(BPoint(x, rect.bottom), BPoint(end, rect.bottom));
	}
	for (float y = rect.top; y < rect.bottom; y += dash + gap) {
		float end = y + dash;
		if (end > rect.bottom)
			end = rect.bottom;
		view->StrokeLine(BPoint(rect.left, y), BPoint(rect.left, end));
		view->StrokeLine(BPoint(rect.right, y), BPoint(rect.right, end));
	}
}


class RecentPanel : public BView {
public:
								RecentPanel(const char* line);
	virtual	void				Draw(BRect update);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			BString				fLine;
};


RecentPanel::RecentPanel(const char* line)
	:
	BView("recent", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fLine(line)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
}


void
RecentPanel::Draw(BRect update)
{
	(void)update;
	SetLowColor(ViewColor());
	FillRect(Bounds(), B_SOLID_LOW);
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	stroke_dashed(this, Bounds());

	float u = be_plain_font->Size() / 13.0f;
	BFont title(be_bold_font);
	title.SetSize(u * 13.0f);
	BFont body(be_plain_font);
	body.SetSize(u * 12.0f);
	font_height titleHeight;
	font_height bodyHeight;
	title.GetHeight(&titleHeight);
	body.GetHeight(&bodyHeight);
	// Top aligned, with the same top padding as the cards (u * 10), so
	// the title lines up with the card title next to it.
	float y = u * 10.0f + titleHeight.ascent;
	float x = u * 14.0f;
	SetFont(&title);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	DrawString("Recently changed", BPoint(x, y));
	y += titleHeight.descent + u * 4.0f + bodyHeight.ascent;
	SetFont(&body);
	SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 160));
	DrawString(fLine.String(), BPoint(x, y));
	SetFont(be_plain_font);
}


BSize
RecentPanel::MinSize()
{
	float u = be_plain_font->Size() / 13.0f;
	return BSize(280, u * 14.0f * 2 + u * 13.0f + u * 12.0f);
}


BSize
RecentPanel::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
}


BSize
RecentPanel::PreferredSize()
{
	return MinSize();
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
	float u = be_plain_font->Size() / 13.0f;
	fGrid = new BGridLayout(u * 10.0f, u * 10.0f);
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
	fGrid->SetRowWeight(row, 1);
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
	BString line("Nothing changed yet");
	if (count > 0 && names != NULL) {
		line = "";
		for (int32 i = 0; i < count; i++) {
			if (i > 0)
				line << " · ";
			line << names[i];
		}
	}
	(void)ids;
	fRecent = new RecentPanel(line.String());
	if (fGrid != NULL) {
		int32 row = fCount / 2;
		fGrid->AddView(fRecent, fCount % 2, row);
		fGrid->SetRowWeight(row, 1);
	}
}
