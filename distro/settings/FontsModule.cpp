/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Fonts page. The rows and the antialiasing controls are the ones from
 * the Appearance preference, with antialiasing in a box.
 */


#include "SettingsFont.h"
#include "SettingsModule.h"

#include "AntialiasingSettingsView.h"
#include "AppearanceNotify.h"
#include "FontView.h"

#include <Alignment.h>
#include <Box.h>
#include <StringView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>


class FontsPage : public BGroupView {
public:
								FontsPage(class FontsModule* module);
	virtual						~FontsPage();
	virtual	void				AllAttached();
	virtual	void				MessageReceived(BMessage* message);
			void				Revert();
			void				Defaults();
			bool				Modified() const;
			bool				CanDefaults() const;

private:
			FontsModule*		fModule;
			FontView*			fFonts;
			AntialiasingSettingsView* fAntialias;
};


class FontsModule : public SettingsModule {
public:
								FontsModule();
	virtual	const char*			Identifier() const { return "fonts"; }
	virtual	const char*			Name() const { return "Fonts"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
									{ return "Change the default fonts"; }
	virtual	const char*			Keywords() const
		{ return "fonts plain bold fixed menu antialiasing hinting"; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);
			void				ClearPage(FontsPage* page);
			void				Touch();

private:
			FontsPage*			fPage;
			SettingsContext*	fContext;
};


static void
keep_height(BView* view)
{
	BSize size = view->PreferredSize();
	if (size.height > 1.0f)
		view->SetExplicitMinSize(BSize(B_SIZE_UNSET, size.height));
	view->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));
}


FontsPage::FontsPage(FontsModule* module)
	:
	BGroupView("fonts page", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fFonts(NULL),
	fAntialias(NULL)
{
	fFonts = new FontView("fonts");
	fAntialias = new AntialiasingSettingsView("antialiasing");
	keep_height(fFonts);
	BBox* alias = new BBox("antialiasing box");
	alias->SetLabel(r5_settings_box_title("Antialiasing"));
	alias->AddChild(fAntialias);
	alias->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));
	BLayoutBuilder::Group<>(this)
		.Add(fFonts, 0)
		.Add(alias, 0)
		.AddGlue();
}


void
FontsPage::AllAttached()
{
	BGroupView::AllAttached();
	keep_height(fFonts);
	InvalidateLayout();
}


FontsPage::~FontsPage()
{
	if (fModule != NULL)
		fModule->ClearPage(this);
}


void
FontsPage::MessageReceived(BMessage* message)
{
	if (message->what == kMsgUpdate) {
		if (fModule != NULL)
			fModule->Touch();
		return;
	}
	BGroupView::MessageReceived(message);
}


void
FontsPage::Revert()
{
	fFonts->Revert();
	fAntialias->Revert();
}


void
FontsPage::Defaults()
{
	fFonts->SetDefaults();
	fAntialias->SetDefaults();
}


bool
FontsPage::Modified() const
{
	return fFonts->IsRevertable() || fAntialias->IsRevertable();
}


bool
FontsPage::CanDefaults() const
{
	return fFonts->IsDefaultable() || fAntialias->IsDefaultable();
}


FontsModule::FontsModule()
	:
	fPage(NULL),
	fContext(NULL)
{
}


BView*
FontsModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fPage = new FontsPage(this);
	return fPage;
}


void
FontsModule::ClearPage(FontsPage* page)
{
	if (fPage == page)
		fPage = NULL;
}


void
FontsModule::Touch()
{
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
FontsModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	return B_OK;
}


status_t
FontsModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
FontsModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Revert();
	return B_OK;
}


bool
FontsModule::IsModified() const
{
	return fPage != NULL && fPage->Modified();
}


bool
FontsModule::CanApplyDefaults() const
{
	return fPage != NULL && fPage->CanDefaults();
}


status_t
FontsModule::ApplyDefaults(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Defaults();
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
	return new FontsModule();
}
