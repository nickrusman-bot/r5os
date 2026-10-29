/*
 * Lock for machine settings. See AdminLock.h.
 */


#include "AdminLock.h"

#include "AccountAdmin.h"

#include <Alert.h>
#include <Button.h>
#include <Control.h>
#include <LayoutBuilder.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <ScrollBar.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <string.h>
#include <unistd.h>


static const uint32 kMsgPressUnlock = 'r5ap';
static const uint32 kMsgRefreshLock = 'r5ar';
static const uint32 kMsgUnlockOk = 'r5ao';
static const uint32 kMsgWatchLock = 'r5aw';


static bool
is_under(BView* view, BView* ancestor)
{
	for (BView* item = view; item != NULL; item = item->Parent()) {
		if (item == ancestor)
			return true;
	}
	return false;
}


static bool
tree_has(BView* view, BView* needle)
{
	if (view == NULL || needle == NULL)
		return false;
	if (view == needle)
		return true;
	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++) {
		if (tree_has(view->ChildAt(i), needle))
			return true;
	}
	return false;
}


class LockInputFilter : public BMessageFilter {
public:
	LockInputFilter(AdminLockBar* bar)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		fBar(bar)
	{
	}

	virtual filter_result
	Filter(BMessage* message, BHandler** target)
	{
		if (message == NULL || target == NULL)
			return B_DISPATCH_MESSAGE;
		uint32 what = message->what;
		if (what != B_MOUSE_DOWN && what != B_MOUSE_UP && what != B_KEY_DOWN
			&& what != B_KEY_UP)
			return B_DISPATCH_MESSAGE;
		if (fBar->AcceptsInput(*target))
			return B_DISPATCH_MESSAGE;
		return B_SKIP_MESSAGE;
	}

private:
	AdminLockBar*	fBar;
};


static status_t
ask_login(BMessage& request, BMessage& reply, BString& error)
{
	BMessenger login("application/x-vnd.Haiku-Login");
	if (!login.IsValid()) {
		error = "Sign in must be running.";
		return B_ERROR;
	}

	status_t status = login.SendMessage(&request, &reply);
	if (status != B_OK) {
		error = strerror(status);
		return status;
	}

	int32 code = B_OK;
	reply.FindInt32("error", &code);
	const char* text = NULL;
	reply.FindString("text", &text);
	if (code != B_OK) {
		error = text != NULL ? text : strerror(code);
		return code;
	}
	return B_OK;
}


bool
r5_admin_change_allowed()
{
	if (geteuid() == 0)
		return true;

	BMessage request(kMsgAdminQuery);
	BMessage reply;
	BString error;
	if (ask_login(request, reply, error) != B_OK)
		return false;

	bool unlocked = false;
	reply.FindBool("unlocked", &unlocked);
	return unlocked;
}


status_t
r5_admin_unlock(const char* name, const char* password, BString& error)
{
	BMessage request(kMsgAdminUnlock);
	request.AddString("name", name != NULL ? name : "");
	request.AddString("password", password != NULL ? password : "");
	BMessage reply;
	return ask_login(request, reply, error);
}


status_t
r5_admin_relock()
{
	BMessage request(kMsgAdminRelock);
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_clock(unsigned long seconds)
{
	BMessage request(kMsgAdminSetClock);
	request.AddInt64("seconds", (int64)seconds);
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_timezone(int32 offset, const char* name)
{
	BMessage request(kMsgAdminSetTimezone);
	request.AddInt32("offset", offset);
	request.AddString("name", name != NULL ? name : "");
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_clock_gmt(bool gmt)
{
	BMessage request(kMsgAdminSetClockGmt);
	request.AddBool("gmt", gmt);
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


class UnlockWindow : public BWindow {
public:
	UnlockWindow(BMessenger target)
		:
		BWindow(BRect(0, 0, 320, 140), "Unlock", B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target),
		fName(NULL),
		fPassword(NULL)
	{
		fName = new BTextControl("name", "Administrator", "user", NULL);
		fPassword = new BTextControl("password", "Password", "", NULL);
		fPassword->TextView()->HideTyping(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(new BStringView("hint",
				"This changes the computer, not only your account."))
			.Add(fName)
			.Add(fPassword)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", "Cancel",
					new BMessage(B_QUIT_REQUESTED)))
				.Add(new BButton("unlock", "Unlock",
					new BMessage(kMsgUnlockOk)))
			.End();

		fPassword->MakeFocus(true);
		CenterOnScreen();
	}

	virtual void
	MessageReceived(BMessage* message)
	{
		if (message->what != kMsgUnlockOk) {
			BWindow::MessageReceived(message);
			return;
		}

		BString error;
		status_t status = r5_admin_unlock(fName->Text(), fPassword->Text(),
			error);
		fPassword->SetText("");
		if (status != B_OK) {
			BAlert* alert = new BAlert("Unlock",
				error.Length() > 0 ? error.String() : "Could not unlock.",
				"OK");
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
			return;
		}

		fTarget.SendMessage(kMsgRefreshLock);
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BMessenger		fTarget;
	BTextControl*	fName;
	BTextControl*	fPassword;
};


AdminLockBar::AdminLockBar()
	:
	BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING),
	fLabel(NULL),
	fButton(NULL),
	fRunner(NULL),
	fWatch(NULL),
	fFilter(NULL),
	fAdmin(false),
	fUnlocked(false),
	fSyncing(false)
{
	fLabel = new BStringView("admin-lock", "Locked");
	fButton = new BButton("admin-unlock", "Unlock...",
		new BMessage(kMsgPressUnlock));
	fFilter = new LockInputFilter(this);
	BLayoutBuilder::Group<>(this)
		.Add(fLabel)
		.Add(fButton);
}


AdminLockBar::~AdminLockBar()
{
	delete fRunner;
	delete fWatch;
	if (Window() != NULL && fFilter != NULL)
		Window()->RemoveCommonFilter(fFilter);
	delete fFilter;
}


void
AdminLockBar::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fButton->SetTarget(this);
	if (fFilter != NULL)
		Window()->AddCommonFilter(fFilter);
	_Reload();
	fRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgRefreshLock), 10000000);
	fWatch = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgWatchLock), 200000);
}


void
AdminLockBar::DetachedFromWindow()
{
	if (fFilter != NULL)
		Window()->RemoveCommonFilter(fFilter);
	BGroupView::DetachedFromWindow();
}


bool
AdminLockBar::AcceptsInput(BHandler* target) const
{
	if (fAdmin || fUnlocked)
		return true;

	BView* view = dynamic_cast<BView*>(target);
	if (view == NULL || is_under(view, const_cast<AdminLockBar*>(this)))
		return true;

	for (BView* item = view; item != NULL; item = item->Parent()) {
		if (dynamic_cast<BScrollBar*>(item) != NULL)
			return true;
		if (dynamic_cast<BControl*>(item) != NULL)
			return false;
	}
	return true;
}


void
AdminLockBar::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPressUnlock:
			if (fUnlocked)
				r5_admin_relock();
			else
				(new UnlockWindow(BMessenger(this)))->Show();
			_Reload();
			break;
		case kMsgRefreshLock:
			_Reload();
			break;
		case kMsgWatchLock:
			_SyncControls();
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
AdminLockBar::_Reload()
{
	bool was = fUnlocked;
	bool wasAdmin = fAdmin;
	BMessage request(kMsgAdminQuery);
	BMessage reply;
	BString error;
	fAdmin = false;
	fUnlocked = false;
	if (ask_login(request, reply, error) == B_OK) {
		reply.FindBool("admin", &fAdmin);
		reply.FindBool("unlocked", &fUnlocked);
	}

	if (fAdmin) {
		fLabel->SetText("Administrator");
		fButton->Hide();
	} else if (fUnlocked) {
		fLabel->SetText("Unlocked for a few minutes");
		fButton->SetLabel("Lock");
		fButton->Show();
	} else {
		fLabel->SetText("Locked");
		fButton->SetLabel("Unlock...");
		fButton->Show();
	}

	_SyncControls();
	if ((was != fUnlocked || wasAdmin != fAdmin) && Window() != NULL)
		Window()->PostMessage(kMsgAdminLockChanged);
}


void
AdminLockBar::_Prune()
{
	BWindow* window = Window();
	for (int32 i = fForced.CountItems() - 1; i >= 0; i--) {
		BView* control = (BView*)fForced.ItemAt(i);
		bool live = false;
		if (window != NULL) {
			int32 count = window->CountChildren();
			for (int32 child = 0; child < count; child++) {
				if (tree_has(window->ChildAt(child), control)) {
					live = true;
					break;
				}
			}
		}
		if (!live)
			fForced.RemoveItem(i);
	}
}


void
AdminLockBar::_DisableTree(BView* view)
{
	if (view == NULL || is_under(view, this))
		return;

	BControl* control = dynamic_cast<BControl*>(view);
	if (control != NULL && dynamic_cast<BScrollBar*>(control) == NULL
		&& control->IsEnabled() && !fForced.HasItem(control)) {
		control->SetEnabled(false);
		fForced.AddItem(control);
	}

	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++)
		_DisableTree(view->ChildAt(i));
}


void
AdminLockBar::_SyncControls()
{
	if (fSyncing || Window() == NULL)
		return;

	fSyncing = true;
	_Prune();
	if (fAdmin || fUnlocked) {
		for (int32 i = 0; i < fForced.CountItems(); i++) {
			BControl* control = (BControl*)fForced.ItemAt(i);
			control->SetEnabled(true);
		}
		fForced.MakeEmpty();
	} else {
		int32 count = Window()->CountChildren();
		for (int32 i = 0; i < count; i++)
			_DisableTree(Window()->ChildAt(i));
	}
	fSyncing = false;
}
