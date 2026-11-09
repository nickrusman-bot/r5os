/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Display page. Sets the screen mode through BScreen, the same call the
 * separate Screen program uses. That program stays installed.
 */


#include "DisplayModule.h"

#include "DisplayScreens.h"
#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <InterfacePrivate.h>
#include <LayoutBuilder.h>
#include <GridView.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <OS.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScreenArrangement.h>
#include <Spinner.h>
#include <String.h>
#include <StringView.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


using BAppearanceKit::BScreenArrangement;
using BAppearanceKit::screen_tile;


static const uint32 kMsgResolution = 'dres';
static const uint32 kMsgColors = 'dcol';
static const uint32 kMsgRefresh = 'dref';
static const uint32 kMsgScope = 'dscp';
static const uint32 kMsgColumns = 'dcln';
static const uint32 kMsgRows = 'drow';
static const uint32 kMsgApply = 'dapp';
static const uint32 kMsgDisplayChoice = 'ddch';
static const uint32 kMsgDisplayTimeout = 'ddto';
static const uint32 kMsgScreen = 'dscr';


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


static float
unit()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 13.0f;
	return size / 13.0f;
}


static rgb_color
quiet_color(uint8 amount)
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), amount);
}


// Same light fill as the Information tiles.
static rgb_color
box_fill()
{
	return mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_BACKGROUND_COLOR), 100);
}


static rgb_color
box_border()
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
}


static BFont
sized_font(float size, bool bold)
{
	BFont font(bold ? be_bold_font : be_plain_font);
	font.SetSize(size);
	return font;
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


class DisplayModule;


class WorkspaceGrid : public BView {
public:
								WorkspaceGrid();
			void				SetGrid(uint32 columns, uint32 rows);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	BSize				MinSize();
	virtual	BSize				PreferredSize();

private:
			BRect				_Cell(int32 column, int32 row) const;
			BSize				_Size() const;

			uint32				fColumns;
			uint32				fRows;
			BMessageFilter*		fFilter;
};


class WorkspaceWatch : public BMessageFilter {
public:
								WorkspaceWatch(WorkspaceGrid* grid)
									:
									BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
									fGrid(grid)
								{
								}

	virtual	filter_result		Filter(BMessage* message, BHandler** target)
								{
									(void)target;
									if (message->what == B_WORKSPACE_ACTIVATED
										|| message->what
											== B_WORKSPACES_CHANGED)
										fGrid->Invalidate();
									return B_DISPATCH_MESSAGE;
								}

private:
			WorkspaceGrid*		fGrid;
};


class ArrangementCard : public BView {
public:
								ArrangementCard()
									:
									BView("arrangement", B_WILL_DRAW
										| B_FULL_UPDATE_ON_RESIZE)
								{
									SetViewColor(ui_color(
										B_DOCUMENT_BACKGROUND_COLOR));
								}

	virtual	void				Draw(BRect updateRect)
								{
									(void)updateRect;
									SetHighColor(ui_color(
										B_DOCUMENT_BACKGROUND_COLOR));
									FillRect(Bounds());
									SetHighColor(box_border());
									StrokeRect(Bounds());
								}

	virtual	void				MessageReceived(BMessage* message)
								{
									if (message->what == B_COLORS_UPDATED)
										Invalidate();
									BView::MessageReceived(message);
								}
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
			void				_ApplySizes();
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
			BAlert*				fAlert;
			BMessageRunner*		fConfirmRunner;
			ArrangementCard*	fArrangement;
			BStringView*		fArrangeTitle;
			BStringView*		fArrangeHint;
			BScreenArrangement*	fScreens;
			BGroupView*			fScreenGroup;
			BBox*				fScreenBox;
			BGroupView*			fWorkGroup;
			BStringView*		fScreenLabel;
			BMenuField*			fResolutionField;
			BMenuField*			fColorsField;
			BMenuField*			fRefreshField;
			BLayoutItem*		fColorsLabelItem;
			BLayoutItem*		fColorsMenuItem;
			BSpinner*			fColumns;
			BSpinner*			fRows;
			BStringView*		fWorkspaceLine;
			WorkspaceGrid*		fGrid;
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
		{ return "Screens, resolution, and workspaces"; }
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
									bool all, bool makeDefault);
			void				_RestoreHardware();

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


WorkspaceGrid::WorkspaceGrid()
	:
	BView("workspaces", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fColumns(1),
	fRows(1),
	fFilter(NULL)
{
	SetViewColor(box_fill());
}


void
WorkspaceGrid::SetGrid(uint32 columns, uint32 rows)
{
	if (columns < 1)
		columns = 1;
	if (rows < 1)
		rows = 1;
	fColumns = columns;
	fRows = rows;
	Invalidate();
	InvalidateLayout();
}


BSize
WorkspaceGrid::_Size() const
{
	float u = unit();
	float width = u * 4.0f * 2.0f + fColumns * u * 34.0f
		+ (fColumns > 1 ? (fColumns - 1) * u * 3.0f : 0);
	float height = u * 4.0f * 2.0f + fRows * u * 22.0f
		+ (fRows > 1 ? (fRows - 1) * u * 3.0f : 0);
	return BSize(width, height);
}


BSize
WorkspaceGrid::MinSize()
{
	return _Size();
}


BSize
WorkspaceGrid::PreferredSize()
{
	return _Size();
}


BRect
WorkspaceGrid::_Cell(int32 column, int32 row) const
{
	float u = unit();
	float left = u * 4.0f + column * (u * 34.0f + u * 3.0f);
	float top = u * 4.0f + row * (u * 22.0f + u * 3.0f);
	return BRect(left, top, left + u * 34.0f, top + u * 22.0f);
}


void
WorkspaceGrid::Draw(BRect updateRect)
{
	(void)updateRect;
	float u = unit();
	SetHighColor(box_fill());
	FillRect(Bounds());
	int32 current = current_workspace();
	rgb_color frame = mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 200);
	for (uint32 row = 0; row < fRows; row++) {
		for (uint32 column = 0; column < fColumns; column++) {
			BRect cell = _Cell(column, row);
			int32 index = (int32)(row * fColumns + column);
			SetHighColor(index == current
				? ui_color(B_STATUS_BAR_COLOR)
				: ui_color(B_DESKTOP_COLOR));
			FillRoundRect(cell, u * 3.0f, u * 3.0f);
			SetHighColor(frame);
			StrokeRoundRect(cell, u * 3.0f, u * 3.0f);
		}
	}
}


void
WorkspaceGrid::MouseDown(BPoint where)
{
	for (uint32 row = 0; row < fRows; row++) {
		for (uint32 column = 0; column < fColumns; column++) {
			if (_Cell(column, row).Contains(where)) {
				activate_workspace((int32)(row * fColumns + column));
				Invalidate();
				return;
			}
		}
	}
}


void
WorkspaceGrid::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (fFilter == NULL && Window() != NULL) {
		fFilter = new WorkspaceWatch(this);
		Window()->AddCommonFilter(fFilter);
	}
}


void
WorkspaceGrid::DetachedFromWindow()
{
	if (fFilter != NULL && Window() != NULL)
		Window()->RemoveCommonFilter(fFilter);
	delete fFilter;
	fFilter = NULL;
	BView::DetachedFromWindow();
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

	display_mode* modes = NULL;
	uint32 count = 0;
	if (display_screen_modes(B_MAIN_SCREEN_ID.id, &modes, &count) == B_OK) {
		fModes = modes;
		fModeCount = count;
	}

	display_mode mode;
	BScreen screen;
	if (screen.GetMode(&mode) == B_OK) {
		fOpened.width = mode.virtual_width;
		fOpened.height = mode.virtual_height;
		fOpened.space = (color_space)mode.space;
		fOpened.refresh = display_refresh_rate(mode);
	}
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
		status = _SetMode(mode, false, true);
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
	return display_screen_match(fModes, fModeCount, choice.width, choice.height,
		choice.space, choice.refresh, &mode);
}


status_t
DisplayModule::_SetMode(const display_mode& mode, bool all, bool makeDefault)
{
	return display_screen_set_mode(mode, all, makeDefault);
}


void
DisplayModule::_RestoreHardware()
{
	_SetMode(fPreviousMode, fActive.allWorkspaces, false);
	fSelected = fActive;
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

	// The trial does not become the boot mode. Keep writes that afterwards.
	// A second thread must not call SetMode: that froze the desktop.
	status_t status = _SetMode(wanted, fSelected.allWorkspaces, false);
	if (status != B_OK)
		return status;

	fPreviousMode = previous;
	fActive = fSelected;
	fSettled = 0;
	fTiming = true;
	return B_OK;
}


void
DisplayModule::Settle(bool keep)
{
	if (!fTiming)
		return;
	if (atomic_add(&fSettled, 1) != 0)
		return;
	fTiming = false;
	if (keep) {
		display_mode mode;
		if (_Match(fSelected, mode))
			_SetMode(mode, fSelected.allWorkspaces, true);
		fActive = fSelected;
		return;
	}
	_RestoreHardware();
}


DisplayView::DisplayView(DisplayModule* module)
	:
	BGroupView(B_VERTICAL, 0),
	fModule(module),
	fUpdating(false),
	fConfirming(false),
	fAlert(NULL),
	fConfirmRunner(NULL),
	fArrangement(NULL),
	fArrangeTitle(NULL),
	fArrangeHint(NULL),
	fScreens(NULL),
	fScreenGroup(NULL),
	fScreenBox(NULL),
	fWorkGroup(NULL),
	fScreenLabel(NULL),
	fResolutionField(NULL),
	fColorsField(NULL),
	fRefreshField(NULL),
	fColorsLabelItem(NULL),
	fColorsMenuItem(NULL),
	fColumns(NULL),
	fRows(NULL),
	fWorkspaceLine(NULL),
	fGrid(NULL),
	fScopeMenu(NULL),
	fAllItem(NULL),
	fResolutionMenu(NULL),
	fColorsMenu(NULL),
	fRefreshMenu(NULL),
	fApply(NULL)
{
	float u = unit();
	fArrangement = new ArrangementCard();
	fArrangeTitle = new BStringView("arrange title", "Arrangement");
	fArrangeHint = new BStringView("arrange hint",
		"Drag the screens to match your desk. Click one to change it.");
	fScreens = new BScreenArrangement("screens", new BMessage(kMsgScreen));
	fScreens->SetDraggable(false);

	fScreenBox = new BBox("screen");
	fScreenBox->SetViewColor(box_fill());
	fScreenLabel = r5_settings_box_title("Screen");
	fScreenBox->SetLabel(fScreenLabel);

	fScopeMenu = new BPopUpMenu("scope", true, true);
	fAllItem = new BMenuItem("All workspaces", new BMessage(kMsgScope));
	fScopeMenu->AddItem(fAllItem);
	fScopeMenu->AddItem(new BMenuItem("This workspace",
		new BMessage(kMsgScope)));
	fAllItem->SetMarked(true);
	BMenuField* scopeField = new BMenuField("scope field", "Apply to:",
		fScopeMenu);

	fResolutionMenu = new BPopUpMenu("resolution", true, true);
	fColorsMenu = new BPopUpMenu("colors", true, true);
	fRefreshMenu = new BPopUpMenu("refresh", true, true);
	fResolutionField = new BMenuField("resolution field", "Resolution:",
		fResolutionMenu);
	fColorsField = new BMenuField("colors field", "Colors:", fColorsMenu);
	fRefreshField = new BMenuField("refresh field", "Refresh rate:",
		fRefreshMenu);

	fApply = new BButton("apply", "Apply", new BMessage(kMsgApply));
	fApply->SetEnabled(false);

	fScreenGroup = new BGroupView(B_VERTICAL, 0);
	fScreenGroup->SetViewColor(box_fill());
	fScreenBox->AddChild(fScreenGroup);
	BGridView* controls = new BGridView(u * 12.0f, u * 8.0f);
	controls->SetViewColor(box_fill());
	fColorsLabelItem = fColorsField->CreateLabelLayoutItem();
	fColorsMenuItem = fColorsField->CreateMenuBarLayoutItem();
	controls->GridLayout()->AddItem(
		fResolutionField->CreateLabelLayoutItem(), 0, 0);
	controls->GridLayout()->AddItem(
		fResolutionField->CreateMenuBarLayoutItem(), 1, 0);
	controls->GridLayout()->AddItem(
		fRefreshField->CreateLabelLayoutItem(), 0, 1);
	controls->GridLayout()->AddItem(
		fRefreshField->CreateMenuBarLayoutItem(), 1, 1);
	controls->GridLayout()->AddItem(fColorsLabelItem, 0, 2);
	controls->GridLayout()->AddItem(fColorsMenuItem, 1, 2);
	controls->GridLayout()->AddItem(scopeField->CreateLabelLayoutItem(), 0, 3);
	controls->GridLayout()->AddItem(
		scopeField->CreateMenuBarLayoutItem(), 1, 3);
	BLayoutBuilder::Group<>(fScreenGroup)
		.Add(controls)
		.AddGroup(B_HORIZONTAL, 0)
			.AddGlue()
			.Add(fApply)
			.End()
		.AddGlue();

	BBox* workBox = new BBox("workspaces");
	workBox->SetViewColor(box_fill());
	workBox->SetLabel(r5_settings_box_title("Workspaces"));
	fColumns = new BSpinner("columns", "Columns:", new BMessage(kMsgColumns));
	fRows = new BSpinner("rows", "Rows:", new BMessage(kMsgRows));
	fColumns->SetRange(1, 32);
	fRows->SetRange(1, 32);
	fGrid = new WorkspaceGrid();
	fWorkspaceLine = new BStringView("workspace line", "");
	fWorkGroup = new BGroupView(B_VERTICAL, 0);
	fWorkGroup->SetViewColor(box_fill());
	workBox->AddChild(fWorkGroup);
	BLayoutBuilder::Group<>(fWorkGroup)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fGrid)
			.AddGroup(B_VERTICAL, 0)
				.Add(fColumns)
				.Add(fRows)
				.End()
			.AddGlue()
			.End()
		.Add(fWorkspaceLine)
		.AddGlue();

	BLayoutBuilder::Group<>(fArrangement)
		.AddGroup(B_HORIZONTAL, u * 10.0f)
			.Add(fArrangeTitle)
			.Add(fArrangeHint)
			.AddGlue()
			.End()
		.Add(fScreens)
		.AddGlue(0);

	BLayoutBuilder::Group<>(this)
		.SetInsets(0)
		.Add(fArrangement, 0)
		.AddGroup(B_HORIZONTAL, 0, 1.0f)
			.Add(fScreenBox, 1.6f)
			.Add(workBox, 1.0f)
			.End();

	_ApplySizes();
}


void
DisplayView::_ApplySizes()
{
	float u = unit();
	fArrangement->SetExplicitMinSize(BSize(0, u * 150.0f + u * 12.5f
		+ u * 6.0f + u * 10.0f + u * 12.0f));
	fArrangement->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		u * 150.0f + u * 36.0f));
	BFont title = sized_font(u * 12.5f, true);
	BFont hint = sized_font(u * 11.5f, false);
	fArrangeTitle->SetFont(&title);
	fArrangeTitle->SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	fArrangeHint->SetFont(&hint);
	fArrangeHint->SetHighColor(quiet_color(140));
	fScreens->SetExplicitMinSize(BSize(u * 220.0f, u * 150.0f));
	fScreens->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, u * 150.0f));
	fScreens->SetExplicitPreferredSize(BSize(B_SIZE_UNLIMITED, u * 150.0f));

	float side = u * 14.0f;
	BGroupLayout* arrangeLayout
		= dynamic_cast<BGroupLayout*>(fArrangement->GetLayout());
	if (arrangeLayout != NULL) {
		arrangeLayout->SetInsets(side, u * 10.0f, side, u * 12.0f);
		arrangeLayout->SetSpacing(u * 6.0f);
	}

	BFont label = sized_font(u * 12.0f, false);
	fResolutionField->SetFont(&label);
	fRefreshField->SetFont(&label);
	fColorsField->SetFont(&label);
	BFont button = sized_font(be_plain_font->Size(), true);
	fApply->SetFont(&button);
	fApply->SetExplicitMinSize(BSize(u * 72.0f, u * 28.0f));
	fApply->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, u * 28.0f));

	BGroupLayout* screenLayout
		= fScreenGroup != NULL
			? dynamic_cast<BGroupLayout*>(fScreenGroup->GetLayout()) : NULL;
	if (screenLayout != NULL) {
		screenLayout->SetInsets(side, u * 8.0f, side, u * 12.0f);
		screenLayout->SetSpacing(u * 8.0f);
	}
	BGroupLayout* workLayout
		= fWorkGroup != NULL
			? dynamic_cast<BGroupLayout*>(fWorkGroup->GetLayout()) : NULL;
	if (workLayout != NULL) {
		workLayout->SetInsets(side, u * 8.0f, side, u * 12.0f);
		workLayout->SetSpacing(u * 8.0f);
	}

	BFont line = sized_font(u * 11.5f, false);
	fWorkspaceLine->SetFont(&line);
	fWorkspaceLine->SetHighColor(quiet_color(160));
	fColumns->SetExplicitMinSize(BSize(u * 120.0f, u * 22.0f));
	fRows->SetExplicitMinSize(BSize(u * 120.0f, u * 22.0f));
	fColumns->SetExplicitMaxSize(BSize(u * 160.0f, u * 22.0f));
	fRows->SetExplicitMaxSize(BSize(u * 160.0f, u * 22.0f));
	fColumns->TextView()->SetExplicitMinSize(BSize(u * 44.0f, u * 22.0f));
	fColumns->TextView()->SetExplicitMaxSize(BSize(u * 44.0f, u * 22.0f));
	fRows->TextView()->SetExplicitMinSize(BSize(u * 44.0f, u * 22.0f));
	fRows->TextView()->SetExplicitMaxSize(BSize(u * 44.0f, u * 22.0f));

	BMenuBar* bars[] = {
		fResolutionField->MenuBar(), fRefreshField->MenuBar(),
		fColorsField->MenuBar()
	};
	for (int32 index = 0; index < 3; index++) {
		if (bars[index] == NULL)
			continue;
		bars[index]->SetExplicitMinSize(BSize(u * 140.0f, u * 26.0f));
		bars[index]->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, u * 26.0f));
	}

	if (GetLayout() != NULL) {
		BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
		if (layout != NULL) {
			layout->SetSpacing(u * 10.0f);
			if (layout->ItemAt(1) != NULL) {
				BGroupLayout* row = dynamic_cast<BGroupLayout*>(
					layout->ItemAt(1)->Layout());
				if (row != NULL)
					row->SetSpacing(u * 10.0f);
			}
		}
	}
}


void
DisplayView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fColumns->SetTarget(this);
	fRows->SetTarget(this);
	fApply->SetTarget(this);
	fScopeMenu->SetTargetForItems(this);
	fScreens->SetTarget(this);
	_ApplySizes();
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

	display_mode recommended;
	bool hasRecommended = display_screen_recommended(fModule->Modes(),
		fModule->ModeCount(), &recommended);

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
		if (hasRecommended
			&& mode.virtual_width == recommended.virtual_width
			&& mode.virtual_height == recommended.virtual_height)
			name.Append(" (recommended)");
		fResolutionMenu->AddItem(new BMenuItem(name.String(), message));
	}
	int32 colorCount = 0;
	for (int32 s = 0; s < 6; s++) {
		if (!spaces[s])
			continue;
		colorCount++;
		BMessage* message = new BMessage(kMsgColors);
		message->AddInt32("space", spaceOrder[s]);
		BString name;
		name.SetToFormat("%" B_PRId32 " bits/pixel",
			display_bits_per_pixel(spaceOrder[s]));
		fColorsMenu->AddItem(new BMenuItem(name.String(), message));
	}
	bool showColors = colorCount > 1;
	if (fColorsLabelItem != NULL)
		fColorsLabelItem->SetVisible(showColors);
	if (fColorsMenuItem != NULL)
		fColorsMenuItem->SetVisible(showColors);
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
		float rate = display_refresh_rate(mode);
		if (seen >= 0.0f && fabs(seen - rate) < 0.05f)
			continue;
		seen = rate;
		BMessage* message = new BMessage(kMsgRefresh);
		message->AddFloat("refresh", rate);
		BString name;
		display_format_refresh(rate, name);
		fRefreshMenu->AddItem(new BMenuItem(name.String(), message));
	}
	if (fRefreshMenu->CountItems() == 0) {
		BString name;
		display_format_refresh(choice.refresh, name);
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
		display_bits_per_pixel(choice.space));
	show_marked_label(fColorsMenu, colorLabel.String());
	BString refreshLabel;
	display_format_refresh(choice.refresh, refreshLabel);
	show_marked_label(fRefreshMenu, refreshLabel.String());

	if (fAllItem != NULL)
		fAllItem->SetMarked(choice.allWorkspaces);
	if (fScopeMenu->ItemAt(1) != NULL)
		fScopeMenu->ItemAt(1)->SetMarked(!choice.allWorkspaces);
	fColumns->SetValue(choice.columns);
	fRows->SetValue(choice.rows);
	fGrid->SetGrid(choice.columns, choice.rows);
	int32 count = (int32)(choice.columns * choice.rows);
	BString line;
	line.SetToFormat("%" B_PRId32 " workspace%s. Changes right away. "
		"Every screen follows the same workspace.",
		count, count == 1 ? "" : "s");
	fWorkspaceLine->SetText(line.String());
	fUpdating = false;
}


void
DisplayView::_UpdatePicture()
{
	if (fModule == NULL || fScreens == NULL)
		return;
	BObjectList<DisplayScreen, true> screens;
	if (display_screens_get(screens) != B_OK || screens.CountItems() == 0) {
		fScreenLabel->SetText("Screen");
		fScreens->SetScreens(NULL, 0);
		fScreens->SetCaption(NULL);
		fScreens->SetDraggable(false);
		return;
	}
	screen_tile* tiles = new screen_tile[screens.CountItems()];
	for (int32 index = 0; index < screens.CountItems(); index++) {
		DisplayScreen* screen = screens.ItemAt(index);
		tiles[index].id = screen->id;
		tiles[index].frame = screen->frame;
		tiles[index].name = screen->name.String();
		tiles[index].main = screen->main;
		tiles[index].enabled = true;
	}
	fScreens->SetScreens(tiles, screens.CountItems());
	delete[] tiles;
	fScreens->SetSelected(screens.ItemAt(0)->id);
	fScreens->SetCaption(screens.CountItems() > 1
		? "The Deskbar is on screen 1." : NULL);
	fScreens->SetDraggable(screens.CountItems() > 1);

	BString title("Screen");
	if (screens.CountItems() == 1)
		title.SetToFormat("Screen – %s", screens.ItemAt(0)->name.String());
	fScreenLabel->SetText(title.String());
	fScreenBox->InvalidateLayout();
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
	_ReadMenus();
	if (workspaces) {
		DisplayChoice choice = fModule->Selected();
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
		case B_COLORS_UPDATED:
		case B_FONTS_UPDATED:
			_ApplySizes();
			_UpdateApply();
			Invalidate();
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
		case kMsgScreen:
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
			delete fConfirmRunner;
			fConfirmRunner = new BMessageRunner(BMessenger(this),
				new BMessage(kMsgDisplayTimeout), 12000000, 1);
			fAlert = new BAlert("Display",
				"Do you wish to keep these settings?\n"
				"The previous picture returns after 12 seconds.",
				"Undo", "Keep", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			fAlert->SetFlags(fAlert->Flags() | B_CLOSE_ON_ESCAPE);
			fAlert->Go(new BInvoker(new BMessage(kMsgDisplayChoice), this));
			break;
		}
		case kMsgDisplayChoice:
		case kMsgDisplayTimeout:
		{
			bool keep = false;
			if (message->what == kMsgDisplayChoice) {
				int32 which = 0;
				message->FindInt32("which", &which);
				keep = which == 1;
			}
			if (fAlert != NULL) {
				BAlert* alert = fAlert;
				fAlert = NULL;
				alert->PostMessage(B_QUIT_REQUESTED);
			}
			delete fConfirmRunner;
			fConfirmRunner = NULL;
			fConfirming = false;
			if (fModule != NULL)
				fModule->Settle(keep);
			if (fModule != NULL)
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
	delete fConfirmRunner;
	fConfirmRunner = NULL;
	if (fAlert != NULL) {
		fAlert->PostMessage(B_QUIT_REQUESTED);
		fAlert = NULL;
	}
	if (fModule != NULL && fConfirming)
		fModule->Settle(false);
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
register_display_page(SettingsManager& manager)
{
	manager.Add(new DisplayModule());
}
