/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Manrope for this application. The system fonts stay as they are.
 * The files are OFL-1.1 and are not modified.
 */


#include "SettingsFont.h"

#include <FindDirectory.h>
#include <Font.h>
#include <Path.h>
#include <View.h>

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

	if (load_face(*sPlain, "manrope-regular.ttf") == B_OK)
		sPlain->SetSize(plainSize);
	else {
		*sPlain = *be_plain_font;
		syslog(LOG_ERR, "settings: Manrope Regular was not loaded\n");
	}

	if (load_face(*sBold, "manrope-bold.ttf") == B_OK)
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


void
r5_settings_apply_plain(BView* view)
{
	if (view == NULL)
		return;
	view->SetFont(r5_settings_plain_font());
	for (int32 i = 0; i < view->CountChildren(); i++)
		r5_settings_apply_plain(view->ChildAt(i));
}
