/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _USER_CARD_H
#define _USER_CARD_H


#include <Control.h>
#include <String.h>


class BBitmap;


namespace BAppearanceKit {


class BUserCard : public BControl {
public:
								BUserCard(const char* name,
									const char* login,
									const char* displayName,
									const char* role,
									const BBitmap* avatar = NULL,
									BMessage* message = NULL);
								BUserCard(BMessage* archive);
	virtual						~BUserCard();

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

	bool						IsSelected() const;
	void						SetSelected(bool selected);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);

	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			struct Data;
			Data*				fData;

			uint32				_reserved[4];
};


} // namespace BAppearanceKit


#endif // _USER_CARD_H
