/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Part of libr5ui.so (docs/LIBR5.md, "Administrator lock"). Relicensed from
 * GPL-3.0-or-later to MIT by the rights holder in November 2026, so any
 * program, Device Manager included, can use it.
 *
 * Lock for machine settings. The program stays the signed-in person.
 * Sign in (Login, uid 0) checks an administrator password and applies the
 * change. A function or field that shipped does not change; add new ones at
 * the end.
 */
#ifndef R5_ADMIN_LOCK_H
#define R5_ADMIN_LOCK_H


#include <GroupView.h>
#include <List.h>
#include <Messenger.h>
#include <SupportDefs.h>


class BButton;
class BHandler;
class BMessage;
class BMessageFilter;
class BMessageRunner;
class BString;
class BStringView;


const uint32 kMsgAdminLockChanged = 'r5ac';


bool r5_admin_change_allowed();
// administrator is the signed-in account. unlocked is that, or a grant
// that is still running. Both are false when Login cannot be asked.
status_t r5_admin_query(bool& administrator, bool& unlocked);
// Asks for an administrator name and password. On success, sends
// kMsgAdminLockChanged to target. The password is not logged.
void r5_admin_prompt_unlock(BMessenger target);
status_t r5_admin_unlock(const char* name, const char* password, BString& error);
status_t r5_admin_relock();
status_t r5_admin_set_clock(unsigned long seconds);
status_t r5_admin_set_timezone(int32 offset, const char* name);
status_t r5_admin_set_clock_gmt(bool gmt);
// Blocks or unblocks one packaged driver under /boot/system. Login checks
// the grant and refuses drivers the system needs to start.
status_t r5_admin_set_driver_blocked(const char* driver, bool blocked,
	BString& error);

// Driver settings: files in the kernel/drivers directory that the kernel
// reads (load_driver_settings()), which is the administrator's settings, not
// the signed-in person's. DriverSettings.cpp.
// True when this program may write that directory itself.
bool r5_driver_settings_writable();
// The value after key on its line. B_NAME_NOT_FOUND without that line.
status_t r5_driver_settings_read(const char* fileName, const char* key,
	BString& value);
// Replaces the lines that start with one of keys by lines (each ending in a
// newline) and keeps the others. With replaceFile the file is only lines.
// Written to a temporary file and renamed.
status_t r5_driver_settings_write(const char* fileName,
	const char* const* keys, int32 keyCount, const char* lines,
	bool replaceFile);
// The same write through Login, which checks the grant and only accepts the
// files and keys it knows (the Media settings).
status_t r5_admin_set_driver_setting(const char* fileName,
	const char* const* keys, int32 keyCount, const char* lines,
	bool replaceFile, BString& error);


namespace BAppearanceKit {


class BAdminLockBar : public BGroupView {
public:
								BAdminLockBar();
	virtual						~BAdminLockBar();
	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			bool				AcceptsInput(BHandler* target) const;
			// A view under here stays usable while the machine lock is on.
			// Used for the signed-in person's own account card.
			void				SetExempt(BView* view, bool exempt);

private:
			void				_Reload();
			void				_SyncControls();
			void				_Prune();
			void				_DisableTree(BView* view);
			bool				_IsExempt(BView* view) const;

			BStringView*		fLabel;
			BButton*			fButton;
			BMessageRunner*		fRunner;
			BMessageRunner*		fWatch;
			BMessageFilter*		fFilter;
			BList				fForced;
			BList				fExempt;
			bool				fAdmin;
			bool				fUnlocked;
			bool				fSyncing;

			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


// The name the system preferences have always used.
typedef BAppearanceKit::BAdminLockBar AdminLockBar;


#endif
