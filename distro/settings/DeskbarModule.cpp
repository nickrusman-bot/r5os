/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Desktop and Deskbar page. Messages the running Deskbar.
 * Does not write Deskbar's settings file. The separate preferences
 * program stays installed.
 */


#include "DeskbarModule.h"

#include <R5UI.h>

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsModule.h"

#include <AppDefs.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Control.h>
#include <Deskbar.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <StringView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <Slider.h>
#include <SupportDefs.h>
#include <SpaceLayoutItem.h>
#include <Spinner.h>
#include <String.h>

#include <syslog.h>


static const uint32 kMsgChanged = 'r5dc';
static const uint32 kMsgPlace = 'r5dp';
static const uint32 kMsgEditTracker = 'r5dt';

// Deskbar's own application messages. They are not in a public header.
// Sort, Tracker, expanders, labels, and the window flags toggle.
// Icon size and the recent counts are absolute. SetLocation is absolute.
static const uint32 kSortRunningApps = 'SAps';
static const uint32 kTrackerFirst = 'TkFt';
static const uint32 kSuperExpando = 'SprE';
static const uint32 kExpandNewTeams = 'ExTm';
static const uint32 kHideLabels = 'hLbs';
static const uint32 kResizeTeamIcons = 'RTIs';
static const uint32 kUpdateRecentCounts = 'upct';

static const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";
static const char* kTrackerSignature = "application/x-vnd.Be-TRAK";
static const char* kModuleId = "deskbar";

static const int32 kMiniState = 0;
static const int32 kMinimumIconSize = 16;
static const int32 kMaximumIconSize = 96;
static const int32 kIconSizeInterval = 8;


struct DeskbarChoices {
	bool				sort;
	bool				trackerFirst;
	bool				expanders;
	bool				expandNew;
	bool				hideLabels;
	int32				iconSize;
	bool				recentDocs;
	bool				recentFolders;
	bool				recentApps;
	int32				recentDocsCount;
	int32				recentFoldersCount;
	int32				recentAppsCount;
	deskbar_location	location;
	bool				showApplications;
	bool				alwaysOnTop;
	bool				autoRaise;
	bool				autoHide;
};


static bool
choices_equal(const DeskbarChoices& a, const DeskbarChoices& b)
{
	return a.sort == b.sort
		&& a.trackerFirst == b.trackerFirst
		&& a.expanders == b.expanders
		&& a.expandNew == b.expandNew
		&& a.hideLabels == b.hideLabels
		&& a.iconSize == b.iconSize
		&& a.recentDocs == b.recentDocs
		&& a.recentFolders == b.recentFolders
		&& a.recentApps == b.recentApps
		&& a.recentDocsCount == b.recentDocsCount
		&& a.recentFoldersCount == b.recentFoldersCount
		&& a.recentAppsCount == b.recentAppsCount
		&& a.location == b.location
		&& a.showApplications == b.showApplications
		&& a.alwaysOnTop == b.alwaysOnTop
		&& a.autoRaise == b.autoRaise
		&& a.autoHide == b.autoHide;
}


static int32
clamp_count(int32 count)
{
	if (count < 0)
		return 0;
	if (count > 50)
		return 50;
	return count;
}


static int32
clamp_icon(int32 size)
{
	if (size < kMinimumIconSize)
		size = kMinimumIconSize;
	if (size > kMaximumIconSize)
		size = kMaximumIconSize;
	return (size / kIconSizeInterval) * kIconSizeInterval;
}


static bool
corner_honors_expand(deskbar_location location)
{
	return location == B_DESKBAR_LEFT_TOP
		|| location == B_DESKBAR_RIGHT_TOP;
}


static bool
effective_expand(const DeskbarChoices& choices)
{
	if (corner_honors_expand(choices.location))
		return choices.showApplications;
	if (choices.location == B_DESKBAR_TOP
		|| choices.location == B_DESKBAR_BOTTOM)
		return true;
	return false;
}


static DeskbarChoices
default_choices()
{
	DeskbarChoices choices;
	choices.sort = false;
	choices.trackerFirst = true;
	choices.expanders = false;
	choices.expandNew = false;
	choices.hideLabels = false;
	choices.iconSize = kMinimumIconSize;
	choices.recentDocs = true;
	choices.recentFolders = true;
	choices.recentApps = true;
	choices.recentDocsCount = 10;
	choices.recentFoldersCount = 10;
	choices.recentAppsCount = 10;
	choices.location = B_DESKBAR_RIGHT_TOP;
	choices.showApplications = true;
	choices.alwaysOnTop = false;
	choices.autoRaise = false;
	choices.autoHide = false;
	return choices;
}


static deskbar_location
location_from_file(bool vertical, bool left, bool top, int32 state,
	bool* showApplications)
{
	if (!vertical && state != kMiniState) {
		*showApplications = true;
		return top ? B_DESKBAR_TOP : B_DESKBAR_BOTTOM;
	}

	deskbar_location location = B_DESKBAR_RIGHT_TOP;
	if (top && left)
		location = B_DESKBAR_LEFT_TOP;
	else if (top)
		location = B_DESKBAR_RIGHT_TOP;
	else if (left)
		location = B_DESKBAR_LEFT_BOTTOM;
	else
		location = B_DESKBAR_RIGHT_BOTTOM;

	if (corner_honors_expand(location))
		*showApplications = state != kMiniState;
	else
		*showApplications = false;
	return location;
}


static DeskbarChoices
load_choices()
{
	DeskbarChoices choices = default_choices();
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append("deskbar/settings") == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		BMessage prefs;
		if (file.InitCheck() == B_OK && prefs.Unflatten(&file) == B_OK) {
			choices.sort = prefs.GetBool("sortRunningApps", choices.sort);
			choices.trackerFirst = prefs.GetBool("trackerAlwaysFirst",
				choices.trackerFirst);
			choices.expanders = prefs.GetBool("superExpando",
				choices.expanders);
			choices.expandNew = prefs.GetBool("expandNewTeams",
				choices.expandNew);
			choices.hideLabels = prefs.GetBool("hideLabels",
				choices.hideLabels);
			choices.iconSize = clamp_icon(prefs.GetInt32("iconSize",
				choices.iconSize));
			choices.recentDocs = prefs.GetBool("recentDocsEnabled",
				choices.recentDocs);
			choices.recentFolders = prefs.GetBool("recentFoldersEnabled",
				choices.recentFolders);
			choices.recentApps = prefs.GetBool("recentAppsEnabled",
				choices.recentApps);
			choices.recentDocsCount = clamp_count(prefs.GetInt32(
				"recentDocsCount", choices.recentDocsCount));
			choices.recentFoldersCount = clamp_count(prefs.GetInt32(
				"recentFoldersCount", choices.recentFoldersCount));
			choices.recentAppsCount = clamp_count(prefs.GetInt32(
				"recentAppsCount", choices.recentAppsCount));
			bool vertical = prefs.GetBool("vertical", true);
			bool left = prefs.GetBool("left", false);
			bool top = prefs.GetBool("top", true);
			int32 state = prefs.GetInt32("state", 1);
			choices.location = location_from_file(vertical, left, top, state,
				&choices.showApplications);
			choices.alwaysOnTop = prefs.GetBool("alwaysOnTop",
				choices.alwaysOnTop);
			choices.autoRaise = prefs.GetBool("autoRaise", choices.autoRaise);
			choices.autoHide = prefs.GetBool("autoHide", choices.autoHide);
		}
	}

	BDeskbar bar;
	if (bar.IsRunning()) {
		bool expanded = true;
		choices.location = bar.Location(&expanded);
		if (corner_honors_expand(choices.location))
			choices.showApplications = expanded;
		else if (choices.location == B_DESKBAR_LEFT_BOTTOM
			|| choices.location == B_DESKBAR_RIGHT_BOTTOM)
			choices.showApplications = false;
		choices.alwaysOnTop = bar.IsAlwaysOnTop();
		choices.autoRaise = bar.IsAutoRaise();
		choices.autoHide = bar.IsAutoHide();
	}
	return choices;
}


static status_t
post_deskbar(BMessage& message)
{
	BMessenger messenger(kDeskbarSignature);
	if (!messenger.IsValid())
		return B_NAME_NOT_FOUND;
	return messenger.SendMessage(&message);
}


static status_t
toggle_deskbar(bool have, bool want, uint32 what)
{
	if (have == want)
		return B_OK;
	BMessage message(what);
	return post_deskbar(message);
}


class DeskbarPlace : public BView {
public:
								DeskbarPlace();
			void				SetPlace(deskbar_location location,
									bool expanded);
			deskbar_location	Location() const { return fLocation; }
			bool				Expanded() const { return fExpanded; }
			void				SetHandler(BHandler* handler);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* message);

private:
			BRect				_Screen() const;
			void				_Pick(BPoint where);
			void				_Notify();

			deskbar_location	fLocation;
			bool				fExpanded;
			bool				fTracking;
			BMessenger			fHandler;
};


class DeskbarModule;


class DeskbarView : public BGroupView {
public:
								DeskbarView(DeskbarModule* module);
	virtual						~DeskbarView();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				ApplyState(const DeskbarChoices& choices);

private:
			DeskbarChoices		_Read() const;
			void				_SyncEnabled();
			void				_EditTracker();
			void				_SetTargets(BView* view);

			DeskbarModule*		fModule;
			bool				fUpdating;
			BCheckBox*			fSort;
			BCheckBox*			fTrackerFirst;
			BCheckBox*			fExpanders;
			BCheckBox*			fExpandNew;
			BCheckBox*			fHideLabels;
			BSlider*			fIconSize;
			BCheckBox*			fRecentDocs;
			BCheckBox*			fRecentFolders;
			BCheckBox*			fRecentApps;
			BSpinner*			fRecentDocsCount;
			BSpinner*			fRecentFoldersCount;
			BSpinner*			fRecentAppsCount;
			DeskbarPlace*		fPlace;
			BCheckBox*			fShowApplications;
			BCheckBox*			fAlwaysOnTop;
			BCheckBox*			fAutoRaise;
			BCheckBox*			fAutoHide;
};


class DeskbarModule : public SettingsModule {
public:
								DeskbarModule();
	virtual						~DeskbarModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Desktop and Deskbar"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
		{ return "Adjust desktop and Deskbar settings"; }
	virtual	const char*			Keywords() const
		{ return "deskbar desktop menu recent applications location"; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const { return fModified; }
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			const DeskbarChoices& Current() const { return fCurrent; }
			void				UserEdited(const DeskbarChoices& choices);
			void				ApplyDefaults();
			void				ClearView(DeskbarView* view);

private:
			status_t			_Apply(const DeskbarChoices& target);
			void				_Push();

			DeskbarView*		fView;
			SettingsContext*	fContext;
			DeskbarChoices		fCurrent;
			DeskbarChoices		fOriginal;
			DeskbarChoices		fCommitted;
			bool				fModified;
};


DeskbarPlace::DeskbarPlace()
	:
	BView("place", B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fLocation(B_DESKBAR_RIGHT_TOP),
	fExpanded(true),
	fTracking(false)
{
	SetExplicitMinSize(BSize(168, 112));
	SetExplicitMaxSize(BSize(168, 112));
	SetExplicitPreferredSize(BSize(168, 112));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
DeskbarPlace::SetPlace(deskbar_location location, bool expanded)
{
	if (fLocation == location && fExpanded == expanded)
		return;
	fLocation = location;
	fExpanded = expanded;
	Invalidate();
}


void
DeskbarPlace::SetHandler(BHandler* handler)
{
	fHandler = BMessenger(handler);
}


BRect
DeskbarPlace::_Screen() const
{
	BRect bounds = Bounds();
	bounds.InsetBy(8, 6);
	float width = bounds.Width();
	float height = bounds.Height();
	const float ratio = 4.0f / 3.0f;
	if (width / ratio > height)
		width = height * ratio;
	else
		height = width / ratio;
	float left = bounds.left + (bounds.Width() - width) / 2.0f;
	float top = bounds.top + (bounds.Height() - height) / 2.0f;
	return BRect(left, top, left + width, top + height);
}


void
DeskbarPlace::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect inner = r5_screen_draw(this, _Screen(),
		ui_color(B_CONTROL_BACKGROUND_COLOR));

	BRect bar = inner.InsetByCopy(3, 3);
	float thickness = 8;
	switch (fLocation) {
		case B_DESKBAR_TOP:
			bar.bottom = bar.top + thickness;
			break;
		case B_DESKBAR_BOTTOM:
			bar.top = bar.bottom - thickness;
			break;
		case B_DESKBAR_LEFT_TOP:
			bar.right = bar.left + thickness + 2;
			bar.bottom = bar.top + (fExpanded ? bar.Height() * 0.72f : 16);
			break;
		case B_DESKBAR_RIGHT_TOP:
			bar.left = bar.right - thickness - 2;
			bar.bottom = bar.top + (fExpanded ? bar.Height() * 0.72f : 16);
			break;
		case B_DESKBAR_LEFT_BOTTOM:
			bar.right = bar.left + thickness + 2;
			bar.top = bar.bottom - 16;
			break;
		case B_DESKBAR_RIGHT_BOTTOM:
			bar.left = bar.right - thickness - 2;
			bar.top = bar.bottom - 16;
			break;
		default:
			break;
	}
	SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	FillRect(bar);
}


void
DeskbarPlace::_Pick(BPoint where)
{
	BRect inner = _Screen().InsetByCopy(5, 5);
	if (!inner.Contains(where))
		return;

	float leftX = inner.left + inner.Width() / 3.0f;
	float rightX = inner.left + 2.0f * inner.Width() / 3.0f;
	float topY = inner.top + inner.Height() / 3.0f;
	float midY = inner.top + inner.Height() / 2.0f;
	float bottomY = inner.top + 2.0f * inner.Height() / 3.0f;

	deskbar_location location = fLocation;
	bool expanded = fExpanded;
	if (where.x < leftX) {
		if (where.y < topY) {
			location = B_DESKBAR_LEFT_TOP;
			expanded = false;
		} else if (where.y > bottomY) {
			location = B_DESKBAR_LEFT_BOTTOM;
			expanded = false;
		} else {
			location = B_DESKBAR_LEFT_TOP;
			expanded = true;
		}
	} else if (where.x > rightX) {
		if (where.y < topY) {
			location = B_DESKBAR_RIGHT_TOP;
			expanded = false;
		} else if (where.y > bottomY) {
			location = B_DESKBAR_RIGHT_BOTTOM;
			expanded = false;
		} else {
			location = B_DESKBAR_RIGHT_TOP;
			expanded = true;
		}
	} else if (where.y < midY) {
		location = B_DESKBAR_TOP;
		expanded = true;
	} else {
		location = B_DESKBAR_BOTTOM;
		expanded = true;
	}

	if (location == fLocation && expanded == fExpanded)
		return;
	fLocation = location;
	fExpanded = expanded;
	Invalidate();
	_Notify();
}


void
DeskbarPlace::_Notify()
{
	if (!fHandler.IsValid())
		return;
	BMessage message(kMsgPlace);
	message.AddInt32("location", static_cast<int32>(fLocation));
	message.AddBool("expand", fExpanded);
	fHandler.SendMessage(&message);
}


void
DeskbarPlace::MouseDown(BPoint where)
{
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	_Pick(where);
}


void
DeskbarPlace::MouseUp(BPoint where)
{
	(void)where;
	fTracking = false;
}


void
DeskbarPlace::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	(void)transit;
	(void)message;
	if (fTracking)
		_Pick(where);
}


DeskbarView::DeskbarView(DeskbarModule* module)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fUpdating(false),
	fSort(NULL),
	fTrackerFirst(NULL),
	fExpanders(NULL),
	fExpandNew(NULL),
	fHideLabels(NULL),
	fIconSize(NULL),
	fRecentDocs(NULL),
	fRecentFolders(NULL),
	fRecentApps(NULL),
	fRecentDocsCount(NULL),
	fRecentFoldersCount(NULL),
	fRecentAppsCount(NULL),
	fPlace(NULL),
	fShowApplications(NULL),
	fAlwaysOnTop(NULL),
	fAutoRaise(NULL),
	fAutoHide(NULL)
{
	fSort = new BCheckBox("sort", "Sort applications by name",
		new BMessage(kMsgChanged));
	fTrackerFirst = new BCheckBox("tracker", "Tracker always first",
		new BMessage(kMsgChanged));
	fExpanders = new BCheckBox("expanders", "Show application expander",
		new BMessage(kMsgChanged));
	fExpandNew = new BCheckBox("expand new", "Expand new applications",
		new BMessage(kMsgChanged));
	fHideLabels = new BCheckBox("labels", "Hide application names",
		new BMessage(kMsgChanged));
	fIconSize = new BSlider("icon_size", "Icon size",
		new BMessage(kMsgChanged), kMinimumIconSize / kIconSizeInterval,
		kMaximumIconSize / kIconSizeInterval, B_HORIZONTAL);
	fIconSize->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fIconSize->SetHashMarkCount((kMaximumIconSize - kMinimumIconSize)
		/ kIconSizeInterval + 1);
	fIconSize->SetLimitLabels("Small", "Large");
	fIconSize->SetModificationMessage(new BMessage(kMsgChanged));

	fRecentDocs = new BCheckBox("documents", "Recent documents:",
		new BMessage(kMsgChanged));
	fRecentFolders = new BCheckBox("folders", "Recent folders:",
		new BMessage(kMsgChanged));
	fRecentApps = new BCheckBox("applications", "Recent applications:",
		new BMessage(kMsgChanged));
	fRecentDocsCount = new BSpinner("document count", NULL,
		new BMessage(kMsgChanged));
	fRecentFoldersCount = new BSpinner("folder count", NULL,
		new BMessage(kMsgChanged));
	fRecentAppsCount = new BSpinner("application count", NULL,
		new BMessage(kMsgChanged));
	fRecentDocsCount->SetRange(0, 50);
	fRecentFoldersCount->SetRange(0, 50);
	fRecentAppsCount->SetRange(0, 50);

	fPlace = new DeskbarPlace();
	fShowApplications = new BCheckBox("show", "Show applications",
		new BMessage(kMsgChanged));
	fAlwaysOnTop = new BCheckBox("top", "Always on top",
		new BMessage(kMsgChanged));
	fAutoRaise = new BCheckBox("raise", "Auto-raise",
		new BMessage(kMsgChanged));
	fAutoHide = new BCheckBox("hide", "Auto-hide",
		new BMessage(kMsgChanged));
	BButton* edit = new BButton("edit", "Edit in Tracker" B_UTF8_ELLIPSIS,
		new BMessage(kMsgEditTracker));

	BBox* applications = new BBox("applications");
	applications->SetLabel(r5_settings_box_title("Applications"));
	BGroupView* applicationsInner = new BGroupView(B_VERTICAL, 0);
	BLayoutBuilder::Group<>(applicationsInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fSort)
		.Add(fTrackerFirst)
		.Add(fExpanders)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(BSpaceLayoutItem::CreateHorizontalStrut(20))
			.Add(fExpandNew)
			.AddGlue()
			.End()
		.Add(fHideLabels)
		.Add(fIconSize);
	applications->AddChild(applicationsInner);
	applications->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BBox* menu = new BBox("menu");
	menu->SetLabel(r5_settings_box_title("Menu"));
	BGroupView* menuInner = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(menuInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, 0)
				.Add(fRecentDocs)
				.Add(fRecentFolders)
				.Add(fRecentApps)
				.End()
			.AddGroup(B_VERTICAL, 0)
				.Add(fRecentDocsCount)
				.Add(fRecentFoldersCount)
				.Add(fRecentAppsCount)
				.End()
			.AddGlue()
			.End()
		.Add(edit);
	menu->AddChild(menuInner);
	menu->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BBox* window = new BBox("window");
	window->SetLabel(r5_settings_box_title("Window"));
	BGroupView* windowInner = new BGroupView(B_HORIZONTAL,
		B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(windowInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fPlace)
		.AddGroup(B_VERTICAL, 0)
			.Add(fShowApplications)
			.Add(fAlwaysOnTop)
			.Add(fAutoRaise)
			.Add(fAutoHide)
			.End()
		.AddGlue();
	window->AddChild(windowInner);
	window->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(applications)
			.Add(menu)
			.End()
		.Add(window)
		.AddGlue();

	if (fModule != NULL)
		ApplyState(fModule->Current());
}


DeskbarView::~DeskbarView()
{
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
DeskbarView::_SetTargets(BView* view)
{
	if (view == NULL)
		return;
	for (int32 i = 0; i < view->CountChildren(); i++) {
		BView* child = view->ChildAt(i);
		BControl* control = dynamic_cast<BControl*>(child);
		if (control != NULL)
			control->SetTarget(this);
		_SetTargets(child);
	}
}


void
DeskbarView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	_SetTargets(this);
	if (fPlace != NULL)
		fPlace->SetHandler(this);
}


DeskbarChoices
DeskbarView::_Read() const
{
	DeskbarChoices choices = default_choices();
	if (fModule != NULL)
		choices = fModule->Current();
	if (fSort != NULL)
		choices.sort = fSort->Value() == B_CONTROL_ON;
	if (fTrackerFirst != NULL)
		choices.trackerFirst = fTrackerFirst->Value() == B_CONTROL_ON;
	if (fExpanders != NULL)
		choices.expanders = fExpanders->Value() == B_CONTROL_ON;
	if (fExpandNew != NULL)
		choices.expandNew = fExpandNew->Value() == B_CONTROL_ON;
	if (fHideLabels != NULL)
		choices.hideLabels = fHideLabels->Value() == B_CONTROL_ON;
	if (fIconSize != NULL)
		choices.iconSize = clamp_icon(fIconSize->Value() * kIconSizeInterval);
	if (fRecentDocs != NULL)
		choices.recentDocs = fRecentDocs->Value() == B_CONTROL_ON;
	if (fRecentFolders != NULL)
		choices.recentFolders = fRecentFolders->Value() == B_CONTROL_ON;
	if (fRecentApps != NULL)
		choices.recentApps = fRecentApps->Value() == B_CONTROL_ON;
	if (fRecentDocsCount != NULL)
		choices.recentDocsCount = clamp_count(fRecentDocsCount->Value());
	if (fRecentFoldersCount != NULL)
		choices.recentFoldersCount = clamp_count(fRecentFoldersCount->Value());
	if (fRecentAppsCount != NULL)
		choices.recentAppsCount = clamp_count(fRecentAppsCount->Value());
	if (fPlace != NULL)
		choices.location = fPlace->Location();
	if (fShowApplications != NULL)
		choices.showApplications = fShowApplications->Value() == B_CONTROL_ON;
	if (fAlwaysOnTop != NULL)
		choices.alwaysOnTop = fAlwaysOnTop->Value() == B_CONTROL_ON;
	if (fAutoRaise != NULL)
		choices.autoRaise = fAutoRaise->Value() == B_CONTROL_ON;
	if (fAutoHide != NULL)
		choices.autoHide = fAutoHide->Value() == B_CONTROL_ON;
	return choices;
}


void
DeskbarView::_SyncEnabled()
{
	DeskbarChoices choices = _Read();
	bool corners = corner_honors_expand(choices.location);
	if (fShowApplications != NULL)
		fShowApplications->SetEnabled(corners);
	if (fExpanders != NULL)
		fExpanders->SetEnabled(corners && choices.showApplications);
	if (fExpandNew != NULL) {
		fExpandNew->SetEnabled(corners && choices.showApplications
			&& choices.expanders);
	}
	if (fAutoRaise != NULL)
		fAutoRaise->SetEnabled(!choices.alwaysOnTop);
	if (fRecentDocsCount != NULL)
		fRecentDocsCount->SetEnabled(choices.recentDocs);
	if (fRecentFoldersCount != NULL)
		fRecentFoldersCount->SetEnabled(choices.recentFolders);
	if (fRecentAppsCount != NULL)
		fRecentAppsCount->SetEnabled(choices.recentApps);
}


void
DeskbarView::ApplyState(const DeskbarChoices& choices)
{
	fUpdating = true;
	if (fSort != NULL)
		fSort->SetValue(choices.sort ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fTrackerFirst != NULL) {
		fTrackerFirst->SetValue(choices.trackerFirst
			? B_CONTROL_ON : B_CONTROL_OFF);
	}
	if (fExpanders != NULL)
		fExpanders->SetValue(choices.expanders ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fExpandNew != NULL)
		fExpandNew->SetValue(choices.expandNew ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fHideLabels != NULL)
		fHideLabels->SetValue(choices.hideLabels ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fIconSize != NULL)
		fIconSize->SetValue(clamp_icon(choices.iconSize) / kIconSizeInterval);
	if (fRecentDocs != NULL)
		fRecentDocs->SetValue(choices.recentDocs ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fRecentFolders != NULL) {
		fRecentFolders->SetValue(choices.recentFolders
			? B_CONTROL_ON : B_CONTROL_OFF);
	}
	if (fRecentApps != NULL)
		fRecentApps->SetValue(choices.recentApps ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fRecentDocsCount != NULL)
		fRecentDocsCount->SetValue(choices.recentDocsCount);
	if (fRecentFoldersCount != NULL)
		fRecentFoldersCount->SetValue(choices.recentFoldersCount);
	if (fRecentAppsCount != NULL)
		fRecentAppsCount->SetValue(choices.recentAppsCount);
	if (fPlace != NULL)
		fPlace->SetPlace(choices.location, choices.showApplications);
	if (fShowApplications != NULL) {
		fShowApplications->SetValue(choices.showApplications
			? B_CONTROL_ON : B_CONTROL_OFF);
	}
	if (fAlwaysOnTop != NULL) {
		fAlwaysOnTop->SetValue(choices.alwaysOnTop
			? B_CONTROL_ON : B_CONTROL_OFF);
	}
	if (fAutoRaise != NULL)
		fAutoRaise->SetValue(choices.autoRaise ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fAutoHide != NULL)
		fAutoHide->SetValue(choices.autoHide ? B_CONTROL_ON : B_CONTROL_OFF);
	fUpdating = false;
	_SyncEnabled();
}


void
DeskbarView::_EditTracker()
{
	BPath path;
	entry_ref ref;
	if (find_directory(B_USER_DESKBAR_DIRECTORY, &path) != B_OK
		|| get_ref_for_path(path.Path(), &ref) != B_OK) {
		syslog(LOG_ERR, "settings deskbar: menu folder is missing");
		return;
	}
	BMessenger tracker(kTrackerSignature);
	if (!tracker.IsValid()) {
		syslog(LOG_ERR, "settings deskbar: Tracker is not running");
		return;
	}
	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &ref);
	status_t status = tracker.SendMessage(&message);
	if (status != B_OK)
		syslog(LOG_ERR, "settings deskbar: Tracker: %s", strerror(status));
}


void
DeskbarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPlace:
		{
			if (fUpdating || fModule == NULL)
				break;
			int32 location = static_cast<int32>(B_DESKBAR_RIGHT_TOP);
			bool expand = true;
			message->FindInt32("location", &location);
			message->FindBool("expand", &expand);
			fUpdating = true;
			if (fShowApplications != NULL && corner_honors_expand(
				static_cast<deskbar_location>(location))) {
				fShowApplications->SetValue(expand
					? B_CONTROL_ON : B_CONTROL_OFF);
			}
			fUpdating = false;
			fModule->UserEdited(_Read());
			_SyncEnabled();
			break;
		}
		case kMsgChanged:
		{
			if (fUpdating || fModule == NULL)
				break;
			DeskbarChoices choices = _Read();
			if (fPlace != NULL)
				fPlace->SetPlace(choices.location, choices.showApplications);
			fModule->UserEdited(choices);
			_SyncEnabled();
			break;
		}
		case kMsgEditTracker:
			_EditTracker();
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


DeskbarModule::DeskbarModule()
	:
	fView(NULL),
	fContext(NULL),
	fCurrent(default_choices()),
	fOriginal(default_choices()),
	fCommitted(default_choices()),
	fModified(false)
{
}


DeskbarModule::~DeskbarModule()
{
}


BView*
DeskbarModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new DeskbarView(this);
	return fView;
}


void
DeskbarModule::ClearView(DeskbarView* view)
{
	if (fView == view)
		fView = NULL;
}


void
DeskbarModule::UserEdited(const DeskbarChoices& choices)
{
	fCurrent = choices;
	fModified = !choices_equal(fCurrent, fOriginal);
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


bool
DeskbarModule::CanApplyDefaults() const
{
	return !choices_equal(fCurrent, default_choices());
}


status_t
DeskbarModule::ApplyDefaults(SettingsContext* context)
{
	if (context != NULL)
		fContext = context;
	fCurrent = default_choices();
	status_t status = _Apply(fCurrent);
	fModified = !choices_equal(fCurrent, fOriginal);
	_Push();
	return status;
}


void
DeskbarModule::_Push()
{
	if (fView != NULL)
		fView->ApplyState(fCurrent);
}


status_t
DeskbarModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fCurrent = load_choices();
	fOriginal = fCurrent;
	fCommitted = fCurrent;
	fModified = false;
	_Push();
	return B_OK;
}


status_t
DeskbarModule::_Apply(const DeskbarChoices& target)
{
	BDeskbar bar;
	if (!bar.IsRunning())
		return B_NAME_NOT_FOUND;

	status_t status = toggle_deskbar(fCommitted.sort, target.sort,
		kSortRunningApps);
	if (status != B_OK)
		return status;
	fCommitted.sort = target.sort;

	status = toggle_deskbar(fCommitted.trackerFirst, target.trackerFirst,
		kTrackerFirst);
	if (status != B_OK)
		return status;
	fCommitted.trackerFirst = target.trackerFirst;

	status = toggle_deskbar(fCommitted.expanders, target.expanders,
		kSuperExpando);
	if (status != B_OK)
		return status;
	fCommitted.expanders = target.expanders;

	status = toggle_deskbar(fCommitted.expandNew, target.expandNew,
		kExpandNewTeams);
	if (status != B_OK)
		return status;
	fCommitted.expandNew = target.expandNew;

	status = toggle_deskbar(fCommitted.hideLabels, target.hideLabels,
		kHideLabels);
	if (status != B_OK)
		return status;
	fCommitted.hideLabels = target.hideLabels;

	if (fCommitted.iconSize != target.iconSize) {
		BMessage message(kResizeTeamIcons);
		message.AddInt32("be:value", clamp_icon(target.iconSize)
			/ kIconSizeInterval);
		status = post_deskbar(message);
		if (status != B_OK)
			return status;
	}
	fCommitted.iconSize = target.iconSize;

	if (fCommitted.recentDocs != target.recentDocs
		|| fCommitted.recentFolders != target.recentFolders
		|| fCommitted.recentApps != target.recentApps
		|| fCommitted.recentDocsCount != target.recentDocsCount
		|| fCommitted.recentFoldersCount != target.recentFoldersCount
		|| fCommitted.recentAppsCount != target.recentAppsCount) {
		BMessage message(kUpdateRecentCounts);
		message.AddInt32("documents", clamp_count(target.recentDocsCount));
		message.AddInt32("folders", clamp_count(target.recentFoldersCount));
		message.AddInt32("applications", clamp_count(target.recentAppsCount));
		message.AddBool("documentsEnabled", target.recentDocs);
		message.AddBool("foldersEnabled", target.recentFolders);
		message.AddBool("applicationsEnabled", target.recentApps);
		status = post_deskbar(message);
		if (status != B_OK)
			return status;
	}
	fCommitted.recentDocs = target.recentDocs;
	fCommitted.recentFolders = target.recentFolders;
	fCommitted.recentApps = target.recentApps;
	fCommitted.recentDocsCount = target.recentDocsCount;
	fCommitted.recentFoldersCount = target.recentFoldersCount;
	fCommitted.recentAppsCount = target.recentAppsCount;

	if (fCommitted.location != target.location
		|| effective_expand(fCommitted) != effective_expand(target)) {
		status = bar.SetLocation(target.location, effective_expand(target));
		if (status != B_OK)
			return status;
	}
	fCommitted.location = target.location;
	fCommitted.showApplications = target.showApplications;

	// Always on top first. Auto-raise is left alone while that is on,
	// because Deskbar would force auto-raise off instead of storing it.
	if (bar.IsAlwaysOnTop() != target.alwaysOnTop) {
		status = bar.SetAlwaysOnTop(target.alwaysOnTop);
		if (status != B_OK)
			return status;
	}
	fCommitted.alwaysOnTop = target.alwaysOnTop;

	if (!target.alwaysOnTop && bar.IsAutoRaise() != target.autoRaise) {
		status = bar.SetAutoRaise(target.autoRaise);
		if (status != B_OK)
			return status;
	}
	fCommitted.autoRaise = target.autoRaise;

	if (bar.IsAutoHide() != target.autoHide) {
		status = bar.SetAutoHide(target.autoHide);
		if (status != B_OK)
			return status;
	}
	fCommitted.autoHide = target.autoHide;
	return B_OK;
}


status_t
DeskbarModule::SaveSettings(SettingsContext* context)
{
	if (context != NULL)
		fContext = context;
	status_t status = _Apply(fCurrent);
	fModified = !choices_equal(fCurrent, fOriginal);
	if (status != B_OK)
		syslog(LOG_ERR, "settings deskbar: %s", strerror(status));
	return status;
}


status_t
DeskbarModule::RevertSettings(SettingsContext* context)
{
	if (context != NULL)
		fContext = context;
	status_t status = B_OK;
	if (!choices_equal(fCommitted, fOriginal))
		status = _Apply(fOriginal);
	if (status == B_OK) {
		fCurrent = fOriginal;
		fModified = false;
	}
	_Push();
	return status;
}


void
register_deskbar_page(SettingsManager& manager)
{
	manager.Add(new DeskbarModule());
}
