/*
 * Copyright 2019, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Author:
 *		Preetpal Kaur <preetpalok123@gmail.com>
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The copyright and license notice above still cover
 * the original work. A build that ships both follows GPL-3.0-or-later, and
 * that notice stays on the original portions.
 */



#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H


#include <Box.h>
#include <CheckBox.h>
#include <OptionPopUp.h>
#include <Slider.h>


class MouseSettings;
class MouseView;


class SettingsView : public BBox {
	public:
								SettingsView(MouseSettings &settings);
		virtual 				~SettingsView();

		virtual void 			AttachedToWindow();

				void 			SetMouseType(int32 type);
				void 			MouseMapUpdated();
				void 			UpdateFromSettings();

	public:
				// FIXME use proper getters/setters for this?
				BCheckBox*		fAcceptFirstClickBox;

	private:
		typedef	BBox			inherited;

		const	MouseSettings&	fSettings;

				BOptionPopUp*	fTypeMenu;
				BOptionPopUp*	fFocusMenu;
				MouseView*		fMouseView;
				BSlider*		fClickSpeedSlider;
				BSlider*		fMouseSpeedSlider;
				BSlider*		fAccelerationSlider;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
				BSlider*		fPointerSizeSlider;
// SPDX-SnippetEnd
};

#endif	/* SETTINGS_VIEW_H */
