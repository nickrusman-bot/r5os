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
#ifndef R5_UI_H
#define R5_UI_H


#include <GraphicsDefs.h>
#include <Rect.h>


class BBitmap;
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


// Draws an account picture: a round plate with a thin ring, and `icon`
// centered inside it. A bitmap whose side is the inner size is drawn
// pixel for pixel; any other size is filtered, never stretched with
// nearest-neighbor. `highlighted` gives the accent plate, used for an
// administrator or the selected account. Without an icon a plain person
// silhouette is drawn. The inner size is r5_avatar_icon_side(circle).
void r5_avatar_draw(BView* view, BRect circle, const BBitmap* icon,
	bool highlighted);

// The icon side, in pixels, that r5_avatar_draw() draws without
// filtering inside a plate of `circleSide` pixels.
float r5_avatar_icon_side(float circleSide);

#endif
