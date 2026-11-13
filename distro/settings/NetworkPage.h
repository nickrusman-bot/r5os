/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * What Connections and Discovery share: Display's filled box and its title,
 * a read-only field, and a link.
 */
#ifndef R5_SETTINGS_NETWORK_PAGE_H
#define R5_SETTINGS_NETWORK_PAGE_H


#include <Box.h>
#include <CheckBox.h>
#include <Control.h>
#include <String.h>
#include <StringView.h>


class BTextView;


float		network_unit();
rgb_color	network_quiet_color(uint8 amount);
rgb_color	network_box_fill();

// A quiet hint on a filled box that wraps instead of being cut off.
BTextView*	network_hint(const char* text);


// Display's box: the fill starts at the top border line.
class FilledBox : public BBox {
public:
								FilledBox(const char* name);

	virtual	void				Draw(BRect updateRect);
};


// Display's box title: window grey above the border line, fill below.
class BoxTitle : public BStringView {
public:
								BoxTitle(const char* text);

	virtual	void				Draw(BRect updateRect);
};


// A switch as the box title, as Enable virtual memory, drawn like
// BoxTitle: window grey above the border line, fill below.
class BoxCheckBox : public BCheckBox {
public:
								BoxCheckBox(const char* name,
									const char* label, BMessage* message);

	virtual	void				Draw(BRect updateRect);
};


// A read-only field. A value that does not fit ends in "…", and the
// tooltip shows all of it.
class ValueField : public BView {
public:
								ValueField(const char* name);

			void				SetText(const char* text);
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_UpdateTip();

			BString				fText;
};


// An underlined link, as Rename… on Information. It is a control, so the
// Settings window grays it while the lock is closed.
class LinkControl : public BControl {
public:
								LinkControl(const char* name,
									const char* label, BMessage* message);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
};


#endif	// R5_SETTINGS_NETWORK_PAGE_H
