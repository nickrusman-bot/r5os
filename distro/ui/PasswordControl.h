/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _PASSWORD_CONTROL_H
#define _PASSWORD_CONTROL_H


#include <Control.h>
#include <String.h>


class BTextControl;
class BTextView;


namespace BAppearanceKit {


class BPasswordControl : public BControl {
public:
								BPasswordControl(const char* name,
									const char* label,
									const char* text = NULL,
									BMessage* message = NULL);
								BPasswordControl(BMessage* archive);
	virtual						~BPasswordControl();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	const char*					Text() const;
	void						SetText(const char* text);
	BTextView*					TextView() const;

	bool						IsPasswordRevealed() const;
	void						SetPasswordRevealed(bool revealed);

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				SetEnabled(bool enabled);

private:
			void				_Init(const char* text);

			struct Data;
			Data*				fData;

			uint32				_reserved[4];
};


} // namespace BAppearanceKit


#endif // _PASSWORD_CONTROL_H
