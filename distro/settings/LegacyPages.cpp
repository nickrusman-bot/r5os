/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Launchers for the preference programs that already exist.
 * They stay separate windows. This file does not embed them.
 */


#include "LegacyPages.h"

#include "SettingsManager.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Button.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <String.h>
#include <StringView.h>

#include <string.h>


class LegacyPage;


class LegacyModule : public SettingsModule {
public:
								LegacyModule(const char* identifier,
									const char* name, const char* category,
									const char* description,
									const char* keywords,
									const char* signature);
	virtual						~LegacyModule();

	virtual	const char*			Identifier() const { return fId.String(); }
	virtual	const char*			Name() const { return fName.String(); }
	virtual	const char*			Category() const { return fCategory.String(); }
	virtual	const char*			Description() const
									{ return fDescription.String(); }
	virtual	const char*			Keywords() const { return fKeywords.String(); }
	virtual	bool				CanSave() const { return false; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);

			const char*			Signature() const { return fSignature.String(); }

private:
			BString				fId;
			BString				fName;
			BString				fCategory;
			BString				fDescription;
			BString				fKeywords;
			BString				fSignature;
			LegacyPage*			fPage;
};


class LegacyPage : public BGroupView {
public:
								LegacyPage(LegacyModule* module,
									SettingsContext* context);
	virtual	void				MessageReceived(BMessage* message);

private:
			LegacyModule*		fModule;
			SettingsContext*	fContext;
};


static const uint32 kMsgOpenLegacy = 'r5sl';


LegacyPage::LegacyPage(LegacyModule* module, SettingsContext* context)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fContext(context)
{
	BString text(module->Description());
	text << " This still opens the existing program. Its settings file is left where it is.";
	BStringView* body = new BStringView("legacy", text.String());
	body->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	BButton* open = new BButton("open", "Open", new BMessage(kMsgOpenLegacy));
	BLayoutBuilder::Group<>(this)
		.Add(body)
		.Add(open)
		.AddGlue();
}


void
LegacyPage::MessageReceived(BMessage* message)
{
	if (message->what != kMsgOpenLegacy || fContext == NULL
		|| fModule == NULL) {
		BGroupView::MessageReceived(message);
		return;
	}
	status_t status = fContext->LaunchLegacy(fModule->Signature());
	if (status != B_OK) {
		BString error("Could not open ");
		error << fModule->Name() << ".\n" << strerror(status);
		BAlert* alert = new BAlert("Settings", error.String(), "OK",
			NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go(NULL);
	}
}


LegacyModule::LegacyModule(const char* identifier, const char* name,
	const char* category, const char* description, const char* keywords,
	const char* signature)
	:
	fId(identifier),
	fName(name),
	fCategory(category),
	fDescription(description),
	fKeywords(keywords),
	fSignature(signature),
	fPage(NULL)
{
}


LegacyModule::~LegacyModule()
{
}


BView*
LegacyModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fPage = new LegacyPage(this, context);
	return fPage;
}


status_t
LegacyModule::LoadSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
LegacyModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
LegacyModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


void
register_legacy_pages(SettingsManager& manager)
{
	struct entry {
		const char* id;
		const char* name;
		const char* category;
		const char* description;
		const char* keywords;
		const char* signature;
	};
	const entry pages[] = {
		{ "colors", "Colors and fonts", "personalization",
			"Colors, fonts, and the window look.",
			"appearance colors fonts antialiasing",
			"application/x-vnd.Haiku-Appearance" },
		{ "deskbar", "Desktop and Deskbar", "personalization",
			"Deskbar size, clock, and recent items.",
			"deskbar desktop menu clock",
			"application/x-vnd.Haiku-DeskbarPreferences" },
		{ "wallpaper", "Wallpaper", "personalization",
			"The picture behind the desktop icons.",
			"background wallpaper image",
			"application/x-vnd.Haiku-Backgrounds" },
		{ "display", "Display", "system",
			"Resolution, refresh, and the workspace color.",
			"screen resolution monitor",
			"application/x-vnd.Haiku-Screen" },
		{ "sound", "Sound", "system",
			"Event sounds and the output device.",
			"audio volume media",
			"application/x-vnd.Haiku-Sounds" },
		{ "keyboard", "Keyboard", "devices",
			"Keymap and the typing repeat rate.",
			"keymap layout keys",
			"application/x-vnd.Haiku-Keymap" },
		{ "mouse", "Mouse", "devices",
			"Mouse speed, focus, and the scroll wheel.",
			"input pointer focus",
			"application/x-vnd.Haiku-Input" },
		{ "bluetooth", "Bluetooth", "devices",
			"Bluetooth devices.",
			"bluetooth wireless",
			"application/x-vnd.Haiku-BluetoothPrefs" },
		{ "printers", "Printers", "devices",
			"Printers and the print queue.",
			"print printer",
			"application/x-vnd.Be-PRNT" },
		{ "network", "Network", "network",
			"Interfaces, DNS, and the hostname.",
			"wifi ethernet ipv6",
			"application/x-vnd.Haiku-Network" },
		{ "accounts", "Users and Accounts", "accounts",
			"People on this computer.",
			"user password administrator",
			"application/x-vnd.R5OS-Users" }
	};
	for (uint32 i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
		manager.Add(new LegacyModule(pages[i].id, pages[i].name,
			pages[i].category, pages[i].description, pages[i].keywords,
			pages[i].signature));
	}
}
