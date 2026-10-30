/*
 * Ctrl+Shift+L asks Login to cover the screen. The session stays.
 */
#ifndef R5_LOCK_SCREEN_FILTER_H
#define R5_LOCK_SCREEN_FILTER_H


#include <InputServerFilter.h>


class LockScreenFilter : public BInputServerFilter {
public:
							LockScreenFilter();

	virtual	filter_result	Filter(BMessage* message, BList* _list);
	virtual	status_t		InitCheck();

private:
			bool			fDown;
};


#endif
