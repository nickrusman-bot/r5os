/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_FONT_H
#define R5_SETTINGS_FONT_H


class BFont;
class BView;


const BFont*	r5_settings_plain_font();
const BFont*	r5_settings_bold_font();
const BFont*	r5_settings_title_font();
void			r5_settings_apply_plain(BView* view);


#endif
