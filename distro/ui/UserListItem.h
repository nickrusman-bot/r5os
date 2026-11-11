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
#ifndef _USER_LIST_ITEM_H
#define _USER_LIST_ITEM_H


#include <ListItem.h>


class BBitmap;


namespace BAppearanceKit {


class BUserListItem : public BListItem {
public:
								BUserListItem(const char* login,
									const char* displayName,
									const char* role,
									const BBitmap* avatar = NULL);
								BUserListItem(BMessage* archive);
	virtual						~BUserListItem();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			const char*			Login() const;
			const char*			DisplayName() const;
			const char*			Role() const;
			const BBitmap*		Avatar() const;

			void				SetDisplayName(const char* displayName);
			void				SetRole(const char* role);
			void				SetAvatar(const BBitmap* avatar);

	virtual	void				DrawItem(BView* owner, BRect itemRect,
									bool complete = false);
	virtual	void				Update(BView* owner, const BFont* font);

private:
			void				_Init();

	virtual	void				_ReservedUserListItem1();
	virtual	void				_ReservedUserListItem2();
	virtual	void				_ReservedUserListItem3();
	virtual	void				_ReservedUserListItem4();

								BUserListItem(const BUserListItem&);
			BUserListItem&		operator=(const BUserListItem&);

			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _USER_LIST_ITEM_H
