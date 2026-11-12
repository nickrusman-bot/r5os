/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 *
 * Follows the look of About R5 (AboutSystem): the same card, fonts and
 * colors, built only from public libr5ui classes.
 */


#include "AboutWindow.h"

#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Card.h>
#include <Catalog.h>
#include <File.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <List.h>
#include <R5IconView.h>
#include <R5Icons.h>
#include <Roster.h>
#include <StringView.h>

#include "Messages.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutWindow"


using BAppearanceKit::BCard;
using BAppearanceKit::BIconView;


static void
make_transparent(BView* view)
{
	view->SetViewColor(B_TRANSPARENT_COLOR);
	view->SetFlags(view->Flags() | B_TRANSPARENT_BACKGROUND);
}


static BString
app_version()
{
	app_info info;
	version_info version;
	if (be_app->GetAppInfo(&info) == B_OK) {
		BFile file(&info.ref, B_READ_ONLY);
		BAppFileInfo appInfo(&file);
		if (appInfo.GetVersionInfo(&version, B_APP_VERSION_KIND) == B_OK) {
			BString text;
			text.SetToFormat("%" B_PRIu32 ".%" B_PRIu32, version.major,
				version.middle);
			return text;
		}
	}
	return BString("1.0");
}


// The header card: icon, "Device" bold and "Manager" plain at twice the
// body size, and a muted line under it, as About R5 does with "R5".
class AboutHeader : public BCard {
public:
	AboutHeader()
		:
		BCard("about-header", BAppearanceKit::B_CARD_DOCUMENT, B_HORIZONTAL),
		fIcon(NULL)
	{
		float side = be_plain_font->Size() * 4.0f;
		BBitmap* bitmap = r5_mime_icon_bitmap(kAppSignature, side);
		fIcon = new BIconView("about-icon", bitmap);
		delete bitmap;

		fFirst = new BStringView("about-title", B_TRANSLATE("Device"));
		fSecond = new BStringView("about-title-2", B_TRANSLATE("Manager"));
		BString line;
		line.SetToFormat(B_TRANSLATE("Version %s \xE2\x80\xA2 Part of R5 Operating System"),
			app_version().String());
		fLine = new BStringView("about-version", line);
		make_transparent(fFirst);
		make_transparent(fSecond);
		make_transparent(fLine);

		BGroupView* title = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		make_transparent(title);
		BLayoutBuilder::Group<>(title)
			.Add(fFirst, 0.0f)
			.Add(fSecond, 0.0f)
			.AddGlue();
		BGroupView* text = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
		make_transparent(text);
		BLayoutBuilder::Group<>(text)
			.Add(title)
			.Add(fLine);

		BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
			.Add(fIcon)
			.Add(text);
		_Apply();
	}

	virtual void AttachedToWindow()
	{
		BCard::AttachedToWindow();
		_Apply();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_FONTS_UPDATED || message->what == B_COLORS_UPDATED)
			_Apply();
		BCard::MessageReceived(message);
	}

private:
	void _Apply()
	{
		float size = be_plain_font->Size();
		BFont bold(be_bold_font);
		bold.SetSize(size * 2.0f);
		fFirst->SetFont(&bold, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
		BFont plain(be_plain_font);
		plain.SetSize(size * 2.0f);
		fSecond->SetFont(&plain, B_FONT_FAMILY_AND_STYLE | B_FONT_SIZE);
		fLine->SetFont(be_plain_font, B_FONT_FAMILY_AND_STYLE);

		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		fFirst->SetHighColor(text);
		fSecond->SetHighColor(text);
		fLine->SetHighColor(tint_color(text, B_DARKEN_2_TINT));
	}

	BIconView*		fIcon;
	BStringView*	fFirst;
	BStringView*	fSecond;
	BStringView*	fLine;
};


// The information card: a label in the text color over a muted value,
// two cells per row, as the system information card of About R5.
class AboutInfo : public BCard {
public:
	AboutInfo()
		:
		BCard("about-info", BAppearanceKit::B_CARD_DOCUMENT, B_VERTICAL)
	{
		BGridLayout* grid = NULL;
		BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
			.AddGrid(B_USE_BIG_SPACING, B_USE_DEFAULT_SPACING)
				.GetLayout(&grid)
			.End();
		_AddCell(grid, 0, 0, B_TRANSLATE("License"),
			B_TRANSLATE("MIT. Other systems may use and change it."));
		_AddCell(grid, 1, 0, B_TRANSLATE("Copyright"),
			B_TRANSLATE("\xC2\xA9 2026 R5OS Contributors"));
		_AddCell(grid, 0, 1, B_TRANSLATE("Based on"),
			B_TRANSLATE("Devices, \xC2\xA9 2008\xE2\x80\x93" "2026 Haiku, Inc. (MIT)"));
		_AddCell(grid, 1, 1, B_TRANSLATE("Hardware names"),
			B_TRANSLATE("The PCI ID and USB ID lists"));
		_Apply();
	}

	virtual void AttachedToWindow()
	{
		BCard::AttachedToWindow();
		_Apply();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_COLORS_UPDATED)
			_Apply();
		BCard::MessageReceived(message);
	}

private:
	void _AddCell(BGridLayout* grid, int32 column, int32 row,
		const char* label, const char* value)
	{
		BStringView* labelView = new BStringView("about-label", label);
		BStringView* valueView = new BStringView("about-value", value);
		make_transparent(labelView);
		make_transparent(valueView);
		BGroupView* cell = new BGroupView(B_VERTICAL, 0);
		make_transparent(cell);
		BLayoutBuilder::Group<>(cell)
			.Add(labelView)
			.Add(valueView);
		grid->AddView(cell, column, row);
		fLabels.AddItem(labelView);
		fValues.AddItem(valueView);
	}

	void _Apply()
	{
		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		rgb_color muted = tint_color(text, B_DARKEN_2_TINT);
		for (int32 i = 0; i < fLabels.CountItems(); i++)
			((BStringView*)fLabels.ItemAt(i))->SetHighColor(text);
		for (int32 i = 0; i < fValues.CountItems(); i++)
			((BStringView*)fValues.ItemAt(i))->SetHighColor(muted);
	}

	BList	fLabels;
	BList	fValues;
};


AboutWindow::AboutWindow()
	:
	BWindow(BRect(0, 0, 399, 199), B_TRANSLATE("About Device Manager"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_BIG_SPACING)
		.SetInsets(B_USE_BIG_SPACING)
		.Add(new AboutHeader())
		.Add(new AboutInfo());
	CenterOnScreen();
}
