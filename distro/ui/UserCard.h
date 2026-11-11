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
#ifndef _USER_CARD_H
#define _USER_CARD_H


#include <Control.h>


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

			const char*			Login() const;
			const char*			DisplayName() const;
			const char*			Role() const;
			const BBitmap*		Avatar() const;

			void				SetDisplayName(const char* displayName);
			void				SetRole(const char* role);
			void				SetAvatar(const BBitmap* avatar);

			bool				IsSelected() const;
			void				SetSelected(bool selected);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);

	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_Init();

	virtual	void				_ReservedUserCard1();
	virtual	void				_ReservedUserCard2();
	virtual	void				_ReservedUserCard3();
	virtual	void				_ReservedUserCard4();

								BUserCard(const BUserCard&);
			BUserCard&			operator=(const BUserCard&);

			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _USER_CARD_H
