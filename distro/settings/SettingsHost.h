/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_SETTINGS_HOST_H
#define R5_SETTINGS_HOST_H


#include "SettingsModule.h"

#include <Messenger.h>


class SettingsStorage;


class SettingsHost : public SettingsContext {
public:
								SettingsHost(SettingsStorage* storage,
									const BMessenger& window);

	virtual	status_t			ReadSettings(const char* moduleId,
									BMessage& settings);
	virtual	status_t			WriteSettings(const char* moduleId,
									const BMessage& settings);
	virtual	bool				AdminChangeAllowed() const;
	virtual	status_t			OpenPage(const char* moduleId);
	virtual	status_t			LaunchLegacy(const char* signature);
	virtual	void				NotifyChanged(const char* moduleId);
	virtual	void				Log(const char* message);

private:
			SettingsStorage*	fStorage;
			BMessenger			fWindow;
};


#endif
