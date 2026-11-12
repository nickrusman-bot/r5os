/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Login side of kMsgAdminSetDriverBlocked (Device Manager).
 */
#ifndef R5_DRIVER_ADMIN_H
#define R5_DRIVER_ADMIN_H


#include <Message.h>


// Blocks or unblocks one packaged kernel driver under /boot/system in
// /boot/system/settings/packages. Runs in Login as uid 0. Replies with
// "error" (int32) and, on failure, "text" (string).
status_t handle_driver_admin_message(BMessage* request, BMessage& reply);


#endif
