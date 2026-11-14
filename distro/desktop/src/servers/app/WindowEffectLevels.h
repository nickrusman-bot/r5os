/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 step 4: the one table of window effect values (decision 8 in
 * generated/gpu-m11/BRIEF.md). The R5 decorator and the compositor both
 * read it; no other file holds these numbers. They are starting values for
 * the measurement, not fixed constants.
 */
#ifndef WINDOW_EFFECT_LEVELS_H
#define WINDOW_EFFECT_LEVELS_H


#include <SupportDefs.h>


// The values of <WindowEffects.h> (libr5ui), repeated here because
// app_server does not link libr5ui. They never change.
enum {
	kWindowEffectShadow			= 0x00000001,
	kWindowEffectFrostedGlass	= 0x00000002,
	kWindowEffectAnimations		= 0x00000004,
	kWindowEffectsAll			= 0x00000007
};

enum {
	kWindowEffectsQuiet		= 1,
	kWindowEffectsBalanced	= 2,
	kWindowEffectsGlassy	= 3
};


struct window_effect_level {
	const char*	name;
	int32		shadowOffset;		// px, down
	int32		shadowSoftness;		// px, how far the blur reaches
	int32		shadowStrength;		// percent of black at the edge
	int32		glassBlur;			// px, the browser blur it stands for
	int32		tabSeeThrough;		// percent of the blur that shows
	int32		glassLevels;		// Dual Kawase levels down and up
};


static const window_effect_level kWindowEffectLevels[] = {
	{ "quiet",		2,	6,	28,	4,	14,	2 },
	{ "balanced",	4,	14,	34,	9,	28,	2 },
	{ "glassy",		7,	24,	40,	16,	44,	2 }
};


// Inactive windows and menus: this part of the softness and the offset of
// the chosen step (decision 8: half).
static const int32 kInactiveShadowDivisor = 2;


static inline int32
window_effect_strength_valid(int32 strength)
{
	if (strength < kWindowEffectsQuiet || strength > kWindowEffectsGlassy)
		return kWindowEffectsBalanced;
	return strength;
}


static inline const window_effect_level&
window_effect_level_for(int32 strength)
{
	return kWindowEffectLevels[window_effect_strength_valid(strength) - 1];
}


// How a shadow is made from a step: a box blur of this radius, three
// times, so it reaches three radii from the footprint; moved down by the
// offset.
struct window_shadow_geometry {
	int32	radius;
	int32	reach;
	int32	offset;
	int32	strength;	// 0..255
};


static inline window_shadow_geometry
window_shadow_for(int32 strength, bool small)
{
	const window_effect_level& level = window_effect_level_for(strength);
	int32 softness = level.shadowSoftness;
	int32 offset = level.shadowOffset;
	if (small) {
		softness /= kInactiveShadowDivisor;
		offset /= kInactiveShadowDivisor;
	}
	window_shadow_geometry geometry;
	geometry.radius = (softness + 1) / 3;
	if (geometry.radius < 1)
		geometry.radius = 1;
	geometry.reach = geometry.radius * 3;
	geometry.offset = offset;
	geometry.strength = level.shadowStrength * 255 / 100;
	return geometry;
}


// The shadow extent of the R5 decorator for a step: how far the active
// window's shadow reaches past its footprint on each side.
static inline void
window_shadow_extent(int32 strength, int32* left, int32* top, int32* right,
	int32* bottom)
{
	window_shadow_geometry geometry = window_shadow_for(strength, false);
	*left = geometry.reach;
	*right = geometry.reach;
	*top = geometry.reach > geometry.offset
		? geometry.reach - geometry.offset : 0;
	*bottom = geometry.reach + geometry.offset;
}


// Dual Kawase for a step: levels down and up, the tap offset, and how far
// the result reaches into what lies around it (the input is grown by it).
struct window_glass_geometry {
	int32	levels;
	float	offset;
	int32	reach;
	int32	seeThrough;	// 0..255
};


static inline window_glass_geometry
window_glass_for(int32 strength)
{
	const window_effect_level& level = window_effect_level_for(strength);
	window_glass_geometry geometry;
	geometry.levels = level.glassLevels < 1 ? 1
		: level.glassLevels > 4 ? 4 : level.glassLevels;
	geometry.offset = (float)level.glassBlur / (float)(1 << geometry.levels)
		- 1.0f;
	if (geometry.offset < 0.5f)
		geometry.offset = 0.5f;
	float reach = 0;
	for (int32 k = 1; k <= geometry.levels; k++) {
		reach += (geometry.offset + 1.0f) * (float)(1 << (k - 1));
		reach += (2.0f * geometry.offset + 1.0f) * (float)(1 << k);
	}
	geometry.reach = (int32)reach + 1;
	geometry.seeThrough = level.tabSeeThrough * 255 / 100;
	return geometry;
}


// M11 step 4b: the window animations (generated/gpu-m11/ANIMATIONS.md).
// Starting values for the measurement, logged with each animation.
static const bigtime_t kOpenAnimation = 150000;		// fade in, 95 % to 100 %
static const bigtime_t kCloseAnimation = 120000;	// the reverse, shorter
static const bigtime_t kWorkspaceAnimation = 200000;	// the slide
static const float kOpenAnimationScale = 0.95f;


// The strength app_server uses now; set by DesktopSettingsPrivate when it
// loads or stores the appearance file. Read by the R5 decorator for its
// shadow extent.
extern int32 gWindowEffectStrength;


#endif	// WINDOW_EFFECT_LEVELS_H
