/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef DISPLAY_SCREENS_H
#define DISPLAY_SCREENS_H


#include <GraphicsDefs.h>
#include <ObjectList.h>
#include <Rect.h>
#include <Screen.h>
#include <String.h>


struct DisplayScreen {
	int32			id;
	BString			name;
	BRect			frame;
	bool			main;
	monitor_info	monitor;
	bool			hasMonitorInfo;
};


status_t display_screens_get(BObjectList<DisplayScreen, true>& screens);
status_t display_screen_modes(int32 screenId, display_mode** modes,
	uint32* count);
bool display_screen_recommended(const display_mode* modes, uint32 count,
	display_mode* mode);
void display_screens_identify();

int32 display_bits_per_pixel(color_space space);
float display_refresh_rate(const display_mode& mode);
void display_format_refresh(float rate, BString& name);
bool display_screen_match(const display_mode* modes, uint32 count,
	int32 width, int32 height, color_space space, float refresh,
	display_mode* mode);
status_t display_screen_set_mode(const display_mode& mode, bool all,
	bool makeDefault);


#endif
