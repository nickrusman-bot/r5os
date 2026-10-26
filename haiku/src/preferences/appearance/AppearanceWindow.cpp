/*
 * Copyright 2002-2025, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm (darkwyrm@earthlink.net)
 *		Alexander von Gluck, kallisti5@unixzen.com
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include "AppearanceWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <Path.h>
#include <SeparatorView.h>
#include <TabView.h>

#include "AntialiasingSettingsView.h"
#include "ColorsView.h"
#include "FontView.h"
#include "LookAndFeelSettingsView.h"


// This file used to be called APRWindow, left this to avoid retranslating everything
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "APRWindow"


static const uint32 kMsgSetDefaults = 'dflt';
static const uint32 kMsgRevert = 'rvrt';


AppearanceWindow::AppearanceWindow(BRect frame)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Appearance"), B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE, B_ALL_WORKSPACES)
{
	fDefaultsButton = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kMsgSetDefaults), B_WILL_DRAW);

	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert), B_WILL_DRAW);

	BTabView* tabView = new BTabView("tabview", B_WIDTH_FROM_LABEL);

	fFontSettings = new FontView(B_TRANSLATE("Fonts"));

	fColorsView = new ColorsView(B_TRANSLATE("Colors"));

	fLookAndFeelSettings = new LookAndFeelSettingsView(
		B_TRANSLATE("Look and feel"));

	fAntialiasingSettings = new AntialiasingSettingsView(
		B_TRANSLATE("Antialiasing"));

	tabView->AddTab(fFontSettings);
	tabView->AddTab(fColorsView);
	tabView->AddTab(fLookAndFeelSettings);
	tabView->AddTab(fAntialiasingSettings);
	tabView->SetBorder(B_NO_BORDER);
	fTabView = tabView;
	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("Appearance_settings");
		BFile file(settingsPath.Path(), B_READ_ONLY);
		BMessage settings;
		int32 tab = 0;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK
			&& settings.FindInt32("tab", &tab) == B_OK)
			tabView->Select(tab);
	}

	_UpdateButtons();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING)
		.Add(tabView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, 0)
			.AddGlue();
}


bool
AppearanceWindow::QuitRequested()
{
	BPath settingsPath;
	if (fTabView != NULL
		&& find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("Appearance_settings");
		BFile file(settingsPath.Path(), B_READ_WRITE | B_CREATE_FILE);
		BMessage settings;
		if (file.InitCheck() == B_OK) {
			settings.Unflatten(&file);
			settings.RemoveName("tab");
			settings.AddInt32("tab", fTabView->Selection());
			file.SetSize(0);
			file.Seek(0, SEEK_SET);
			settings.Flatten(&file);
		}
	}
	return BWindow::QuitRequested();
}


void
AppearanceWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgUpdate:
			_UpdateButtons();
			break;

		case kMsgSetDefaults:
			fFontSettings->SetDefaults();
			fColorsView->SetDefaults();
			fLookAndFeelSettings->SetDefaults();
			fAntialiasingSettings->SetDefaults();

			_UpdateButtons();
			break;

		case kMsgRevert:
			fFontSettings->Revert();
			fColorsView->Revert();
			fLookAndFeelSettings->Revert();
			fAntialiasingSettings->Revert();

			_UpdateButtons();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
AppearanceWindow::_UpdateButtons()
{
	fDefaultsButton->SetEnabled(_IsDefaultable());
	fRevertButton->SetEnabled(_IsRevertable());
}


bool
AppearanceWindow::_IsDefaultable() const
{
//	printf("fonts defaultable: %d\n", fFontSettings->IsDefaultable());
//	printf("colors defaultable: %d\n", fColorsView->IsDefaultable());
//	printf("AA defaultable: %d\n", fAntialiasingSettings->IsDefaultable());
//	printf("decor defaultable: %d\n", fLookAndFeelSettings->IsDefaultable());
	return fFontSettings->IsDefaultable()
		|| fColorsView->IsDefaultable()
		|| fLookAndFeelSettings->IsDefaultable()
		|| fAntialiasingSettings->IsDefaultable();
}


bool
AppearanceWindow::_IsRevertable() const
{
//	printf("fonts revertable: %d\n", fFontSettings->IsRevertable());
//	printf("colors revertable: %d\n", fColorsView->IsRevertable());
//	printf("AA revertable: %d\n", fAntialiasingSettings->IsRevertable());
//	printf("decor revertable: %d\n", fLookAndFeelSettings->IsRevertable());
	return fFontSettings->IsRevertable()
		|| fColorsView->IsRevertable()
		|| fLookAndFeelSettings->IsRevertable()
		|| fAntialiasingSettings->IsRevertable();
}
