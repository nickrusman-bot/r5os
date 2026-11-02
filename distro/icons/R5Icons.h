/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_ICONS_H
#define R5_ICONS_H


#include <SupportDefs.h>


class BBitmap;
class BButton;


// Public icon helpers. The long description is docs/ICONS.md.
//
// The numbered values below are part of the call contract. Add a new
// icon at the end. Do not renumber one that already shipped.
//
// R5_ICON_BACK, R5_ICON_FORWARD, and R5_ICON_UP are the arrows Tracker
// draws in its location bar. R5_ICON_HOME is the house Tracker draws
// on the home folder. The bytes are unmodified Tracker HVIF. None of
// these pictures includes the preferences wrench. About R5 does not
// use this header.
enum r5_icon {
	R5_ICON_BACK = 1,
	R5_ICON_FORWARD = 2,
	R5_ICON_UP = 3,
	R5_ICON_HOME = 4
};


// A new BBitmap of side x side pixels, or NULL. The caller deletes it.
// `side` is the edge in pixels and must be at least 1.
BBitmap* r5_icon_bitmap(r5_icon which, float side);

// Same, from HVIF bytes the caller already has. Any vector icon.
BBitmap* r5_vector_icon_bitmap(const uint8* data, size_t size, float side);

// The icon of a file or folder, through the public node API. Any path
// that has an icon. NULL if the path cannot be opened or has no icon.
BBitmap* r5_file_icon_bitmap(const char* path, float side);

// The icon of a MIME type, through the public MIME API. Any type the
// system knows, for example "application/x-vnd.Haiku-About".
BBitmap* r5_mime_icon_bitmap(const char* type, float side);


// Makes `button` a square of `buttonHeight` and draws the icon inside
// it, with the same outer height as a text button on that row. The
// label is left as the caller set it. An empty label keeps the icon
// centered. These calls do not draw the preferences wrench.
status_t r5_button_set_icon(BButton* button, r5_icon which,
	float buttonHeight);
status_t r5_button_set_vector(BButton* button, const uint8* data,
	size_t size, float buttonHeight);
status_t r5_button_set_file_icon(BButton* button, const char* path,
	float buttonHeight);
status_t r5_button_set_mime_icon(BButton* button, const char* type,
	float buttonHeight);


#endif
