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
#ifndef _PAGE_HEADER_H
#define _PAGE_HEADER_H


#include <String.h>

#include "Card.h"


class BBitmap;


namespace BAppearanceKit {


class BPageHeader : public BCard {
public:
								BPageHeader(const char* name);
								BPageHeader(BMessage* archive);
	virtual						~BPageHeader();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				Draw(BRect updateRect);
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	void				MessageReceived(BMessage* message);

			void				SetEyebrow(const char* text);
			void				SetTitle(const char* text);
			void				SetSubtitle(const char* text);
			void				SetIcon(const BBitmap* icon);

private:
			void				_Init();
			void				_Fonts(BFont& eyebrow, BFont& title,
									BFont& subtitle) const;
			float				_Height() const;
			BBitmap*			_Copy(const BBitmap* icon) const;

	virtual	void				_ReservedPageHeader1();
	virtual	void				_ReservedPageHeader2();
	virtual	void				_ReservedPageHeader3();
	virtual	void				_ReservedPageHeader4();

								BPageHeader(const BPageHeader&);
			BPageHeader&		operator=(const BPageHeader&);

			BString				fEyebrow;
			BString				fTitle;
			BString				fSubtitle;
			BBitmap*			fIcon;
			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _PAGE_HEADER_H
