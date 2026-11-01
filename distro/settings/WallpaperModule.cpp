/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Wallpaper page. Same Desktop attribute as Backgrounds, written here.
 * The Backgrounds program stays on the disk.
 */


#include "WallpaperModule.h"

#include "SettingsManager.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Box.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <Control.h>
#include <Cursor.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <String.h>
#include <TextControl.h>
#include <TranslationUtils.h>

#include <be_apps/Tracker/Background.h>

#include <OS.h>

#include <fs_attr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <algorithm>
#include <vector>


static const uint32 kMsgScope = 'r5ws';
static const uint32 kMsgOtherFolder = 'r5wf';
static const uint32 kMsgImage = 'r5wi';
static const uint32 kMsgOtherImage = 'r5wo';
static const uint32 kMsgPlacement = 'r5wp';
static const uint32 kMsgOffset = 'r5xy';
static const uint32 kMsgPreview = 'r5pv';
static const uint32 kMsgOutline = 'r5ol';
static const uint32 kMsgColor = 'r5cl';
static const uint32 kMsgDesktopColor = 'udsc';

static const int32 kScopeCurrent = 0;
static const int32 kScopeAll = 1;
static const int32 kScopeDefault = 2;
static const int32 kScopeFolder = 3;

static const int32 kModePosition = 0;
static const int32 kModeCenter = 1;
static const int32 kModeScale = 2;
static const int32 kModeTile = 3;

static const char* kBgInfo = "be:bgndimginfo";
static const char* kBgOutline = "be:bgndimginfoerasetext";
static const char* kBgPath = "be:bgndimginfopath";
static const char* kBgWorkspaces = "be:bgndimginfoworkspaces";
static const char* kBgOffset = "be:bgndimginfooffset";
static const char* kBgMode = "be:bgndimginfomode";
static const char* kBgSet = "be:bgndimginfoset";
static const char* kRecentFile = "Backgrounds_settings";

static const int32 kRecentLimit = 10;


class WallpaperModule;
class WallpaperView;


class ImageFilter : public BRefFilter {
public:
	virtual	bool				Filter(const entry_ref* ref, BNode* node,
									struct stat_beos* stat,
									const char* mimeType);
};


struct BgRecord {
	int32						workspace;
	BString						path;
	int32						mode;
	int32						x;
	int32						y;
	bool						outline;
	int32						imageSet;
};


class WallpaperPreview : public BView {
public:
								WallpaperPreview(WallpaperView* owner);
	virtual						~WallpaperPreview();

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint point);
	virtual	void				MouseUp(BPoint point);
	virtual	void				MouseMoved(BPoint point, uint32 transit,
									const BMessage* message);

			void				SetPicture(const char* path);
			void				SetMode(int32 mode);
			void				SetOffset(int32 x, int32 y);
			void				SetColor(rgb_color color);
			void				SetDragEnabled(bool enabled);

private:
			WallpaperView*		fOwner;
			BBitmap*			fBitmap;
			BString				fPath;
			int32				fMode;
			float				fX;
			float				fY;
			rgb_color			fColor;
			bool				fDragEnabled;
			bool				fDragging;
			BPoint				fDragStart;
			float				fDragX;
			float				fDragY;
			float				fXRatio;
			float				fYRatio;
};


class WallpaperView : public BGroupView {
public:
								WallpaperView(WallpaperModule* module);
	virtual						~WallpaperView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
			void				Apply();

private:
			void				_FillImageMenu();
			void				_MarkScope();
			void				_MarkPlacement();
			void				_LoadRecent();
			void				_SaveRecent();
			void				_AddRecent(const char* path);
			void				_Targets();

			WallpaperModule*	fModule;
			WallpaperPreview*	fPreview;
			BPopUpMenu*			fWorkspaceMenu;
			BPopUpMenu*			fImageMenu;
			BPopUpMenu*			fPlacementMenu;
			BTextControl*		fX;
			BTextControl*		fY;
			BCheckBox*			fOutline;
			BColorControl*		fPicker;
			BFilePanel*			fImagePanel;
			BFilePanel*			fFolderPanel;
			ImageFilter			fImageFilter;
			std::vector<BString>	fRecent;
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
		{ return "Picture, placement, and color behind the icons."; }
	virtual	const char*			Keywords() const
		{ return "background wallpaper image color workspace folder"; }

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
			bool				Outline() const { return fOutline; }
			rgb_color			Color() const { return fColor; }
			bool				IsDesktop() const { return fDesktop; }
			int32				Scope() const { return fScope; }
			const char*			NodePath() const { return fNodePath.String(); }
			int32				PictureCount() const
									{ return (int32)fPictures.size(); }
			const char*			PictureAt(int32 index) const;

			void				SetPicture(const char* path);
			void				SetMode(int32 mode);
			void				SetOffset(int32 x, int32 y);
			void				SetOutline(bool outline);
			void				SetColor(rgb_color color);
			status_t			SelectScope(int32 scope, const char* folder);
			void				AddPicture(const char* path);
			void				ClearView(WallpaperView* view);

private:
			status_t			_LoadDesktop();
			status_t			_LoadNode(const char* path, bool desktop);
			void				_AdoptRecord();
			void				_RememberPicture(const char* path);
			void				_ScanArtwork();
			void				_Changed();

			WallpaperView*		fView;
			SettingsContext*	fContext;
			std::vector<BgRecord>	fRecords;
			std::vector<BString>	fPictures;
			BString				fNodePath;
			BString				fPath;
			int32				fScope;
			int32				fMode;
			int32				fOffsetX;
			int32				fOffsetY;
			bool				fOutline;
			int32				fImageSet;
			rgb_color			fColor;
			bool				fDesktop;
			bool				fModified;
};


static bool
same_color(rgb_color a, rgb_color b)
{
	return a.red == b.red && a.green == b.green && a.blue == b.blue
		&& a.alpha == b.alpha;
}


static int32
workspace_bit()
{
	uint32 mask = 1;
	int32 workspace = current_workspace();
	while (workspace > 0) {
		mask <<= 1;
		workspace--;
	}
	return (int32)mask;
}


static int32
find_record(const std::vector<BgRecord>& records, int32 mask)
{
	int32 fallback = -1;
	uint32 bit = (uint32)mask;
	for (int32 i = 0; i < (int32)records.size(); i++) {
		uint32 workspace = (uint32)records[i].workspace;
		if (workspace == bit)
			return i;
		if ((workspace & bit) != 0)
			fallback = i;
	}
	return fallback;
}


static bool
image_file(const char* path, BNode& node)
{
	BNodeInfo info(&node);
	char type[B_MIME_TYPE_LENGTH];
	if (info.GetType(type) == B_OK && strncmp(type, "image/", 6) == 0)
		return true;

	const char* leaf = strrchr(path, '/');
	leaf = leaf != NULL ? leaf + 1 : path;
	const char* dot = strrchr(leaf, '.');
	if (dot == NULL)
		return false;
	BString extension(dot);
	return extension.ICompare(".png") == 0 || extension.ICompare(".jpg") == 0
		|| extension.ICompare(".jpeg") == 0 || extension.ICompare(".gif") == 0
		|| extension.ICompare(".bmp") == 0 || extension.ICompare(".tga") == 0
		|| extension.ICompare(".tif") == 0 || extension.ICompare(".tiff") == 0
		|| extension.ICompare(".webp") == 0;
}


static bool
leaf_less(const BString& left, const BString& right)
{
	BPath leftPath(left.String());
	BPath rightPath(right.String());
	BString leftLeaf(leftPath.InitCheck() == B_OK
		? leftPath.Leaf() : left.String());
	BString rightLeaf(rightPath.InitCheck() == B_OK
		? rightPath.Leaf() : right.String());
	int compare = leftLeaf.ICompare(rightLeaf);
	if (compare == 0)
		return left.ICompare(right) < 0;
	return compare < 0;
}


static status_t
ensure_directory(const char* path)
{
	status_t status = create_directory(path, 0755);
	if (status == B_FILE_EXISTS)
		return B_OK;
	return status;
}


static status_t
default_folder(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append("Tracker");
	status = ensure_directory(path.Path());
	if (status != B_OK)
		return status;
	path.Append("DefaultFolderTemplate");
	return ensure_directory(path.Path());
}


static status_t
read_records(BNode& node, std::vector<BgRecord>& records)
{
	records.clear();
	attr_info info;
	if (node.GetAttrInfo(kBgInfo, &info) != B_OK || info.size <= 0)
		return B_OK;

	char* buffer = new(std::nothrow) char[info.size];
	if (buffer == NULL)
		return B_NO_MEMORY;
	ssize_t bytes = node.ReadAttr(kBgInfo, B_MESSAGE_TYPE, 0, buffer,
		info.size);
	BMessage stored;
	status_t status = bytes == info.size ? stored.Unflatten(buffer) : B_ERROR;
	delete[] buffer;
	if (status != B_OK)
		return status;

	for (int32 index = 0; ; index++) {
		const char* picture = NULL;
		if (stored.FindString(kBgPath, index, &picture) != B_OK)
			break;
		BgRecord record;
		record.path = picture != NULL ? picture : "";
		record.workspace = (int32)B_ALL_WORKSPACES;
		record.mode = kModePosition;
		record.x = 0;
		record.y = 0;
		record.outline = false;
		record.imageSet = 0;
		stored.FindInt32(kBgWorkspaces, index, &record.workspace);
		stored.FindInt32(kBgMode, index, &record.mode);
		stored.FindBool(kBgOutline, index, &record.outline);
		stored.FindInt32(kBgSet, index, &record.imageSet);
		BPoint offset;
		if (stored.FindPoint(kBgOffset, index, &offset) == B_OK) {
			record.x = (int32)offset.x;
			record.y = (int32)offset.y;
		}
		if (record.mode < kModePosition || record.mode > kModeTile)
			record.mode = kModePosition;
		records.push_back(record);
	}
	return B_OK;
}


static status_t
write_records(BNode& node, const std::vector<BgRecord>& records, bool desktop)
{
	BMessage stored;
	for (int32 i = 0; i < (int32)records.size(); i++) {
		const BgRecord& record = records[i];
		stored.AddBool(kBgOutline, record.outline);
		stored.AddString(kBgPath, record.path.String());
		stored.AddInt32(kBgWorkspaces, record.workspace);
		stored.AddPoint(kBgOffset, BPoint(record.x, record.y));
		stored.AddInt32(kBgMode, record.mode);
		if (desktop)
			stored.AddInt32(kBgSet, record.imageSet);
	}

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
	ssize_t written = node.WriteAttr(kBgInfo, B_MESSAGE_TYPE, 0, buffer, flat);
	delete[] buffer;
	if (written < B_OK)
		return written;
	if (written != flat)
		return B_ERROR;
	return B_OK;
}


struct NotifyJob {
	bool						desktop;
	bool						custom;
	char						path[B_PATH_NAME_LENGTH];
};


static void
notify_tracker(const NotifyJob& job)
{
	BMessenger tracker("application/x-vnd.Be-TRAK");
	if (!tracker.IsValid()) {
		syslog(LOG_ERR, "settings wallpaper: Tracker is not running\n");
		return;
	}
	if (job.desktop) {
		tracker.SendMessage(B_RESTORE_BACKGROUND_IMAGE);
		BMessenger screen("application/x-vnd.Haiku-Screen");
		if (screen.IsValid())
			screen.SendMessage(kMsgDesktopColor);
		return;
	}

	for (int32 i = 0; ; i++) {
		BMessage message(B_GET_PROPERTY);
		message.AddSpecifier("Poses");
		message.AddSpecifier("Window", i);
		BMessage reply;
		tracker.SendMessage(&message, &reply);

		int32 error = B_OK;
		if (reply.what == B_MESSAGE_NOT_UNDERSTOOD
			&& reply.FindInt32("error", &error) == B_OK) {
			if (error == B_BAD_INDEX)
				break;
			if (error != B_BAD_SCRIPT_SYNTAX)
				continue;
		}

		BMessenger window;
		if (reply.FindMessenger("result", &window) != B_OK)
			continue;

		if (job.custom) {
			BMessage pathMessage(B_GET_PROPERTY);
			pathMessage.AddSpecifier("Path");
			pathMessage.AddSpecifier("Poses");
			pathMessage.AddSpecifier("Window", i);
			BMessage pathReply;
			tracker.SendMessage(&pathMessage, &pathReply);
			if (pathReply.what == B_MESSAGE_NOT_UNDERSTOOD)
				continue;
			entry_ref ref;
			if (pathReply.FindRef("result", &ref) == B_OK) {
				BPath path(&ref);
				if (path.InitCheck() != B_OK
					|| strcmp(path.Path(), job.path) != 0)
					continue;
			}
		}
		window.SendMessage(B_RESTORE_BACKGROUND_IMAGE);
	}
}


static int32
notify_thread(void* data)
{
	NotifyJob* job = static_cast<NotifyJob*>(data);
	if (job != NULL) {
		notify_tracker(*job);
		delete job;
	}
	return 0;
}


static void
start_notify(bool desktop, bool custom, const char* path)
{
	NotifyJob* job = new(std::nothrow) NotifyJob;
	if (job == NULL)
		return;
	job->desktop = desktop;
	job->custom = custom;
	job->path[0] = '\0';
	if (path != NULL)
		strlcpy(job->path, path, sizeof(job->path));
	thread_id thread = spawn_thread(notify_thread, "wallpaper notify",
		B_NORMAL_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		notify_thread(job);
	}
}


bool
ImageFilter::Filter(const entry_ref* ref, BNode* node, struct stat_beos* stat,
	const char* mimeType)
{
	(void)stat;
	(void)mimeType;
	if (ref == NULL || node == NULL)
		return false;
	if (node->IsDirectory())
		return true;
	BPath path(ref);
	if (path.InitCheck() != B_OK)
		return false;
	return image_file(path.Path(), *node);
}


WallpaperPreview::WallpaperPreview(WallpaperView* owner)
	:
	BView("preview", B_WILL_DRAW),
	fOwner(owner),
	fBitmap(NULL),
	fMode(kModePosition),
	fX(0),
	fY(0),
	fColor({255, 255, 255, 255}),
	fDragEnabled(false),
	fDragging(false),
	fXRatio(1),
	fYRatio(1)
{
	BRect screen = BScreen().Frame();
	float width = screen.Width();
	float height = screen.Height();
	if (width < 1 || height < 1) {
		width = 1024;
		height = 768;
	}
	float previewWidth = 180.0f;
	float previewHeight = ceilf(previewWidth * height / width);
	SetExplicitMinSize(BSize(previewWidth, previewHeight));
	SetExplicitMaxSize(BSize(previewWidth, previewHeight));
}


WallpaperPreview::~WallpaperPreview()
{
	delete fBitmap;
}


void
WallpaperPreview::SetPicture(const char* path)
{
	if (path == NULL)
		path = "";
	if (fPath == path)
		return;
	fPath = path;
	delete fBitmap;
	fBitmap = NULL;
	if (fPath.Length() > 0)
		fBitmap = BTranslationUtils::GetBitmap(fPath.String());
	Invalidate();
}


void
WallpaperPreview::SetMode(int32 mode)
{
	fMode = mode;
	Invalidate();
}


void
WallpaperPreview::SetOffset(int32 x, int32 y)
{
	fX = x;
	fY = y;
	Invalidate();
}


void
WallpaperPreview::SetColor(rgb_color color)
{
	fColor = color;
	SetViewColor(color);
	Invalidate();
}


void
WallpaperPreview::SetDragEnabled(bool enabled)
{
	fDragEnabled = enabled;
}


void
WallpaperPreview::Draw(BRect updateRect)
{
	(void)updateRect;
	SetLowColor(fColor);
	FillRect(Bounds(), B_SOLID_LOW);
	if (fBitmap != NULL && Window() != NULL) {
		BRect screen = BScreen(Window()).Frame();
		float screenWidth = screen.Width();
		float screenHeight = screen.Height();
		if (screenWidth < 1)
			screenWidth = 1;
		if (screenHeight < 1)
			screenHeight = 1;
		float xRatio = Bounds().Width() / screenWidth;
		float yRatio = Bounds().Height() / screenHeight;
		BRect bitmap = fBitmap->Bounds();
		if (fMode == kModeScale) {
			DrawBitmap(fBitmap, bitmap, Bounds());
		} else if (fMode == kModeTile) {
			float tileWidth = bitmap.Width() * xRatio;
			float tileHeight = bitmap.Height() * yRatio;
			if (tileWidth < 1)
				tileWidth = 1;
			if (tileHeight < 1)
				tileHeight = 1;
			for (float top = 0; top < Bounds().Height(); top += tileHeight) {
				for (float left = 0; left < Bounds().Width();
					left += tileWidth) {
					BRect dest(left, top, left + tileWidth, top + tileHeight);
					DrawBitmap(fBitmap, bitmap, dest);
				}
			}
		} else {
			float destWidth = bitmap.Width() * xRatio;
			float destHeight = bitmap.Height() * yRatio;
			float left = fX * xRatio;
			float top = fY * yRatio;
			if (fMode == kModeCenter) {
				left = (Bounds().Width() - destWidth) / 2;
				top = (Bounds().Height() - destHeight) / 2;
			}
			BRect dest(left, top, left + destWidth, top + destHeight);
			DrawBitmap(fBitmap, bitmap, dest);
		}
	}
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	StrokeRect(Bounds());
}


void
WallpaperPreview::MouseDown(BPoint point)
{
	if (!fDragEnabled || !Bounds().Contains(point) || fBitmap == NULL)
		return;
	uint32 buttons = 0;
	GetMouse(&point, &buttons);
	if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
		return;
	BRect screen = BScreen(Window()).Frame();
	float screenWidth = screen.Width();
	float screenHeight = screen.Height();
	if (screenWidth < 1)
		screenWidth = 1;
	if (screenHeight < 1)
		screenHeight = 1;
	fXRatio = Bounds().Width() / screenWidth;
	fYRatio = Bounds().Height() / screenHeight;
	if (fXRatio == 0)
		fXRatio = 1;
	if (fYRatio == 0)
		fYRatio = 1;
	fDragging = true;
	fDragStart = point;
	fDragX = fX;
	fDragY = fY;
	SetMouseEventMask(B_POINTER_EVENTS,
		B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
	BCursor cursor(B_CURSOR_ID_GRABBING);
	SetViewCursor(&cursor);
}


void
WallpaperPreview::MouseUp(BPoint point)
{
	(void)point;
	if (!fDragging)
		return;
	fDragging = false;
	BCursor cursor(B_CURSOR_ID_GRAB);
	SetViewCursor(&cursor);
}


void
WallpaperPreview::MouseMoved(BPoint point, uint32 transit,
	const BMessage* message)
{
	(void)transit;
	(void)message;
	if (!fDragging || fOwner == NULL)
		return;
	float x = fDragX + (point.x - fDragStart.x) / fXRatio;
	float y = fDragY + (point.y - fDragStart.y) / fYRatio;
	fX = x;
	fY = y;
	Invalidate();
	BMessage update(kMsgPreview);
	update.AddInt32("x", (int32)x);
	update.AddInt32("y", (int32)y);
	BMessenger(fOwner).SendMessage(&update);
}


WallpaperView::WallpaperView(WallpaperModule* module)
	:
	BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fPreview(NULL),
	fWorkspaceMenu(NULL),
	fImageMenu(NULL),
	fPlacementMenu(NULL),
	fX(NULL),
	fY(NULL),
	fOutline(NULL),
	fPicker(NULL),
	fImagePanel(NULL),
	fFolderPanel(NULL),
	fAdjusting(false)
{
	fPreview = new WallpaperPreview(this);

	fX = new BTextControl("x", "X:", "", new BMessage(kMsgOffset));
	fY = new BTextControl("y", "Y:", "", new BMessage(kMsgOffset));
	fX->TextView()->SetAlignment(B_ALIGN_RIGHT);
	fY->TextView()->SetAlignment(B_ALIGN_RIGHT);
	fX->TextView()->SetMaxBytes(5);
	fY->TextView()->SetMaxBytes(5);
	for (int32 i = 0; i < 256; i++) {
		if ((i < '0' || i > '9') && i != '-') {
			fX->TextView()->DisallowChar(i);
			fY->TextView()->DisallowChar(i);
		}
	}
	fX->SetExplicitMaxSize(BSize(90, B_SIZE_UNSET));
	fY->SetExplicitMaxSize(BSize(90, B_SIZE_UNSET));

	BBox* previewBox = new BBox("preview");
	previewBox->SetLabel("Preview");
	BLayoutBuilder::Group<>(previewBox, B_VERTICAL, 0)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGlue()
		.AddGroup(B_HORIZONTAL, 0)
			.AddGlue()
			.Add(fPreview)
			.AddGlue()
			.End()
		.AddStrut(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fX)
			.Add(fY)
			.AddGlue()
			.End()
		.AddGlue();

	fWorkspaceMenu = new BPopUpMenu("workspace");
	BMessage* all = new BMessage(kMsgScope);
	all->AddInt32("scope", kScopeAll);
	fWorkspaceMenu->AddItem(new BMenuItem("All workspaces", all));
	BMessage* current = new BMessage(kMsgScope);
	current->AddInt32("scope", kScopeCurrent);
	BMenuItem* currentItem = new BMenuItem("Current workspace", current);
	fWorkspaceMenu->AddItem(currentItem);
	currentItem->SetMarked(true);
	fWorkspaceMenu->AddSeparatorItem();
	BMessage* fallback = new BMessage(kMsgScope);
	fallback->AddInt32("scope", kScopeDefault);
	fWorkspaceMenu->AddItem(new BMenuItem("Default folder", fallback));
	fWorkspaceMenu->AddItem(new BMenuItem("Other folder" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOtherFolder)));

	BMenuField* workspaceField = new BMenuField("workspace", NULL,
		fWorkspaceMenu);
	BBox* rightBox = new BBox("background");
	rightBox->SetLabel(workspaceField);

	fImageMenu = new BPopUpMenu("image");
	BMenuField* imageField = new BMenuField("image", "Image:", fImageMenu);
	imageField->SetAlignment(B_ALIGN_RIGHT);

	fPlacementMenu = new BPopUpMenu("placement");
	struct item { const char* label; int32 mode; };
	const item items[] = {
		{ "Manual", kModePosition },
		{ "Center", kModeCenter },
		{ "Scale to fit", kModeScale },
		{ "Tile", kModeTile }
	};
	for (uint32 i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		BMessage* message = new BMessage(kMsgPlacement);
		message->AddInt32("mode", items[i].mode);
		fPlacementMenu->AddItem(new BMenuItem(items[i].label, message));
	}
	BMenuField* placementField = new BMenuField("placement", "Placement:",
		fPlacementMenu);
	placementField->SetAlignment(B_ALIGN_RIGHT);

	fOutline = new BCheckBox("Icon label outline", new BMessage(kMsgOutline));
	fPicker = new BColorControl(BPoint(0, 0), B_CELLS_32x8, 8.0f, "color",
		new BMessage(kMsgColor));

	BLayoutBuilder::Group<>(rightBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(imageField->CreateLabelLayoutItem(), 0, 0)
			.AddGroup(B_HORIZONTAL, 0, 1, 0)
				.Add(imageField->CreateMenuBarLayoutItem(), 0.0f)
				.AddGlue()
				.End()
			.Add(placementField->CreateLabelLayoutItem(), 0, 1)
			.AddGroup(B_HORIZONTAL, 0, 1, 1)
				.Add(placementField->CreateMenuBarLayoutItem(), 0.0f)
				.AddGlue()
				.End()
			.Add(fOutline, 1, 2)
			.End()
		.AddGlue()
		.Add(fPicker);

	float align = floorf(rightBox->TopBorderOffset()
		- previewBox->TopBorderOffset()) - 1;
	if (align < 0)
		align = 0;

	BLayoutBuilder::Group<>(this)
		.SetInsets(B_USE_SMALL_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_SMALL_SPACING, B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, 0, 0)
			.AddStrut(align)
			.Add(previewBox)
			.End()
		.Add(rightBox);

	_LoadRecent();
	Apply();
}


WallpaperView::~WallpaperView()
{
	delete fFolderPanel;
	delete fImagePanel;
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
WallpaperView::_Targets()
{
	fWorkspaceMenu->SetTargetForItems(this);
	fImageMenu->SetTargetForItems(this);
	fPlacementMenu->SetTargetForItems(this);
	fX->SetTarget(this);
	fY->SetTarget(this);
	fOutline->SetTarget(this);
	fPicker->SetTarget(this);
}


void
WallpaperView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	if (fImagePanel == NULL) {
		BPath artwork;
		entry_ref ref;
		if (find_directory(B_SYSTEM_DATA_DIRECTORY, &artwork) == B_OK) {
			artwork.Append("artwork");
			get_ref_for_path(artwork.Path(), &ref);
		}
		fImagePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), &ref,
			B_FILE_NODE, false, NULL, &fImageFilter);
		fImagePanel->SetButtonLabel(B_DEFAULT_BUTTON, "Select");
		fFolderPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
			B_DIRECTORY_NODE, false, NULL);
		fFolderPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Select");
	}
	_Targets();
}


void
WallpaperView::_LoadRecent()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kRecentFile);
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;
	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;
	BString folder;
	for (int32 i = 0; settings.FindString("recentfolder", i, &folder) == B_OK;
		i++) {
		if ((int32)fRecent.size() >= kRecentLimit)
			break;
		_AddRecent(folder.String());
	}
}


void
WallpaperView::_SaveRecent()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append(kRecentFile);
	BMessage settings;
	BFile file(path.Path(), B_READ_WRITE | B_CREATE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Unflatten(&file);
	settings.RemoveName("recentfolder");
	for (int32 i = 0; i < (int32)fRecent.size(); i++)
		settings.AddString("recentfolder", fRecent[i].String());
	file.Seek(0, SEEK_SET);
	if (settings.Flatten(&file) == B_OK)
		file.SetSize(file.Position());
}


void
WallpaperView::_AddRecent(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;
	for (int32 i = 0; i < (int32)fRecent.size(); i++) {
		if (fRecent[i] == path)
			return;
	}
	if ((int32)fRecent.size() >= kRecentLimit) {
		fRecent.erase(fRecent.begin());
		if (fWorkspaceMenu->CountItems() > 6)
			delete fWorkspaceMenu->RemoveItem((int32)6);
	}
	if (fRecent.empty() && fWorkspaceMenu->CountItems() == 5)
		fWorkspaceMenu->AddSeparatorItem();
	BMessage* message = new BMessage(kMsgScope);
	message->AddInt32("scope", kScopeFolder);
	message->AddString("path", path);
	BPath folder(path);
	BString label("Folder: ");
	label << (folder.InitCheck() == B_OK ? folder.Leaf() : path);
	fWorkspaceMenu->AddItem(new BMenuItem(label.String(), message));
	fRecent.push_back(path);
}


void
WallpaperView::_FillImageMenu()
{
	while (fImageMenu->CountItems() > 0)
		delete fImageMenu->RemoveItem((int32)0);

	BMessage* none = new BMessage(kMsgImage);
	none->AddString("path", "");
	const char* noneLabel = "None";
	if (fModule != NULL && !fModule->IsDesktop())
		noneLabel = "Default";
	fImageMenu->AddItem(new BMenuItem(noneLabel, none));
	fImageMenu->AddSeparatorItem();

	std::vector<BString> pictures;
	if (fModule != NULL) {
		for (int32 i = 0; i < fModule->PictureCount(); i++)
			pictures.push_back(fModule->PictureAt(i));
	}
	std::sort(pictures.begin(), pictures.end(), leaf_less);
	const char* selected = fModule != NULL ? fModule->Path() : "";
	bool marked = false;
	for (int32 i = 0; i < (int32)pictures.size(); i++) {
		BMessage* message = new BMessage(kMsgImage);
		message->AddString("path", pictures[i].String());
		BPath path(pictures[i].String());
		const char* leaf = path.InitCheck() == B_OK
			? path.Leaf() : pictures[i].String();
		BMenuItem* item = new BMenuItem(leaf, message);
		fImageMenu->AddItem(item);
		if (selected != NULL && pictures[i] == selected) {
			item->SetMarked(true);
			marked = true;
		}
	}
	fImageMenu->AddSeparatorItem();
	fImageMenu->AddItem(new BMenuItem("Other" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOtherImage)));
	if (!marked && fImageMenu->ItemAt(0) != NULL)
		fImageMenu->ItemAt(0)->SetMarked(true);
	if (Window() != NULL)
		fImageMenu->SetTargetForItems(this);
}


void
WallpaperView::_MarkScope()
{
	if (fModule == NULL)
		return;
	int32 scope = fModule->Scope();
	const char* node = fModule->NodePath();
	for (int32 i = 0; i < fWorkspaceMenu->CountItems(); i++) {
		BMenuItem* item = fWorkspaceMenu->ItemAt(i);
		BMessage* message = item->Message();
		if (message == NULL)
			continue;
		int32 itemScope = -1;
		if (message->FindInt32("scope", &itemScope) != B_OK)
			continue;
		bool mark = itemScope == scope && scope != kScopeFolder;
		if (scope == kScopeFolder && itemScope == kScopeFolder) {
			const char* path = NULL;
			message->FindString("path", &path);
			mark = path != NULL && node != NULL && strcmp(path, node) == 0;
		}
		if (mark)
			item->SetMarked(true);
	}
}


void
WallpaperView::_MarkPlacement()
{
	int32 mode = fModule != NULL ? fModule->Mode() : kModePosition;
	bool desktop = fModule != NULL && fModule->IsDesktop();
	BMenu* menu = fPlacementMenu;
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		BMessage* message = item->Message();
		int32 itemMode = kModePosition;
		if (message != NULL)
			message->FindInt32("mode", &itemMode);
		item->SetMarked(itemMode == mode);
		if (itemMode == kModeCenter || itemMode == kModeScale)
			item->SetEnabled(desktop);
	}
}


void
WallpaperView::Apply()
{
	fAdjusting = true;
	_FillImageMenu();
	_MarkScope();
	_MarkPlacement();

	const char* path = fModule != NULL ? fModule->Path() : "";
	int32 mode = fModule != NULL ? fModule->Mode() : kModePosition;
	bool hasPicture = path != NULL && path[0] != '\0';
	bool manual = hasPicture && mode == kModePosition;
	fPlacementMenu->SetEnabled(hasPicture);
	fX->SetEnabled(manual);
	fY->SetEnabled(manual);
	char buffer[32];
	if (manual) {
		snprintf(buffer, sizeof(buffer), "%" B_PRId32,
			fModule != NULL ? fModule->OffsetX() : 0);
		fX->SetText(buffer);
		snprintf(buffer, sizeof(buffer), "%" B_PRId32,
			fModule != NULL ? fModule->OffsetY() : 0);
		fY->SetText(buffer);
	} else {
		fX->SetText("");
		fY->SetText("");
	}
	fX->TextView()->MakeSelectable(manual);
	fY->TextView()->MakeSelectable(manual);
	fX->TextView()->MakeEditable(manual);
	fY->TextView()->MakeEditable(manual);

	bool outline = fModule != NULL && fModule->Outline();
	fOutline->SetValue(outline ? B_CONTROL_ON : B_CONTROL_OFF);

	rgb_color color = {255, 255, 255, 255};
	bool desktop = false;
	if (fModule != NULL) {
		color = fModule->Color();
		desktop = fModule->IsDesktop();
	}
	fPicker->SetEnabled(desktop);
	fPicker->SetValue(color);

	fPreview->SetColor(color);
	fPreview->SetPicture(path);
	fPreview->SetMode(mode);
	fPreview->SetOffset(fModule != NULL ? fModule->OffsetX() : 0,
		fModule != NULL ? fModule->OffsetY() : 0);
	fPreview->SetDragEnabled(manual);
	fAdjusting = false;
	if (Window() != NULL)
		_Targets();
}


void
WallpaperView::MessageReceived(BMessage* message)
{
	if (fAdjusting) {
		BGroupView::MessageReceived(message);
		return;
	}
	if (message->WasDropped() && fPicker->IsEnabled()) {
		const void* data = NULL;
		ssize_t size = 0;
		if (message->FindData("RGBColor", B_RGB_COLOR_TYPE, &data, &size)
			== B_OK && size >= (ssize_t)sizeof(rgb_color) && fModule != NULL) {
			rgb_color color = *static_cast<const rgb_color*>(data);
			fModule->SetColor(color);
			Apply();
			return;
		}
	}

	switch (message->what) {
		case kMsgScope:
		{
			int32 scope = kScopeCurrent;
			if (message->FindInt32("scope", &scope) != B_OK || fModule == NULL)
				break;
			const char* path = NULL;
			message->FindString("path", &path);
			status_t status = fModule->SelectScope(scope, path);
			Apply();
			if (status != B_OK) {
				BString text("Could not open that folder.\n");
				text << strerror(status);
				BAlert* alert = new BAlert("Settings", text.String(), "OK",
					NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				alert->Go(NULL);
			}
			break;
		}
		case kMsgOtherFolder:
			if (fFolderPanel != NULL)
				fFolderPanel->Show();
			break;
		case kMsgOtherImage:
			if (fImagePanel != NULL)
				fImagePanel->Show();
			break;
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK || fModule == NULL)
				break;
			BNode node(&ref);
			BPath path(&ref);
			if (node.InitCheck() != B_OK || path.InitCheck() != B_OK)
				break;
			if (node.IsDirectory()) {
				_AddRecent(path.Path());
				_SaveRecent();
				_Targets();
				status_t status = fModule->SelectScope(kScopeFolder,
					path.Path());
				Apply();
				if (status != B_OK) {
					BString text("Could not open that folder.\n");
					text << strerror(status);
					BAlert* alert = new BAlert("Settings", text.String(),
						"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					alert->Go(NULL);
				}
			} else {
				fModule->AddPicture(path.Path());
				fModule->SetPicture(path.Path());
				Apply();
			}
			break;
		}
		case kMsgImage:
		{
			const char* path = NULL;
			if (message->FindString("path", &path) == B_OK && fModule != NULL)
				fModule->SetPicture(path);
			Apply();
			break;
		}
		case kMsgPlacement:
		{
			int32 mode = kModePosition;
			if (message->FindInt32("mode", &mode) == B_OK && fModule != NULL)
				fModule->SetMode(mode);
			Apply();
			break;
		}
		case kMsgOffset:
			if (fModule != NULL) {
				fModule->SetOffset(atoi(fX->Text()), atoi(fY->Text()));
				fPreview->SetOffset(fModule->OffsetX(), fModule->OffsetY());
			}
			break;
		case kMsgPreview:
		{
			int32 x = 0;
			int32 y = 0;
			if (message->FindInt32("x", &x) != B_OK
				|| message->FindInt32("y", &y) != B_OK || fModule == NULL)
				break;
			fAdjusting = true;
			char buffer[32];
			snprintf(buffer, sizeof(buffer), "%" B_PRId32, x);
			fX->SetText(buffer);
			snprintf(buffer, sizeof(buffer), "%" B_PRId32, y);
			fY->SetText(buffer);
			fAdjusting = false;
			fModule->SetOffset(x, y);
			break;
		}
		case kMsgOutline:
			if (fModule != NULL)
				fModule->SetOutline(fOutline->Value() == B_CONTROL_ON);
			break;
		case kMsgColor:
			if (fModule != NULL) {
				fModule->SetColor(fPicker->ValueAsColor());
				fPreview->SetColor(fModule->Color());
			}
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


WallpaperModule::WallpaperModule()
	:
	fView(NULL),
	fContext(NULL),
	fScope(kScopeCurrent),
	fMode(kModePosition),
	fOffsetX(0),
	fOffsetY(0),
	fOutline(true),
	fImageSet(0),
	fColor({255, 255, 255, 255}),
	fDesktop(true),
	fModified(false)
{
}


WallpaperModule::~WallpaperModule()
{
}


const char*
WallpaperModule::PictureAt(int32 index) const
{
	if (index < 0 || index >= (int32)fPictures.size())
		return "";
	return fPictures[index].String();
}


BView*
WallpaperModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new WallpaperView(this);
	return fView;
}


void
WallpaperModule::ClearView(WallpaperView* view)
{
	if (fView == view)
		fView = NULL;
}


void
WallpaperModule::_RememberPicture(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return;
	for (int32 i = 0; i < (int32)fPictures.size(); i++) {
		if (fPictures[i] == path)
			return;
	}
	fPictures.push_back(path);
}


void
WallpaperModule::_ScanArtwork()
{
	BPath path;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &path) != B_OK)
		return;
	path.Append("artwork");
	BDirectory directory(path.Path());
	if (directory.InitCheck() != B_OK)
		return;
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsFile())
			continue;
		BPath file;
		if (entry.GetPath(&file) != B_OK)
			continue;
		BNode node(&entry);
		if (node.InitCheck() != B_OK || !image_file(file.Path(), node))
			continue;
		_RememberPicture(file.Path());
	}
}


void
WallpaperModule::_AdoptRecord()
{
	fPath = "";
	fMode = kModePosition;
	fOffsetX = 0;
	fOffsetY = 0;
	fOutline = true;
	fImageSet = 0;
	int32 index = -1;
	if (fDesktop)
		index = find_record(fRecords, workspace_bit());
	else if (!fRecords.empty())
		index = 0;
	if (index < 0)
		return;
	const BgRecord& record = fRecords[index];
	fPath = record.path;
	fMode = record.mode;
	fOffsetX = record.x;
	fOffsetY = record.y;
	fOutline = record.outline;
	fImageSet = record.imageSet;
	if (fMode < kModePosition || fMode > kModeTile)
		fMode = kModePosition;
	_RememberPicture(fPath.String());
}


status_t
WallpaperModule::_LoadNode(const char* path, bool desktop)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	BNode node(path);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;
	std::vector<BgRecord> records;
	status = read_records(node, records);
	if (status != B_OK)
		return status;
	fRecords.swap(records);
	fNodePath = path;
	fDesktop = desktop;
	_AdoptRecord();
	if (desktop)
		fColor = BScreen().DesktopColor();
	else {
		fColor.red = 255;
		fColor.green = 255;
		fColor.blue = 255;
		fColor.alpha = 255;
	}
	return B_OK;
}


status_t
WallpaperModule::_LoadDesktop()
{
	BPath path;
	status_t status = find_directory(B_DESKTOP_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return _LoadNode(path.Path(), true);
}


status_t
WallpaperModule::LoadSettings(SettingsContext* context)
{
	(void)context;
	_ScanArtwork();
	fScope = kScopeCurrent;
	status_t status = _LoadDesktop();
	fModified = false;
	return status;
}


status_t
WallpaperModule::SaveSettings(SettingsContext* context)
{
	(void)context;
	if (fNodePath.Length() == 0)
		return B_NO_INIT;

	std::vector<BgRecord> next = fRecords;
	BgRecord edited;
	edited.path = fPath;
	edited.mode = fMode;
	edited.x = fOffsetX;
	edited.y = fOffsetY;
	edited.outline = fOutline;
	edited.imageSet = fImageSet;
	edited.workspace = (int32)B_ALL_WORKSPACES;

	if (!fDesktop) {
		if (next.empty())
			next.push_back(edited);
		else {
			int32 workspace = next[0].workspace;
			int32 imageSet = next[0].imageSet;
			next[0] = edited;
			next[0].workspace = workspace;
			next[0].imageSet = imageSet;
		}
	} else if (fScope == kScopeAll) {
		if (!next.empty())
			edited.imageSet = next[0].imageSet;
		edited.workspace = (int32)B_ALL_WORKSPACES;
		next.clear();
		next.push_back(edited);
		int32 count = count_workspaces();
		for (int32 i = 0; i < count; i++)
			BScreen().SetDesktopColor(fColor, i, true);
	} else {
		int32 mask = workspace_bit();
		int32 index = find_record(next, mask);
		if (index >= 0) {
			uint32 workspace = (uint32)next[index].workspace;
			uint32 bit = (uint32)mask;
			if ((workspace & bit) != 0 && workspace != bit) {
				next[index].workspace = (int32)(workspace ^ bit);
				edited.workspace = mask;
				edited.imageSet = next[index].imageSet;
				next.push_back(edited);
			} else {
				int32 imageSet = next[index].imageSet;
				next[index] = edited;
				next[index].workspace = mask;
				next[index].imageSet = imageSet;
			}
		} else {
			edited.workspace = mask;
			next.push_back(edited);
		}
		BScreen().SetDesktopColor(fColor, true);
	}

	BNode node(fNodePath.String());
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;
	status = write_records(node, next, fDesktop);
	if (status != B_OK) {
		syslog(LOG_ERR, "settings wallpaper: write failed: %s\n",
			strerror(status));
		return status;
	}
	fRecords.swap(next);
	fModified = false;

	bool custom = false;
	if (!fDesktop) {
		BPath fallback;
		if (default_folder(fallback) != B_OK
			|| strcmp(fallback.Path(), fNodePath.String()) != 0)
			custom = true;
	}
	start_notify(fDesktop, custom, fNodePath.String());
	return B_OK;
}


status_t
WallpaperModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	int32 scope = fScope;
	BString node = fNodePath;
	status_t status = B_OK;
	if (scope == kScopeFolder || scope == kScopeDefault)
		status = _LoadNode(node.String(), false);
	else {
		fScope = kScopeCurrent;
		status = _LoadDesktop();
	}
	if (status == B_OK)
		fScope = scope == kScopeAll ? kScopeCurrent : scope;
	fModified = false;
	if (fView != NULL)
		fView->Apply();
	return status;
}


status_t
WallpaperModule::SelectScope(int32 scope, const char* folder)
{
	if ((scope == kScopeCurrent || scope == kScopeAll) && fDesktop
		&& (fScope == kScopeCurrent || fScope == kScopeAll)) {
		if (fScope != scope) {
			fScope = scope;
			_Changed();
		}
		return B_OK;
	}

	BPath path;
	status_t status = B_OK;
	bool desktop = true;
	if (scope == kScopeDefault) {
		status = default_folder(path);
		desktop = false;
	} else if (scope == kScopeFolder) {
		if (folder == NULL || folder[0] == '\0')
			return B_BAD_VALUE;
		status = path.SetTo(folder);
		desktop = false;
	} else {
		status = find_directory(B_DESKTOP_DIRECTORY, &path);
		desktop = true;
	}
	if (status != B_OK)
		return status;
	status = _LoadNode(path.Path(), desktop);
	if (status != B_OK)
		return status;
	fScope = scope;
	if (desktop && scope == kScopeAll) {
		_Changed();
		return B_OK;
	}
	fModified = false;
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
	return B_OK;
}


void
WallpaperModule::SetPicture(const char* path)
{
	if (path == NULL)
		path = "";
	if (fPath == path)
		return;
	fPath = path;
	_Changed();
}


void
WallpaperModule::SetMode(int32 mode)
{
	if (fMode == mode)
		return;
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
WallpaperModule::SetOutline(bool outline)
{
	if (fOutline == outline)
		return;
	fOutline = outline;
	_Changed();
}


void
WallpaperModule::SetColor(rgb_color color)
{
	if (same_color(fColor, color))
		return;
	fColor = color;
	_Changed();
}


void
WallpaperModule::AddPicture(const char* path)
{
	_RememberPicture(path);
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
