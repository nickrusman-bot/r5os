/*
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


bool is_listed_account(const struct passwd* pwd);
bool is_admin_user(const struct passwd* pwd);
bool is_disabled_user(const char* name);
bool is_first_home_admin(const struct passwd* pwd);
bool current_user_is_admin();
int32 admin_count();

status_t migrate_admin_off_root();
status_t write_desktop_session();
status_t handle_admin_message(BMessage* request, BMessage& reply);


#endif
