/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "WindowEffectsBox.h"

#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <StringView.h>

#include <WindowEffects.h>


static const uint32 kMsgShadow = 'wesh';
static const uint32 kMsgGlass = 'wegl';
static const uint32 kMsgStrength = 'west';

// Defaults: both on and Balanced (docs/APP-SERVER-COMPOSITOR.md, "Settings").
static const uint32 kDefaultEffects = BAppearanceKit::B_WINDOW_SHADOW
	| BAppearanceKit::B_WINDOW_FROSTED_GLASS;
static const int32 kDefaultStrength = BAppearanceKit::B_WINDOW_EFFECTS_BALANCED;
// The checkboxes of this box. Other bits are kept as they are.
static const uint32 kBoxEffects = kDefaultEffects;

static const struct {
	int32		strength;
	const char*	label;
} kStrengths[] = {
	{ BAppearanceKit::B_WINDOW_EFFECTS_QUIET, "Quiet" },
	{ BAppearanceKit::B_WINDOW_EFFECTS_BALANCED, "Balanced" },
	{ BAppearanceKit::B_WINDOW_EFFECTS_GLASSY, "Glassy" }
};


WindowEffectsBox::WindowEffectsBox(const char* name, BView* label)
	:
	BBox(name),
	fTarget(NULL)
{
	if (label != NULL)
		SetLabel(label);
	else
		SetLabel("Window effects");

	fEffects = BAppearanceKit::window_effects();
	fStrength = BAppearanceKit::window_effect_strength();
	fSavedEffects = fEffects;
	fSavedStrength = fStrength;

	fShadow = new BCheckBox("shadow", "Shadow", new BMessage(kMsgShadow));
	fGlass = new BCheckBox("frosted glass", "Frosted glass",
		new BMessage(kMsgGlass));

	fStrengthMenu = new BPopUpMenu("strength");
	for (size_t i = 0; i < sizeof(kStrengths) / sizeof(kStrengths[0]); i++) {
		BMessage* message = new BMessage(kMsgStrength);
		message->AddInt32("strength", kStrengths[i].strength);
		fStrengthMenu->AddItem(new BMenuItem(kStrengths[i].label, message));
	}
	fStrengthField = new BMenuField("strength", "Strength:", fStrengthMenu);
	BStringView* hint = new BStringView("hint", "Less effect uses less CPU");

	BView* content = BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fShadow)
		.Add(fGlass)
		.AddGroup(B_HORIZONTAL)
			.Add(fStrengthField)
			.AddGlue()
			.End()
		.Add(hint)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.View();
	AddChild(content);
	_Update();
}


void
WindowEffectsBox::AttachedToWindow()
{
	BBox::AttachedToWindow();
	fShadow->SetTarget(this);
	fGlass->SetTarget(this);
	fStrengthMenu->SetTargetForItems(this);
}


void
WindowEffectsBox::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgShadow:
		case kMsgGlass:
		{
			uint32 flag = message->what == kMsgShadow
				? (uint32)BAppearanceKit::B_WINDOW_SHADOW
				: (uint32)BAppearanceKit::B_WINDOW_FROSTED_GLASS;
			BCheckBox* box = message->what == kMsgShadow ? fShadow : fGlass;
			uint32 effects = fEffects & ~flag;
			if (box->Value() == B_CONTROL_ON)
				effects |= flag;
			_Apply(effects, fStrength);
			break;
		}

		case kMsgStrength:
		{
			int32 strength;
			if (message->FindInt32("strength", &strength) == B_OK)
				_Apply(fEffects, strength);
			break;
		}

		default:
			BBox::MessageReceived(message);
			break;
	}
}


void
WindowEffectsBox::SetTarget(BHandler* target)
{
	fTarget = target;
}


bool
WindowEffectsBox::IsDefaultable() const
{
	return (fEffects & kBoxEffects) != kDefaultEffects
		|| fStrength != kDefaultStrength;
}


void
WindowEffectsBox::SetDefaults()
{
	_Apply((fEffects & ~kBoxEffects) | kDefaultEffects, kDefaultStrength);
}


bool
WindowEffectsBox::IsRevertable() const
{
	return fEffects != fSavedEffects || fStrength != fSavedStrength;
}


void
WindowEffectsBox::Revert()
{
	_Apply(fSavedEffects, fSavedStrength);
}


void
WindowEffectsBox::_Apply(uint32 effects, int32 strength)
{
	if (effects != fEffects
		&& BAppearanceKit::set_window_effects(effects) == B_OK)
		fEffects = effects;
	if (strength != fStrength
		&& BAppearanceKit::set_window_effect_strength(
			(enum BAppearanceKit::window_effect_strength)strength) == B_OK)
		fStrength = strength;
	_Update();
	if (fTarget != NULL)
		BMessenger(fTarget).SendMessage(kMsgWindowEffectsChanged);
}


void
WindowEffectsBox::_Update()
{
	fShadow->SetValue((fEffects & BAppearanceKit::B_WINDOW_SHADOW) != 0
		? B_CONTROL_ON : B_CONTROL_OFF);
	fGlass->SetValue((fEffects & BAppearanceKit::B_WINDOW_FROSTED_GLASS) != 0
		? B_CONTROL_ON : B_CONTROL_OFF);
	for (int32 i = 0; i < fStrengthMenu->CountItems(); i++) {
		BMenuItem* item = fStrengthMenu->ItemAt(i);
		int32 strength;
		if (item->Message() != NULL
			&& item->Message()->FindInt32("strength", &strength) == B_OK)
			item->SetMarked(strength == fStrength);
	}
}
