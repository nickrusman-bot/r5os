/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "Sidebar.h"

#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <ScrollView.h>
#include <Window.h>

#include <new>
#include <string.h>
#include <vector>


namespace BAppearanceKit {


struct Section {
	BSidebarItem*				item;
	std::vector<BSidebarItem*>	children;
};


struct BSidebar::Data {
	int32				hover;
	bool				single;
	bool				filtering;
	bool				inList;
	BString				filter;
	BString				expandedBefore;
	BString				footer;
	std::vector<Section> sections;
};


static const char* kSidebarClassName = "BAppearanceKit::BSidebar";


BSidebar::BSidebar(const char* name)
	:
	BOutlineListView(name, B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS),
	fData(NULL)
{
	_Init();
}


BSidebar::BSidebar(BMessage* archive)
	:
	BOutlineListView(archive),
	fData(NULL)
{
	_Init();
	bool single = true;
	if (archive != NULL && archive->FindBool("r5:single", &single) == B_OK)
		fData->single = single;
}


BSidebar::~BSidebar()
{
	if (fData != NULL && !fData->inList) {
		for (uint32 s = 0; s < fData->sections.size(); s++) {
			delete fData->sections[s].item;
			for (uint32 c = 0; c < fData->sections[s].children.size(); c++)
				delete fData->sections[s].children[c];
		}
	}
	delete fData;
}


BArchivable*
BSidebar::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kSidebarClassName))
		return NULL;
	return new BSidebar(archive);
}


status_t
BSidebar::Archive(BMessage* archive, bool deep) const
{
	status_t status = BOutlineListView::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kSidebarClassName) != B_OK)
		status = archive->AddString("class", kSidebarClassName);
	if (status == B_OK)
		status = archive->AddBool("r5:single", fData->single);
	return status;
}


void
BSidebar::Draw(BRect updateRect)
{
	int32 count = CountItems();
	BRect itemFrame(0, 0, Bounds().right, -1);
	BRect group;
	bool haveGroup = false;
	for (int32 i = 0; i < count; i++) {
		BSidebarItem* item = _ItemAt(i);
		itemFrame.bottom = itemFrame.top + ceilf(ItemAt(i)->Height()) - 1;
		bool inGroup = false;
		bool first = false;
		bool last = false;
		if (item != NULL && item->OutlineLevel() == 0 && item->IsExpanded()
			&& CountItemsUnder(item, true) > 0) {
			inGroup = true;
			first = true;
			group = itemFrame;
			haveGroup = true;
		} else if (haveGroup && item != NULL && item->OutlineLevel() > 0) {
			inGroup = true;
			group.bottom = itemFrame.bottom;
			last = (i + 1 >= count) || ItemAt(i + 1)->OutlineLevel() == 0;
		} else
			haveGroup = false;
		if (item != NULL)
			item->_SetRole(inGroup, first, last);
		itemFrame.top = itemFrame.bottom + 1;
	}

	itemFrame.Set(0, 0, Bounds().right, -1);
	haveGroup = false;
	BRect paint;
	for (int32 i = 0; i < count; i++) {
		BListItem* raw = ItemAt(i);
		itemFrame.bottom = itemFrame.top + ceilf(raw->Height()) - 1;
		BSidebarItem* item = dynamic_cast<BSidebarItem*>(raw);
		if (item != NULL && item->fInGroup && item->fFirst) {
			paint = itemFrame;
			haveGroup = true;
		} else if (haveGroup && item != NULL && item->fInGroup)
			paint.bottom = itemFrame.bottom;
		if (haveGroup && item != NULL && item->fLast) {
			float radius = be_plain_font->Size() * 0.25f;
			SetHighColor(mix_color(ui_color(B_LIST_BACKGROUND_COLOR),
				ui_color(B_CONTROL_HIGHLIGHT_COLOR), 40));
			FillRoundRect(paint, radius, radius);
			haveGroup = false;
		}
		itemFrame.top = itemFrame.bottom + 1;
	}

	BListView::Draw(updateRect);
	if (fData->footer.Length() == 0)
		return;
	float itemsBottom = -1;
	BRect row(0, 0, Bounds().right, -1);
	for (int32 i = 0; i < count; i++) {
		row.bottom = row.top + ceilf(ItemAt(i)->Height()) - 1;
		itemsBottom = row.bottom;
		row.top = row.bottom + 1;
	}
	float u = be_plain_font->Size() / 13.0f;
	float inset = u * 4.0f;
	float pad = u * 10.0f;
	BFont font(be_plain_font);
	font.SetSize(u * 11.0f);
	BFont strong(font);
	strong.SetFace(B_BOLD_FACE);
	SetFont(&font);
	font_height line;
	font.GetHeight(&line);
	float text = line.ascent + line.descent;
	float boxHeight = text * 2 + pad * 2;
	float top = Bounds().bottom - boxHeight - inset;
	if (top < itemsBottom + inset)
		return;
	BRect box(inset, top, Bounds().right - inset, top + boxHeight);
	// #f3f6fb and #dbe3ef have no system colour. A light mix of the
	// list background and the control highlight stands in for both.
	SetHighColor(mix_color(ui_color(B_LIST_BACKGROUND_COLOR),
		ui_color(B_CONTROL_HIGHLIGHT_COLOR), 18));
	FillRect(box);
	SetHighColor(mix_color(ui_color(B_LIST_BACKGROUND_COLOR),
		ui_color(B_CONTROL_HIGHLIGHT_COLOR), 48));
	StrokeRect(box);
	SetFont(&strong);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	float y = box.top + pad + line.ascent;
	const char* tip = "Tip:";
	DrawString(tip, BPoint(box.left + pad, y));
	float used = strong.StringWidth(tip) + u * 4.0f;
	SetFont(&font);
	SetHighColor(mix_color(ui_color(B_LIST_BACKGROUND_COLOR),
		ui_color(B_LIST_ITEM_TEXT_COLOR), 150));
	BString rest(fData->footer);
	float room = box.Width() - pad * 2 - used;
	int32 fit = rest.Length();
	while (fit > 0 && font.StringWidth(rest.String(), fit) > room)
		fit--;
	while (fit > 0 && fit < rest.Length() && rest.ByteAt(fit) != ' ')
		fit--;
	if (fit <= 0)
		fit = rest.Length();
	BString first(rest);
	first.Truncate(fit);
	DrawString(first.String(), BPoint(box.left + pad + used, y));
	if (fit < rest.Length()) {
		const char* next = rest.String() + fit;
		while (*next == ' ')
			next++;
		y += text * 1.45f;
		DrawString(next, BPoint(box.left + pad, y));
	}
	SetFont(be_plain_font);
}


void
BSidebar::DrawItem(BListItem* item, BRect frame, bool complete)
{
	if (item != NULL)
		item->DrawItem(this, frame, complete);
}


void
BSidebar::DrawLatch(BRect itemRect, int32 level, bool collapsed,
	bool highlighted, bool misTracked)
{
	// The count or the down arrow is the only mark on the right.
	(void)itemRect;
	(void)level;
	(void)collapsed;
	(void)highlighted;
	(void)misTracked;
}


void
BSidebar::MouseDown(BPoint where)
{
	// A click can fold or unfold, which moves the rows.
	_SetHover(-1);
	int32 index = IndexOf(where);
	BSidebarItem* item = index >= 0 ? _ItemAt(index) : NULL;
	if (item == NULL || !item->IsEnabled())
		return;
	if (item->OutlineLevel() == 0 && CountItemsUnder(item, true) > 0) {
		Select(index);
		ExpandOnly(item);
		return;
	}
	BOutlineListView::MouseDown(where);
}


void
BSidebar::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	int32 index = -1;
	if (transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW
		&& dragMessage == NULL) {
		index = IndexOf(where);
		BSidebarItem* item = index >= 0 ? _ItemAt(index) : NULL;
		if (item == NULL || !item->IsEnabled() || item->_IsSeparator())
			index = -1;
	}
	_SetHover(index);
	BOutlineListView::MouseMoved(where, transit, dragMessage);
}


void
BSidebar::_SetHover(int32 index)
{
	if (fData == NULL || index == fData->hover)
		return;
	fData->hover = index;
	// Folding and Search change the row order, so clear every item, not
	// only the one that was marked before.
	for (int32 i = 0; i < FullListCountItems(); i++) {
		BSidebarItem* item = dynamic_cast<BSidebarItem*>(FullListItemAt(i));
		if (item != NULL)
			item->_SetHovered(false);
	}
	BSidebarItem* item = index >= 0 ? _ItemAt(index) : NULL;
	if (item != NULL)
		item->_SetHovered(true);
	Invalidate();
}


void
BSidebar::KeyDown(const char* bytes, int32 numBytes)
{
	if (bytes == NULL || numBytes < 1) {
		BOutlineListView::KeyDown(bytes, numBytes);
		return;
	}
	int32 index = CurrentSelection();
	BSidebarItem* item = index >= 0 ? _ItemAt(index) : NULL;
	switch (bytes[0]) {
		case B_RIGHT_ARROW:
			if (item != NULL && item->OutlineLevel() == 0
				&& CountItemsUnder(item, true) > 0)
				ExpandOnly(item);
			return;
		case B_LEFT_ARROW:
			if (item != NULL && item->OutlineLevel() == 0
				&& item->IsExpanded())
				Collapse(item);
			else if (item != NULL && item->OutlineLevel() > 0) {
				BSidebarItem* parent = dynamic_cast<BSidebarItem*>(
					Superitem(item));
				if (parent != NULL)
					Collapse(parent);
			}
			return;
		case B_ENTER:
			if (SelectionMessage() != NULL && Window() != NULL)
				Window()->PostMessage(SelectionMessage(), this);
			return;
		case B_UP_ARROW:
		case B_DOWN_ARROW:
			BOutlineListView::KeyDown(bytes, numBytes);
			index = CurrentSelection();
			while (index >= 0 && index < CountItems()
				&& _ItemAt(index) != NULL && !_ItemAt(index)->IsEnabled()) {
				index += bytes[0] == B_DOWN_ARROW ? 1 : -1;
			}
			if (index >= 0 && index < CountItems())
				Select(index);
			return;
		default:
			BOutlineListView::KeyDown(bytes, numBytes);
			break;
	}
}


void
BSidebar::SetSingleExpand(bool single)
{
	fData->single = single;
}


bool
BSidebar::IsSingleExpand() const
{
	return fData->single;
}


void
BSidebar::ExpandOnly(BSidebarItem* section)
{
	BMessage* saved = SelectionMessage();
	BMessage copy;
	if (saved != NULL)
		copy = *saved;
	SetSelectionMessage(NULL);
	for (int32 i = 0; i < FullListCountItems(); i++) {
		BSidebarItem* item = dynamic_cast<BSidebarItem*>(
			FullListItemAt(i));
		if (item == NULL || item->OutlineLevel() != 0)
			continue;
		if (CountItemsUnder(item, true) == 0)
			continue;
		if (item == section)
			Expand(item);
		else if (item->IsExpanded())
			Collapse(item);
	}
	if (copy.what != 0)
		SetSelectionMessage(new BMessage(copy));
}


BSidebarItem*
BSidebar::ItemForId(const char* id) const
{
	if (id == NULL)
		return NULL;
	if (!fData->sections.empty()) {
		for (uint32 s = 0; s < fData->sections.size(); s++) {
			if (strcmp(fData->sections[s].item->Id(), id) == 0)
				return fData->sections[s].item;
			for (uint32 c = 0; c < fData->sections[s].children.size(); c++) {
				if (strcmp(fData->sections[s].children[c]->Id(), id) == 0)
					return fData->sections[s].children[c];
			}
		}
	}
	for (int32 i = 0; i < FullListCountItems(); i++) {
		BSidebarItem* item = dynamic_cast<BSidebarItem*>(FullListItemAt(i));
		if (item != NULL && strcmp(item->Id(), id) == 0)
			return item;
	}
	return NULL;
}


bool
BSidebar::SelectId(const char* id)
{
	BSidebarItem* item = ItemForId(id);
	if (item == NULL)
		return false;
	BSidebarItem* parent = dynamic_cast<BSidebarItem*>(Superitem(item));
	if (fData->filtering) {
		// The filter already unfolded every match.
	} else if (parent != NULL)
		ExpandOnly(parent);
	else if (CountItemsUnder(item, true) > 0)
		ExpandOnly(item);
	else
		ExpandOnly(NULL);
	if (fData->sections.empty() == false && IndexOf(item) < 0)
		return false;
	int32 index = IndexOf(item);
	if (index < 0)
		return false;
	Select(index);
	ScrollToSelection();
	return true;
}


void
BSidebar::SetFilter(const char* text)
{
	BString next(text);
	if (next == fData->filter)
		return;
	if (!fData->filtering)
		_Capture();
	if (next.Length() == 0) {
		fData->filtering = false;
		fData->filter = "";
		_DetachAll();
		_Install(NULL);
		BSidebarItem* section = ItemForId(fData->expandedBefore.String());
		ExpandOnly(section);
		fData->expandedBefore = "";
		return;
	}
	if (!fData->filtering) {
		fData->expandedBefore = "";
		for (int32 i = 0; i < CountItems(); i++) {
			BSidebarItem* item = _ItemAt(i);
			if (item != NULL && item->OutlineLevel() == 0 && item->IsExpanded()
				&& CountItemsUnder(item, true) > 0) {
				fData->expandedBefore = item->Id();
				break;
			}
		}
		fData->filtering = true;
	}
	fData->filter = next;
	_DetachAll();
	_Install(next.String());
}


const char*
BSidebar::Filter() const
{
	return fData->filter.String();
}


void
BSidebar::SetFooter(const char* text)
{
	BString next(text);
	if (next == fData->footer)
		return;
	fData->footer = next;
	Invalidate();
}


void
BSidebar::ExpandOrCollapse(BListItem* item, bool expand)
{
	if (expand && fData->single && !fData->filtering) {
		for (int32 i = 0; i < CountItems(); i++) {
			BListItem* other = ItemAt(i);
			if (other != item && other->OutlineLevel() == 0
				&& other->IsExpanded())
				BOutlineListView::ExpandOrCollapse(other, false);
		}
	}
	BOutlineListView::ExpandOrCollapse(item, expand);
}


void
BSidebar::_Init()
{
	fData = new(std::nothrow) Data;
	fData->hover = -1;
	fData->single = true;
	fData->filtering = false;
	fData->inList = true;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	SetViewUIColor(B_LIST_BACKGROUND_COLOR);
	// The rows, the tip, and the selection depend on the width. Without a
	// full redraw, a width change at start leaves the old right edge behind.
	SetFlags(Flags() | B_FULL_UPDATE_ON_RESIZE);
}


void
BSidebar::_Capture()
{
	fData->sections.clear();
	Section* current = NULL;
	for (int32 i = 0; i < FullListCountItems(); i++) {
		BSidebarItem* item = dynamic_cast<BSidebarItem*>(FullListItemAt(i));
		if (item == NULL)
			continue;
		if (item->OutlineLevel() == 0) {
			Section section;
			section.item = item;
			fData->sections.push_back(section);
			current = &fData->sections.back();
		} else if (current != NULL)
			current->children.push_back(item);
	}
}


void
BSidebar::_DetachAll()
{
	for (int32 i = FullListCountItems() - 1; i >= 0; i--) {
		BListItem* item = FullListItemAt(i);
		if (item != NULL && item->OutlineLevel() > 0)
			RemoveItem(item);
	}
	while (FullListCountItems() > 0)
		RemoveItem(FullListItemAt(0));
	fData->inList = false;
}


void
BSidebar::_Install(const char* filter)
{
	for (uint32 s = 0; s < fData->sections.size(); s++) {
		Section& section = fData->sections[s];
		bool keep = section.item->IsEmphasized()
			|| (section.item->Label()[0] == '\0'
				&& section.item->Id()[0] == '\0');
		std::vector<BSidebarItem*> shown;
		for (uint32 c = 0; c < section.children.size(); c++) {
			if (filter == NULL || filter[0] == '\0'
				|| _Matches(section.children[c], filter))
				shown.push_back(section.children[c]);
		}
		if (!shown.empty())
			keep = true;
		if (!keep)
			continue;
		AddItem(section.item);
		for (uint32 c = 0; c < shown.size(); c++)
			AddUnder(shown[c], section.item);
		if (filter != NULL && filter[0] != '\0' && !shown.empty())
			Expand(section.item);
		else
			Collapse(section.item);
	}
	fData->inList = true;
	Invalidate();
}


bool
BSidebar::_Matches(BSidebarItem* item, const char* filter) const
{
	if (item == NULL || filter == NULL || filter[0] == '\0')
		return true;
	BString hay(item->Label());
	hay << " " << item->Keywords();
	return hay.IFindFirst(filter) >= 0;
}


BSidebarItem*
BSidebar::_ItemAt(int32 index) const
{
	return dynamic_cast<BSidebarItem*>(ItemAt(index));
}


void
BSidebar::_ReservedSidebar1()
{
}


void
BSidebar::_ReservedSidebar2()
{
}


void
BSidebar::_ReservedSidebar3()
{
}


void
BSidebar::_ReservedSidebar4()
{
}


}	// namespace BAppearanceKit
