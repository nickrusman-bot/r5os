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
#ifndef _ACCENT_BUTTON_H
#define _ACCENT_BUTTON_H


#include <Button.h>


namespace BAppearanceKit {


// The main action of a window, filled with the accent color (the
// selection blue, B_STATUS_BAR_COLOR), with a bold label. It is a
// BButton: the message, target, MakeDefault() and Enter work the same.
// It is taller than a plain button. By default it is as wide as its
// label; a caller that wants the full row width sets an explicit
// maximum width of B_SIZE_UNLIMITED. Use one per window.
class BAccentButton : public BButton {
public:
								BAccentButton(const char* name,
									const char* label,
									BMessage* message = NULL);
								BAccentButton(BMessage* archive);
	virtual						~BAccentButton();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_Init();

	virtual	void				_ReservedAccentButton1();
	virtual	void				_ReservedAccentButton2();
	virtual	void				_ReservedAccentButton3();
	virtual	void				_ReservedAccentButton4();

								BAccentButton(const BAccentButton&);
			BAccentButton&		operator=(const BAccentButton&);

			bool				fHover;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _ACCENT_BUTTON_H
