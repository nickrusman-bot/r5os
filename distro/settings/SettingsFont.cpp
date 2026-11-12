/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Manrope for this application. The system fonts stay as they are.
 * The files are OFL-1.1 and are not modified.
 */


#include "SettingsFont.h"

#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Font.h>
#include <Path.h>
#include <StringView.h>
#include <View.h>

#include <string.h>
#include <syslog.h>


static BFont* sPlain = NULL;
static BFont* sBold = NULL;
static BFont* sTitle = NULL;


static status_t
load_face(BFont& font, const char* fileName)
{
	BPath path;
	status_t status = find_directory(B_SYSTEM_DATA_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	status = path.Append("settings/fonts");
	if (status != B_OK)
		return status;
	status = path.Append(fileName);
	if (status != B_OK)
		return status;
	return font.LoadFont(path.Path());
}


static void
ensure_fonts()
{
	if (sPlain != NULL)
		return;

	float plainSize = be_plain_font->Size();
	float boldSize = be_bold_font->Size();
	sPlain = new BFont(be_plain_font);
	sBold = new BFont(be_bold_font);

	if (sPlain->SetFamilyAndStyle("Manrope", "Regular") == B_OK)
		sPlain->SetSize(plainSize);
	else if (load_face(*sPlain, "manrope-regular.ttf") == B_OK)
		sPlain->SetSize(plainSize);
	else {
		*sPlain = *be_plain_font;
		syslog(LOG_ERR, "settings: Manrope Regular was not loaded\n");
	}

	if (sBold->SetFamilyAndStyle("Manrope", "Bold") == B_OK)
		sBold->SetSize(boldSize);
	else if (load_face(*sBold, "manrope-bold.ttf") == B_OK)
		sBold->SetSize(boldSize);
	else {
		*sBold = *be_bold_font;
		syslog(LOG_ERR, "settings: Manrope Bold was not loaded\n");
	}

	sTitle = new BFont(sBold);
	sTitle->SetSize(boldSize + 8);
}


const BFont*
r5_settings_plain_font()
{
	ensure_fonts();
	return sPlain;
}


const BFont*
r5_settings_bold_font()
{
	ensure_fonts();
	return sBold;
}


const BFont*
r5_settings_title_font()
{
	ensure_fonts();
	return sTitle;
}


BStringView*
r5_settings_box_title(const char* text)
{
	BStringView* title = new BStringView("box title", text);
	title->SetFont(r5_settings_bold_font());
	return title;
}


rgb_color
r5_settings_tile_fill()
{
	return mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 100);
}


static bool
keeps_face(BView* view, const BFont* face)
{
	if (view == NULL || face == NULL)
		return false;
	BFont current;
	view->GetFont(&current);
	font_family leftFamily;
	font_style leftStyle;
	font_family rightFamily;
	font_style rightStyle;
	current.GetFamilyAndStyle(&leftFamily, &leftStyle);
	face->GetFamilyAndStyle(&rightFamily, &rightStyle);
	// Face() does not round-trip through SetFont. Family and style do.
	return current.Size() == face->Size()
		&& strcmp(leftFamily, rightFamily) == 0
		&& strcmp(leftStyle, rightStyle) == 0;
}


void
r5_settings_apply_plain(BView* view)
{
	if (view == NULL)
		return;
	// Box titles and the two bold checkboxes set their font first.
	// This walk must leave those faces in place.
	const char* name = view->Name();
	bool boxTitle = name != NULL && strcmp(name, "box title") == 0;
	if (!boxTitle
		&& !keeps_face(view, r5_settings_bold_font())
		&& !keeps_face(view, r5_settings_title_font()))
		view->SetFont(r5_settings_plain_font());
	for (int32 i = 0; i < view->CountChildren(); i++)
		r5_settings_apply_plain(view->ChildAt(i));
}
