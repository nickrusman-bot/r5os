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
#ifndef _CARD_H
#define _CARD_H


#include <GroupLayout.h>
#include <View.h>


namespace BAppearanceKit {


enum card_style {
	B_CARD_DOCUMENT = 0,
	B_CARD_PANEL = 1
};


class BCard : public BView {
public:
								BCard(const char* name,
									card_style style = B_CARD_DOCUMENT,
									orientation orientation = B_VERTICAL);
								BCard(BMessage* archive);
	virtual						~BCard();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);

			void				SetStyle(card_style style);
			card_style			Style() const;

private:
			void				_Init();

	virtual	void				_ReservedCard1();
	virtual	void				_ReservedCard2();
	virtual	void				_ReservedCard3();
	virtual	void				_ReservedCard4();

								BCard(const BCard&);
			BCard&				operator=(const BCard&);

			card_style			fStyle;
			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _CARD_H
