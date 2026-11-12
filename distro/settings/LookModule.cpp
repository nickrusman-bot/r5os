/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Look and feel page. Decorator, control look, and arrow style are the
 * Appearance preference controls. Colors is a box under them.
 */


#include "SettingsFont.h"
#include "SettingsModule.h"

#include "AppearanceNotify.h"
#include "ColorsView.h"
#include "LookAndFeelSettingsView.h"

#include <Alignment.h>
#include <Box.h>
#include <StringView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>


class LookPage : public BGroupView {
public:
								LookPage(class LookModule* module);
	virtual						~LookPage();
	virtual	void				AllAttached();
	virtual	void				MessageReceived(BMessage* message);
			void				Revert();
			void				Defaults();
			bool				Modified() const;
			bool				CanDefaults() const;

private:
			LookModule*			fModule;
			LookAndFeelSettingsView* fLook;
			ColorsView*			fColors;
};


class LookModule : public SettingsModule {
public:
								LookModule();
	virtual	const char*			Identifier() const { return "look"; }
	virtual	const char*			Name() const { return "Look and Feel"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
		{ return "Change the default decorator and colors"; }
	virtual	const char*			Keywords() const
		{ return "decorator colors control look scrollbar appearance"; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);
			void				ClearPage(LookPage* page);
			void				Touch();

private:
			LookPage*			fPage;
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


LookPage::LookPage(LookModule* module)
	:
	BGroupView("look page", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fLook(NULL),
	fColors(NULL)
{
	fLook = new LookAndFeelSettingsView("look");
	fColors = new ColorsView("colors");
	keep_height(fLook);
	BBox* colors = new BBox("colors box");
	colors->SetLabel(r5_settings_box_title("Colors"));
	colors->AddChild(fColors);
	colors->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_USE_FULL_HEIGHT));
	BLayoutBuilder::Group<>(this)
		.Add(fLook, 0)
		.Add(colors);
}


void
LookPage::AllAttached()
{
	BGroupView::AllAttached();
	keep_height(fLook);
	InvalidateLayout();
}


LookPage::~LookPage()
{
	if (fModule != NULL)
		fModule->ClearPage(this);
}


void
LookPage::MessageReceived(BMessage* message)
{
	if (message->what == kMsgUpdate) {
		if (fModule != NULL)
			fModule->Touch();
		return;
	}
	BGroupView::MessageReceived(message);
}


void
LookPage::Revert()
{
	fLook->Revert();
	fColors->Revert();
}


void
LookPage::Defaults()
{
	fLook->SetDefaults();
	fColors->SetDefaults();
}


bool
LookPage::Modified() const
{
	return fLook->IsRevertable() || fColors->IsRevertable();
}


bool
LookPage::CanDefaults() const
{
	return fLook->IsDefaultable() || fColors->IsDefaultable();
}


LookModule::LookModule()
	:
	fPage(NULL),
	fContext(NULL)
{
}


BView*
LookModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fPage = new LookPage(this);
	return fPage;
}


void
LookModule::ClearPage(LookPage* page)
{
	if (fPage == page)
		fPage = NULL;
}


void
LookModule::Touch()
{
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
LookModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	return B_OK;
}


status_t
LookModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
LookModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Revert();
	return B_OK;
}


bool
LookModule::IsModified() const
{
	return fPage != NULL && fPage->Modified();
}


bool
LookModule::CanApplyDefaults() const
{
	return fPage != NULL && fPage->CanDefaults();
}


status_t
LookModule::ApplyDefaults(SettingsContext* context)
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
	return new LookModule();
}
