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
#ifndef _SIDEBAR_ITEM_H
#define _SIDEBAR_ITEM_H


#include <ListItem.h>
#include <String.h>


class BBitmap;


namespace BAppearanceKit {


enum sidebar_badge {
	B_SIDEBAR_BADGE_NONE = 0,
	B_SIDEBAR_BADGE_LOCKED = 1
};


class BSidebarItem : public BListItem {
public:
								BSidebarItem(const char* label, const char* id,
									const BBitmap* icon = NULL);
								BSidebarItem(BMessage* archive);
	virtual						~BSidebarItem();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);
	virtual	void				Update(BView* owner, const BFont* font);

			const char*			Label() const;
			const char*			Id() const;
			void				SetIcon(const BBitmap* icon);
			void				SetCount(int32 count);
			int32				Count() const;
			void				SetBadge(sidebar_badge badge);
			sidebar_badge		Badge() const;
			void				SetEmphasized(bool bold);
			bool				IsEmphasized() const;
			void				SetKeywords(const char* keywords);
			const char*			Keywords() const;

private:
			void				_SetRole(bool inGroup, bool first, bool last);
			bool				_IsSeparator() const;
			void				_SetHovered(bool hovered);
			BBitmap*			_Copy(const BBitmap* icon) const;

	virtual	void				_ReservedSidebarItem1();
	virtual	void				_ReservedSidebarItem2();
	virtual	void				_ReservedSidebarItem3();
	virtual	void				_ReservedSidebarItem4();

								BSidebarItem(const BSidebarItem&);
			BSidebarItem&		operator=(const BSidebarItem&);

			BString				fLabel;
			BString				fId;
			BString				fKeywords;
			BBitmap*			fIcon;
			BBitmap*			fLock;
			int32				fCount;
			sidebar_badge		fBadge;
			bool				fEmphasized;
			bool				fInGroup;
			bool				fFirst;
			bool				fLast;
			struct Data;
			Data*				fData;
			uint32				_reserved[4];

			friend class BSidebar;
};


}	// namespace BAppearanceKit


#endif	// _SIDEBAR_ITEM_H
