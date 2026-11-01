/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_MANAGER_H
#define R5_SETTINGS_MANAGER_H


#include <List.h>
#include <ObjectList.h>
#include <SupportDefs.h>

#include <image.h>


class SettingsContext;
class SettingsModule;


class SettingsManager {
public:
								SettingsManager();
								~SettingsManager();

			void				Add(SettingsModule* module);
			status_t			LoadAddOns(SettingsContext* context);
			int32				Count() const;
			SettingsModule*		ModuleAt(int32 index) const;
			SettingsModule*		Find(const char* identifier) const;

private:
			status_t			_LoadOne(const char* path,
									SettingsContext* context);
			void				_Scan(const char* directory,
									SettingsContext* context);

			BObjectList<SettingsModule, true> fModules;
			BList				fImages;
};


#endif
