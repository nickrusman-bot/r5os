/*
 * Lock for machine settings. The preference app stays the signed-in user.
 * Sign in (uid 0) checks an administrator password and applies the change.
 */
#ifndef R5_ADMIN_LOCK_H
#define R5_ADMIN_LOCK_H


#include <GroupView.h>
#include <SupportDefs.h>


class BButton;
class BMessage;
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
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Reload();

			BStringView*		fLabel;
			BButton*			fButton;
			BMessageRunner*		fRunner;
			bool				fUnlocked;
};


#endif
