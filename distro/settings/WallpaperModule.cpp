/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Wallpaper page. Writes the same Desktop attribute as Backgrounds.
 * One picture for every workspace. Color, per-workspace pictures, and
 * folder backgrounds stay in the existing Backgrounds program.
 */


#include "WallpaperModule.h"

#include "SettingsManager.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Button.h>
#include <Control.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Node.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>

#include <be_apps/Tracker/Background.h>

#include <fs_attr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>


static const uint32 kMsgChoose = 'r5wc';
static const uint32 kMsgNone = 'r5wn';
static const uint32 kMsgPlacement = 'r5wp';
static const uint32 kMsgOffset = 'r5wo';
static const uint32 kMsgOpenBackgrounds = 'r5wb';

static const char* kBgInfo = "be:bgndimginfo";
static const char* kBgOutline = "be:bgndimginfoerasetext";
static const char* kBgPath = "be:bgndimginfopath";
static const char* kBgWorkspaces = "be:bgndimginfoworkspaces";
static const char* kBgOffset = "be:bgndimginfooffset";
static const char* kBgMode = "be:bgndimginfomode";
static const char* kBgSet = "be:bgndimginfoset";

static const int32 kModePosition = 0;
static const int32 kModeCenter = 1;
static const int32 kModeScale = 2;
static const int32 kModeTile = 3;


class WallpaperModule;


class WallpaperView : public BGroupView {
public:
								WallpaperView(WallpaperModule* module,
									SettingsContext* context);
	virtual						~WallpaperView();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				Apply();

private:
			WallpaperModule*	fModule;
			SettingsContext*	fContext;
			BFilePanel*			fPanel;
			BStringView*		fPathView;
			BMenuField*			fPlacement;
			BTextControl*		fX;
			BTextControl*		fY;
			bool				fAdjusting;
};


class WallpaperModule : public SettingsModule {
public:
								WallpaperModule();
	virtual						~WallpaperModule();

	virtual	const char*			Identifier() const { return "wallpaper"; }
	virtual	const char*			Name() const { return "Wallpaper"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
		{ return "One picture behind the icons, on every workspace."; }
	virtual	const char*			Keywords() const
									{ return "background wallpaper image"; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const { return fModified; }

			const char*			Path() const { return fPath.String(); }
			int32				Mode() const { return fMode; }
			int32				OffsetX() const { return fOffsetX; }
			int32				OffsetY() const { return fOffsetY; }
			void				SetPath(const char* path);
			void				SetMode(int32 mode);
			void				SetOffset(int32 x, int32 y);
			void				ClearView(WallpaperView* view);

private:
			void				_Changed();

			WallpaperView*		fView;
			SettingsContext*	fContext;
			BString				fPath;
			int32				fMode;
			int32				fOffsetX;
			int32				fOffsetY;
			bool				fOutline;
			int32				fImageSet;
			bool				fModified;
};


static status_t
desktop_node(BNode& node)
{
	BPath path;
	status_t status = find_directory(B_DESKTOP_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return node.SetTo(path.Path());
}


static status_t
read_desktop(BString& path, int32& mode, int32& x, int32& y, bool& outline,
	int32& imageSet)
{
	path = "";
	mode = kModeScale;
	x = 0;
	y = 0;
	outline = false;
	imageSet = 0;

	BNode node;
	status_t status = desktop_node(node);
	if (status != B_OK)
		return status;

	attr_info info;
	if (node.GetAttrInfo(kBgInfo, &info) != B_OK || info.size <= 0)
		return B_OK;

	char* buffer = new(std::nothrow) char[info.size];
	if (buffer == NULL)
		return B_NO_MEMORY;
	ssize_t bytes = node.ReadAttr(kBgInfo, B_MESSAGE_TYPE, 0, buffer,
		info.size);
	BMessage stored;
	status = bytes == info.size ? stored.Unflatten(buffer) : B_ERROR;
	delete[] buffer;
	if (status != B_OK)
		return status;

	const char* picture = NULL;
	if (stored.FindString(kBgPath, 0, &picture) == B_OK && picture != NULL)
		path = picture;
	stored.FindInt32(kBgMode, 0, &mode);
	BPoint offset;
	if (stored.FindPoint(kBgOffset, 0, &offset) == B_OK) {
		x = (int32)offset.x;
		y = (int32)offset.y;
	}
	stored.FindBool(kBgOutline, 0, &outline);
	stored.FindInt32(kBgSet, 0, &imageSet);
	if (mode < kModePosition || mode > kModeTile)
		mode = kModeScale;
	return B_OK;
}


static status_t
write_desktop(const char* path, int32 mode, int32 x, int32 y, bool outline,
	int32 imageSet)
{
	BMessage stored;
	stored.AddBool(kBgOutline, outline);
	stored.AddString(kBgPath, path != NULL ? path : "");
	stored.AddInt32(kBgWorkspaces, (int32)B_ALL_WORKSPACES);
	stored.AddPoint(kBgOffset, BPoint(x, y));
	stored.AddInt32(kBgMode, mode);
	stored.AddInt32(kBgSet, imageSet);

	ssize_t flat = stored.FlattenedSize();
	if (flat < B_OK)
		return flat;
	char* buffer = new(std::nothrow) char[flat];
	if (buffer == NULL)
		return B_NO_MEMORY;
	status_t status = stored.Flatten(buffer, flat);
	if (status != B_OK) {
		delete[] buffer;
		return status;
	}

	BNode node;
	status = desktop_node(node);
	if (status != B_OK) {
		delete[] buffer;
		return status;
	}
	ssize_t written = node.WriteAttr(kBgInfo, B_MESSAGE_TYPE, 0, buffer, flat);
	delete[] buffer;
	if (written < B_OK)
		return written;
	if (written != flat)
		return B_ERROR;

	BMessenger tracker("application/x-vnd.Be-TRAK");
	if (tracker.IsValid())
		tracker.SendMessage(B_RESTORE_BACKGROUND_IMAGE);
	else
		syslog(LOG_ERR, "settings wallpaper: Tracker is not running\n");
	return B_OK;
}


WallpaperView::WallpaperView(WallpaperModule* module, SettingsContext* context)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fContext(context),
	fPanel(NULL),
	fPathView(NULL),
	fPlacement(NULL),
	fX(NULL),
	fY(NULL),
	fAdjusting(false)
{
	fPathView = new BStringView("path", "No picture");
	fPathView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BPopUpMenu* menu = new BPopUpMenu("placement");
	struct item { const char* label; int32 mode; };
	const item items[] = {
		{ "Center", kModeCenter },
		{ "Fill the screen", kModeScale },
		{ "Tile", kModeTile },
		{ "Position", kModePosition }
	};
	for (uint32 i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		BMessage* message = new BMessage(kMsgPlacement);
		message->AddInt32("mode", items[i].mode);
		menu->AddItem(new BMenuItem(items[i].label, message));
	}
	fPlacement = new BMenuField("placement", "Placement:", menu);

	fX = new BTextControl("x", "X:", "0", NULL);
	fY = new BTextControl("y", "Y:", "0", NULL);
	Apply();
	fX->SetModificationMessage(new BMessage(kMsgOffset));
	fY->SetModificationMessage(new BMessage(kMsgOffset));

	BStringView* note = new BStringView("note",
		"Save uses this picture on every workspace.");
	note->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this)
		.Add(fPathView)
		.Add(new BButton("choose", "Choose picture", new BMessage(kMsgChoose)))
		.Add(new BButton("none", "No picture", new BMessage(kMsgNone)))
		.Add(fPlacement)
		.AddGroup(B_HORIZONTAL)
			.Add(fX)
			.Add(fY)
		.End()
		.Add(note)
		.Add(new BButton("more", "More wallpaper options",
			new BMessage(kMsgOpenBackgrounds)))
		.AddGlue();
}


WallpaperView::~WallpaperView()
{
	delete fPanel;
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
WallpaperView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	if (fPanel == NULL) {
		fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
			B_FILE_NODE, false);
	}
	for (int32 i = 0; i < CountChildren(); i++) {
		BControl* control = dynamic_cast<BControl*>(ChildAt(i));
		if (control != NULL)
			control->SetTarget(this);
	}
	if (fPlacement->Menu() != NULL)
		fPlacement->Menu()->SetTargetForItems(this);
	fX->SetTarget(this);
	fY->SetTarget(this);
}


void
WallpaperView::Apply()
{
	fAdjusting = true;
	const char* path = fModule != NULL ? fModule->Path() : "";
	fPathView->SetText(path != NULL && path[0] != '\0' ? path : "No picture");
	int32 mode = fModule != NULL ? fModule->Mode() : kModeScale;
	BMenu* menu = fPlacement->Menu();
	if (menu != NULL) {
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			BMessage* message = item->Message();
			int32 itemMode = kModeScale;
			if (message != NULL)
				message->FindInt32("mode", &itemMode);
			item->SetMarked(itemMode == mode);
		}
	}
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%" B_PRId32,
		fModule != NULL ? fModule->OffsetX() : 0);
	fX->SetText(buffer);
	snprintf(buffer, sizeof(buffer), "%" B_PRId32,
		fModule != NULL ? fModule->OffsetY() : 0);
	fY->SetText(buffer);
	bool position = mode == kModePosition;
	fX->SetEnabled(position);
	fY->SetEnabled(position);
	fAdjusting = false;
}


void
WallpaperView::MessageReceived(BMessage* message)
{
	if (fAdjusting) {
		BGroupView::MessageReceived(message);
		return;
	}
	switch (message->what) {
		case kMsgChoose:
			if (fPanel != NULL)
				fPanel->Show();
			break;
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK || fModule == NULL)
				break;
			BPath path(&ref);
			if (path.InitCheck() == B_OK)
				fModule->SetPath(path.Path());
			Apply();
			break;
		}
		case kMsgNone:
			if (fModule != NULL)
				fModule->SetPath("");
			Apply();
			break;
		case kMsgPlacement:
		{
			int32 mode = kModeScale;
			if (message->FindInt32("mode", &mode) == B_OK && fModule != NULL)
				fModule->SetMode(mode);
			Apply();
			break;
		}
		case kMsgOffset:
			if (fModule != NULL) {
				fModule->SetOffset(atoi(fX->Text()), atoi(fY->Text()));
			}
			break;
		case kMsgOpenBackgrounds:
		{
			status_t status = B_NO_INIT;
			if (fContext != NULL) {
				status = fContext->LaunchLegacy(
					"application/x-vnd.Haiku-Backgrounds");
			}
			if (status != B_OK) {
				BAlert* alert = new BAlert("Settings",
					"Could not open Backgrounds.", "OK", NULL, NULL,
					B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->Go(NULL);
			}
			break;
		}
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


WallpaperModule::WallpaperModule()
	:
	fView(NULL),
	fContext(NULL),
	fMode(kModeScale),
	fOffsetX(0),
	fOffsetY(0),
	fOutline(false),
	fImageSet(0),
	fModified(false)
{
}


WallpaperModule::~WallpaperModule()
{
}


BView*
WallpaperModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new WallpaperView(this, context);
	return fView;
}


void
WallpaperModule::ClearView(WallpaperView* view)
{
	if (fView == view)
		fView = NULL;
}


status_t
WallpaperModule::LoadSettings(SettingsContext* context)
{
	(void)context;
	fModified = false;
	return read_desktop(fPath, fMode, fOffsetX, fOffsetY, fOutline, fImageSet);
}


status_t
WallpaperModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	status_t status = write_desktop(fPath.String(), fMode, fOffsetX, fOffsetY,
		fOutline, fImageSet);
	if (status == B_OK)
		fModified = false;
	return status;
}


status_t
WallpaperModule::RevertSettings(SettingsContext* context)
{
	status_t status = LoadSettings(context);
	if (fView != NULL)
		fView->Apply();
	return status;
}


void
WallpaperModule::SetPath(const char* path)
{
	fPath = path != NULL ? path : "";
	_Changed();
}


void
WallpaperModule::SetMode(int32 mode)
{
	fMode = mode;
	_Changed();
}


void
WallpaperModule::SetOffset(int32 x, int32 y)
{
	if (fOffsetX == x && fOffsetY == y)
		return;
	fOffsetX = x;
	fOffsetY = y;
	_Changed();
}


void
WallpaperModule::_Changed()
{
	fModified = true;
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


void
register_wallpaper_page(SettingsManager& manager)
{
	manager.Add(new WallpaperModule());
}
