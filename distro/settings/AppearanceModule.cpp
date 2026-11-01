/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Native Appearance page. Stores one R5OS setting of its own.
 * Colors and fonts stay in the existing Appearance preferences.
 */


#include "SettingsModule.h"

#include <Button.h>
#include <CheckBox.h>
#include <Control.h>
#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <String.h>
#include <StringView.h>

#include <string.h>
#include <syslog.h>


static const uint32 kMsgLargeSample = 'r5as';
static const uint32 kMsgOpenColors = 'r5ao';
static const char* kModuleId = "appearance";


class AppearanceModule;


class AppearanceView : public BGroupView {
public:
								AppearanceView(AppearanceModule* module,
									SettingsContext* context);
	virtual						~AppearanceView();
	virtual	void				MessageReceived(BMessage* message);
			void				Apply(bool large);

private:
			AppearanceModule*	fModule;
			SettingsContext*	fContext;
			BCheckBox*			fLarge;
			BStringView*		fSample;
};


class AppearanceModule : public SettingsModule {
public:
								AppearanceModule();
	virtual						~AppearanceModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Appearance"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
		{ return "A sample label size for this page. Colors and fonts stay in the existing Appearance preferences."; }
	virtual	const char*			Keywords() const
									{ return "sample text size appearance"; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const { return fModified; }

			void				SetLarge(bool large);
			bool				Large() const { return fLarge; }
			void				ClearView(AppearanceView* view);

private:
			void				_Push();

			AppearanceView*		fView;
			SettingsContext*	fContext;
			bool				fLarge;
			bool				fSaved;
			bool				fModified;
};


AppearanceView::AppearanceView(AppearanceModule* module,
	SettingsContext* context)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fContext(context),
	fLarge(NULL),
	fSample(NULL)
{
	fLarge = new BCheckBox("large", "Larger sample text",
		new BMessage(kMsgLargeSample));
	fSample = new BStringView("sample", "Sample");
	BButton* colors = new BButton("colors", "Open colors and fonts",
		new BMessage(kMsgOpenColors));
	BLayoutBuilder::Group<>(this)
		.Add(fLarge)
		.Add(fSample)
		.Add(colors)
		.AddGlue();
	Apply(module->Large());
}


AppearanceView::~AppearanceView()
{
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
AppearanceView::Apply(bool large)
{
	if (fLarge != NULL)
		fLarge->SetValue(large ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fSample == NULL)
		return;
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() + (large ? 6.0f : 0.0f));
	fSample->SetFont(&font);
	fSample->Invalidate();
}


void
AppearanceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgLargeSample:
			if (fModule != NULL)
				fModule->SetLarge(fLarge->Value() == B_CONTROL_ON);
			break;
		case kMsgOpenColors:
		{
			status_t status = B_NO_INIT;
			if (fContext != NULL) {
				status = fContext->LaunchLegacy(
					"application/x-vnd.Haiku-Appearance");
			}
			if (status != B_OK) {
				syslog(LOG_ERR, "settings appearance launch: %s",
					strerror(status));
			}
			break;
		}
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


AppearanceModule::AppearanceModule()
	:
	fView(NULL),
	fContext(NULL),
	fLarge(false),
	fSaved(false),
	fModified(false)
{
}


AppearanceModule::~AppearanceModule()
{
}


BView*
AppearanceModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new AppearanceView(this, context);
	return fView;
}


void
AppearanceModule::ClearView(AppearanceView* view)
{
	if (fView == view)
		fView = NULL;
}


void
AppearanceModule::SetLarge(bool large)
{
	if (fLarge == large)
		return;
	fLarge = large;
	fModified = fLarge != fSaved;
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


void
AppearanceModule::_Push()
{
	if (fView != NULL)
		fView->Apply(fLarge);
}


status_t
AppearanceModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fLarge = false;
	if (context != NULL) {
		BMessage settings;
		status_t status = context->ReadSettings(Identifier(), settings);
		if (status == B_OK)
			settings.FindBool("large_sample", &fLarge);
	}
	fSaved = fLarge;
	fModified = false;
	_Push();
	return B_OK;
}


status_t
AppearanceModule::SaveSettings(SettingsContext* context)
{
	if (context == NULL)
		return B_NO_INIT;
	if (RequiresAdmin() && !context->AdminChangeAllowed())
		return B_NOT_ALLOWED;
	BMessage settings;
	settings.AddBool("large_sample", fLarge);
	status_t status = context->WriteSettings(Identifier(), settings);
	if (status != B_OK)
		return status;
	fSaved = fLarge;
	fModified = false;
	return B_OK;
}


status_t
AppearanceModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	fLarge = fSaved;
	fModified = false;
	_Push();
	return B_OK;
}


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new AppearanceModule();
}
