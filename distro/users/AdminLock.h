/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Lock for machine settings. The preference app stays the signed-in user.
 * Sign in (uid 0) checks an administrator password and applies the change.
 */
#ifndef R5_ADMIN_LOCK_H
#define R5_ADMIN_LOCK_H


#include <GroupView.h>
#include <List.h>
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
status_t r5_admin_unlock(const char* name, const char* password, BString& error);
status_t r5_admin_relock();
status_t r5_admin_set_clock(unsigned long seconds);
status_t r5_admin_set_timezone(int32 offset, const char* name);
status_t r5_admin_set_clock_gmt(bool gmt);


class AdminLockBar : public BGroupView {
public:
								AdminLockBar();
	virtual						~AdminLockBar();
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
};


#endif
