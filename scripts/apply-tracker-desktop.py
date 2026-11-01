#!/usr/bin/env python3
"""Tracker desktop/list overlay: #2372 #2864 #3291 #8246 #20363 #20419
#19070 #16678 #17901 #18060 #18514 #19084.
Also applies scripts/tracker-deskbar-more.diff (#5338 #16185 #11176 #18667
#7328 #16359 #15975 #15910 #13431 #16144 #16853 #9216 #20261 #16643 #19546
#11495 #11029 #16726 #11402 #8551 #19520 #8772 #19661 #19663 #12335 #19573
#8544 #8543 #9383 #19433 #12859 #14900 #16968 #14846 #9974 #4154
#7644). The leaf-menu margin no longer turns on scroll arrows (#13431).
Well-known folder names use the catalog when SYS:NAME is missing (#15527).
Icon names do not keep the list column width (#15444)."""
from __future__ import annotations

import pathlib
import subprocess
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def apply_more_diff() -> None:
    """Deskbar and Tracker fixes kept as one patch against the r5 tree."""
    marker = HAIKU / "src/kits/tracker/StatusWindow.cpp"
    if marker.is_file() and "Haiku #5338" in marker.read_text(
            encoding="utf-8", errors="replace"):
        print("already tracker-deskbar-more.diff")
        return
    diff = pathlib.Path(__file__).resolve().parent / "tracker-deskbar-more.diff"
    payload = diff.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    result = subprocess.run(
        ["patch", "-p1", "--forward", "--batch"],
        input=payload,
        cwd=HAIKU,
        capture_output=True,
    )
    if result.returncode != 0:
        out = result.stdout.decode("utf-8", "replace")
        err = result.stderr.decode("utf-8", "replace")
        raise SystemExit(f"tracker-deskbar-more.diff failed\n{out}\n{err}")
    print("patched tracker-deskbar-more.diff")


def main() -> int:
    apply_more_diff()
    pose = HAIKU / "src/kits/tracker/PoseView.cpp"
    replace_once(
        pose,
        """		case kIconMode:
		{
			const float gridOffset = ceilf(IconSizeInt() * 0.875f),
				offsetValue = ceilf(IconSizeInt() * 0.625f);
			grid->Set(IconSizeInt() + gridOffset, IconSizeInt() + gridOffset);
			offset->Set(offsetValue, offsetValue);
			break;
		}
""",
        """		case kIconMode:
		{
			const float gridOffset = ceilf(IconSizeInt() * 0.5f),
				offsetValue = ceilf(IconSizeInt() * 0.25f);
			grid->Set(IconSizeInt() + gridOffset, IconSizeInt() + gridOffset);
			offset->Set(offsetValue, offsetValue);
			break;
		}
""",
        "Haiku #2372 #18514 icon grid",
    )
    if "The scan left the screen" in pose.read_text(encoding="utf-8"):
        print("already PoseView.cpp (Haiku #2864 place scan)")
    else:
        replace_once(
            pose,
            """		if (checkValidLocation && !rect.Intersects(viewBounds)) {
			fHintLocation = PinToGrid(BPoint(0.0, 0.0), fGrid, fOffset);
			pose->SetLocation(fHintLocation, this);
			rect = pose->CalcRect(this);
			break;
		}
""",
            """		if (checkValidLocation && !rect.Intersects(viewBounds)) {
			// Do not pile leftovers at 0,0 (Haiku #2864 #8246 #20363).
			fHintLocation = PinToGrid(
				BPoint(fOffset.x + 8, fOffset.y + 8), fGrid, fOffset);
			pose->SetLocation(fHintLocation, this);
			rect = pose->CalcRect(this);
			break;
		}
""",
            "Haiku #2864 #8246 #20363 place",
        )
    if "ConvertFromScreen(&leftTop)" in pose.read_text(encoding="utf-8"):
        print("already PoseView.cpp (Haiku #3291 screen to view)")
    else:
        replace_once(
            pose,
            """		deskbarFrame.InsetBy(-10, -10);
		if (deskbarFrame.Intersects(rect))
			return false;
""",
            """		deskbarFrame.InsetBy(-24, -24);
		if (deskbarFrame.Intersects(rect))
			return false;
""",
            "Haiku #3291 deskbar inset",
        )
        replace_once(
            pose,
            """	if (GetDeskbarFrame(&deskbarFrame) == B_OK) {
		deskbarFrame.InsetBy(-24, -24);
		if (deskbarFrame.Intersects(rect))
			return false;
	}""",
            """	if (GetDeskbarFrame(&deskbarFrame) == B_OK) {
		BPoint leftTop = deskbarFrame.LeftTop();
		BPoint rightBottom = deskbarFrame.RightBottom();
		ConvertFromScreen(&leftTop);
		ConvertFromScreen(&rightBottom);
		deskbarFrame = BRect(leftTop, rightBottom);
		deskbarFrame.InsetBy(-10, -10);
		if (deskbarFrame.Intersects(rect))
			return false;
	}""",
            "Haiku #3291 screen to view",
        )
    replace_once(
        pose,
        """	SortPoses();
	UpdateCount();
	Invalidate();
}


void
BPoseView::RemoveVolumePoses()
""",
        """	SortPoses();
	if (ViewMode() != kListMode)
		CheckAutoPlacedPoses();
	UpdateCount();
	Invalidate();
}


void
BPoseView::RemoveVolumePoses()
""",
        "Haiku #2864 add volumes",
    )
    replace_once(
        pose,
        """				CreateVolumePose(&volume);
			} else if (targetModel->IsTrash()) {""",
        """				CreateVolumePose(&volume);
				if (ViewMode() != kListMode)
					CheckAutoPlacedPoses();
			} else if (targetModel->IsTrash()) {""",
        "Haiku #2864 mount",
    )
    replace_once(
        pose,
        """		BPose* pose = EntryCreated(targetModel->NodeRef(), &nodeRef, ref.name, &index);

		if (IsFiltering()) {""",
        """		BPose* pose = EntryCreated(targetModel->NodeRef(), &nodeRef, ref.name, &index);
		if (pose != NULL && ViewMode() != kListMode) {
			RemoveFromVSList(pose);
			fHintLocation = pose->Location(this);
			BRect viewBounds(Bounds());
			PlacePose(pose, viewBounds);
			AddToVSList(pose);
			pose->SetSaveLocation();
		}

		if (IsFiltering()) {""",
        "Haiku #8246 new folder",
    )
    replace_once(
        pose,
        """		if (!rect.Intersects(bounds)) {
			// pose doesn't fit on screen
			if (!locationNeedsUpdating) {
				// didn't already invalidate and remove in the desktop case
				Invalidate(rect);
				RemoveFromVSList(pose);
			}
			BPoint loc(pose->Location(this));
			loc.ConstrainTo(bounds);
				// place it onscreen

			pose->SetLocation(loc, this);
				// set the new location
			locationNeedsUpdating = true;
		}""",
        """		if (!bounds.Contains(rect) || (desktop && !IsValidLocation(rect))) {
			if (!locationNeedsUpdating) {
				Invalidate(rect);
				RemoveFromVSList(pose);
			}
			fHintLocation = pose->Location(this);
			PlacePose(pose, bounds);
			locationNeedsUpdating = true;
		}""",
        "Haiku #20363 bottom place",
    )
    replace_once(
        pose,
        """	BRect rect(fExtent.InsetByCopy(-fOffset));
	if (!rect.IsValid())
		rect.Set(LeftTop().x, LeftTop().y, LeftTop().x, LeftTop().y);
""",
        """	BRect rect(fExtent);
	if (!rect.IsValid())
		rect.Set(LeftTop().x, LeftTop().y, LeftTop().x, LeftTop().y);
""",
        "Haiku #2372 icon extent",
    )
    replace_once(
        pose,
        """	if (maxWidth > kMinColumnWidth || maxWidth < column->Width()) {
		ResizeColumn(column, maxWidth);
		return true;
	}
""",
        """	if (maxWidth > column->Width()) {
		ResizeColumn(column, maxWidth);
		return true;
	}
""",
        "Haiku #18060 size column",
    )

    container = HAIKU / "src/kits/tracker/ContainerWindow.cpp"
    replace_once(
        container,
        """	// update "Clean up" on Desktop and bail out
	if (PoseView()->IsDesktopView()) {
		Shortcuts()->UpdateCleanupItem(Shortcuts()->FindItem(parent, kCleanup, kCleanupAll));
		return;
	}
""",
        """	if (PoseView()->IsDesktopView()) {
		Shortcuts()->UpdateCleanupItem(Shortcuts()->FindItem(parent, kCleanup, kCleanupAll));
		// still offer Arrange by on the Desktop (Haiku #20419)
	}
""",
        "Haiku #20419 arrange by",
    )
    replace_once(
        container,
        """	if (PoseView()->IsDesktopView()) {
		Shortcuts()->UpdateCleanupItem(Shortcuts()->FindItem(parent, kCleanup, kCleanupAll));
		// still offer Arrange by on the Desktop (Haiku #20419)
	}

	// bail out if no "Select ..." item found
	int32 selectIndex = parent->IndexOf(parent->FindItem(kShowSelectionWindow));
	if (selectIndex == B_ERROR)
		return;

	// add "Arrange by >" menu before "Select..."
	parent->AddItem(fArrangeByItem, selectIndex);

	// mark items
	uint32 attrHash;
	int32 itemCount = fArrangeByItem->Submenu()->CountItems();
""",
        """	if (PoseView()->IsDesktopView()) {
		Shortcuts()->UpdateCleanupItem(Shortcuts()->FindItem(parent, kCleanup, kCleanupAll));
		// still offer Arrange by on the Desktop (Haiku #20419)
	}

	if (fArrangeByItem == NULL) {
		if (fAttrMenu == NULL)
			NewAttributesMenu();
		fArrangeByItem = NewArrangeByMenu();
	}

	// bail out if no "Select ..." item found
	int32 selectIndex = parent->IndexOf(parent->FindItem(kShowSelectionWindow));
	if (selectIndex == B_ERROR || fArrangeByItem == NULL)
		return;

	// add "Arrange by >" menu before "Select..."
	parent->AddItem(fArrangeByItem, selectIndex);

	BMenu* submenu = fArrangeByItem->Submenu();
	if (submenu == NULL)
		return;

	// mark items
	uint32 attrHash;
	int32 itemCount = submenu->CountItems();
""",
        "Haiku #20419 desktop arrange-by null",
    )
    replace_once(
        container,
        """		BMenuItem* item = fArrangeByItem->Submenu()->ItemAt(index);
""",
        """		BMenuItem* item = submenu->ItemAt(index);
""",
        "Haiku #20419 arrange-by item loop",
    )

    status = HAIKU / "src/kits/tracker/StatusWindow.cpp"
    replace_once(
        status,
        """		SetHighColor(0, 0, 0);
		if (Value() == B_CONTROL_ON)
			updateRect.OffsetBy(1, 1);
		FillRect(updateRect);
	} else {
		updateRect = Bounds();
		updateRect.InsetBy(9, 7);
		BRect rect(updateRect);
		rect.right -= 3;

		updateRect.left += 3;
		updateRect.OffsetBy(1, 0);
		SetHighColor(0, 0, 0);
""",
        """		SetHighUIColor(B_CONTROL_TEXT_COLOR);
		if (Value() == B_CONTROL_ON)
			updateRect.OffsetBy(1, 1);
		FillRect(updateRect);
	} else {
		updateRect = Bounds();
		updateRect.InsetBy(9, 7);
		BRect rect(updateRect);
		rect.right -= 3;

		updateRect.left += 3;
		updateRect.OffsetBy(1, 0);
		SetHighUIColor(B_CONTROL_TEXT_COLOR);
""",
        "Haiku #19070 stop/pause",
    )
    replace_once(
        status,
        """	SetHighColor(20, 20, 20);""",
        """	SetHighUIColor(B_PANEL_TEXT_COLOR);""",
        "Haiku #19070 status text",
    )

    title = HAIKU / "src/kits/tracker/TitleView.cpp"
    replace_once(
        title,
        """	view->SetHighUIColor(B_PANEL_TEXT_COLOR, pressed ? B_DARKEN_1_TINT : 1.0f);
""",
        """	view->SetHighUIColor(B_DOCUMENT_TEXT_COLOR, pressed ? B_DARKEN_1_TINT : 1.0f);
""",
        "Haiku #16678 header text",
    )
    replace_once(
        title,
        """	rgb_color baseColor = ui_color(B_CONTROL_BACKGROUND_COLOR);""",
        """	rgb_color baseColor = ui_color(B_PANEL_BACKGROUND_COLOR);""",
        "Haiku #16678 header fill",
    )
    replace_once(
        title,
        """	float tint = B_NO_TINT;
	color_which highColor = view->HighUIColor(&tint);
	view->SetHighUIColor(view->LowUIColor(), B_DARKEN_1_TINT);
	view->StrokeLine(from, to);
	view->SetHighUIColor(highColor, tint);
""",
        """	float tint = B_NO_TINT;
	color_which highColor = view->HighUIColor(&tint);
	float lineTint = view->LowColor().IsLight() ? B_DARKEN_1_TINT : B_LIGHTEN_1_TINT;
	view->SetHighUIColor(view->LowUIColor(), lineTint);
	view->StrokeLine(from, to);
	view->SetHighUIColor(highColor, tint);
""",
        "Haiku #19084 resize line",
    )

    desk = HAIKU / "src/kits/tracker/DeskWindow.cpp"
    replace_once(
        desk,
        """#include <fcntl.h>
#include <unistd.h>
""",
        """#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
""",
        "desktop log includes",
    )
    replace_once(
        desk,
        """BDeskWindow::BDeskWindow(LockingList<BWindow>* windowList, uint32 openFlags)
""",
        """static void
desk_log(const char* text)
{
	int fd = open("/dev/dprintf", O_WRONLY | O_CLOEXEC);
	if (fd < 0 || text == NULL)
		return;
	write(fd, text, strlen(text));
	close(fd);
}


BDeskWindow::BDeskWindow(LockingList<BWindow>* windowList, uint32 openFlags)
""",
        "desktop failure log",
    )
    replace_once(
        desk,
        """	// create pose view
	BDirectory deskDir;
	if (FSGetDeskDir(&deskDir) == B_OK) {
		BEntry entry;
		deskDir.GetEntry(&entry);
		Model* model = new Model(&entry, true);
		if (model->InitCheck() == B_OK)
			CreatePoseView(model);
		else
			delete model;
	}
}


BDeskWindow::~BDeskWindow()
{
	SaveDesktopPoseLocations();
""",
        """	// create pose view
	BDirectory deskDir;
	status_t deskStatus = FSGetDeskDir(&deskDir);
	if (deskStatus == B_OK) {
		BEntry entry;
		status_t entryStatus = deskDir.GetEntry(&entry);
		if (entryStatus == B_OK) {
			Model* model = new Model(&entry, true);
			if (model->InitCheck() == B_OK)
				CreatePoseView(model);
			else {
				char line[160];
				snprintf(line, sizeof(line),
					"Tracker: desktop model: %s\\n",
					strerror(model->InitCheck()));
				desk_log(line);
				delete model;
			}
		} else {
			char line[160];
			snprintf(line, sizeof(line),
				"Tracker: desktop entry: %s\\n", strerror(entryStatus));
			desk_log(line);
		}
	} else {
		char line[160];
		snprintf(line, sizeof(line),
			"Tracker: desktop directory: %s\\n", strerror(deskStatus));
		desk_log(line);
	}
}


BDeskWindow::~BDeskWindow()
{
	if (PoseView() == NULL) {
		stop_watching(this);
		return;
	}

	SaveDesktopPoseLocations();
""",
        "null desktop pose view",
    )
    replace_once(
        desk,
        """	_inherited::Init();

	entry_ref ref;
""",
        """	_inherited::Init();

	// PoseView() is null when the desktop directory did not open.
	// BShelf then calls AddFilter on that null view.
	if (PoseView() == NULL)
		return;

	entry_ref ref;
""",
        "do not shelf a missing desktop",
    )

    count = HAIKU / "src/kits/tracker/CountView.cpp"
    replace_once(
        count,
        """	rgb_color color = ViewColor();
	if (IsTypingAhead())
		color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	SetLowColor(color);
	be_control_look->DrawBorder(this, bounds, updateRect,
""",
        """	rgb_color color = ViewColor();
	if (IsTypingAhead())
		color = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	SetLowColor(color);
	SetHighColor(color);
	FillRect(bounds);
	be_control_look->DrawBorder(this, bounds, updateRect,
""",
        "Haiku #17901 count fill",
    )
    open_with = HAIKU / "src/kits/tracker/OpenWithWindow.cpp"
    replace_once(
        open_with,
        "#include <Path.h>\n",
        "#include <FindDirectory.h>\n#include <Path.h>\n",
        "Haiku #12859 find directory",
    )
    replace_once(
        open_with,
        """\t\tapp_info trackerInfo;
		if (*appModel->EntryRef() != trackerInfo.ref) {
			// this is an inactive copy of the Tracker, remove it
""",
        """\t\tapp_info trackerInfo;
		// The comparison used to read an uninitialized app_info.
		if (be_roster->GetActiveAppInfo(&trackerInfo) != B_OK
			|| *appModel->EntryRef() != trackerInfo.ref) {
			// this is an inactive copy of the Tracker, remove it
""",
        "Haiku #12859 running Tracker",
    )
    replace_once(
        open_with,
        """	if (FSInTrashDir(appModel->EntryRef()))
		return false;

	if (ShowAllApplications()) {
""",
        """	if (FSInTrashDir(appModel->EntryRef()))
		return false;

	// Tracker add-ons are for the Add-ons menu, not Open With.
	if (_InTrackerAddOns(appModel->EntryRef()))
		return false;

	if (ShowAllApplications()) {
""",
        "Haiku #12859 add-ons",
    )
    replace_once(
        open_with,
        """bool
SearchForSignatureEntryList::CanOpenWithFilter(const Model* appModel,
""",
        """static bool
_InTrackerAddOns(const entry_ref* ref)
{
	BPath path;
	if (ref == NULL || BEntry(ref).GetPath(&path) != B_OK || path.Path() == NULL)
		return false;

	const directory_which places[] = {
		B_SYSTEM_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_NONPACKAGED_ADDONS_DIRECTORY
	};
	BString full(path.Path());
	for (uint32 index = 0; index < sizeof(places) / sizeof(places[0]); index++) {
		BPath addons;
		if (find_directory(places[index], &addons) != B_OK)
			continue;
		if (addons.Append("Tracker") != B_OK)
			continue;
		BString prefix(addons.Path());
		prefix << '/';
		if (full.StartsWith(prefix))
			return true;
	}
	return false;
}


bool
SearchForSignatureEntryList::CanOpenWithFilter(const Model* appModel,
""",
        "Haiku #12859 helper",
    )

    pose_h = HAIKU / "src/kits/tracker/Pose.h"
    replace_once(
        pose_h,
        """	bool NeedsSaveLocation() const;
	void SetSaveLocation();
""",
        """	bool NeedsSaveLocation() const;
	void SetSaveLocation();
	void ClearSaveLocation();
""",
        "mount point clear save",
    )
    replace_once(
        pose_h,
        """inline void
BPose::SetSaveLocation()
{
	fNeedsSaveLocation = true;
}
""",
        """inline void
BPose::SetSaveLocation()
{
	fNeedsSaveLocation = true;
}


inline void
BPose::ClearSaveLocation()
{
	fNeedsSaveLocation = false;
}
""",
        "mount point clear save impl",
    )
    replace_once(
        pose,
        """		if (checkValidLocation && !rect.Intersects(viewBounds)) {
			// Do not pile leftovers at 0,0 (Haiku #2864 #8246 #20363).
			fHintLocation = PinToGrid(
				BPoint(fOffset.x + 8, fOffset.y + 8), fGrid, fOffset);
			pose->SetLocation(fHintLocation, this);
			rect = pose->CalcRect(this);
			break;
		}
""",
        """		if (checkValidLocation && !rect.Intersects(viewBounds)) {
			// The scan left the screen. Do not drop the icon on the
			// first cell: that cell is usually already taken, so a late
			// icon such as VBox Share lands on top of another one.
			fHintLocation = PinToGrid(
				BPoint(fOffset.x + 8, fOffset.y + 8), fGrid, fOffset);
			pose->SetLocation(fHintLocation, this);
			rect = pose->CalcRect(this);
			rect.InsetBy(-3, 0);
			for (int32 guard = 0; guard < 400
				&& SlotOccupied(rect, viewBounds); guard++) {
				NextSlot(pose, rect, viewBounds);
				if (!rect.Intersects(viewBounds))
					break;
			}
			break;
		}
		if (++slots >= 400)
			break;
""",
        "do not stack a late desktop icon",
    )
    replace_once(
        pose,
        """	// find an empty slot to put pose into
	while (SlotOccupied(rect, viewBounds)
""",
        """	// find an empty slot to put pose into
	int32 slots = 0;
	while (SlotOccupied(rect, viewBounds)
""",
        "desktop place slot cap",
    )
    replace_once(
        pose,
        """void
BPoseView::SavePoseLocations(BRect* frameIfDesktop)
""",
        """static bool
_IsMountPoint(const Model* model)
{
	// The directory's device differs from its parent's when another
	// filesystem is mounted there (VBox Share). stat is enough; do not
	// open the node for attributes.
	if (model == NULL || model->EntryRef() == NULL)
		return false;

	BEntry entry(model->EntryRef());
	BNode node(&entry);
	node_ref self;
	if (node.GetNodeRef(&self) != B_OK)
		return false;

	BEntry parent;
	if (entry.GetParent(&parent) != B_OK)
		return false;

	BNode parentNode(&parent);
	node_ref parentRef;
	if (parentNode.GetNodeRef(&parentRef) != B_OK)
		return false;

	return self.device != parentRef.device;
}


void
BPoseView::SavePoseLocations(BRect* frameIfDesktop)
""",
        "mount point helper",
    )
    replace_once(
        pose,
        """		if (pose->NeedsSaveLocation() && pose->HasLocation()) {
			Model* model = pose->TargetModel();
			poseInfo.fInvisible = false;
""",
        """		if (pose->NeedsSaveLocation() && pose->HasLocation()) {
			Model* model = pose->TargetModel();
			if (_IsMountPoint(model)) {
				// Writing the icon position into the mounted filesystem
				// blocks Tracker. The icon still moves until the next boot.
				pose->ClearSaveLocation();
				continue;
			}
			poseInfo.fInvisible = false;
""",
        "do not save a mount-point icon",
    )

    view_state = HAIKU / "src/kits/tracker/ViewState.h"
    replace_once(
        view_state,
        """	bool ReverseSort() const;

	void SetViewMode(uint32);""",
        """	bool ReverseSort() const;
	bool ReverseSecondarySort() const;

	void SetViewMode(uint32);""",
        "Haiku #14900 secondary reverse getter",
    )
    replace_once(
        view_state,
        """	void SetReverseSort(bool);

	bool StateNeedsSaving();""",
        """	void SetReverseSort(bool);
	void SetReverseSecondarySort(bool);

	bool StateNeedsSaving();""",
        "Haiku #14900 secondary reverse setter",
    )
    replace_once(
        view_state,
        """	bool fReverseSort;

	void _Init();""",
        """	bool fReverseSort;
	bool fReverseSecondarySort;

	void _Init();""",
        "Haiku #14900 secondary reverse field",
    )
    replace_once(
        view_state,
        """inline bool
BViewState::ReverseSort() const
{
	return fReverseSort;
}""",
        """inline bool
BViewState::ReverseSort() const
{
	return fReverseSort;
}


inline bool
BViewState::ReverseSecondarySort() const
{
	return fReverseSecondarySort;
}""",
        "Haiku #14900 secondary reverse inline",
    )
    replace_once(
        view_state,
        """inline void
BViewState::SetReverseSort(bool on)
{
	fReverseSort = on;
}""",
        """inline void
BViewState::SetReverseSort(bool on)
{
	fReverseSort = on;
}


inline void
BViewState::SetReverseSecondarySort(bool on)
{
	fReverseSecondarySort = on;
}""",
        "Haiku #14900 secondary reverse set inline",
    )
    view_state_cpp = HAIKU / "src/kits/tracker/ViewState.cpp"
    replace_once(
        view_state_cpp,
        """	fSecondarySortType = 0;
	fReverseSort = false;
}""",
        """	fSecondarySortType = 0;
	fReverseSort = false;
	fReverseSecondarySort = false;
}""",
        "Haiku #14900 secondary reverse init",
    )

    pose_h = HAIKU / "src/kits/tracker/PoseView.h"
    replace_once(
        pose_h,
        """	void SetReverseSort(bool reverse);
	uint32 PrimarySort() const;""",
        """	void SetReverseSort(bool reverse);
	void SetReverseSecondarySort(bool reverse);
	uint32 PrimarySort() const;""",
        "Haiku #14900 pose setter",
    )
    replace_once(
        pose_h,
        """	bool ReverseSort() const;
	void CheckPoseSortOrder(BPose*, int32 index);""",
        """	bool ReverseSort() const;
	bool ReverseSecondarySort() const;
	void CheckPoseSortOrder(BPose*, int32 index);""",
        "Haiku #14900 pose getter",
    )
    replace_once(
        pose_h,
        """inline bool
BPoseView::ReverseSort() const
{
	return fViewState->ReverseSort();
}""",
        """inline bool
BPoseView::ReverseSort() const
{
	return fViewState->ReverseSort();
}


inline bool
BPoseView::ReverseSecondarySort() const
{
	return fViewState->ReverseSecondarySort();
}""",
        "Haiku #14900 pose getter inline",
    )
    replace_once(
        pose,
        """void
BPoseView::SetReverseSort(bool reverse)
{
	fViewState->SetReverseSort(reverse);
}""",
        """void
BPoseView::SetReverseSort(bool reverse)
{
	fViewState->SetReverseSort(reverse);
}


void
BPoseView::SetReverseSecondarySort(bool reverse)
{
	fViewState->SetReverseSecondarySort(reverse);
}""",
        "Haiku #14900 pose setter body",
    )
    replace_once(
        pose,
        """	uint32 sort = view->PrimarySort();
	BColumn* column = view->ColumnFor(sort);
	if (column == NULL)
		return 0;

	BPose* primary;
	BPose* secondary;
	if (!view->ReverseSort()) {
		primary = const_cast<BPose*>(p1);
		secondary = const_cast<BPose*>(p2);
	} else {
		primary = const_cast<BPose*>(p2);
		secondary = const_cast<BPose*>(p1);
	}

	int32 result = 0;
	// We perform a loop in case there is a secondary sort
	for (int32 count = 0; ; count++) {
		BTextWidget* widget1 = primary->WidgetFor(sort);""",
        """	uint32 sort = view->PrimarySort();
	BColumn* column = view->ColumnFor(sort);
	if (column == NULL)
		return 0;

	int32 result = 0;
	// We perform a loop in case there is a secondary sort.
	// Each pass uses its own reverse flag (Haiku #14900).
	for (int32 count = 0; ; count++) {
		bool reverse = count == 0 ? view->ReverseSort()
			: view->ReverseSecondarySort();
		BPose* primary = const_cast<BPose*>(reverse ? p2 : p1);
		BPose* secondary = const_cast<BPose*>(reverse ? p1 : p2);
		BTextWidget* widget1 = primary->WidgetFor(sort);""",
        "Haiku #14900 compare secondary reverse",
    )

    title = HAIKU / "src/kits/tracker/TitleView.cpp"
    replace_once(
        title,
        """		BPoint center(titleLocation.x - 6,
			roundf((bounds.top + bounds.bottom) / 2.0));
		BPoint triangle[3];
		if (fParent->PoseView()->ReverseSort()) {""",
        """		BPoint center(titleLocation.x - 6,
			roundf((bounds.top + bounds.bottom) / 2.0));
		BPoint triangle[3];
		bool reverse = secondary
			? fParent->PoseView()->ReverseSecondarySort()
			: fParent->PoseView()->ReverseSort();
		if (reverse) {""",
        "Haiku #14900 secondary triangle",
    )
    replace_once(
        title,
        """	// For now:
	// if we hit the primary sort field again
	// then if shift key was down, switch primary and secondary
	if (hash == primarySort) {
		if (shift && secondarySort) {
			poseView->SetPrimarySort(secondarySort);
			poseView->SetSecondarySort(primarySort);
		} else
			poseView->SetReverseSort(!poseView->ReverseSort());
	} else if (shift) {
		// hit secondary sort column with shift key, disable
		if (hash == secondarySort)
			poseView->SetSecondarySort(0);
		else
			poseView->SetSecondarySort(hash);
	} else {
		poseView->SetPrimarySort(hash);
		poseView->SetReverseSort(false);
	}

	if (poseView->PrimarySort() == poseView->SecondarySort())
		poseView->SetSecondarySort(0);""",
        """	// Shift-click on the secondary column cycles ascending, descending,
	// then off. A plain click clears the secondary sort (Haiku #14900).
	// The secondary reverse flag is not written into the view-state
	// attribute, so it lasts for this open window only.
	if (hash == primarySort) {
		if (shift && secondarySort) {
			bool primaryReverse = poseView->ReverseSort();
			bool secondaryReverse = poseView->ReverseSecondarySort();
			poseView->SetPrimarySort(secondarySort);
			poseView->SetSecondarySort(primarySort);
			poseView->SetReverseSort(secondaryReverse);
			poseView->SetReverseSecondarySort(primaryReverse);
		} else {
			poseView->SetReverseSort(!poseView->ReverseSort());
			poseView->SetSecondarySort(0);
			poseView->SetReverseSecondarySort(false);
		}
	} else if (shift) {
		if (hash == secondarySort) {
			if (!poseView->ReverseSecondarySort())
				poseView->SetReverseSecondarySort(true);
			else {
				poseView->SetSecondarySort(0);
				poseView->SetReverseSecondarySort(false);
			}
		} else {
			poseView->SetSecondarySort(hash);
			poseView->SetReverseSecondarySort(false);
		}
	} else {
		poseView->SetPrimarySort(hash);
		poseView->SetReverseSort(false);
		poseView->SetSecondarySort(0);
		poseView->SetReverseSecondarySort(false);
	}

	if (poseView->PrimarySort() == poseView->SecondarySort()) {
		poseView->SetSecondarySort(0);
		poseView->SetReverseSecondarySort(false);
	}""",
        "Haiku #14900 shift-click cycle",
    )

    navigator_h = HAIKU / "src/kits/tracker/Navigator.h"
    replace_once(
        navigator_h,
        """	void UpdateLocation(const Model* newmodel, int32 action);

	BContainerWindow* Window() const;""",
        """	void UpdateLocation(const Model* newmodel, int32 action);
	BTextControl* Location() const;

	BContainerWindow* Window() const;""",
        "Haiku #16968 location accessor",
    )
    navigator = HAIKU / "src/kits/tracker/Navigator.cpp"
    replace_once(
        navigator,
        """void
BNavigator::UpdateLocation(const Model* newmodel, int32 action)
{""",
        """BTextControl*
BNavigator::Location() const
{
	return fLocation;
}


void
BNavigator::UpdateLocation(const Model* newmodel, int32 action)
{""",
        "Haiku #16968 location method",
    )

    window = HAIKU / "src/kits/tracker/ContainerWindow.cpp"
    replace_once(
        window,
        """#include <Screen.h>
#include <UnicodeChar.h>""",
        """#include <Screen.h>
#include <TextControl.h>
#include <UnicodeChar.h>""",
        "Haiku #16968 text control include",
    )
    replace_once(
        window,
        """	return B_DISPATCH_MESSAGE;
}


}	// namespace BPrivate""",
        """	return B_DISPATCH_MESSAGE;
}


filter_result
LocationSelectAllFilter(BMessage* message, BHandler**, BMessageFilter* filter)
{
	// Command+A in the location field must select the path. The Window
	// menu shortcut otherwise selects every file (Haiku #16968).
	BContainerWindow* window = dynamic_cast<BContainerWindow*>(filter->Looper());
	if (window == NULL || window->Navigator() == NULL)
		return B_DISPATCH_MESSAGE;

	BTextControl* location = window->Navigator()->Location();
	if (location == NULL || location->TextView() == NULL)
		return B_DISPATCH_MESSAGE;

	if (window->CurrentFocus() != location->TextView())
		return B_DISPATCH_MESSAGE;

	int32 modifiers = 0;
	if (message->FindInt32("modifiers", &modifiers) != B_OK)
		return B_DISPATCH_MESSAGE;

	if ((modifiers & B_COMMAND_KEY) == 0 || (modifiers & B_SHIFT_KEY) != 0)
		return B_DISPATCH_MESSAGE;

	const char* bytes = NULL;
	if (message->FindString("bytes", &bytes) != B_OK
		|| bytes == NULL || (bytes[0] != 'a' && bytes[0] != 'A')) {
		return B_DISPATCH_MESSAGE;
	}

	location->TextView()->SelectAll();
	return B_SKIP_MESSAGE;
}


}	// namespace BPrivate""",
        "Haiku #16968 select-all filter",
    )
    replace_once(
        window,
        """	AddCommonFilter(new BMessageFilter(B_MOUSE_DOWN, ActivateWindowFilter));""",
        """	AddCommonFilter(new BMessageFilter(B_MOUSE_DOWN, ActivateWindowFilter));
	AddCommonFilter(new BMessageFilter(B_KEY_DOWN, LocationSelectAllFilter));""",
        "Haiku #16968 install filter",
    )

    time_view = HAIKU / "src/apps/deskbar/TimeView.cpp"
    replace_once(
        time_view,
        """#include <string.h>

#include <Application.h>""",
        """#include <string.h>

#include <String.h>

#include <Application.h>""",
        "Haiku #14846 string include",
    )
    replace_once(
        time_view,
        """	SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);

	DrawString(fCurrentTimeStr, fTimeLocation);

	PopState();
}""",
        """	SetHighUIColor(B_MENU_ITEM_TEXT_COLOR);

	BString shown(fCurrentTimeStr);
	BPoint where(fTimeLocation);
	if (Vertical()) {
		float limit = Bounds().Width();
		if (limit > 0 && StringWidth(shown.String()) > limit) {
			// Keep the weekday when it is shown. Otherwise keep the clock.
			// Do not slide the string so the first letter is cut (Haiku #14846).
			TruncateString(&shown,
				fShowDayOfWeek ? B_TRUNCATE_END : B_TRUNCATE_BEGINNING,
				limit);
			where.x = 0;
		}
	}
	DrawString(shown.String(), where);

	PopState();
}""",
        "Haiku #14846 truncate clock",
    )
    replace_once(
        time_view,
        """	if (Vertical()) {
		float timeWidth = StringWidth(fCurrentTimeStr);
		if (timeWidth > fMaxWidth && !fShowDayOfWeek) {
			float difference = timeWidth - fMaxWidth;
			fDateLocation.x -= difference;
			fTimeLocation.x -= difference;
		} else {
			fDateLocation.x = 0.0;
			fTimeLocation.x = 0.0;
		}
	}
}""",
        """	// A vertical Deskbar that is narrower than the clock string is
	// shortened in Draw() instead of being shifted off the left edge.
}""",
        "Haiku #14846 stop left shift",
    )

    replace_once(
        pose,
        """		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
		{
			int32 index;
			BPose* pose = FindNearbyPose(key, &index);
""",
        """		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
		{
			// Ctrl+Up/Down matches Home/End on a keyboard without those
			// keys. Command+Up stays Open parent (Haiku #9974).
			if ((key == B_UP_ARROW || key == B_DOWN_ARROW)
				&& (modifiers() & (B_CONTROL_KEY | B_COMMAND_KEY
					| B_SHIFT_KEY | B_OPTION_KEY)) == B_CONTROL_KEY) {
				if (ViewMode() == kListMode) {
					int32 count = CurrentPoseList()->CountItems();
					if (count > 0) {
						MoveOrChangePoseSelection(key == B_UP_ARROW
							? 0 : count - 1);
					}
				} else
					ScrollView(key == B_UP_ARROW ? B_HOME : B_END);
				break;
			}

			int32 index;
			BPose* pose = FindNearbyPose(key, &index);
""",
        "Haiku #9974 ctrl up down",
    )

    find_panel = HAIKU / "src/kits/tracker/FindPanel.cpp"
    replace_once(
        find_panel,
        """		if (userSpecifiedName != NULL && !fEditTemplateOnly) {
			// change the name of the old query per users request
			fRef.set_name(userSpecifiedName);
			entry.SetTo(&fRef);
		}
""",
        """		if (!fEditTemplateOnly) {
			// A second Find used to keep the first file name, so the
			// result window title never followed the new text (Haiku #4154).
			if (userSpecifiedName != NULL)
				fRef.set_name(userSpecifiedName);
			else if (find) {
				BString name;
				GetDefaultName(name);
				if (name.Length() > 0)
					fRef.set_name(name.String());
			}
			entry.SetTo(&fRef);
			entry.Remove();
		}
""",
        "Haiku #4154 find title",
    )

    deskbar_menu = HAIKU / "src/apps/deskbar/DeskbarMenu.cpp"
    replace_once(
        deskbar_menu,
        'B_TRANSLATE("Show replicants")',
        'B_TRANSLATE("Show replicants handle")',
        "Haiku #7644 replicants handle",
    )

    menu = HAIKU / "src/kits/interface/Menu.cpp"
    replace_once(
        menu,
        """		*scrollOn = screenFrame.top > frame.top
			|| screenFrame.bottom < frame.bottom;
""",
        """		// The 4px placement margin is not part of this test. A leaf
		// menu that starts at the top of the screen still fits, so it
		// does not grow up and down arrows (Haiku #13431).
		BRect fullScreen = screen.Frame();
		*scrollOn = fullScreen.top > frame.top
			|| fullScreen.bottom < frame.bottom;
""",
        "Haiku #13431 no false scroll",
    )

    model = HAIKU / "src/kits/tracker/Model.cpp"
    replace_once(
        model,
        """void
Model::CacheLocalizedName()
{
	if (!fLocalizedNameIsCached) {
		fLocalizedNameIsCached = true;
		if (BLocaleRoster::Default()->GetLocalizedFileName(
				fLocalizedName, fEntryRef, true) == B_OK)
			fHasLocalizedName = true;
		else
			fHasLocalizedName = false;
	}
}
""",
        """static bool
_WellKnownLocalizedName(const node_ref* node, BString& name)
{
	if (node == NULL)
		return false;

	const WellKnowEntryList::WellKnownEntry* known
		= WellKnowEntryList::MatchEntry(node);
	if (known == NULL)
		return false;

	const char* context = NULL;
	switch (known->which) {
		case B_SYSTEM_DIRECTORY:
			context = "B_SYSTEM_DIRECTORY";
			break;
		case B_APPS_DIRECTORY:
		case B_BEOS_APPS_DIRECTORY:
			context = "B_APPS_DIRECTORY";
			break;
		case B_SYSTEM_DEVELOP_DIRECTORY:
			context = "B_SYSTEM_DEVELOP_DIRECTORY";
			break;
		case B_PREFERENCES_DIRECTORY:
		case B_BEOS_PREFERENCES_DIRECTORY:
			context = "B_PREFERENCES_DIRECTORY";
			break;
		case B_USER_DIRECTORY:
			context = "B_USER_DIRECTORY";
			break;
		case B_USER_CONFIG_DIRECTORY:
			context = "B_USER_CONFIG_DIRECTORY";
			break;
		default:
			return false;
	}

	const char* translated = B_CATALOG->GetString(known->name.String(),
		context);
	if (translated == NULL || translated[0] == '\\0')
		return false;

	name = translated;
	return true;
}


void
Model::CacheLocalizedName()
{
	if (!fLocalizedNameIsCached) {
		fLocalizedNameIsCached = true;
		if (BLocaleRoster::Default()->GetLocalizedFileName(
				fLocalizedName, fEntryRef, true) == B_OK)
			fHasLocalizedName = true;
		else {
			// Catalog strings for system, home, and the other
			// well-known folders apply when SYS:NAME is missing
			// (Haiku #15527).
			fHasLocalizedName = _WellKnownLocalizedName(NodeRef(),
				fLocalizedName);
		}
	}
}
""",
        "Haiku #15527 well-known names",
    )

    widgets = HAIKU / "src/kits/tracker/WidgetAttributeText.cpp"
    replace_once(
        widgets,
        """	bool widthChanged = view->ViewMode() == kListMode ? fColumn->Width() != fOldWidth : false;
""",
        """	// Icon mode used to keep the name truncated to the list column
	// (Haiku #15444). Refit when that column width is still in effect.
	bool widthChanged = view->ViewMode() == kListMode
		? fColumn->Width() != fOldWidth
		: fOldWidth == fColumn->Width();
""",
        "Haiku #15444 icon name width",
    )
    patch_locale_names()
    return 0


def patch_locale_names() -> None:
    """Reload catalogs and icon titles when the preferred language changes,
    and do not restore a saved position for a mounted volume."""
    model_h = HAIKU / "src/kits/tracker/Model.h"
    replace_once(
        model_h,
        """	bool HasLocalizedName() const;

private:
""",
        """	bool HasLocalizedName() const;
	void ResetLocalizedName();

private:
""",
        "reset localized name",
    )
    model = HAIKU / "src/kits/tracker/Model.cpp"
    replace_once(
        model,
        """			fHasLocalizedName = _WellKnownLocalizedName(NodeRef(),
				fLocalizedName);
		}
	}
}


void
Model::FinishSettingUpType()
""",
        """			fHasLocalizedName = _WellKnownLocalizedName(NodeRef(),
				fLocalizedName);
		}
	}
}


void
Model::ResetLocalizedName()
{
	fLocalizedNameIsCached = false;
	fHasLocalizedName = false;
	fLocalizedName.Truncate(0);
	if (gLocalizedNamePreferred && IsNodeOpen())
		CacheLocalizedName();
}


void
Model::FinishSettingUpType()
""",
        "reset localized name body",
    )

    widgets_h = HAIKU / "src/kits/tracker/WidgetAttributeText.h"
    replace_once(
        widgets_h,
        """	void SetDirty(bool);

protected:
""",
        """	void SetDirty(bool);
	virtual void ReloadValue() { SetDirty(true); }

protected:
""",
        "reload attribute text",
    )
    replace_once(
        widgets_h,
        """class StringAttributeText : public WidgetAttributeText {
public:
	StringAttributeText(const Model*, const BColumn*);
""",
        """class StringAttributeText : public WidgetAttributeText {
public:
	StringAttributeText(const Model*, const BColumn*);

	virtual void ReloadValue();
""",
        "reload string attribute",
    )
    widgets = HAIKU / "src/kits/tracker/WidgetAttributeText.cpp"
    replace_once(
        widgets,
        """void
WidgetAttributeText::SetDirty(bool value)
{
	fDirty = value;
}


// #pragma mark - StringAttributeText
""",
        """void
WidgetAttributeText::SetDirty(bool value)
{
	fDirty = value;
}


// #pragma mark - StringAttributeText


void
StringAttributeText::ReloadValue()
{
	fValueDirty = true;
	SetDirty(true);
}
""",
        "reload string attribute body",
    )

    text_h = HAIKU / "src/kits/tracker/TextWidget.h"
    replace_once(
        text_h,
        """	const char* Text(const BPoseView* view) const;
		// returns the untruncated version of the text
""",
        """	const char* Text(const BPoseView* view) const;
		// returns the untruncated version of the text
	void ReloadText();
""",
        "text widget reload",
    )
    replace_once(
        text_h,
        """inline uint32
BTextWidget::AttrHash() const
{
	return fAttrHash;
}
""",
        """inline uint32
BTextWidget::AttrHash() const
{
	return fAttrHash;
}


inline void
BTextWidget::ReloadText()
{
	if (fText != NULL)
		fText->ReloadValue();
}
""",
        "text widget reload body",
    )

    pose_h = HAIKU / "src/kits/tracker/Pose.h"
    replace_once(
        pose_h,
        """	void UpdateIcon(BPoint poseLoc, BPoseView*);
""",
        """	void UpdateIcon(BPoint poseLoc, BPoseView*);
	void ReloadLabels();
""",
        "pose reload labels",
    )
    pose = HAIKU / "src/kits/tracker/Pose.cpp"
    replace_once(
        pose,
        """void
BPose::DrawToggleSwitch(BRect, BPoseView*)
{
}
""",
        """void
BPose::DrawToggleSwitch(BRect, BPoseView*)
{
}


void
BPose::ReloadLabels()
{
	int32 count = fWidgetList.CountItems();
	for (int32 index = 0; index < count; index++) {
		BTextWidget* widget = fWidgetList.ItemAt(index);
		if (widget != NULL)
			widget->ReloadText();
	}
}
""",
        "pose reload labels body",
    )

    pose_view = HAIKU / "src/kits/tracker/PoseView.cpp"
    replace_once(
        pose_view,
        """		|| poseInfo->fLocation.y > kSanePoseLocation) {
		// location values not realistic, probably screwed up, force reset
		poseInfo->fInitedDirectory = -1LL;
	}
}
""",
        """		|| poseInfo->fLocation.y > kSanePoseLocation) {
		// location values not realistic, probably screwed up, force reset
		poseInfo->fInitedDirectory = -1LL;
	}

	// A position saved on the mounted volume is ignored. VBox Share
	// otherwise comes back on top of Trash when Tracker starts again.
	if (_IsMountPoint(model))
		poseInfo->fInitedDirectory = -1LL;
}
""",
        "ignore a saved mount-point position",
    )

    tracker = HAIKU / "src/kits/tracker/Tracker.cpp"
    replace_once(
        tracker,
        """#include "Tracker.h"
""",
        """#include "Tracker.h"


namespace BPrivate {
	void ForceUnloadCatalog();
}
""",
        "tracker catalog reload",
    )
    replace_once(
        tracker,
        """		case B_LOCALE_CHANGED:
		{
			BLocaleRoster::Default()->Refresh();
			bool localize;
			if (message->FindBool("filesys", &localize) == B_OK)
				gLocalizedNamePreferred = localize;
			break;
		}
""",
        """		case B_LOCALE_CHANGED:
		{
			// Apply the message after the file. A missing file must not
			// put English back on top of the language the user just chose.
			BLocaleRoster::Default()->Refresh();
			BLocaleRoster::Default()->ApplySettings(message);
			BPrivate::ForceUnloadCatalog();
			gLocalizedNamePreferred
				= BLocaleRoster::Default()->IsFilesystemTranslationPreferred();

			bool languageChanged = message->HasString("language");
			AutoLock<WindowList> lock(&fWindowList);
			int32 count = fWindowList.CountItems();
			for (int32 index = 0; index < count; index++) {
				BContainerWindow* window = dynamic_cast<BContainerWindow*>(
					fWindowList.ItemAt(index));
				if (window == NULL)
					continue;

				AutoLock<BWindow> windowLock(window);
				if (!windowLock.IsLocked())
					continue;

				window->LocaleChanged(languageChanged);
			}
			break;
		}
""",
        "tracker applies locale message",
    )

    container_h = HAIKU / "src/kits/tracker/ContainerWindow.h"
    replace_once(
        container_h,
        """	virtual void MenusEnded();
""",
        """	virtual void MenusEnded();
	void LocaleChanged(bool languageChanged);
""",
        "locale changed declaration",
    )
    container = HAIKU / "src/kits/tracker/ContainerWindow.cpp"
    replace_once(
        container,
        """	AddWindowContextMenu(fWindowContextMenu);
}


void
BContainerWindow::Init(const BMessage* message)
""",
        """	AddWindowContextMenu(fWindowContextMenu);
}


void
BContainerWindow::LocaleChanged(bool languageChanged)
{
	// The caller holds the window lock. RepopulateMenus() is protected.
	BPoseView* poseView = PoseView();
	if (poseView != NULL) {
		int32 poseCount = poseView->CountItems();
		for (int32 poseIndex = 0; poseIndex < poseCount; poseIndex++) {
			BPose* pose = poseView->PoseAtIndex(poseIndex);
			if (pose == NULL || pose->TargetModel() == NULL)
				continue;
			pose->TargetModel()->ResetLocalizedName();
			pose->ReloadLabels();
		}
		poseView->Invalidate();
	}
	if (languageChanged && ShouldAddMenus())
		RepopulateMenus();
}


void
BContainerWindow::Init(const BMessage* message)
""",
        "locale changed rebuilds menus",
    )


if __name__ == "__main__":
    raise SystemExit(main())
