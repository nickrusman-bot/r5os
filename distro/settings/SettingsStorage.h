/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * One BMessage file per module under ~/config/settings/r5-settings/.
 * Existing Haiku preference files are not read or rewritten here.
 */
#ifndef R5_SETTINGS_STORAGE_H
#define R5_SETTINGS_STORAGE_H


#include <SupportDefs.h>


class BMessage;


class SettingsStorage {
public:
								SettingsStorage();

			status_t			Read(const char* moduleId,
									BMessage& settings) const;
			status_t			Write(const char* moduleId,
									const BMessage& settings) const;

private:
			status_t			_PathFor(const char* moduleId,
									char* path, size_t pathLength) const;
			bool				_ValidId(const char* moduleId) const;
};


#endif
