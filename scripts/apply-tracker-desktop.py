#!/usr/bin/env python3
"""Tracker desktop/list overlay: #2372 #2864 #3291 #8246 #20363 #20419
#19070 #16678 #17901 #18060 #18514 #19084.
Also applies scripts/tracker-deskbar-more.diff (#5338 #16185 #11176 #18667
#7328 #16359 #15975 #15910 #13431 #16144 #16853 #9216 #20261 #16643 #19546
#11495 #11029 #16726 #11402 #8551 #19520 #8772 #19661 #19663 #12335 #19573
#8544 #8543 #9383 #19433)."""
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
