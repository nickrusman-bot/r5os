/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Notifications page. Writes the notification server's settings file.
 * The separate preferences program stays installed.
 */


#include "NotificationsModule.h"

#include "SettingsManager.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Control.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <Mime.h>
#include <Node.h>
#include <Notification.h>
#include <Path.h>
#include <ScrollView.h>
#include <Slider.h>
#include <String.h>
#include <SupportDefs.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <vector>


static const uint32 kMsgEnable = 'r5no';
static const uint32 kMsgWidth = 'r5nw';
static const uint32 kMsgTimeout = 'r5nt';
static const uint32 kMsgPosition = 'r5np';
static const uint32 kMsgSelect = 'r5ns';
static const uint32 kMsgMute = 'r5nm';
static const uint32 kMsgAdd = 'r5na';
static const uint32 kMsgAddRef = 'r5nr';
static const uint32 kMsgRemove = 'r5nx';

static const int32 kDefaultTimeout = 10;
static const int32 kMinimumTimeout = 3;
static const int32 kMaximumTimeout = 30;
static const float kDefaultWidth = 300.0f;
static const float kMinimumWidth = 300.0f;
static const float kMaximumWidth = 1000.0f;
static const int32 kWidthStep = 50;
static const int32 kFollowDeskbar = (int32)B_FOLLOW_NONE;
static const int32 kDefaultPosition
	= (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_RIGHT);
static const type_code kAppUsageType = 'ipau';

// Field names shared with the notification server. The file is
// ~/config/settings/system/notifications.
static const char* kAutoStartName = "auto-start";
static const char* kTimeoutName = "timeout";
static const char* kWidthName = "width";
static const char* kIconSizeName = "icon size";
static const char* kPositionName = "notification position";
static const char* kAppUsageName = "app_usage";


struct AppFilter {
	BString				name;
	BString				signature;
	bool				allow;
};


struct NotificationState {
	bool				enabled;
	int32				timeout;
	float				width;
	int32				iconSize;
	int32				position;
	std::vector<AppFilter> apps;
};


class NotificationsModule;


static bool known_position(int32 position);
static NotificationState factory_state();
static bool states_equal(const NotificationState& a,
	const NotificationState& b);
static bool general_is_factory(const NotificationState& state);


class AppItem : public BStringItem {
public:
								AppItem(const char* name,
									const char* signature, bool allow);
			void				SetAllow(bool allow);
			const char*			AppName() const { return fName.String(); }
			const char*			Signature() const
									{ return fSignature.String(); }
			bool				Allow() const { return fAllow; }

private:
			void				_Refresh();

			BString				fName;
			BString				fSignature;
			bool				fAllow;
};


class AppSignatureFilter : public BRefFilter {
public:
	virtual	bool				Filter(const entry_ref* ref, BNode* node,
									struct stat_beos* stat,
									const char* mimeType);
};


static const char* position_name(int32 position);


class NotificationPlace : public BView {
public:
								NotificationPlace();
			void				SetPosition(int32 position);
			int32				Position() const { return fPosition; }
			void				SetEnabled(bool enabled);
			void				SetHandler(BHandler* handler);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* message);

private:
			BRect				_Screen() const;
			deskbar_location	_Deskbar() const;
			BRect				_Bar(BRect inner) const;
			BRect				_Card(BRect inner) const;
			void				_Pick(BPoint where);
			void				_Notify();

			int32				fPosition;
			bool				fEnabled;
			bool				fTracking;
			BMessenger			fHandler;
};


class NotificationsView : public BGroupView {
public:
								NotificationsView(NotificationsModule* module);
	virtual						~NotificationsView();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				ApplyState(const NotificationState& state);

private:
			NotificationState	_Read() const;
			void				_FillApps(const std::vector<AppFilter>& apps);
			void				_SyncSelection();
			void				_EnableGeneral(bool enabled);
			void				_SetTimeoutLabel(int32 timeout);
			void				_Edited(bool sample);
			void				_Alert(const char* text);

			NotificationsModule* fModule;
			bool				fUpdating;
			BCheckBox*			fEnable;
			BSlider*			fWidth;
			BSlider*			fTimeout;
			NotificationPlace*	fPlace;
			BListView*			fApps;
			BButton*			fAdd;
			BButton*			fRemove;
			BCheckBox*			fMute;
			AppSignatureFilter*	fFilter;
			BFilePanel*			fPanel;
};


class NotificationsModule : public SettingsModule {
public:
								NotificationsModule();
	virtual						~NotificationsModule();

	virtual	const char*			Identifier() const { return "notifications"; }
	virtual	const char*			Name() const { return "Notifications"; }
	virtual	const char*			Category() const { return "system"; }
	virtual	const char*			Description() const;
	virtual	const char*			Keywords() const;
	virtual	bool				CanSave() const { return true; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const { return fModified; }
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			const NotificationState& Current() const { return fCurrent; }
			void				ClearView(NotificationsView* view);
			void				UserEdited(const NotificationState& state,
									bool sample);

private:
			void				_Push();
			status_t			_Write(const NotificationState& state);

			NotificationsView*	fView;
			SettingsContext*	fContext;
			NotificationState	fCurrent;
			NotificationState	fOriginal;
			NotificationState	fCommitted;
			bool				fModified;
			bool				fPendingSample;
};


static bool
known_position(int32 position)
{
	return position == kFollowDeskbar
		|| position == (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_RIGHT)
		|| position == (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT)
		|| position == (int32)(B_FOLLOW_TOP | B_FOLLOW_RIGHT)
		|| position == (int32)(B_FOLLOW_TOP | B_FOLLOW_LEFT);
}


static NotificationState
factory_state()
{
	NotificationState state;
	state.enabled = true;
	state.timeout = kDefaultTimeout;
	state.width = kDefaultWidth;
	state.iconSize = B_LARGE_ICON;
	state.position = kDefaultPosition;
	return state;
}


static bool
apps_equal(const std::vector<AppFilter>& a, const std::vector<AppFilter>& b)
{
	if (a.size() != b.size())
		return false;
	for (uint32 i = 0; i < a.size(); i++) {
		if (a[i].name != b[i].name || a[i].signature != b[i].signature
			|| a[i].allow != b[i].allow)
			return false;
	}
	return true;
}


static bool
states_equal(const NotificationState& a, const NotificationState& b)
{
	return a.enabled == b.enabled
		&& a.timeout == b.timeout
		&& a.width == b.width
		&& a.iconSize == b.iconSize
		&& a.position == b.position
		&& apps_equal(a.apps, b.apps);
}


static bool
general_is_factory(const NotificationState& state)
{
	return state.enabled
		&& state.timeout == kDefaultTimeout
		&& state.width == kDefaultWidth
		&& state.position == kDefaultPosition;
}


static void
set_timeout_label(BSlider* slider, int32 timeout)
{
	BString label;
	if (timeout == 1)
		label = "Timeout: 1 second";
	else {
		label.SetToFormat("Timeout: %" B_PRId32 " seconds", timeout);
	}
	slider->SetLabel(label.String());
}


static void
send_sample()
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetMessageID("NotificationsSample");
	notification.SetGroup("Notifications");
	notification.SetTitle("Notifications sample");
	notification.SetContent("This is a test notification message");
	status_t status = notification.Send();
	if (status != B_OK) {
		syslog(LOG_ERR, "settings notifications: sample: %s",
			strerror(status));
	}
}


static status_t
settings_path(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	status = path.Append("system");
	if (status != B_OK)
		return status;
	status = create_directory(path.Path(), 0755);
	if (status != B_OK)
		return status;
	return path.Append("notifications");
}


static status_t
write_state(const NotificationState& state)
{
	BPath path;
	status_t status = settings_path(path);
	if (status != B_OK)
		return status;

	BMessage store;
	status = store.AddBool(kAutoStartName, state.enabled);
	if (status == B_OK)
		status = store.AddInt32(kTimeoutName, state.timeout);
	if (status == B_OK)
		status = store.AddFloat(kWidthName, state.width);
	if (status == B_OK)
		status = store.AddInt32(kIconSizeName, state.iconSize);
	if (status == B_OK)
		status = store.AddInt32(kPositionName, state.position);
	for (uint32 i = 0; i < state.apps.size() && status == B_OK; i++) {
		BMessage inner;
		status = inner.AddString("name", state.apps[i].name);
		if (status == B_OK)
			status = inner.AddString("signature", state.apps[i].signature);
		if (status == B_OK)
			status = inner.AddBool("allow", state.apps[i].allow);
		ssize_t flatSize = 0;
		char* buffer = NULL;
		if (status == B_OK) {
			flatSize = inner.FlattenedSize();
			buffer = new(std::nothrow) char[flatSize];
			if (buffer == NULL)
				status = B_NO_MEMORY;
		}
		if (status == B_OK)
			status = inner.Flatten(buffer, flatSize);
		if (status == B_OK) {
			status = store.AddData(kAppUsageName, kAppUsageType, buffer,
				flatSize);
		}
		delete[] buffer;
	}
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return store.Flatten(&file);
}


static NotificationState
load_state(status_t* statusOut)
{
	NotificationState state = factory_state();
	*statusOut = B_OK;

	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, false);
	if (status != B_OK) {
		*statusOut = status;
		return state;
	}
	if (path.Append("system") != B_OK || path.Append("notifications") != B_OK) {
		*statusOut = B_BAD_VALUE;
		return state;
	}

	BFile file(path.Path(), B_READ_ONLY);
	status = file.InitCheck();
	if (status == B_ENTRY_NOT_FOUND)
		return state;
	if (status != B_OK) {
		*statusOut = status;
		return state;
	}

	BMessage store;
	status = store.Unflatten(&file);
	if (status != B_OK) {
		// The server creates an empty file so it can watch the node.
		off_t size = 0;
		if (file.GetSize(&size) == B_OK && size == 0)
			return state;
		*statusOut = status;
		return state;
	}

	state.enabled = store.GetBool(kAutoStartName, true);
	int32 timeout = kDefaultTimeout;
	if (store.FindInt32(kTimeoutName, &timeout) != B_OK
		|| timeout < kMinimumTimeout || timeout > kMaximumTimeout)
		timeout = kDefaultTimeout;
	state.timeout = timeout;

	float width = kDefaultWidth;
	if (store.FindFloat(kWidthName, &width) != B_OK
		|| width < kMinimumWidth || width > kMaximumWidth)
		width = kDefaultWidth;
	state.width = width;

	int32 iconSize = B_LARGE_ICON;
	if (store.FindInt32(kIconSizeName, &iconSize) == B_OK)
		state.iconSize = iconSize;

	int32 position = kDefaultPosition;
	if (store.FindInt32(kPositionName, &position) != B_OK
		|| !known_position(position))
		position = kDefaultPosition;
	state.position = position;

	type_code type;
	int32 count = 0;
	if (store.GetInfo(kAppUsageName, &type, &count) != B_OK)
		return state;
	for (int32 i = 0; i < count; i++) {
		const void* data = NULL;
		ssize_t flatSize = 0;
		if (store.FindData(kAppUsageName, kAppUsageType, i, &data,
			&flatSize) != B_OK)
			continue;
		BMessage inner;
		if (inner.Unflatten((const char*)data) != B_OK)
			continue;
		AppFilter app;
		const char* name = inner.GetString("name", "");
		const char* signature = inner.GetString("signature", "");
		if (signature == NULL || signature[0] == '\0')
			continue;
		app.name = name != NULL && name[0] != '\0' ? name : signature;
		app.signature = signature;
		app.allow = inner.GetBool("allow", true);
		state.apps.push_back(app);
	}
	return state;
}


AppItem::AppItem(const char* name, const char* signature, bool allow)
	:
	BStringItem(""),
	fName(name),
	fSignature(signature),
	fAllow(allow)
{
	_Refresh();
}


void
AppItem::SetAllow(bool allow)
{
	fAllow = allow;
	_Refresh();
}


void
AppItem::_Refresh()
{
	BString label(fName);
	if (!fAllow)
		label << " (muted)";
	SetText(label.String());
}


bool
AppSignatureFilter::Filter(const entry_ref* ref, BNode* node,
	struct stat_beos* stat, const char* mimeType)
{
	(void)ref;
	(void)stat;
	(void)mimeType;
	if (node != NULL && node->IsDirectory())
		return true;
	if (node == NULL)
		return false;
	char signature[B_MIME_TYPE_LENGTH];
	memset(signature, 0, sizeof(signature));
	ssize_t size = node->ReadAttr("BEOS:APP_SIG", 0, 0, signature,
		sizeof(signature) - 1);
	return size > 0 && signature[0] != '\0';
}


static const char*
position_name(int32 position)
{
	if (position == (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_RIGHT))
		return "Lower right";
	if (position == (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT))
		return "Lower left";
	if (position == (int32)(B_FOLLOW_TOP | B_FOLLOW_RIGHT))
		return "Upper right";
	if (position == (int32)(B_FOLLOW_TOP | B_FOLLOW_LEFT))
		return "Upper left";
	return "Follow Deskbar";
}


NotificationPlace::NotificationPlace()
	:
	BView("place", B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fPosition(kFollowDeskbar),
	fEnabled(true),
	fTracking(false)
{
	SetExplicitMinSize(BSize(200, 148));
	SetExplicitMaxSize(BSize(200, 148));
	SetExplicitPreferredSize(BSize(200, 148));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
NotificationPlace::SetPosition(int32 position)
{
	if (!known_position(position))
		position = kFollowDeskbar;
	if (fPosition == position)
		return;
	fPosition = position;
	Invalidate();
}


void
NotificationPlace::SetEnabled(bool enabled)
{
	if (fEnabled == enabled)
		return;
	fEnabled = enabled;
	Invalidate();
}


void
NotificationPlace::SetHandler(BHandler* handler)
{
	fHandler = BMessenger(handler);
}


BRect
NotificationPlace::_Screen() const
{
	BRect bounds = Bounds();
	bounds.bottom -= 20;
	bounds.InsetBy(4, 4);
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


deskbar_location
NotificationPlace::_Deskbar() const
{
	BDeskbar deskbar;
	if (deskbar.IsRunning())
		return deskbar.Location();
	return B_DESKBAR_RIGHT_TOP;
}


BRect
NotificationPlace::_Bar(BRect inner) const
{
	BRect bar = inner.InsetByCopy(2, 2);
	float thickness = 7;
	float tall = bar.Height() * 0.55f;
	switch (_Deskbar()) {
		case B_DESKBAR_TOP:
			bar.bottom = bar.top + thickness;
			break;
		case B_DESKBAR_BOTTOM:
			bar.top = bar.bottom - thickness;
			break;
		case B_DESKBAR_LEFT_TOP:
			bar.right = bar.left + thickness;
			bar.bottom = bar.top + tall;
			break;
		case B_DESKBAR_LEFT_BOTTOM:
			bar.right = bar.left + thickness;
			bar.top = bar.bottom - 16;
			break;
		case B_DESKBAR_RIGHT_BOTTOM:
			bar.left = bar.right - thickness;
			bar.top = bar.bottom - 16;
			break;
		default:
			bar.left = bar.right - thickness;
			bar.bottom = bar.top + tall;
			break;
	}
	return bar;
}


BRect
NotificationPlace::_Card(BRect inner) const
{
	float cardWidth = std::max(42.0f, inner.Width() * 0.40f);
	float cardHeight = 22;
	BRect card(0, 0, cardWidth, cardHeight);
	if (fPosition == kFollowDeskbar) {
		BRect bar = _Bar(inner);
		switch (_Deskbar()) {
			case B_DESKBAR_LEFT_TOP:
			case B_DESKBAR_LEFT_BOTTOM:
				card.OffsetTo(bar.right + 3, bar.top);
				break;
			case B_DESKBAR_TOP:
				card.OffsetTo(inner.right - cardWidth - 4, bar.bottom + 3);
				break;
			case B_DESKBAR_BOTTOM:
				card.OffsetTo(inner.right - cardWidth - 4,
					bar.top - cardHeight - 3);
				break;
			default:
				card.OffsetTo(bar.left - cardWidth - 3, bar.top);
				break;
		}
	} else if (fPosition == (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT)) {
		card.OffsetTo(inner.left + 4, inner.bottom - cardHeight - 4);
	} else if (fPosition == (int32)(B_FOLLOW_TOP | B_FOLLOW_RIGHT)) {
		card.OffsetTo(inner.right - cardWidth - 4, inner.top + 4);
	} else if (fPosition == (int32)(B_FOLLOW_TOP | B_FOLLOW_LEFT)) {
		card.OffsetTo(inner.left + 4, inner.top + 4);
	} else {
		card.OffsetTo(inner.right - cardWidth - 4,
			inner.bottom - cardHeight - 4);
	}
	if (card.left < inner.left + 2)
		card.OffsetBy(inner.left + 2 - card.left, 0);
	if (card.top < inner.top + 2)
		card.OffsetBy(0, inner.top + 2 - card.top);
	if (card.right > inner.right - 2)
		card.OffsetBy(inner.right - 2 - card.right, 0);
	if (card.bottom > inner.bottom - 2)
		card.OffsetBy(0, inner.bottom - 2 - card.bottom);
	return card;
}


void
NotificationPlace::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect outer = _Screen();
	BRect inner = outer.InsetByCopy(5, 5);
	rgb_color border = ui_color(B_CONTROL_BORDER_COLOR);
	rgb_color text = ui_color(fEnabled
		? B_CONTROL_TEXT_COLOR : B_CONTROL_BORDER_COLOR);
	SetHighColor(border);
	FillRoundRect(outer, 4, 4);
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_1_TINT));
	FillRoundRect(inner, 2, 2);
	SetHighColor(border);
	StrokeRoundRect(inner, 2, 2);

	BRect bar = _Bar(inner);
	SetHighColor(fPosition == kFollowDeskbar ? text : border);
	FillRect(bar);

	BRect card = _Card(inner);
	SetHighColor(ui_color(B_LIST_BACKGROUND_COLOR));
	FillRoundRect(card, 2, 2);
	SetHighColor(border);
	StrokeRoundRect(card, 2, 2);
	BRect icon = card.InsetByCopy(3, 4);
	icon.right = icon.left + icon.Height();
	SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
	FillRect(icon);
	SetHighColor(border);
	float line = icon.right + 4;
	StrokeLine(BPoint(line, card.top + 7), BPoint(card.right - 4, card.top + 7));
	StrokeLine(BPoint(line, card.top + 12),
		BPoint(card.right - 8, card.top + 12));

	const char* name = position_name(fPosition);
	float textWidth = StringWidth(name);
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	float x = outer.left + (outer.Width() - textWidth) / 2.0f;
	float y = Bounds().bottom - 2 - fontHeight.descent;
	SetHighColor(text);
	DrawString(name, BPoint(x, y));
}


void
NotificationPlace::_Pick(BPoint where)
{
	if (!fEnabled)
		return;
	BRect inner = _Screen().InsetByCopy(5, 5);
	if (!inner.Contains(where))
		return;

	int32 position = fPosition;
	if (_Bar(inner).InsetByCopy(-2, -2).Contains(where))
		position = kFollowDeskbar;
	else if (where.x < inner.left + inner.Width() / 2.0f) {
		position = where.y < inner.top + inner.Height() / 2.0f
			? (int32)(B_FOLLOW_TOP | B_FOLLOW_LEFT)
			: (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_LEFT);
	} else {
		position = where.y < inner.top + inner.Height() / 2.0f
			? (int32)(B_FOLLOW_TOP | B_FOLLOW_RIGHT)
			: (int32)(B_FOLLOW_BOTTOM | B_FOLLOW_RIGHT);
	}
	if (position == fPosition)
		return;
	fPosition = position;
	Invalidate();
	_Notify();
}


void
NotificationPlace::_Notify()
{
	if (!fHandler.IsValid())
		return;
	BMessage message(kMsgPosition);
	message.AddInt32("position", fPosition);
	fHandler.SendMessage(&message);
}


void
NotificationPlace::MouseDown(BPoint where)
{
	if (!fEnabled)
		return;
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	_Pick(where);
}


void
NotificationPlace::MouseUp(BPoint where)
{
	(void)where;
	fTracking = false;
}


void
NotificationPlace::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	(void)transit;
	(void)message;
	if (fTracking)
		_Pick(where);
}


NotificationsView::NotificationsView(NotificationsModule* module)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fUpdating(false),
	fEnable(NULL),
	fWidth(NULL),
	fTimeout(NULL),
	fPlace(NULL),
	fApps(NULL),
	fAdd(NULL),
	fRemove(NULL),
	fMute(NULL),
	fFilter(NULL),
	fPanel(NULL)
{
	fEnable = new BCheckBox("server", "Enable notifications",
		new BMessage(kMsgEnable));

	float ratio = be_plain_font->Size() / 12.0f;
	int32 minWidth = (int32)(kMinimumWidth / kWidthStep * ratio);
	int32 maxWidth = (int32)(kMaximumWidth / kWidthStep * ratio);
	if (maxWidth <= minWidth)
		maxWidth = minWidth + 1;
	fWidth = new BSlider("width", "Window width", new BMessage(kMsgWidth),
		minWidth, maxWidth, B_HORIZONTAL);
	fWidth->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fWidth->SetHashMarkCount(maxWidth - minWidth + 1);
	fWidth->SetLimitLabels("narrow", "wide");

	fTimeout = new BSlider("duration", "Timeout:", new BMessage(kMsgTimeout),
		kMinimumTimeout, kMaximumTimeout, B_HORIZONTAL);
	fTimeout->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fTimeout->SetHashMarkCount(kMaximumTimeout - kMinimumTimeout + 1);
	BString minLabel;
	minLabel << kMinimumTimeout;
	BString maxLabel;
	maxLabel << kMaximumTimeout;
	fTimeout->SetLimitLabels(minLabel.String(), maxLabel.String());

	fPlace = new NotificationPlace();

	BBox* general = new BBox("general");
	general->SetLabel(fEnable);
	BGroupView* generalInner = new BGroupView(B_VERTICAL, 0);
	BLayoutBuilder::Group<>(generalInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fWidth)
		.Add(fTimeout)
		.AddGroup(B_HORIZONTAL, 0)
			.Add(fPlace)
			.AddGlue()
			.End();
	general->AddChild(generalInner);
	general->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	fApps = new BListView("applications");
	fApps->SetSelectionMessage(new BMessage(kMsgSelect));
	BScrollView* scroll = new BScrollView("app scroll", fApps, 0, false, true);
	scroll->SetExplicitMinSize(BSize(220, 160));
	scroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 220));
	fAdd = new BButton("add", "Add" B_UTF8_ELLIPSIS, new BMessage(kMsgAdd));
	fRemove = new BButton("remove", "Remove", new BMessage(kMsgRemove));
	fMute = new BCheckBox("mute", "Mute notifications from this application",
		new BMessage(kMsgMute));
	float buttonWidth = std::max(fAdd->PreferredSize().Width(),
		fRemove->PreferredSize().Width());
	fAdd->SetExplicitMinSize(BSize(buttonWidth, B_SIZE_UNSET));
	fRemove->SetExplicitMinSize(BSize(buttonWidth, B_SIZE_UNSET));

	BBox* applications = new BBox("applications");
	applications->SetLabel("Applications");
	BGroupView* applicationsInner = new BGroupView(B_VERTICAL,
		B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(applicationsInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(scroll)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(fAdd)
				.Add(fRemove)
				.AddGlue()
				.End()
			.End()
		.Add(fMute);
	applications->AddChild(applicationsInner);
	applications->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH,
		B_ALIGN_TOP));

	fFilter = new AppSignatureFilter();
	fPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, B_FILE_NODE, false,
		NULL, fFilter);

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(general)
			.Add(applications)
			.End()
		.AddGlue();

	if (fModule != NULL)
		ApplyState(fModule->Current());
}


NotificationsView::~NotificationsView()
{
	delete fPanel;
	delete fFilter;
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
NotificationsView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fEnable->SetTarget(this);
	fWidth->SetTarget(this);
	fTimeout->SetTarget(this);
	fPlace->SetHandler(this);
	fApps->SetTarget(this);
	fAdd->SetTarget(this);
	fRemove->SetTarget(this);
	fMute->SetTarget(this);
	if (fPanel != NULL)
		fPanel->SetTarget(this);
	_SyncSelection();
}


void
NotificationsView::ApplyState(const NotificationState& state)
{
	fUpdating = true;
	fEnable->SetValue(state.enabled ? B_CONTROL_ON : B_CONTROL_OFF);
	int32 width = (int32)(state.width / kWidthStep);
	fWidth->SetValue(width);
	fTimeout->SetValue(state.timeout);
	_SetTimeoutLabel(state.timeout);
	fPlace->SetPosition(state.position);
	_FillApps(state.apps);
	_EnableGeneral(state.enabled);
	fUpdating = false;
	_SyncSelection();
}


NotificationState
NotificationsView::_Read() const
{
	NotificationState state = factory_state();
	state.enabled = fEnable->Value() == B_CONTROL_ON;
	state.timeout = fTimeout->Value();
	state.width = (float)(fWidth->Value() * kWidthStep);
	state.position = fPlace->Position();
	for (int32 i = 0; i < fApps->CountItems(); i++) {
		AppItem* item = dynamic_cast<AppItem*>(fApps->ItemAt(i));
		if (item == NULL)
			continue;
		AppFilter app;
		app.name = item->AppName();
		app.signature = item->Signature();
		app.allow = item->Allow();
		state.apps.push_back(app);
	}
	return state;
}


void
NotificationsView::_FillApps(const std::vector<AppFilter>& apps)
{
	while (fApps->CountItems() > 0)
		delete fApps->RemoveItem((int32)0);
	for (uint32 i = 0; i < apps.size(); i++) {
		fApps->AddItem(new AppItem(apps[i].name.String(),
			apps[i].signature.String(), apps[i].allow));
	}
}


void
NotificationsView::_SyncSelection()
{
	int32 index = fApps->CurrentSelection();
	AppItem* item = index >= 0
		? dynamic_cast<AppItem*>(fApps->ItemAt(index)) : NULL;
	fRemove->SetEnabled(item != NULL);
	fMute->SetEnabled(item != NULL);
	fUpdating = true;
	if (item != NULL)
		fMute->SetValue(item->Allow() ? B_CONTROL_OFF : B_CONTROL_ON);
	else
		fMute->SetValue(B_CONTROL_OFF);
	fUpdating = false;
}


void
NotificationsView::_EnableGeneral(bool enabled)
{
	fWidth->SetEnabled(enabled);
	fTimeout->SetEnabled(enabled);
	fPlace->SetEnabled(enabled);
}


void
NotificationsView::_SetTimeoutLabel(int32 timeout)
{
	set_timeout_label(fTimeout, timeout);
}


void
NotificationsView::_Edited(bool sample)
{
	if (fUpdating || fModule == NULL)
		return;
	_SetTimeoutLabel(fTimeout->Value());
	_EnableGeneral(fEnable->Value() == B_CONTROL_ON);
	fModule->UserEdited(_Read(), sample);
}


void
NotificationsView::_Alert(const char* text)
{
	BAlert* alert = new BAlert("Notifications", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go(NULL);
}


void
NotificationsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEnable:
			_Edited(false);
			break;
		case kMsgWidth:
		case kMsgTimeout:
		case kMsgPosition:
			_Edited(true);
			break;
		case kMsgSelect:
			if (!fUpdating)
				_SyncSelection();
			break;
		case kMsgMute:
		{
			if (fUpdating || fModule == NULL)
				break;
			int32 index = fApps->CurrentSelection();
			AppItem* item = index >= 0
				? dynamic_cast<AppItem*>(fApps->ItemAt(index)) : NULL;
			if (item == NULL)
				break;
			item->SetAllow(fMute->Value() == B_CONTROL_OFF);
			_Edited(false);
			break;
		}
		case kMsgAdd:
		{
			if (fPanel == NULL)
				break;
			BMessage add(kMsgAddRef);
			fPanel->SetMessage(&add);
			fPanel->Show();
			break;
		}
		case kMsgAddRef:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;
			BEntry entry(&ref, true);
			BPath path(&entry);
			BNode node(&entry);
			char signature[B_MIME_TYPE_LENGTH];
			memset(signature, 0, sizeof(signature));
			ssize_t size = node.ReadAttr("BEOS:APP_SIG", 0, 0, signature,
				sizeof(signature) - 1);
			if (size <= 0 || signature[0] == '\0') {
				_Alert("Application does not have a valid signature.");
				break;
			}
			bool exists = false;
			for (int32 i = 0; i < fApps->CountItems(); i++) {
				AppItem* item = dynamic_cast<AppItem*>(fApps->ItemAt(i));
				if (item != NULL && strcmp(item->Signature(), signature) == 0)
					exists = true;
			}
			if (exists) {
				BString text(path.Leaf());
				text << " is already listed.";
				_Alert(text.String());
				break;
			}
			const char* name = path.Leaf();
			if (name == NULL || name[0] == '\0')
				name = signature;
			fApps->AddItem(new AppItem(name, signature, true));
			fApps->Select(fApps->CountItems() - 1);
			fApps->ScrollToSelection();
			_SyncSelection();
			_Edited(false);
			break;
		}
		case kMsgRemove:
		{
			int32 index = fApps->CurrentSelection();
			if (index < 0)
				break;
			delete fApps->RemoveItem(index);
			_SyncSelection();
			_Edited(false);
			break;
		}
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


const char*
NotificationsModule::Description() const
{
	return "How long a notification stays, where it shows, and which "
		"applications are muted.";
}


const char*
NotificationsModule::Keywords() const
{
	return "timeout position mute width popup alert";
}


NotificationsModule::NotificationsModule()
	:
	fView(NULL),
	fContext(NULL),
	fCurrent(factory_state()),
	fOriginal(factory_state()),
	fCommitted(factory_state()),
	fModified(false),
	fPendingSample(false)
{
}


NotificationsModule::~NotificationsModule()
{
}


BView*
NotificationsModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new NotificationsView(this);
	return fView;
}


void
NotificationsModule::ClearView(NotificationsView* view)
{
	if (fView == view)
		fView = NULL;
}


void
NotificationsModule::UserEdited(const NotificationState& state, bool sample)
{
	int32 iconSize = fCurrent.iconSize;
	fCurrent = state;
	fCurrent.iconSize = iconSize;
	fPendingSample = sample;
	fModified = !states_equal(fCurrent, fOriginal);
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


bool
NotificationsModule::CanApplyDefaults() const
{
	return !general_is_factory(fCurrent);
}


status_t
NotificationsModule::ApplyDefaults(SettingsContext* context)
{
	if (context != NULL)
		fContext = context;
	NotificationState next = fCurrent;
	next.enabled = true;
	next.timeout = kDefaultTimeout;
	next.width = kDefaultWidth;
	next.position = kDefaultPosition;
	status_t status = _Write(next);
	if (status != B_OK)
		return status;
	fCurrent = next;
	fCommitted = next;
	fPendingSample = false;
	fModified = !states_equal(fCurrent, fOriginal);
	_Push();
	return B_OK;
}


void
NotificationsModule::_Push()
{
	if (fView != NULL)
		fView->ApplyState(fCurrent);
}


status_t
NotificationsModule::_Write(const NotificationState& state)
{
	status_t status = write_state(state);
	if (status != B_OK) {
		syslog(LOG_ERR, "settings notifications: %s", strerror(status));
	}
	return status;
}


status_t
NotificationsModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	status_t status = B_OK;
	fCurrent = load_state(&status);
	fOriginal = fCurrent;
	fCommitted = fCurrent;
	fModified = false;
	fPendingSample = false;
	_Push();
	if (status != B_OK)
		syslog(LOG_ERR, "settings notifications: %s", strerror(status));
	return status;
}


status_t
NotificationsModule::SaveSettings(SettingsContext* context)
{
	if (context != NULL)
		fContext = context;
	status_t status = _Write(fCurrent);
	if (status != B_OK) {
		fPendingSample = false;
		return status;
	}
	fCommitted = fCurrent;
	if (fPendingSample)
		send_sample();
	fPendingSample = false;
	fModified = !states_equal(fCurrent, fOriginal);
	return B_OK;
}


status_t
NotificationsModule::RevertSettings(SettingsContext* context)
{
	if (context != NULL)
		fContext = context;
	status_t status = B_OK;
	if (!states_equal(fCommitted, fOriginal))
		status = _Write(fOriginal);
	if (status == B_OK) {
		fCurrent = fOriginal;
		fCommitted = fOriginal;
		fModified = false;
		fPendingSample = false;
	}
	_Push();
	return status;
}


void
register_notifications_page(SettingsManager& manager)
{
	manager.Add(new NotificationsModule());
}
