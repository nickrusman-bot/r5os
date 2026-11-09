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
#ifndef _SIDEBAR_H
#define _SIDEBAR_H


#include <OutlineListView.h>

#include "SidebarItem.h"


namespace BAppearanceKit {


class BSidebar : public BOutlineListView {
public:
								BSidebar(const char* name);
								BSidebar(BMessage* archive);
	virtual						~BSidebar();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				Draw(BRect updateRect);
	virtual	void				DrawItem(BListItem* item, BRect frame,
									bool complete = false);
	virtual	void				DrawLatch(BRect itemRect, int32 level,
									bool collapsed, bool highlighted,
									bool misTracked);
	virtual	void				MouseDown(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);

			void				SetSingleExpand(bool single);
			bool				IsSingleExpand() const;
			void				ExpandOnly(BSidebarItem* section);
			BSidebarItem*		ItemForId(const char* id) const;
			bool				SelectId(const char* id);
			void				SetFilter(const char* text);
			const char*			Filter() const;
			void				SetFooter(const char* text);

protected:
	virtual	void				ExpandOrCollapse(BListItem* item, bool expand);

private:
			void				_Init();
			void				_Capture();
			void				_DetachAll();
			void				_Install(const char* filter);
			bool				_Matches(BSidebarItem* item,
									const char* filter) const;
			BSidebarItem*		_ItemAt(int32 index) const;

	virtual	void				_ReservedSidebar1();
	virtual	void				_ReservedSidebar2();
	virtual	void				_ReservedSidebar3();
	virtual	void				_ReservedSidebar4();

								BSidebar(const BSidebar&);
			BSidebar&			operator=(const BSidebar&);

			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _SIDEBAR_H
