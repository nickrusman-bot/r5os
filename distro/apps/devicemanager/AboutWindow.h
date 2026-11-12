/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_ABOUT_WINDOW_H
#define DM_ABOUT_WINDOW_H


#include <Window.h>


// In the style of About R5: a document card with the icon, the name in two
// weights and a muted version line, and a card of label and value cells.
class AboutWindow : public BWindow {
public:
								AboutWindow();
};


#endif
