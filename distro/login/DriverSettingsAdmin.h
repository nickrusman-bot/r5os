/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Login side of kMsgAdminSetDriverSetting (Media).
 */
#ifndef R5_DRIVER_SETTINGS_ADMIN_H
#define R5_DRIVER_SETTINGS_ADMIN_H


#include <Message.h>


// Writes one of the Media driver settings in kernel/drivers for a standard
// account with an administrator grant. Runs in Login as uid 0. Replies with
// "error" (int32) and, on failure, "text" (string).
status_t handle_driver_settings_admin_message(BMessage* request,
	BMessage& reply);


#endif
