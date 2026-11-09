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
#ifndef _ACTION_CARD_H
#define _ACTION_CARD_H


#include <Invoker.h>
#include <List.h>
#include <Rect.h>
#include <String.h>

#include "Card.h"


class BBitmap;


namespace BAppearanceKit {


class BActionCard : public BCard, public BInvoker {
public:
								BActionCard(const char* name,
									BMessage* message);
								BActionCard(BMessage* archive);
	virtual						~BActionCard();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);

			void				SetIcon(const BBitmap* icon);
			void				SetTitle(const char* title);
			void				SetSummary(const char* summary);
			int32				AddTag(const char* label);
			void				RemoveAllTags();
			int32				CountTags() const;
			void				SetTagColor(rgb_color tint);
			void				SetTagMessage(BMessage* message);
			void				SetHighlighted(bool on);

private:
			void				_Init();
			void				_Layout(float width);
			float				_HeightFor(float width);
			int32				_TagAt(BPoint where) const;
			BBitmap*			_Copy(const BBitmap* icon) const;
			float				_Width() const;

	virtual	void				_ReservedActionCard1();
	virtual	void				_ReservedActionCard2();
	virtual	void				_ReservedActionCard3();
	virtual	void				_ReservedActionCard4();

								BActionCard(const BActionCard&);
			BActionCard&		operator=(const BActionCard&);

			BString				fTitle;
			BString				fSummary;
			BString				fLineOne;
			BString				fLineTwo;
			BBitmap*			fIcon;
			BList				fTags;
			BList				fTagFrames;
			BMessage*			fTagMessage;
			rgb_color			fTagColor;
			bool				fHover;
			bool				fPressed;
			bool				fHighlighted;
			int32				fPressedTag;
			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _ACTION_CARD_H
