/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Screen list and mode helpers for the Display page.
 * One screen today: the main screen. A later screen is another item
 * from display_screens_get(), not a change to the page.
 */


#include "DisplayScreens.h"

#include <InterfaceDefs.h>
#include <OS.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int32
display_bits_per_pixel(color_space space)
{
	switch (space) {
		case B_RGB30:
			return 30;
		case B_RGB32:
		case B_RGB24:
			return 24;
		case B_RGB16:
			return 16;
		case B_RGB15:
			return 15;
		case B_CMAP8:
			return 8;
		default:
			return 0;
	}
}


float
display_refresh_rate(const display_mode& mode)
{
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0)
		return 0.0f;
	return rint(10 * float(mode.timing.pixel_clock * 1000)
		/ float(mode.timing.h_total * mode.timing.v_total)) / 10.0f;
}


void
display_format_refresh(float rate, BString& name)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.*g", rate >= 100.0f ? 4 : 3, rate);
	name.SetTo(buffer);
	name.Append(" Hz");
}


static int
compare_mode(const void* left, const void* right)
{
	const display_mode* a = static_cast<const display_mode*>(left);
	const display_mode* b = static_cast<const display_mode*>(right);
	if (a->virtual_width != b->virtual_width)
		return (int)a->virtual_width - (int)b->virtual_width;
	if (a->virtual_height != b->virtual_height)
		return (int)a->virtual_height - (int)b->virtual_height;
	float difference = display_refresh_rate(*a) - display_refresh_rate(*b);
	if (difference < 0)
		return -1;
	if (difference > 0)
		return 1;
	return 0;
}


static BString
screen_name(const DisplayScreen& screen)
{
	if (screen.hasMonitorInfo) {
		BString name;
		if (screen.monitor.vendor[0] != '\0') {
			name = screen.monitor.vendor;
			name.RemoveSet(" ");
		}
		BString model(screen.monitor.name);
		model.Trim();
		if (model.Length() > 0) {
			if (name.Length() > 0)
				name << " ";
			name << model;
		}
		if (name.Length() > 0)
			return name;
	}
	return BString("Screen");
}


status_t
display_screens_get(BObjectList<DisplayScreen, true>& screens)
{
	screens.MakeEmpty();
	BScreen screen(B_MAIN_SCREEN_ID);
	if (!screen.IsValid())
		return B_ERROR;

	DisplayScreen* item = new DisplayScreen();
	item->id = B_MAIN_SCREEN_ID.id;
	item->frame = screen.Frame();
	item->main = true;
	memset(&item->monitor, 0, sizeof(item->monitor));
	item->hasMonitorInfo = screen.GetMonitorInfo(&item->monitor) == B_OK
		&& item->monitor.name[0] != '\0';
	item->name = screen_name(*item);
	screens.AddItem(item);
	return B_OK;
}


status_t
display_screen_modes(int32 screenId, display_mode** modes, uint32* count)
{
	(void)screenId;
	if (modes == NULL || count == NULL)
		return B_BAD_VALUE;
	*modes = NULL;
	*count = 0;
	BScreen screen(B_MAIN_SCREEN_ID);
	display_mode* list = NULL;
	uint32 found = 0;
	status_t status = screen.GetModeList(&list, &found);
	if (status != B_OK || list == NULL)
		return status;
	qsort(list, found, sizeof(display_mode), compare_mode);
	*modes = list;
	*count = found;
	return B_OK;
}


bool
display_screen_recommended(int32 screenId, display_mode* mode)
{
	if (mode == NULL)
		return false;
	display_mode* modes = NULL;
	uint32 count = 0;
	if (display_screen_modes(screenId, &modes, &count) != B_OK
		|| modes == NULL || count == 0) {
		free(modes);
		return false;
	}
	const display_mode* best = &modes[0];
	for (uint32 index = 1; index < count; index++) {
		uint32 area = modes[index].virtual_width * modes[index].virtual_height;
		uint32 bestArea = best->virtual_width * best->virtual_height;
		if (area > bestArea)
			best = &modes[index];
		else if (area == bestArea
			&& display_refresh_rate(modes[index])
				> display_refresh_rate(*best))
			best = &modes[index];
	}
	*mode = *best;
	free(modes);
	return true;
}


void
display_screens_identify()
{
	// One screen: there is nothing to flash. A later screen list
	// calls this to light each panel.
}


bool
display_screen_match(const display_mode* modes, uint32 count, int32 width,
	int32 height, color_space space, float refresh, display_mode* mode)
{
	int32 best = -1;
	float bestDiff = 999.0f;
	for (uint32 i = 0; i < count; i++) {
		if (modes[i].virtual_width != width
			|| modes[i].virtual_height != height
			|| (color_space)modes[i].space != space)
			continue;
		float diff = fabs(display_refresh_rate(modes[i]) - refresh);
		if (diff < bestDiff) {
			bestDiff = diff;
			best = (int32)i;
		}
	}
	if (best < 0)
		return false;
	*mode = modes[best];
	mode->h_display_start = 0;
	mode->v_display_start = 0;
	return true;
}


status_t
display_screen_set_mode(const display_mode& mode, bool all, bool makeDefault)
{
	BScreen screen;
	int32 workspace = current_workspace();
	display_mode copy = mode;
	status_t status = screen.SetMode(workspace, &copy, makeDefault);
	if (status != B_OK || !all)
		return status;
	int32 count = count_workspaces();
	for (int32 i = 0; i < count; i++) {
		if (i == workspace)
			continue;
		copy = mode;
		screen.SetMode(i, &copy, makeDefault);
	}
	return B_OK;
}
