/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Public Settings module API. Version 1.
 * Add a virtual method only at the end, then bump kSettingsApiVersion.
 * A module built for a different version is not loaded.
 */
#ifndef R5_SETTINGS_MODULE_H
#define R5_SETTINGS_MODULE_H


#include <Rect.h>
#include <SupportDefs.h>


class BMessage;
class BView;


static const int32 kSettingsApiVersion = 1;


class SettingsContext {
public:
	virtual						~SettingsContext() {}

	virtual	status_t			ReadSettings(const char* moduleId,
									BMessage& settings) = 0;
	virtual	status_t			WriteSettings(const char* moduleId,
									const BMessage& settings) = 0;
	virtual	bool				AdminChangeAllowed() const = 0;
	virtual	status_t			OpenPage(const char* moduleId) = 0;
	virtual	status_t			LaunchLegacy(const char* signature) = 0;
	virtual	void				NotifyChanged(const char* moduleId) = 0;
	virtual	void				Log(const char* message) = 0;
};


class SettingsModule {
public:
	virtual						~SettingsModule() {}

	virtual	int32				ApiVersion() const
									{ return kSettingsApiVersion; }
	virtual	const char*			Identifier() const = 0;
	virtual	const char*			Name() const = 0;
	virtual	const char*			Category() const = 0;
	virtual	const char*			Description() const = 0;
	virtual	const char*			Keywords() const { return ""; }
	virtual	bool				RequiresAdmin() const { return false; }
	virtual	bool				CanSave() const { return true; }

	// The window owns the returned view. The module must not delete it.
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame) = 0;

	virtual	status_t			LoadSettings(SettingsContext* context) = 0;
	virtual	status_t			SaveSettings(SettingsContext* context) = 0;
	virtual	status_t			RevertSettings(SettingsContext* context) = 0;
	virtual	bool				IsModified() const { return false; }
};


#endif
