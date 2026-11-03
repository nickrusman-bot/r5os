/*
 * Copyright 2002-2025, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm (darkwyrm@earthlink.net)
 *		Alexander von Gluck, kallisti5@unixzen.com
 *		Stephan Aßmus <superstippi@gmx.de>
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The copyright and license notice above still cover
 * the original work. A build that ships both follows GPL-3.0-or-later, and
 * that notice stays on the original portions.
 */

#ifndef APPEARANCE_WINDOW_H
#define APPEARANCE_WINDOW_H


#include <Application.h>
#include <Button.h>
#include <Window.h>
#include <Message.h>
#include <TabView.h>

class ColorsView;
class AntialiasingSettingsView;
class FontView;
class LookAndFeelSettingsView;


class AppearanceWindow : public BWindow {
public:
									AppearanceWindow(BRect frame);
			void					MessageReceived(BMessage *message);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	virtual	bool					QuitRequested();
// SPDX-SnippetEnd

private:
			void					_UpdateButtons();
			bool					_IsDefaultable() const;
			bool					_IsRevertable() const;

		ColorsView*					fColorsView;
		BButton*					fDefaultsButton;
		BButton*					fRevertButton;

		AntialiasingSettingsView* 	fAntialiasingSettings;
		FontView*					fFontSettings;
		LookAndFeelSettingsView*	fLookAndFeelSettings;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		BTabView*					fTabView;
// SPDX-SnippetEnd
};


static const int32 kMsgUpdate = 'updt';


#endif /* APPEARANCE_WINDOW_H */
