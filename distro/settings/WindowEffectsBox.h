/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 step 4: the "Window effects" box on Look and feel, in Settings and in
 * the separate Appearance program (one source for both). It sets the
 * effects through <WindowEffects.h>, at once.
 */
#ifndef WINDOW_EFFECTS_BOX_H
#define WINDOW_EFFECTS_BOX_H


#include <Box.h>


class BCheckBox;
class BMenuField;
class BPopUpMenu;


// Sent to the target after every change the box applied.
static const uint32 kMsgWindowEffectsChanged = 'wefc';


class WindowEffectsBox : public BBox {
public:
								WindowEffectsBox(const char* name,
									BView* label);

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				SetTarget(BHandler* target);

			bool				IsDefaultable() const;
			void				SetDefaults();
			bool				IsRevertable() const;
			void				Revert();

private:
			void				_Apply(uint32 effects, int32 strength);
			void				_Update();

			BCheckBox*			fShadow;
			BCheckBox*			fGlass;
			BMenuField*			fStrengthField;
			BPopUpMenu*			fStrengthMenu;
			BHandler*			fTarget;

			uint32				fEffects;
			int32				fStrength;
			uint32				fSavedEffects;
			int32				fSavedStrength;
};


#endif	// WINDOW_EFFECTS_BOX_H
