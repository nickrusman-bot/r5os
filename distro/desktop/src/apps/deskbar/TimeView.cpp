/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their respective
holders.
All rights reserved.
*/

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */



#include "TimeView.h"

#include <algorithm>

#include <string.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <String.h>

// SPDX-SnippetEnd
#include <Application.h>
#include <Catalog.h>
#include <Debug.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <FormattingConventions.h>
// SPDX-SnippetEnd
#include <Locale.h>
#include <MenuItem.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <TimeFormat.h>
// SPDX-SnippetEnd
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <Window.h>

#include "BarApp.h"
#include "BarView.h"
#include "BarWindow.h"
#include "StatusView.h"
#include "CalendarMenuWindow.h"
#include "StatusView.h"


static const float kHMargin = 2.0;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TimeView"


TTimeView::TTimeView(float maxWidth, float height, TBarView* barView)
	:
	BView(BRect(-100, -100, -90, -90), "_deskbar_tv_",
		B_FOLLOW_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_PULSE_NEEDED | B_FRAME_EVENTS),
	fBarView(barView),
	fParent(NULL),
	fMaxWidth(maxWidth),
	fHeight(height),
	fShowLevel(0),
	fShowSeconds(false),
	fShowDayOfWeek(false),
	fShowTimeZone(false),
	fCalendarWindow(NULL),
	fTimeFormat(NULL),
	fDateFormat(NULL)
{
	fCurrentTime = fLastTime = time(NULL);
	fSeconds = fMinute = fHour = 0;
	fCurrentTimeStr[0] = 0;
	fCurrentDateStr[0] = 0;
	fLastTimeStr[0] = 0;
	fLastDateStr[0] = 0;
	fNeedToUpdate = true;
	UpdateTimeFormat();
}


#ifdef AS_REPLICANT
TTimeView::TTimeView(BMessage* data)
	: BView(data),
	fTimeFormat(NULL),
	fDateFormat(NULL)
{
	fCurrentTime = fLastTime = time(NULL);
	data->FindBool("seconds", &fShowSeconds);

	UpdateTimeFormat();
}
#endif


TTimeView::~TTimeView()
{
	if (fCalendarWindowMessenger.IsValid())
		fCalendarWindowMessenger.SendMessage(B_QUIT_REQUESTED);

	delete fTimeFormat;
	delete fDateFormat;
}


#ifdef AS_REPLICANT
BArchivable*
TTimeView::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "TTimeView"))
		return NULL;

	return new TTimeView(data);
}


status_t
TTimeView::Archive(BMessage* data, bool deep) const
{
	BView::Archive(data, deep);
	data->AddBool("orientation", Vertical());
	data->AddInt16("showLevel", fShowLevel);
	data->AddBool("showSeconds", fShowSeconds);
	data->AddBool("showDayOfWeek", fShowDayOfWeek);
	data->AddBool("showTimeZone", fShowTimeZone);
	data->AddInt32("deskbar:private_align", B_ALIGN_RIGHT);

	return B_OK;
}
#endif


void
TTimeView::AttachedToWindow()
{
	fCurrentTime = time(NULL);

	SetFont(be_plain_font);
	if (Parent()) {
		fParent = Parent();
		AdoptParentColors();
	} else
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	ResizeToPreferred();
	CalculateTextPlacement();
}


void
TTimeView::Draw(BRect /*updateRect*/)
{
	PushState();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetHighUIColor(B_MENU_BACKGROUND_COLOR);
// SPDX-SnippetEnd
	FillRect(Bounds());
	SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BString shown(fCurrentTimeStr);
	BPoint where(fTimeLocation);
	if (Vertical()) {
		float limit = Bounds().Width();
		if (limit > 0 && StringWidth(shown.String()) > limit) {
			// Keep the weekday when it is shown. Otherwise keep the clock.
			// Do not slide the string so the first letter is cut (Haiku #14846).
			TruncateString(&shown,
				fShowDayOfWeek ? B_TRUNCATE_END : B_TRUNCATE_BEGINNING,
				limit);
			where.x = 0;
		}
	}
	DrawString(shown.String(), where);
// SPDX-SnippetEnd

	PopState();
}


void
TTimeView::FrameMoved(BPoint)
{
	Update();
}


void
TTimeView::GetPreferredSize(float* width, float* height)
{
	float timeWidth = StringWidth(fCurrentTimeStr);

	// set the height based on the font size
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	fHeight = fontHeight.ascent + fontHeight.descent - 2;
		// reduce height by 2px so that clock doesn't draw on top of border

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	timeWidth += kHMargin * 2;
// SPDX-SnippetEnd
	if (Vertical()) {
		float appWidth = static_cast<TBarApp*>(be_app)->Settings()->width;
		*width = fMaxWidth
			= std::min(appWidth - (gDragRegionWidth + kHMargin) * 2, timeWidth);
	} else
		*width = fMaxWidth = timeWidth;

	*height = fHeight;
}


void
TTimeView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kChangeTime:
		{
			// launch the time prefs app
			be_roster->Launch("application/x-vnd.Haiku-Time");
			// tell Time preflet to switch to the clock tab
			BMessenger messenger("application/x-vnd.Haiku-Time");
			BMessage switchToClock('SlCk');
			messenger.SendMessage(&switchToClock);
			break;
		}

		case kShowHideTime:
		{
			be_app->MessageReceived(message);
			break;
		}

		case kShowCalendar:
		{
			BRect bounds(Bounds());
			BPoint center(bounds.LeftTop());
			center += BPoint(bounds.Width() / 2, bounds.Height() / 2);
			ShowCalendar(center);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
TTimeView::MouseDown(BPoint point)
{
	uint32 buttons;

	Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	if (buttons == B_SECONDARY_MOUSE_BUTTON) {
		ShowTimeOptions(ConvertToScreen(point));
		return;
	} else if (buttons == B_PRIMARY_MOUSE_BUTTON)
		ShowCalendar(point);

	// invalidate last time/date strings and call the pulse
	// method directly to change the display instantly
	fLastDateStr[0] = '\0';
	fLastTimeStr[0] = '\0';
	Pulse();
}


void
TTimeView::Pulse()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The clock uses the same short time pattern as the Locale preflet.
	// Reloading the settings file here would put en_US back when that
	// file was not written yet.
	BFormattingConventions conventions;
	if (BLocale::Default()->GetFormattingConventions(&conventions) == B_OK) {
		static bool sSaw24 = false;
		static BString sSawID;
		static bool sReady = false;
		bool use24 = conventions.Use24HourClock();
		if (!sReady || use24 != sSaw24 || sSawID != conventions.ID()) {
			sReady = true;
			sSaw24 = use24;
			sSawID = conventions.ID();
			UpdateTimeFormat();
		}
	}

// SPDX-SnippetEnd
	time_t curTime = time(NULL);
	tm* ct = localtime(&curTime);
	if (ct == NULL)
		return;

	fCurrentTime = curTime;

	GetCurrentTime();
	GetCurrentDate();
	if (strcmp(fCurrentTimeStr, fLastTimeStr) != 0) {
		// Update bounds when the size of the strings has changed
		Update();

		strlcpy(fLastTimeStr, fCurrentTimeStr, sizeof(fLastTimeStr));
		fNeedToUpdate = true;
	}

	// Update the tooltip if the date has changed
	if (strcmp(fCurrentDateStr, fLastDateStr) != 0) {
		strlcpy(fLastDateStr, fCurrentDateStr, sizeof(fLastDateStr));
		SetToolTip(fCurrentDateStr);
	}

	if (fNeedToUpdate) {
		fSeconds = ct->tm_sec;
		fMinute = ct->tm_min;
		fHour = ct->tm_hour;

		Draw(Bounds());
		fNeedToUpdate = false;
	}
}


void
TTimeView::ResizeToPreferred()
{
	float width;
	float height;
	float oldWidth = Bounds().Width();
	float oldHeight = Bounds().Height();

	GetPreferredSize(&width, &height);
	if (height != oldHeight || width != oldWidth) {
		ResizeTo(width, height);
		MoveBy(oldWidth - width, 0);
		fNeedToUpdate = true;
	}
}


//	# pragma mark - Public methods


bool
TTimeView::ShowSeconds() const
{
	return fShowSeconds;
}


void
TTimeView::SetShowSeconds(bool show)
{
	fShowSeconds = show;
	UpdateTimeFormat();
	Update();
}


bool
TTimeView::ShowDayOfWeek() const
{
	return fShowDayOfWeek;
}


void
TTimeView::SetShowDayOfWeek(bool show)
{
	fShowDayOfWeek = show;
	UpdateTimeFormat();
	Update();
}


bool
TTimeView::ShowTimeZone() const
{
	return fShowTimeZone;
}


void
TTimeView::SetShowTimeZone(bool show)
{
	fShowTimeZone = show;
	UpdateTimeFormat();
	Update();
}


void
TTimeView::ShowCalendar(BPoint where)
{
	if (fCalendarWindowMessenger.IsValid()) {
		// If the calendar is already shown, just activate it
		BMessage activate(B_SET_PROPERTY);
		activate.AddSpecifier("Active");
		activate.AddBool("data", true);

		if (fCalendarWindowMessenger.SendMessage(&activate) == B_OK)
			return;
	}

	where.y = Bounds().bottom + 4.0;
	ConvertToScreen(&where);

	if (where.y >= BScreen().Frame().bottom)
		where.y -= (Bounds().Height() + 4.0);

	fCalendarWindow = new CalendarMenuWindow(where);
	fCalendarWindowMessenger = BMessenger(fCalendarWindow);
	fCalendarWindow->Show();
}


bool
TTimeView::IsShowingCalendar()
{
	return fCalendarWindow != NULL && !fCalendarWindow->IsHidden();
}


//	# pragma mark - Private methods


void
TTimeView::UpdateTimeFormat()
{
	int32 fields = B_DATE_ELEMENT_HOUR | B_DATE_ELEMENT_MINUTE;
	if (fShowSeconds)
		fields |= B_DATE_ELEMENT_SECOND;
	if (fShowDayOfWeek)
		fields |= B_DATE_ELEMENT_WEEKDAY;
	if (fShowTimeZone)
		fields |= B_DATE_ELEMENT_TIMEZONE;

	delete fTimeFormat;
	fTimeFormat = new BDateTimeFormat(BLocale::Default());
	fTimeFormat->SetDateTimeFormat(B_SHORT_DATE_FORMAT, B_SHORT_TIME_FORMAT,
		fields);

	delete fDateFormat;
	fDateFormat = new BDateFormat(BLocale::Default());
}


void
TTimeView::GetCurrentTime()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// BTimeFormat reads the formatting country's own pattern. The
	// date-time pattern generator can keep an English AM/PM skeleton.
	BTimeFormatStyle style = B_SHORT_TIME_FORMAT;
	if (fShowTimeZone)
		style = B_FULL_TIME_FORMAT;
	else if (fShowSeconds)
		style = B_MEDIUM_TIME_FORMAT;

	BString formatted;
	BTimeFormat timeFormat;
	if (timeFormat.Format(formatted, fCurrentTime, style) == B_OK
		&& formatted.Length() > 0) {
		strlcpy(fCurrentTimeStr, formatted.String(), sizeof(fCurrentTimeStr));
	}
// SPDX-SnippetEnd
}


void
TTimeView::GetCurrentDate()
{
	char tmp[sizeof(fCurrentDateStr)];

	fDateFormat->Format(tmp, sizeof(fCurrentDateStr), fCurrentTime,
		B_FULL_DATE_FORMAT);

	// remove leading 0 from date when month is less than 10 (MM/DD/YY)
	// or remove leading 0 from date when day is less than 10 (DD/MM/YY)
	const char* str = tmp;
	if (str[0] == '0')
		str++;

	strlcpy(fCurrentDateStr, str, sizeof(fCurrentDateStr));
}


void
TTimeView::CalculateTextPlacement()
{
	fDateLocation.x = 0.0;
	fTimeLocation.x = 0.0;

	BFont font;
	GetFont(&font);

	const char* stringArray[1];
	stringArray[0] = fCurrentTimeStr;
	BRect rectArray[1];
	escapement_delta delta = { 0.0, 0.0 };
	font.GetBoundingBoxesForStrings(stringArray, 1, B_SCREEN_METRIC, &delta,
		rectArray);

	// center vertically
	fTimeLocation.y = fDateLocation.y = ceilf((Bounds().Height()
		- rectArray[0].Height() + 1.0) / 2.0 - rectArray[0].top);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// A vertical Deskbar that is narrower than the clock string is
	// shortened in Draw() instead of being shifted off the left edge.
// SPDX-SnippetEnd
}


void
TTimeView::ShowTimeOptions(BPoint point)
{
	BPopUpMenu* menu = new BPopUpMenu("", false, false);
	menu->SetFont(be_plain_font);
	BMenuItem* item;

	item = new BMenuItem(B_TRANSLATE("Time preferences" B_UTF8_ELLIPSIS),
		new BMessage(kChangeTime));
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Hide clock"),
		new BMessage(kShowHideTime));
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Show calendar" B_UTF8_ELLIPSIS),
		new BMessage(kShowCalendar));
	menu->AddItem(item);

	menu->SetTargetForItems(this);
	// Changed to accept screen coord system point;
	// not constrained to this view now
	menu->Go(point, true, true, BRect(point.x - 4, point.y - 4,
		point.x + 4, point.y +4), true);
}


void
TTimeView::Update()
{
	GetCurrentTime();
	GetCurrentDate();
	SetToolTip(fCurrentDateStr);

	ResizeToPreferred();
	CalculateTextPlacement();

	if (fParent != NULL)
		fParent->Invalidate();
}


bool
TTimeView::Vertical()
{
	if (fBarView == NULL)
		return true;

	return fBarView->Vertical();
}
