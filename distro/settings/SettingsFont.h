/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_SETTINGS_FONT_H
#define R5_SETTINGS_FONT_H


#include <GraphicsDefs.h>


class BFont;
class BStringView;
class BView;


const BFont*	r5_settings_plain_font();
const BFont*	r5_settings_bold_font();
const BFont*	r5_settings_title_font();
void			r5_settings_apply_plain(BView* view);
BStringView*	r5_settings_box_title(const char* text);
// The light fill of the Information tiles and the Display boxes: the panel
// mixed toward the document background by 100. One call, so they match.
rgb_color		r5_settings_tile_fill();


#endif
