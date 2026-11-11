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
#ifndef _ICON_VIEW_H
#define _ICON_VIEW_H


#include <View.h>


class BBitmap;


namespace BAppearanceKit {


// Shows one icon at its own pixel size. The view is exactly as large as
// the bitmap, and the bitmap is never scaled, so a vector icon rendered
// at the right size stays sharp. The background is the parent's color.
class BIconView : public BView {
public:
								BIconView(const char* name,
									const BBitmap* icon = NULL);
								BIconView(BMessage* archive);
	virtual						~BIconView();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			// The view keeps its own copy. NULL leaves an empty view.
			void				SetIcon(const BBitmap* icon);
			const BBitmap*		Icon() const;

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_Init();

	virtual	void				_ReservedIconView1();
	virtual	void				_ReservedIconView2();
	virtual	void				_ReservedIconView3();
	virtual	void				_ReservedIconView4();

								BIconView(const BIconView&);
			BIconView&			operator=(const BIconView&);

			BBitmap*			fIcon;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _ICON_VIEW_H
