/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Display page. Sets the screen mode through BScreen, the same call the
 * separate Screen program uses. That program stays installed.
 */


#include "DisplayModule.h"

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <InterfacePrivate.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <OS.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <R5UI.h>
#include <Screen.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const uint32 kMsgResolution = 'dres';
static const uint32 kMsgColors = 'dcol';
static const uint32 kMsgRefresh = 'dref';
static const uint32 kMsgScope = 'dscp';
static const uint32 kMsgColumns = 'dcln';
static const uint32 kMsgRows = 'drow';
static const uint32 kMsgApply = 'dapp';


struct DisplayChoice {
	int32		width;
	int32		height;
	color_space	space;
	float		refresh;
	bool		allWorkspaces;
	uint32		columns;
	uint32		rows;

	bool		operator==(const DisplayChoice& other) const
	{
		return width == other.width && height == other.height
			&& space == other.space
			&& fabs(refresh - other.refresh) < 0.15f
			&& allWorkspaces == other.allWorkspaces
			&& columns == other.columns && rows == other.rows;
	}

	bool		operator!=(const DisplayChoice& other) const
	{
		return !(*this == other);
	}
};


static int32
bits_per_pixel(color_space space)
{
	switch (space) {
		case B_RGB30:
			return 30;
		case B_RGB32:
		case B_RGB24:
			return 24;
		case B_RGB16:
			return 16;
		case B_RGB15:
			return 15;
		case B_CMAP8:
			return 8;
		default:
			return 0;
	}
}


static float
refresh_rate(const display_mode& mode)
{
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0)
		return 0.0f;
	return rint(10 * float(mode.timing.pixel_clock * 1000)
		/ float(mode.timing.h_total * mode.timing.v_total)) / 10.0f;
}


static void
format_refresh(float rate, BString& name)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.*g", rate >= 100.0f ? 4 : 3, rate);
	name.SetTo(buffer);
	name.Append(" Hz");
}


static void
show_marked_label(BMenu* menu, const char* fallback)
{
	if (menu == NULL || menu->Superitem() == NULL)
		return;
	BMenuItem* marked = menu->FindMarked();
	if (marked != NULL)
		menu->Superitem()->SetLabel(marked->Label());
	else if (fallback != NULL)
		menu->Superitem()->SetLabel(fallback);
}


static int
compare_mode(const void* left, const void* right)
{
	const display_mode* a = static_cast<const display_mode*>(left);
	const display_mode* b = static_cast<const display_mode*>(right);
	if (a->virtual_width != b->virtual_width)
		return (int)a->virtual_width - (int)b->virtual_width;
	if (a->virtual_height != b->virtual_height)
		return (int)a->virtual_height - (int)b->virtual_height;
	float difference = refresh_rate(*a) - refresh_rate(*b);
	if (difference < 0)
		return -1;
	if (difference > 0)
		return 1;
	return 0;
}


static void
choice_from_mode(DisplayChoice& choice, const display_mode& mode)
{
	choice.width = mode.virtual_width;
	choice.height = mode.virtual_height;
	choice.space = (color_space)mode.space;
	choice.refresh = refresh_rate(mode);
}


class DisplayModule;


class DisplayPicture : public BView {
public:
								DisplayPicture();
			void				SetPicture(const char* caption,
									rgb_color color);
	virtual	void				Draw(BRect updateRect);

private:
			BString				fCaption;
			rgb_color			fColor;
};


class DisplayView : public BGroupView {
public:
								DisplayView(DisplayModule* module);
	virtual						~DisplayView();
	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				MessageReceived(BMessage* message);
			void				ShowChoice(const DisplayChoice& choice,
									bool hardware);

private:
			void				_FillMenus();
			void				_FillRefresh();
			void				_MarkChoice();
			void				_ReadMenus();
			void				_UpdatePicture();
			void				_UpdateApply();
			void				_Edited(bool workspaces);

			DisplayModule*		fModule;
			bool				fUpdating;
			bool				fConfirming;
			BBox*				fScreenBox;
			BStringView*		fScreenLabel;
			BStringView*		fDevice;
			DisplayPicture*		fPicture;
			BSpinner*			fColumns;
			BSpinner*			fRows;
			BPopUpMenu*			fScopeMenu;
			BMenuItem*			fAllItem;
			BPopUpMenu*			fResolutionMenu;
			BPopUpMenu*			fColorsMenu;
			BPopUpMenu*			fRefreshMenu;
			BButton*			fApply;
};


class DisplayModule : public SettingsModule {
public:
								DisplayModule();
	virtual						~DisplayModule();

	virtual	const char*			Identifier() const { return "display"; }
	virtual	const char*			Name() const { return "Display"; }
	virtual	const char*			Category() const { return "system"; }
	virtual	const char*			Description() const
									{ return "Configure your screen settings"; }
	virtual	const char*			Keywords() const
		{ return "screen display resolution refresh colors workspaces"; }
	virtual	bool				RequiresAdmin() const { return true; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;

			const DisplayChoice& Selected() const { return fSelected; }
			const DisplayChoice& Active() const { return fActive; }
			display_mode*		Modes() const { return fModes; }
			uint32				ModeCount() const { return fModeCount; }
			void				UserEdited(const DisplayChoice& choice,
									bool workspaces);
			status_t			Apply();
			void				Settle(bool keep);
			void				ClearView(DisplayView* view);

private:
			bool				_Match(const DisplayChoice& choice,
									display_mode& mode) const;
			status_t			_SetMode(const display_mode& mode,
									bool all);
			void				_RestoreHardware();
			static int32		_Timeout(void* data);

			DisplayView*		fView;
			SettingsContext*	fContext;
			display_mode*		fModes;
			uint32				fModeCount;
			DisplayChoice		fOpened;
			DisplayChoice		fActive;
			DisplayChoice		fSelected;
			display_mode		fPreviousMode;
			int32				fSettled;
			bool				fTiming;
};


DisplayPicture::DisplayPicture()
	:
	BView("monitor", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fColor(make_color(40, 90, 160))
{
	SetExplicitMinSize(BSize(180, 110));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 140));
	SetExplicitPreferredSize(BSize(220, 128));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
DisplayPicture::SetPicture(const char* caption, rgb_color color)
{
	fCaption = caption != NULL ? caption : "";
	fColor = color;
	Invalidate();
}


void
DisplayPicture::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect content = r5_screen_draw(this, Bounds().InsetByCopy(2, 2), fColor);
	int32 luminance = fColor.red * 30 + fColor.green * 59 + fColor.blue * 11;
	rgb_color text = luminance < 14000
		? make_color(255, 255, 255) : make_color(0, 0, 0);
	r5_screen_caption(this, content, fCaption.String(), text);
}


DisplayModule::DisplayModule()
	:
	fView(NULL),
	fContext(NULL),
	fModes(NULL),
	fModeCount(0),
	fSettled(1),
	fTiming(false)
{
	memset(&fPreviousMode, 0, sizeof(fPreviousMode));
	fOpened.width = 0;
	fOpened.height = 0;
	fOpened.space = B_RGB32;
	fOpened.refresh = 0;
	fOpened.allWorkspaces = true;
	fOpened.columns = 1;
	fOpened.rows = 1;
	fActive = fOpened;
	fSelected = fOpened;
}


DisplayModule::~DisplayModule()
{
	free(fModes);
}


BView*
DisplayModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new DisplayView(this);
	return fView;
}


void
DisplayModule::ClearView(DisplayView* view)
{
	if (fView == view)
		fView = NULL;
}


status_t
DisplayModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	free(fModes);
	fModes = NULL;
	fModeCount = 0;

	BScreen screen;
	display_mode* modes = NULL;
	uint32 count = 0;
	if (screen.GetModeList(&modes, &count) == B_OK && modes != NULL) {
		qsort(modes, count, sizeof(display_mode), compare_mode);
		fModes = modes;
		fModeCount = count;
	}

	display_mode mode;
	if (screen.GetMode(&mode) == B_OK)
		choice_from_mode(fOpened, mode);
	else
		choice_from_mode(fOpened, display_mode());
	fOpened.allWorkspaces = true;
	BPrivate::get_workspaces_layout(&fOpened.columns, &fOpened.rows);
	fActive = fOpened;
	fSelected = fOpened;
	return B_OK;
}


status_t
DisplayModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	return B_OK;
}


status_t
DisplayModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	Settle(false);
	BPrivate::set_workspaces_layout(fOpened.columns, fOpened.rows);
	fSelected = fOpened;
	status_t status = B_OK;
	display_mode mode;
	if (_Match(fOpened, mode))
		status = _SetMode(mode, false);
	if (status == B_OK)
		fActive = fOpened;
	if (fView != NULL)
		fView->ShowChoice(fActive, true);
	return status;
}


bool
DisplayModule::IsModified() const
{
	return fSelected != fOpened;
}


void
DisplayModule::UserEdited(const DisplayChoice& choice, bool workspaces)
{
	fSelected = choice;
	if (workspaces && fContext != NULL && fContext->AdminChangeAllowed()) {
		BPrivate::set_workspaces_layout(choice.columns, choice.rows);
		uint32 columns = choice.columns;
		uint32 rows = choice.rows;
		BPrivate::get_workspaces_layout(&columns, &rows);
		fSelected.columns = columns;
		fSelected.rows = rows;
	}
	if (fView != NULL)
		fView->ShowChoice(fSelected, false);
}


bool
DisplayModule::_Match(const DisplayChoice& choice, display_mode& mode) const
{
	int32 best = -1;
	float bestDiff = 999.0f;
	for (uint32 i = 0; i < fModeCount; i++) {
		if (fModes[i].virtual_width != choice.width
			|| fModes[i].virtual_height != choice.height
			|| (color_space)fModes[i].space != choice.space)
			continue;
		float diff = fabs(refresh_rate(fModes[i]) - choice.refresh);
		if (diff < bestDiff) {
			bestDiff = diff;
			best = (int32)i;
		}
	}
	if (best < 0)
		return false;
	mode = fModes[best];
	mode.h_display_start = 0;
	mode.v_display_start = 0;
	return true;
}


status_t
DisplayModule::_SetMode(const display_mode& mode, bool all)
{
	BScreen screen;
	int32 workspace = current_workspace();
	display_mode copy = mode;
	status_t status = screen.SetMode(workspace, &copy, true);
	if (status != B_OK || !all)
		return status;
	int32 count = count_workspaces();
	for (int32 i = 0; i < count; i++) {
		if (i == workspace)
			continue;
		copy = mode;
		screen.SetMode(i, &copy, true);
	}
	return B_OK;
}


void
DisplayModule::_RestoreHardware()
{
	_SetMode(fPreviousMode, fActive.allWorkspaces);
	fSelected = fActive;
}


int32
DisplayModule::_Timeout(void* data)
{
	DisplayModule* module = static_cast<DisplayModule*>(data);
	snooze(12000000);
	if (atomic_add(&module->fSettled, 2) != 0)
		return 0;
	module->_RestoreHardware();
	return 0;
}


status_t
DisplayModule::Apply()
{
	if (fContext == NULL || !fContext->AdminChangeAllowed())
		return B_PERMISSION_DENIED;
	if (fTiming && atomic_add(&fSettled, 0) == 0)
		return B_BUSY;

	display_mode wanted;
	if (!_Match(fSelected, wanted))
		return B_ENTRY_NOT_FOUND;

	BScreen screen;
	display_mode previous;
	if (screen.GetMode(&previous) != B_OK)
		return B_ERROR;

	status_t status = _SetMode(wanted, fSelected.allWorkspaces);
	if (status != B_OK)
		return status;

	fPreviousMode = previous;
	fActive = fSelected;
	fSettled = 0;
	fTiming = true;
	thread_id thread = spawn_thread(_Timeout, "display mode",
		B_LOW_PRIORITY, this);
	if (thread >= 0)
		resume_thread(thread);
	else {
		fTiming = false;
		fSettled = 1;
	}
	return B_OK;
}


void
DisplayModule::Settle(bool keep)
{
	if (!fTiming)
		return;
	int32 add = keep ? 1 : 2;
	if (atomic_add(&fSettled, add) != 0) {
		if (keep) {
			display_mode mode;
			if (_Match(fSelected, mode))
				_SetMode(mode, fSelected.allWorkspaces);
			fActive = fSelected;
		}
		fTiming = false;
		return;
	}
	if (!keep)
		_RestoreHardware();
	fTiming = false;
}


DisplayView::DisplayView(DisplayModule* module)
	:
	BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fUpdating(false),
	fConfirming(false),
	fScreenBox(NULL),
	fScreenLabel(NULL),
	fDevice(NULL),
	fPicture(NULL),
	fColumns(NULL),
	fRows(NULL),
	fScopeMenu(NULL),
	fAllItem(NULL),
	fResolutionMenu(NULL),
	fColorsMenu(NULL),
	fRefreshMenu(NULL),
	fApply(NULL)
{
	fScreenBox = new BBox("screen");
	fScreenLabel = r5_settings_box_title("Display");
	fScreenBox->SetLabel(fScreenLabel);
	BGroupView* screenGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	screenGroup->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING);
	fScreenBox->AddChild(screenGroup);

	fDevice = new BStringView("device", "");
	fDevice->SetAlignment(B_ALIGN_CENTER);
	fPicture = new DisplayPicture();
	screenGroup->AddChild(fDevice);
	screenGroup->AddChild(fPicture);

	BBox* workBox = new BBox("workspaces");
	workBox->SetLabel(r5_settings_box_title("Workspaces"));
	BGroupLayout* workLayout = new BGroupLayout(B_VERTICAL);
	workLayout->SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	workBox->SetLayout(workLayout);
	fColumns = new BSpinner("columns", "Columns:", new BMessage(kMsgColumns));
	fRows = new BSpinner("rows", "Rows:", new BMessage(kMsgRows));
	fColumns->SetRange(1, 32);
	fRows->SetRange(1, 32);
	fColumns->SetAlignment(B_ALIGN_RIGHT);
	fRows->SetAlignment(B_ALIGN_RIGHT);
	BLayoutItem* columnText = fColumns->CreateTextViewLayoutItem();
	BLayoutItem* rowText = fRows->CreateTextViewLayoutItem();
	columnText->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
	rowText->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
	workBox->AddChild(BLayoutBuilder::Group<>()
		.AddGroup(B_HORIZONTAL, 0)
			.AddGlue()
			.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
				.Add(fColumns->CreateLabelLayoutItem(), 0, 0)
				.Add(columnText, 1, 0)
				.Add(fRows->CreateLabelLayoutItem(), 0, 1)
				.Add(rowText, 1, 1)
				.End()
			.AddGlue()
			.End()
		.View());

	fScopeMenu = new BPopUpMenu("scope", true, true);
	fAllItem = new BMenuItem("All workspaces", new BMessage(kMsgScope));
	fScopeMenu->AddItem(fAllItem);
	fScopeMenu->AddItem(new BMenuItem("Current workspace",
		new BMessage(kMsgScope)));
	fAllItem->SetMarked(true);
	BMenuField* scopeField = new BMenuField("scope field", NULL, fScopeMenu);

	fResolutionMenu = new BPopUpMenu("resolution", true, true);
	fColorsMenu = new BPopUpMenu("colors", true, true);
	fRefreshMenu = new BPopUpMenu("refresh", true, true);
	BMenuField* resolutionField = new BMenuField("resolution field",
		"Resolution:", fResolutionMenu);
	BMenuField* colorsField = new BMenuField("colors field",
		"Colors:", fColorsMenu);
	BMenuField* refreshField = new BMenuField("refresh field",
		"Refresh rate:", fRefreshMenu);
	resolutionField->SetAlignment(B_ALIGN_RIGHT);
	colorsField->SetAlignment(B_ALIGN_RIGHT);
	refreshField->SetAlignment(B_ALIGN_RIGHT);

	fApply = new BButton("apply", "Apply", new BMessage(kMsgApply));
	fApply->SetEnabled(false);

	BBox* controls = new BBox("controls");
	controls->SetLabel(scopeField);
	BGroupView* controlsGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	controlsGroup->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING);
	controls->AddChild(controlsGroup);
	BLayoutBuilder::Group<>(controlsGroup)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(resolutionField->CreateLabelLayoutItem(), 0, 0)
			.Add(resolutionField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(colorsField->CreateLabelLayoutItem(), 0, 1)
			.Add(colorsField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(refreshField->CreateLabelLayoutItem(), 0, 2)
			.Add(refreshField->CreateMenuBarLayoutItem(), 1, 2)
			.End()
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApply)
			.End();

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
			.Add(fScreenBox)
			.Add(workBox)
			.End()
		.Add(controls);
}


void
DisplayView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fColumns->SetTarget(this);
	fRows->SetTarget(this);
	fApply->SetTarget(this);
	fScopeMenu->SetTargetForItems(this);
}


void
DisplayView::AllAttached()
{
	BGroupView::AllAttached();
	_FillMenus();
	if (fModule != NULL)
		ShowChoice(fModule->Selected(), true);
}


void
DisplayView::_FillMenus()
{
	if (fModule == NULL)
		return;
	fUpdating = true;
	while (fResolutionMenu->CountItems() > 0)
		delete fResolutionMenu->RemoveItem((int32)0);
	while (fColorsMenu->CountItems() > 0)
		delete fColorsMenu->RemoveItem((int32)0);

	int32 previousWidth = -1;
	int32 previousHeight = -1;
	bool spaces[6];
	memset(spaces, 0, sizeof(spaces));
	color_space spaceOrder[] = {
		B_CMAP8, B_RGB15, B_RGB16, B_RGB24, B_RGB32, B_RGB30
	};
	for (uint32 i = 0; i < fModule->ModeCount(); i++) {
		display_mode mode = fModule->Modes()[i];
		for (int32 s = 0; s < 6; s++) {
			if ((color_space)mode.space == spaceOrder[s])
				spaces[s] = true;
		}
		if (mode.virtual_width == previousWidth
			&& mode.virtual_height == previousHeight)
			continue;
		previousWidth = mode.virtual_width;
		previousHeight = mode.virtual_height;
		BMessage* message = new BMessage(kMsgResolution);
		message->AddInt32("width", mode.virtual_width);
		message->AddInt32("height", mode.virtual_height);
		BString name;
		name.SetToFormat("%" B_PRId32 " × %" B_PRId32,
			mode.virtual_width, mode.virtual_height);
		fResolutionMenu->AddItem(new BMenuItem(name.String(), message));
	}
	for (int32 s = 0; s < 6; s++) {
		if (!spaces[s])
			continue;
		BMessage* message = new BMessage(kMsgColors);
		message->AddInt32("space", spaceOrder[s]);
		BString name;
		name.SetToFormat("%" B_PRId32 " bits/pixel",
			bits_per_pixel(spaceOrder[s]));
		fColorsMenu->AddItem(new BMenuItem(name.String(), message));
	}
	fResolutionMenu->SetTargetForItems(this);
	fColorsMenu->SetTargetForItems(this);
	fUpdating = false;
}


void
DisplayView::_FillRefresh()
{
	if (fModule == NULL)
		return;
	fUpdating = true;
	while (fRefreshMenu->CountItems() > 0)
		delete fRefreshMenu->RemoveItem((int32)0);

	DisplayChoice choice = fModule->Selected();
	float seen = -1.0f;
	for (uint32 i = 0; i < fModule->ModeCount(); i++) {
		display_mode mode = fModule->Modes()[i];
		if (mode.virtual_width != choice.width
			|| mode.virtual_height != choice.height
			|| (color_space)mode.space != choice.space)
			continue;
		float rate = refresh_rate(mode);
		if (seen >= 0.0f && fabs(seen - rate) < 0.05f)
			continue;
		seen = rate;
		BMessage* message = new BMessage(kMsgRefresh);
		message->AddFloat("refresh", rate);
		BString name;
		format_refresh(rate, name);
		fRefreshMenu->AddItem(new BMenuItem(name.String(), message));
	}
	if (fRefreshMenu->CountItems() == 0) {
		BString name;
		format_refresh(choice.refresh, name);
		BMessage* message = new BMessage(kMsgRefresh);
		message->AddFloat("refresh", choice.refresh);
		BMenuItem* item = new BMenuItem(name.String(), message);
		fRefreshMenu->AddItem(item);
		item->SetEnabled(false);
	}
	fRefreshMenu->SetTargetForItems(this);
	fUpdating = false;
}


void
DisplayView::_MarkChoice()
{
	if (fModule == NULL)
		return;
	DisplayChoice choice = fModule->Selected();
	fUpdating = true;
	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++) {
		BMenuItem* item = fResolutionMenu->ItemAt(i);
		BMessage* message = item->Message();
		int32 width = 0;
		int32 height = 0;
		if (message != NULL) {
			message->FindInt32("width", &width);
			message->FindInt32("height", &height);
		}
		item->SetMarked(width == choice.width && height == choice.height);
	}
	for (int32 i = 0; i < fColorsMenu->CountItems(); i++) {
		BMenuItem* item = fColorsMenu->ItemAt(i);
		int32 space = 0;
		if (item->Message() != NULL)
			item->Message()->FindInt32("space", &space);
		item->SetMarked((color_space)space == choice.space);
	}
	BMenuItem* refreshMarked = NULL;
	float best = 999.0f;
	for (int32 i = 0; i < fRefreshMenu->CountItems(); i++) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		float rate = 0;
		if (item->Message() != NULL)
			item->Message()->FindFloat("refresh", &rate);
		float diff = fabs(rate - choice.refresh);
		item->SetMarked(false);
		if (diff < best) {
			best = diff;
			refreshMarked = item;
		}
	}
	if (refreshMarked != NULL)
		refreshMarked->SetMarked(true);

	BString resolutionLabel;
	resolutionLabel.SetToFormat("%" B_PRId32 " × %" B_PRId32,
		choice.width, choice.height);
	show_marked_label(fResolutionMenu, resolutionLabel.String());
	BString colorLabel;
	colorLabel.SetToFormat("%" B_PRId32 " bits/pixel",
		bits_per_pixel(choice.space));
	show_marked_label(fColorsMenu, colorLabel.String());
	BString refreshLabel;
	format_refresh(choice.refresh, refreshLabel);
	show_marked_label(fRefreshMenu, refreshLabel.String());

	if (fAllItem != NULL)
		fAllItem->SetMarked(choice.allWorkspaces);
	if (fScopeMenu->ItemAt(1) != NULL)
		fScopeMenu->ItemAt(1)->SetMarked(!choice.allWorkspaces);
	fColumns->SetValue(choice.columns);
	fRows->SetValue(choice.rows);
	fUpdating = false;
}


void
DisplayView::_UpdatePicture()
{
	if (fModule == NULL)
		return;
	DisplayChoice choice = fModule->Selected();
	BScreen screen;
	rgb_color desktop = screen.DesktopColor();
	BString caption;
	monitor_info info;
	float inches = 0;
	if (screen.GetMonitorInfo(&info) == B_OK && info.width > 0
		&& info.height > 0) {
		double xInches = info.width / 2.54;
		double yInches = info.height / 2.54;
		if (xInches > 0 && yInches > 0) {
			int32 dpi = (int32)round((choice.width / xInches
				+ choice.height / yInches) / 2.0);
			if (dpi > 0)
				caption.SetToFormat("%" B_PRId32 " dpi", dpi);
		}
		inches = (float)(round(sqrt(info.width * info.width
			+ info.height * info.height) / 0.254) / 10.0);
		BString label;
		label.SetToFormat("%g\"", inches);
		fScreenLabel->SetText(label.String());
	} else
		fScreenLabel->SetText("Display");
	fScreenBox->InvalidateLayout();

	accelerant_device_info device;
	if (screen.GetDeviceInfo(&device) == B_OK) {
		BString text;
		if (device.name[0] != '\0' && device.chipset[0] != '\0')
			text.SetToFormat("%s (%s)", device.name, device.chipset);
		else if (device.name[0] != '\0')
			text = device.name;
		else
			text = device.chipset;
		fDevice->SetText(text.String());
	} else
		fDevice->SetText("");
	fPicture->SetPicture(caption.String(), desktop);
}


void
DisplayView::_UpdateApply()
{
	if (fApply == NULL || fModule == NULL)
		return;
	bool same = fModule->Selected().width == fModule->Active().width
		&& fModule->Selected().height == fModule->Active().height
		&& fModule->Selected().space == fModule->Active().space
		&& fabs(fModule->Selected().refresh - fModule->Active().refresh)
			< 0.15f
		&& fModule->Selected().allWorkspaces
			== fModule->Active().allWorkspaces;
	fApply->SetEnabled(!same && !fConfirming);
}


void
DisplayView::ShowChoice(const DisplayChoice& choice, bool hardware)
{
	(void)choice;
	(void)hardware;
	_FillRefresh();
	_MarkChoice();
	_UpdatePicture();
	_UpdateApply();
}


void
DisplayView::_ReadMenus()
{
	if (fModule == NULL || fUpdating)
		return;
	DisplayChoice choice = fModule->Selected();
	BMenuItem* resolution = fResolutionMenu->FindMarked();
	if (resolution != NULL && resolution->Message() != NULL) {
		resolution->Message()->FindInt32("width", &choice.width);
		resolution->Message()->FindInt32("height", &choice.height);
	}
	BMenuItem* colors = fColorsMenu->FindMarked();
	if (colors != NULL && colors->Message() != NULL) {
		int32 space = choice.space;
		colors->Message()->FindInt32("space", &space);
		choice.space = (color_space)space;
	}
	BMenuItem* refresh = fRefreshMenu->FindMarked();
	if (refresh != NULL && refresh->Message() != NULL)
		refresh->Message()->FindFloat("refresh", &choice.refresh);
	choice.allWorkspaces = fAllItem != NULL && fAllItem->IsMarked();
	choice.columns = (uint32)fColumns->Value();
	choice.rows = (uint32)fRows->Value();
	fModule->UserEdited(choice, false);
}


void
DisplayView::_Edited(bool workspaces)
{
	if (fUpdating || fModule == NULL)
		return;
	DisplayChoice choice = fModule->Selected();
	choice.columns = (uint32)fColumns->Value();
	choice.rows = (uint32)fRows->Value();
	_ReadMenus();
	if (workspaces) {
		choice = fModule->Selected();
		choice.columns = (uint32)fColumns->Value();
		choice.rows = (uint32)fRows->Value();
		fModule->UserEdited(choice, true);
	}
}


void
DisplayView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshPage:
			_UpdateApply();
			break;
		case kMsgResolution:
		case kMsgColors:
		case kMsgRefresh:
		case kMsgScope:
			_ReadMenus();
			break;
		case kMsgColumns:
		case kMsgRows:
			_Edited(true);
			break;
		case kMsgApply:
		{
			if (fModule == NULL || fConfirming)
				break;
			status_t status = fModule->Apply();
			if (status != B_OK) {
				BString text("The screen mode could not be set.\n");
				text << strerror(status);
				BAlert* alert = new BAlert("Display", text.String(), "OK",
					NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}
			fConfirming = true;
			_UpdateApply();
			BAlert* alert = new BAlert("Display",
				"Do you wish to keep these settings?\n"
				"The previous picture returns after 12 seconds.",
				"Undo", "Keep", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			int32 which = alert->Go();
			fConfirming = false;
			fModule->Settle(which == 1);
			ShowChoice(fModule->Selected(), true);
			break;
		}
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


DisplayView::~DisplayView()
{
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
register_display_page(SettingsManager& manager)
{
	manager.Add(new DisplayModule());
}
