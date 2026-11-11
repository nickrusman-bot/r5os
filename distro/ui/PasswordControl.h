/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _PASSWORD_CONTROL_H
#define _PASSWORD_CONTROL_H


#include <Control.h>
#include <String.h>


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
	virtual	void				FrameResized(float newWidth, float newHeight);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				SetEnabled(bool enabled);
	virtual	void				WindowActivated(bool active);

	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_LayoutChildren();
			BRect				_EyeButtonRect() const;
			void				_DrawEyeButton();

			struct Data;
			Data*				fData;

			uint32				_reserved[4];
};


} // namespace BAppearanceKit


#endif // _PASSWORD_CONTROL_H
