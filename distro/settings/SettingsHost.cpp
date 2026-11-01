/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "SettingsHost.h"

#include "SettingsMessages.h"
#include "SettingsStorage.h"

#include "AdminLock.h"

#include <Message.h>
#include <Roster.h>

#include <syslog.h>


SettingsHost::SettingsHost(SettingsStorage* storage, const BMessenger& window)
	:
	fStorage(storage),
	fWindow(window)
{
}


status_t
SettingsHost::ReadSettings(const char* moduleId, BMessage& settings)
{
	if (fStorage == NULL)
		return B_NO_INIT;
	return fStorage->Read(moduleId, settings);
}


status_t
SettingsHost::WriteSettings(const char* moduleId, const BMessage& settings)
{
	if (fStorage == NULL)
		return B_NO_INIT;
	return fStorage->Write(moduleId, settings);
}


bool
SettingsHost::AdminChangeAllowed() const
{
	return r5_admin_change_allowed();
}


status_t
SettingsHost::OpenPage(const char* moduleId)
{
	if (moduleId == NULL || moduleId[0] == '\0')
		return B_BAD_VALUE;
	BMessage message(kMsgOpenPage);
	message.AddString("module", moduleId);
	return fWindow.SendMessage(&message);
}


status_t
SettingsHost::LaunchLegacy(const char* signature)
{
	if (signature == NULL || signature[0] == '\0')
		return B_BAD_VALUE;
	return be_roster->Launch(signature);
}


void
SettingsHost::NotifyChanged(const char* moduleId)
{
	BMessage message(kMsgModuleChanged);
	if (moduleId != NULL)
		message.AddString("module", moduleId);
	fWindow.SendMessage(&message);
}


void
SettingsHost::Log(const char* message)
{
	syslog(LOG_INFO, "settings: %s", message != NULL ? message : "");
}
