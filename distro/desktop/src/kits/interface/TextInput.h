/*
 * Copyright 2001-2020 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Frans van Nispen (xlr8@tref.nl)
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The copyright and license notice above still cover
 * the original work. A build that ships both follows GPL-3.0-or-later, and
 * that notice stays on the original portions.
 */


//! The BTextView derivative owned by an instance of BTextControl.

#ifndef	_TEXT_CONTROLI_H
#define	_TEXT_CONTROLI_H


#include <TextView.h>


class BTextControl;

namespace BPrivate {

class _BTextInput_ : public BTextView {
public:
						_BTextInput_(BRect frame, BRect textRect,
							uint32 resizeMask,
							uint32 flags = B_WILL_DRAW | B_PULSE_NEEDED);
						_BTextInput_(BMessage *data);
virtual					~_BTextInput_();

static	BArchivable*	Instantiate(BMessage *data);
virtual	status_t		Archive(BMessage *data, bool deep = true) const;

virtual	void			MouseDown(BPoint where);
virtual	void			FrameResized(float width, float height);
virtual	void			KeyDown(const char *bytes, int32 numBytes);
virtual	void			MakeFocus(bool focusState = true);

virtual	BSize			MinSize();

		void			SetInitialText();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		void			SetText(const char* text,
							const text_run_array* runs = NULL);
// SPDX-SnippetEnd

virtual	void			Paste(BClipboard *clipboard);

protected:

virtual	void			InsertText(const char *inText, int32 inLength,
								   int32 inOffset, const text_run_array *inRuns);
virtual	void			DeleteText(int32 fromOffset, int32 toOffset);

private:

		BTextControl	*TextControl();

		char			*fPreviousText;
		bool			fInMouseDown;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		bool			fSuppressModification;
// SPDX-SnippetEnd
};

}	// namespace BPrivate

using namespace BPrivate;


#endif	// _TEXT_CONTROLI_H

