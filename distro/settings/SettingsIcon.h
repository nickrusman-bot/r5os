/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_SETTINGS_ICON_H
#define R5_SETTINGS_ICON_H


class BBitmap;


// Draws the Haiku preferences wrench over an icon that belongs to this
// Settings application: the page icon and the home cards. About R5 is
// not a settings module. The Back and Home buttons are Tracker icons
// and do not go through here.
void r5_settings_apply_prefs_overlay(BBitmap* icon);


#endif
