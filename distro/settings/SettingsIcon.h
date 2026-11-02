/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_ICON_H
#define R5_SETTINGS_ICON_H


class BBitmap;


// Draws the Haiku preferences wrench over a settings icon. Every icon
// this application shows goes through here.
void r5_settings_apply_prefs_overlay(BBitmap* icon);


#endif
