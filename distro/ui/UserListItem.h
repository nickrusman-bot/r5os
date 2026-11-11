/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _USER_LIST_ITEM_H
#define _USER_LIST_ITEM_H


#include <ListItem.h>
#include <String.h>


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

	const char*					Login() const;
	const char*					DisplayName() const;
	const char*					Role() const;
	const BBitmap*				Avatar() const;

	void						SetDisplayName(const char* displayName);
	void						SetRole(const char* role);
	void						SetAvatar(const BBitmap* avatar);

	virtual	void				DrawItem(BView* owner, BRect itemRect,
									bool complete = false);
	virtual	void				Update(BView* owner, const BFont* font);

private:
			struct Data;
			Data*				fData;

			uint32				_reserved[4];
};


} // namespace BAppearanceKit


#endif // _USER_LIST_ITEM_H
