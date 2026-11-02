/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "SettingsManager.h"

#include "SettingsModule.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>


struct SettingsAddonImage {
	image_id id;
};


SettingsManager::SettingsManager()
	:
	fModules(20),
	fImages()
{
}


SettingsManager::~SettingsManager()
{
	fModules.MakeEmpty();
	for (int32 i = 0; i < fImages.CountItems(); i++) {
		SettingsAddonImage* image
			= static_cast<SettingsAddonImage*>(fImages.ItemAt(i));
		if (image != NULL) {
			unload_add_on(image->id);
			delete image;
		}
	}
}


void
SettingsManager::Add(SettingsModule* module)
{
	if (module == NULL || module->Identifier() == NULL)
		return;
	if (Find(module->Identifier()) != NULL) {
		syslog(LOG_ERR, "settings: duplicate module %s", module->Identifier());
		delete module;
		return;
	}
	fModules.AddItem(module);
}


status_t
SettingsManager::LoadAddOns(SettingsContext* context)
{
	directory_which which[] = {
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY
	};
	for (uint32 i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		BPath path;
		if (find_directory(which[i], &path) != B_OK)
			continue;
		if (path.Append("Settings") != B_OK)
			continue;
		_Scan(path.Path(), context);
	}
	return B_OK;
}


int32
SettingsManager::Count() const
{
	return fModules.CountItems();
}


SettingsModule*
SettingsManager::ModuleAt(int32 index) const
{
	return fModules.ItemAt(index);
}


SettingsModule*
SettingsManager::Find(const char* identifier) const
{
	if (identifier == NULL)
		return NULL;
	for (int32 i = 0; i < fModules.CountItems(); i++) {
		SettingsModule* module = fModules.ItemAt(i);
		if (module != NULL && module->Identifier() != NULL
			&& strcmp(module->Identifier(), identifier) == 0)
			return module;
	}
	return NULL;
}


void
SettingsManager::_Scan(const char* directory, SettingsContext* context)
{
	BDirectory dir(directory);
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsFile())
			continue;
		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;
		_LoadOne(path.Path(), context);
	}
}


status_t
SettingsManager::_LoadOne(const char* path, SettingsContext* context)
{
	image_id image = load_add_on(path);
	if (image < 0) {
		syslog(LOG_ERR, "settings: load %s: %s", path, strerror(image));
		return image;
	}

	void* versionSymbol = NULL;
	status_t status = get_image_symbol(image, "settings_api_version",
		B_SYMBOL_TYPE_TEXT, &versionSymbol);
	if (status != B_OK || versionSymbol == NULL) {
		syslog(LOG_ERR, "settings: %s has no settings_api_version", path);
		unload_add_on(image);
		return B_BAD_TYPE;
	}

	typedef int32 (*version_fn)();
	int32 version = reinterpret_cast<version_fn>(versionSymbol)();
	if (version != kSettingsApiVersion) {
		syslog(LOG_ERR, "settings: %s api %ld, host %ld", path,
			(long)version, (long)kSettingsApiVersion);
		unload_add_on(image);
		return B_MISMATCHED_VALUES;
	}

	void* createSymbol = NULL;
	status = get_image_symbol(image, "instantiate_settings_module",
		B_SYMBOL_TYPE_TEXT, &createSymbol);
	if (status != B_OK || createSymbol == NULL) {
		syslog(LOG_ERR, "settings: %s has no instantiate_settings_module",
			path);
		unload_add_on(image);
		return B_BAD_TYPE;
	}

	typedef SettingsModule* (*create_fn)();
	SettingsModule* module = reinterpret_cast<create_fn>(createSymbol)();
	if (module == NULL) {
		unload_add_on(image);
		return B_NO_INIT;
	}
	if (module->ApiVersion() != kSettingsApiVersion
		|| module->Identifier() == NULL || module->Name() == NULL
		|| module->Category() == NULL) {
		delete module;
		unload_add_on(image);
		return B_BAD_DATA;
	}

	if (context != NULL)
		module->LoadSettings(context);

	if (Find(module->Identifier()) != NULL) {
		syslog(LOG_ERR, "settings: duplicate module %s from %s",
			module->Identifier(), path);
		delete module;
		unload_add_on(image);
		return B_NAME_IN_USE;
	}

	fModules.AddItem(module);
	SettingsAddonImage* kept = new SettingsAddonImage();
	kept->id = image;
	fImages.AddItem(kept);
	return B_OK;
}
