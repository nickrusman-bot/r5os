/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Private to libr5ui. The messages the administrator lock sends to Login
 * (application/x-vnd.Haiku-Login). Login defines the same values in
 * distro/users/AccountAdmin.h. A value that shipped does not change.
 */
#ifndef R5_ADMIN_LOCK_PROTOCOL_H
#define R5_ADMIN_LOCK_PROTOCOL_H


#include <SupportDefs.h>


static const char* const kAdminLockLoginSignature = "application/x-vnd.Haiku-Login";

static const uint32 kMsgAdminQuery = 'r5aq';
static const uint32 kMsgAdminUnlock = 'r5au';
static const uint32 kMsgAdminRelock = 'r5al';
static const uint32 kMsgAdminSetClock = 'r5at';
static const uint32 kMsgAdminSetTimezone = 'r5az';
static const uint32 kMsgAdminSetClockGmt = 'r5ag';
static const uint32 kMsgAdminSetDriverBlocked = 'r5dv';
static const uint32 kMsgAdminSetDriverSetting = 'r5ds';


#endif
