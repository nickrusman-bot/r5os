/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Desktop and Deskbar page. Messages the running Deskbar.
 * Does not write Deskbar's settings file. The separate preferences
 * program stays installed.
 */


#include "DeskbarModule.h"

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
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
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
static const uint32 kMsgDefaults = 'r5df';
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
			BMenuField*			fLocation;
			BCheckBox*			fShowApplications;
			BCheckBox*			fAlwaysOnTop;
			BCheckBox*			fAutoRaise;
			BCheckBox*			fAutoHide;
			BButton*			fDefaults;
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
		{ return "Where the Deskbar sits, the application list, and recent items."; }
	virtual	const char*			Keywords() const
		{ return "deskbar desktop menu recent applications location"; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const { return fModified; }

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
	fLocation(NULL),
	fShowApplications(NULL),
	fAlwaysOnTop(NULL),
	fAutoRaise(NULL),
	fAutoHide(NULL),
	fDefaults(NULL)
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

	BMenu* places = new BMenu("Right top");
	const struct {
		deskbar_location	location;
		const char*			label;
	} kPlaces[] = {
		{ B_DESKBAR_TOP, "Top" },
		{ B_DESKBAR_BOTTOM, "Bottom" },
		{ B_DESKBAR_LEFT_TOP, "Left top" },
		{ B_DESKBAR_RIGHT_TOP, "Right top" },
		{ B_DESKBAR_LEFT_BOTTOM, "Left bottom" },
		{ B_DESKBAR_RIGHT_BOTTOM, "Right bottom" }
	};
	for (uint32 i = 0; i < sizeof(kPlaces) / sizeof(kPlaces[0]); i++) {
		BMessage* message = new BMessage(kMsgChanged);
		message->AddInt32("location", static_cast<int32>(kPlaces[i].location));
		places->AddItem(new BMenuItem(kPlaces[i].label, message));
	}
	places->SetLabelFromMarked(true);
	fLocation = new BMenuField("location", "Location:", places);
	fShowApplications = new BCheckBox("show", "Show applications",
		new BMessage(kMsgChanged));
	fAlwaysOnTop = new BCheckBox("top", "Always on top",
		new BMessage(kMsgChanged));
	fAutoRaise = new BCheckBox("raise", "Auto-raise",
		new BMessage(kMsgChanged));
	fAutoHide = new BCheckBox("hide", "Auto-hide",
		new BMessage(kMsgChanged));
	fDefaults = new BButton("defaults", "Defaults",
		new BMessage(kMsgDefaults));
	BButton* edit = new BButton("edit", "Edit in Tracker" B_UTF8_ELLIPSIS,
		new BMessage(kMsgEditTracker));

	BBox* applications = new BBox("applications");
	applications->SetLabel("Applications");
	BGroupView* applicationsInner = new BGroupView(B_VERTICAL, 0);
	BLayoutBuilder::Group<>(applicationsInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fSort)
		.Add(fTrackerFirst)
		.Add(fExpanders)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(BSpaceLayoutItem::CreateHorizontalStrut(
				B_USE_DEFAULT_SPACING * 2))
			.Add(fExpandNew)
			.End()
		.Add(fHideLabels)
		.Add(fIconSize);
	applications->AddChild(applicationsInner);
	applications->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BBox* menu = new BBox("menu");
	menu->SetLabel("Menu");
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
	window->SetLabel("Window");
	BGroupView* windowInner = new BGroupView(B_HORIZONTAL,
		B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(windowInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fLocation)
			.Add(fShowApplications)
			.End()
		.AddGroup(B_VERTICAL, 0)
			.Add(fAlwaysOnTop)
			.Add(fAutoRaise)
			.Add(fAutoHide)
			.End()
		.AddGlue();
	window->AddChild(windowInner);
	window->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	BLayoutBuilder::Group<>(this)
		.Add(applications)
		.Add(menu)
		.Add(window)
		.Add(fDefaults)
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
		BMenuField* field = dynamic_cast<BMenuField*>(child);
		if (field != NULL && field->Menu() != NULL)
			field->Menu()->SetTargetForItems(this);
		_SetTargets(child);
	}
}


void
DeskbarView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	_SetTargets(this);
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
	if (fLocation != NULL && fLocation->Menu() != NULL) {
		BMenuItem* marked = fLocation->Menu()->FindMarked();
		int32 location = static_cast<int32>(choices.location);
		if (marked != NULL && marked->Message() != NULL
			&& marked->Message()->FindInt32("location", &location) == B_OK)
			choices.location = static_cast<deskbar_location>(location);
	}
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
	if (fDefaults != NULL)
		fDefaults->SetEnabled(!choices_equal(choices, default_choices()));
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
	if (fLocation != NULL && fLocation->Menu() != NULL) {
		BMenu* menu = fLocation->Menu();
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			int32 location = -1;
			bool mark = item != NULL && item->Message() != NULL
				&& item->Message()->FindInt32("location", &location) == B_OK
				&& location == static_cast<int32>(choices.location);
			if (item != NULL)
				item->SetMarked(mark);
		}
	}
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
		case kMsgChanged:
			if (fUpdating || fModule == NULL)
				break;
			fModule->UserEdited(_Read());
			_SyncEnabled();
			break;
		case kMsgDefaults:
			if (fModule != NULL)
				fModule->ApplyDefaults();
			break;
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


void
DeskbarModule::ApplyDefaults()
{
	fCurrent = default_choices();
	fModified = !choices_equal(fCurrent, fOriginal);
	_Push();
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
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
	if (status == B_OK) {
		fOriginal = fCurrent;
		fModified = false;
	}
	if (status != B_OK) {
		syslog(LOG_ERR, "settings deskbar: %s", strerror(status));
	}
	_Push();
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
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
	return status;
}


void
register_deskbar_page(SettingsManager& manager)
{
	manager.Add(new DeskbarModule());
}
