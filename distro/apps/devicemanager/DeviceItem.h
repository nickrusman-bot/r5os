/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_DEVICE_ITEM_H
#define DM_DEVICE_ITEM_H


#include <ListItem.h>
#include <String.h>


class BBitmap;


// One row of the tree: a group, the computer, or a device. The icon is
// owned by the window's icon cache.
class DeviceItem : public BListItem {
public:
								DeviceItem(const char* text,
									const BBitmap* icon, bool isGroup,
									int32 group, uint32 cookie,
									int32 status, bool dimmed);

	virtual	void				DrawItem(BView* owner, BRect frame,
									bool complete = false);
	virtual	void				Update(BView* owner, const BFont* font);

			const char*			Text() const { return fText.String(); }
			bool				IsGroup() const { return fIsGroup; }
			int32				Group() const { return fGroup; }
			uint32				Cookie() const { return fCookie; }
			int32				Status() const { return fStatus; }

	static	int					Compare(const BListItem* a,
									const BListItem* b);

private:
			void				_DrawBadge(BView* owner, BRect icon);

			BString				fText;
			const BBitmap*		fIcon;
			bool				fIsGroup;
			int32				fGroup;
			uint32				fCookie;
			int32				fStatus;
			bool				fDimmed;
};


#endif
