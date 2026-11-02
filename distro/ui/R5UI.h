/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_UI_H
#define R5_UI_H


#include <GraphicsDefs.h>
#include <Rect.h>


class BView;


// Public UI helpers in libr5ui.so. The long description for developers
// is docs/LIBR5.md.
//
// The soname is libr5ui.so. The regular image and the minimum image
// both install it in /system/lib. A Jam program links libr5ui.so and
// includes this header. This is not libbe. Do not move these calls
// into libbe.
//
// Adding a function keeps the library compatible. Changing or removing
// a function breaks programs already built against this soname.


// Draw the screen picture used by Settings. bounds is the area the
// picture may use. The picture keeps a 4:3 shape and is centered.
// contentColor is the fill inside the frame. The return value is that
// inner rectangle, in the same coordinates, for anything the caller
// draws on top. The caller still draws its own label outside bounds.
BRect r5_screen_draw(BView* view, BRect bounds, rgb_color contentColor);

// Draw text centered in the inner rectangle from r5_screen_draw().
// A NULL or empty text draws nothing. Text that does not fit is skipped.
void r5_screen_caption(BView* view, BRect content, const char* text,
	rgb_color textColor);


#endif
