/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * R5OS account helper. Login stays uid 0 and applies passwd changes.
 */
#ifndef R5_ACCOUNT_ADMIN_H
#define R5_ACCOUNT_ADMIN_H


#include <SupportDefs.h>

#include <pwd.h>


class BMessage;
class BString;


extern const char* kR5LoginSignature;
extern const char* kAdminGroupName;

static const uid_t kMinPersonUid = 1000;

const uint32 kMsgAdminAdd = 'r5aa';
const uint32 kMsgAdminDelete = 'r5ad';
const uint32 kMsgAdminSetDisabled = 'r5ax';
// The lock messages below have the same values in libr5ui's
// AdminLockProtocol.h. Change neither.
const uint32 kMsgAdminQuery = 'r5aq';
const uint32 kMsgAdminUnlock = 'r5au';
const uint32 kMsgAdminRelock = 'r5al';
const uint32 kMsgAdminSetClock = 'r5at';
const uint32 kMsgAdminSetTimezone = 'r5az';
const uint32 kMsgAdminSetClockGmt = 'r5ag';
const uint32 kMsgAdminGetProfile = 'r5ah';
const uint32 kMsgAdminSetProfile = 'r5af';
// Device Manager: block or unblock one packaged kernel driver. Fields
// "driver" (string, the driver file under /boot/system) and "blocked" (bool).
const uint32 kMsgAdminSetDriverBlocked = 'r5dv';
// Media: write a driver setting in kernel/drivers. Fields "file" (string),
// "key" (strings, the lines replaced), "lines" (string) and "replace" (bool).
// Login only accepts the files and keys in DriverSettingsAdmin.cpp.
const uint32 kMsgAdminSetDriverSetting = 'r5ds';

// How long an administrator password keeps one preference app unlocked.
static const int32 kAdminGrantSeconds = 5 * 60;


bool is_listed_account(const struct passwd* pwd);
bool is_admin_user(const struct passwd* pwd);
bool is_disabled_user(const char* name);
bool is_first_home_admin(const struct passwd* pwd);
bool current_user_is_admin();
bool account_password_matches(const struct passwd* pwd, const char* plain);
int32 admin_count();

status_t migrate_admin_off_root();
status_t write_desktop_session(const char* login);
status_t clear_desktop_session();
status_t signed_in_name(BString& name);
void clear_admin_grants();
void refresh_machine_settings_access();
status_t handle_admin_message(BMessage* request, BMessage& reply);
// True when the sender is an administrator or holds a running grant.
bool admin_sender_may_change(BMessage* request);


#endif
