/*
 * R5OS Users & Accounts. Haiku #8940.
 */


#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Messenger.h>
#include <Path.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <SupportDefs.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>

#include "AccountAdmin.h"
#include "AdminLock.h"
#include "HomeLock.h"

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


static const char* kSignature = "application/x-vnd.R5OS-Users";

static const uint32 kMsgAdd = 'uadd';
static const uint32 kMsgDelete = 'udel';
static const uint32 kMsgDisable = 'udis';
static const uint32 kMsgRefresh = 'urfr';
static const uint32 kMsgSelect = 'usel';
static const uint32 kMsgSave = 'usav';
static const uint32 kMsgPick = 'upic';
static const uint32 kMsgClearPicture = 'uclr';
static const uint32 kMsgPassword = 'upwd';

static const int32 kAvatarSide = 128;


static void
wipe_string(BString& text)
{
	int32 length = text.Length();
	if (length <= 0)
		return;
	char* buffer = text.LockBuffer(length);
	if (buffer != NULL)
		memset(buffer, 0, length);
	text.UnlockBuffer(length);
}


static void
show_error(const char* text)
{
	BAlert* alert = new BAlert("Users", text, "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


static status_t
ask_login(BMessage& request, BMessage& reply, BString& error)
{
	BMessenger login(kR5LoginSignature);
	if (!login.IsValid()) {
		error = "Sign in must be running to change accounts.";
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


static status_t
ask_login(BMessage& request, BString& error)
{
	BMessage reply;
	return ask_login(request, reply, error);
}


static rgb_color
tint_for_name(const char* name)
{
	static const rgb_color kTints[] = {
		{ 232, 168, 74, 255 },
		{ 90, 156, 196, 255 },
		{ 168, 128, 196, 255 },
		{ 92, 176, 140, 255 },
		{ 196, 112, 112, 255 }
	};
	uint32 hash = 0;
	if (name != NULL) {
		for (const char* c = name; *c != '\0'; c++)
			hash = hash * 33 + (uint8)*c;
	}
	return kTints[hash % 5];
}


static BBitmap*
circle_bitmap(const BBitmap* source, rgb_color tint, char initial)
{
	BBitmap* shown = new BBitmap(BRect(0, 0, kAvatarSide - 1, kAvatarSide - 1),
		B_RGBA32);
	if (shown->InitCheck() != B_OK)
		return shown;

	int32 bpr = shown->BytesPerRow();
	uint8* bits = (uint8*)shown->Bits();
	memset(bits, 0, shown->BitsLength());

	const uint8* src = NULL;
	int32 srcBpr = 0;
	int32 srcW = 0;
	int32 srcH = 0;
	int32 crop = 0;
	int32 originX = 0;
	int32 originY = 0;
	if (source != NULL && (source->ColorSpace() == B_RGBA32
		|| source->ColorSpace() == B_RGB32)) {
		src = (const uint8*)source->Bits();
		srcBpr = source->BytesPerRow();
		srcW = source->Bounds().IntegerWidth() + 1;
		srcH = source->Bounds().IntegerHeight() + 1;
		crop = srcW < srcH ? srcW : srcH;
		originX = (srcW - crop) / 2;
		originY = (srcH - crop) / 2;
	}

	float cx = (kAvatarSide - 1) / 2.0f;
	float cy = cx;
	float radius = cx - 3.0f;
	for (int32 y = 0; y < kAvatarSide; y++) {
		uint8* row = bits + y * bpr;
		for (int32 x = 0; x < kAvatarSide; x++) {
			float dx = x - cx;
			float dy = y - cy;
			float dist = dx * dx + dy * dy;
			uint8* pixel = row + x * 4;
			if (dist > radius * radius)
				continue;
			if (dist > (radius - 3.0f) * (radius - 3.0f)) {
				pixel[0] = 210;
				pixel[1] = 164;
				pixel[2] = 64;
				pixel[3] = 255;
				continue;
			}
			if (src != NULL && crop > 0) {
				int32 sx = originX + (int32)((x / (float)kAvatarSide) * crop);
				int32 sy = originY + (int32)((y / (float)kAvatarSide) * crop);
				if (sx >= srcW)
					sx = srcW - 1;
				if (sy >= srcH)
					sy = srcH - 1;
				const uint8* sample = src + sy * srcBpr + sx * 4;
				pixel[0] = sample[0];
				pixel[1] = sample[1];
				pixel[2] = sample[2];
				pixel[3] = 255;
			} else {
				pixel[0] = tint.blue;
				pixel[1] = tint.green;
				pixel[2] = tint.red;
				pixel[3] = 255;
			}
		}
	}

	(void)initial;
	return shown;
}


class AvatarView : public BView {
public:
	AvatarView()
		:
		BView("avatar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fShown(NULL),
		fInitial('?'),
		fTint(tint_for_name(NULL)),
		fHasPhoto(false)
	{
		SetExplicitMinSize(BSize(kAvatarSide, kAvatarSide));
		SetExplicitMaxSize(BSize(kAvatarSide, kAvatarSide));
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	}

	~AvatarView()
	{
		delete fShown;
	}

	void SetPerson(const char* name, const BBitmap* source)
	{
		fInitial = (name != NULL && name[0] != '\0') ? name[0] : '?';
		if (fInitial >= 'a' && fInitial <= 'z')
			fInitial = (char)(fInitial - 'a' + 'A');
		fTint = tint_for_name(name);
		delete fShown;
		fHasPhoto = source != NULL;
		fShown = circle_bitmap(source, fTint, fInitial);
		Invalidate();
	}

	virtual void Draw(BRect update)
	{
		SetLowColor(ViewColor());
		FillRect(update, B_SOLID_LOW);
		if (fShown != NULL) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			DrawBitmap(fShown, BPoint(0, 0));
		}
		if (!fHasPhoto) {
			BFont font(be_bold_font);
			font.SetSize(46);
			SetFont(&font);
			SetDrawingMode(B_OP_OVER);
			SetHighColor(255, 255, 255);
			char text[2] = { fInitial, '\0' };
			float width = StringWidth(text);
			font_height height;
			font.GetHeight(&height);
			DrawString(text, BPoint((kAvatarSide - width) / 2,
				(kAvatarSide + height.ascent - height.descent) / 2));
		}
	}

private:
	BBitmap*	fShown;
	char		fInitial;
	rgb_color	fTint;
	bool		fHasPhoto;
};


class UserItem : public BStringItem {
public:
	UserItem(const char* name, const char* real, uid_t uid, bool admin,
		bool disabled)
		:
		BStringItem(name),
		fName(name),
		fReal(real != NULL ? real : ""),
		fUid(uid),
		fAdmin(admin),
		fDisabled(disabled)
	{
		SetHeight(40);
	}

	const char* Name() const		{ return fName.String(); }
	const char* Real() const		{ return fReal.String(); }
	uid_t Uid() const				{ return fUid; }
	bool IsAdmin() const			{ return fAdmin; }
	bool IsDisabled() const			{ return fDisabled; }

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color text = ui_color(IsSelected()
			? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		rgb_color bg = ui_color(IsSelected()
			? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
		owner->SetHighColor(bg);
		owner->FillRect(frame);

		BString title(fReal.Length() > 0 ? fReal : fName);
		owner->SetFont(be_bold_font);
		owner->SetHighColor(text);
		owner->DrawString(title.String(), BPoint(frame.left + 10, frame.top + 16));

		BString sub(fName);
		sub << (fAdmin ? "  ·  administrator" : "  ·  standard");
		if (fDisabled)
			sub << "  ·  off";
		owner->SetFont(be_plain_font);
		owner->SetHighColor(tint_color(text, IsSelected() ? 0.15 : 0.35));
		owner->DrawString(sub.String(), BPoint(frame.left + 10, frame.top + 32));
	}

private:
	BString	fName;
	BString	fReal;
	uid_t	fUid;
	bool	fAdmin;
	bool	fDisabled;
};


class PasswordWindow : public BWindow {
public:
	PasswordWindow(BWindow* parent, const char* name, bool needCurrent)
		:
		BWindow(BRect(0, 0, 340, 180), "Change password", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fParent(parent),
		fName(name),
		fNeedCurrent(needCurrent),
		fCurrent(NULL)
	{
		if (needCurrent) {
			fCurrent = new BTextControl("current", "Current", "", NULL);
			fCurrent->TextView()->HideTyping(true);
		}
		fNext = new BTextControl("next", "New password", "", NULL);
		fNext->TextView()->HideTyping(true);
		fRepeat = new BTextControl("repeat", "Repeat", "", NULL);
		fRepeat->TextView()->HideTyping(true);

		BLayoutBuilder::Group<> layout(this, B_VERTICAL, B_USE_SMALL_SPACING);
		layout.SetInsets(B_USE_WINDOW_INSETS);
		if (fCurrent != NULL)
			layout.Add(fCurrent);
		layout.Add(fNext)
			.Add(fRepeat)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", "Cancel",
					new BMessage(B_QUIT_REQUESTED)))
				.Add(new BButton("ok", "Change", new BMessage('pwok')))
			.End();

		if (fCurrent != NULL)
			fCurrent->MakeFocus(true);
		else
			fNext->MakeFocus(true);
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != 'pwok') {
			BWindow::MessageReceived(message);
			return;
		}

		BString current(fCurrent != NULL ? fCurrent->Text() : "");
		BString next(fNext->Text());
		BString repeat(fRepeat->Text());
		if (next.Length() == 0) {
			show_error("Type a new password.");
			wipe_string(current);
			wipe_string(next);
			wipe_string(repeat);
			return;
		}
		if (next != repeat) {
			show_error("The passwords do not match.");
			wipe_string(current);
			wipe_string(next);
			wipe_string(repeat);
			return;
		}

		BMessage request(kMsgAdminSetProfile);
		request.AddString("name", fName.String());
		request.AddString("password", next.String());
		request.AddString("current", current.String());
		BString error;
		status_t status = ask_login(request, error);
		wipe_string(current);
		wipe_string(next);
		wipe_string(repeat);
		if (status != B_OK) {
			show_error(error.Length() > 0 ? error.String()
				: "The password could not be changed.");
			return;
		}
		if (fParent != NULL)
			fParent->PostMessage(kMsgRefresh);
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BWindow*		fParent;
	BString			fName;
	bool			fNeedCurrent;
	BTextControl*	fCurrent;
	BTextControl*	fNext;
	BTextControl*	fRepeat;
};


class CreateWindow : public BWindow {
public:
	CreateWindow(BWindow* parent)
		:
		BWindow(BRect(0, 0, 380, 240), "Add account", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fParent(parent)
	{
		fName = new BTextControl("name", "Username", "", NULL);
		fReal = new BTextControl("real", "Name", "", NULL);
		fPassword = new BTextControl("password", "Password", "", NULL);
		fPassword->TextView()->HideTyping(true);
		fRepeat = new BTextControl("repeat", "Repeat password", "", NULL);
		fRepeat->TextView()->HideTyping(true);
		fAdmin = new BCheckBox("admin", "Administrator", NULL);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fName)
			.Add(fReal)
			.Add(fPassword)
			.Add(fRepeat)
			.Add(fAdmin)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", "Cancel",
					new BMessage(B_QUIT_REQUESTED)))
				.Add(new BButton("create", "Create", new BMessage('ucrt')))
			.End();

		fName->MakeFocus(true);
		AddShortcut('W', 0, new BMessage(B_QUIT_REQUESTED));
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != 'ucrt') {
			BWindow::MessageReceived(message);
			return;
		}

		const char* name = fName->Text();
		BString password(fPassword->Text());
		BString repeat(fRepeat->Text());
		if (password != repeat) {
			wipe_string(password);
			wipe_string(repeat);
			show_error("The passwords do not match.");
			return;
		}

		BMessage request(kMsgAdminAdd);
		request.AddString("name", name);
		request.AddString("real", fReal->Text());
		request.AddString("password", password.String());
		request.AddBool("admin", fAdmin->Value() == B_CONTROL_ON);

		BString error;
		status_t status = ask_login(request, error);
		wipe_string(password);
		wipe_string(repeat);
		if (status != B_OK) {
			show_error(error.String());
			return;
		}

		fParent->PostMessage(kMsgRefresh);
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BWindow*		fParent;
	BTextControl*	fName;
	BTextControl*	fReal;
	BTextControl*	fPassword;
	BTextControl*	fRepeat;
	BCheckBox*		fAdmin;
};


class UsersWindow : public BWindow {
public:
	UsersWindow()
		:
		BWindow(BRect(80, 60, 760, 520), "Users & Accounts",
			B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
		fList(NULL),
		fPeople(NULL),
		fAvatar(NULL),
		fPanel(NULL),
		fPhoto(NULL),
		fClearPhoto(false),
		fManaging(false)
	{
		fList = new BListView("people");
		fList->SetSelectionMessage(new BMessage(kMsgSelect));
		fList->SetExplicitMinSize(BSize(220, 220));
		fAdd = new BButton("add", "Add...", new BMessage(kMsgAdd));
		fDisable = new BButton("disable", "Disable", new BMessage(kMsgDisable));
		fDelete = new BButton("delete", "Delete...", new BMessage(kMsgDelete));

		BStringView* people = new BStringView("people-title", "People");
		BFont font(be_bold_font);
		people->SetFont(&font);

		fPeople = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
		BLayoutBuilder::Group<>(fPeople)
			.Add(people)
			.Add(new BScrollView("scroll", fList, 0, false, true))
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fAdd)
				.Add(fDisable)
				.Add(fDelete)
			.End();

		fAvatar = new AvatarView();
		fNameView = new BStringView("username", "");
		fNameView->SetFont(be_plain_font);
		fReal = new BTextControl("real", "Name", "", NULL);
		fEmail = new BTextControl("email", "Email", "", NULL);
		fRole = new BStringView("role", "");

		BGroupView* card = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
		card->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		BLayoutBuilder::Group<>(card)
			.SetInsets(B_USE_BIG_SPACING)
			.AddGroup(B_HORIZONTAL, B_USE_BIG_SPACING)
				.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
					.Add(fAvatar)
					.Add(new BButton("picture", "Choose picture...",
						new BMessage(kMsgPick)))
					.Add(new BButton("clear", "Remove picture",
						new BMessage(kMsgClearPicture)))
				.End()
				.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
					.Add(fReal)
					.Add(fNameView)
					.Add(fEmail)
					.Add(fRole)
					.Add(new BButton("password", "Change password...",
						new BMessage(kMsgPassword)))
					.AddGlue()
				.End()
			.End()
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("save", "Save", new BMessage(kMsgSave)))
			.End();

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.AddGroup(B_HORIZONTAL, B_USE_BIG_SPACING)
				.Add(fPeople)
				.Add(card)
			.End()
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new AdminLockBar())
			.End();

		_Fill();
		CenterOnScreen();
	}

	~UsersWindow()
	{
		delete fPanel;
		delete fPhoto;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgAdd:
				_Add();
				break;
			case kMsgRefresh:
				_Fill();
				break;
			case kMsgSelect:
				_ShowSelected();
				_UpdateButtons();
				break;
			case kMsgDisable:
				_DisableOrEnable();
				break;
			case kMsgDelete:
				_Delete();
				break;
			case kMsgAdminLockChanged:
				_Fill();
				break;
			case kMsgSave:
				_Save();
				break;
			case kMsgPick:
				if (message->HasRef("refs"))
					_PictureRef(message);
				else
					_Pick();
				break;
			case kMsgClearPicture:
				_ClearPicture();
				break;
			case kMsgPassword:
				_Password();
				break;
			case B_REFS_RECEIVED:
				_PictureRef(message);
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	bool _Managing() const
	{
		return r5_admin_change_allowed();
	}

	void _ApplyChrome()
	{
		fManaging = _Managing();
		if (fManaging) {
			if (fPeople->IsHidden())
				fPeople->Show();
			SetTitle("Users & Accounts");
		} else {
			if (!fPeople->IsHidden())
				fPeople->Hide();
			SetTitle("Your account");
		}
	}

	void _Fill()
	{
		BString keep;
		UserItem* selected = _Selected();
		if (selected != NULL)
			keep = selected->Name();

		fList->MakeEmpty();
		setpwent();
		int32 select = -1;
		int32 index = 0;
		uid_t self = getuid();
		for (;;) {
			struct passwd* pwd = getpwent();
			if (pwd == NULL)
				break;
			if (!is_listed_account(pwd))
				continue;
			bool admin = is_admin_user(pwd);
			bool disabled = is_disabled_user(pwd->pw_name);
			fList->AddItem(new UserItem(pwd->pw_name,
				pwd->pw_gecos != NULL ? pwd->pw_gecos : "",
				pwd->pw_uid, admin, disabled));
			if (keep.Length() > 0 && keep == pwd->pw_name)
				select = index;
			else if (select < 0 && pwd->pw_uid == self)
				select = index;
			index++;
		}
		endpwent();
		_ApplyChrome();
		if (!fManaging) {
			_LoadUid(self);
			return;
		}
		if (select < 0 && fList->CountItems() > 0)
			select = 0;
		if (select >= 0)
			fList->Select(select);
		else
			_ShowSelected();
		_UpdateButtons();
	}

	UserItem* _Selected() const
	{
		return dynamic_cast<UserItem*>(
			fList->ItemAt(fList->CurrentSelection()));
	}

	bool _SelectedIsFirstHome() const
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return false;
		struct passwd* pwd = getpwnam(item->Name());
		return is_first_home_admin(pwd);
	}

	void _ShowSelected()
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return;
		_LoadName(item->Name());
	}

	void _LoadUid(uid_t uid)
	{
		struct passwd* pwd = getpwuid(uid);
		if (pwd == NULL || pwd->pw_name == NULL)
			return;
		_LoadName(pwd->pw_name);
	}

	void _LoadName(const char* name)
	{
		struct passwd* pwd = getpwnam(name);
		if (pwd == NULL)
			return;
		fAccount = pwd->pw_name;
		fReal->SetText(pwd->pw_gecos != NULL ? pwd->pw_gecos : "");
		BString userLine("Username   ");
		userLine << pwd->pw_name;
		fNameView->SetText(userLine.String());
		BString role(is_admin_user(pwd) ? "Administrator" : "Standard account");
		if (is_disabled_user(pwd->pw_name))
			role << "  ·  sign-in off";
		fRole->SetText(role.String());

		delete fPhoto;
		fPhoto = NULL;
		fClearPhoto = false;
		fEmail->SetText("");

		BMessage request(kMsgAdminGetProfile);
		request.AddString("name", pwd->pw_name);
		BMessage reply;
		BString error;
		if (ask_login(request, reply, error) == B_OK) {
			const char* email = NULL;
			if (reply.FindString("email", &email) == B_OK && email != NULL)
				fEmail->SetText(email);
			const void* data = NULL;
			ssize_t size = 0;
			if (reply.FindData("picture", B_RAW_TYPE, &data, &size) == B_OK)
				fPhoto = _BitmapFromBlob(data, size);
		}
		fAvatar->SetPerson(fReal->Text()[0] != '\0' ? fReal->Text()
			: pwd->pw_name, fPhoto);
	}

	BBitmap* _BitmapFromBlob(const void* data, ssize_t size) const
	{
		if (data == NULL || size < (ssize_t)(sizeof(int32) * 4))
			return NULL;
		const uint8* bytes = (const uint8*)data;
		int32 width = 0;
		int32 height = 0;
		int32 bpr = 0;
		int32 space = 0;
		memcpy(&width, bytes, sizeof(int32));
		memcpy(&height, bytes + 4, sizeof(int32));
		memcpy(&bpr, bytes + 8, sizeof(int32));
		memcpy(&space, bytes + 12, sizeof(int32));
		if (width < 1 || height < 1 || width > 512 || height > 512)
			return NULL;
		if (space != B_RGBA32 && space != B_RGB32)
			return NULL;
		if (bpr < width * 4)
			return NULL;
		ssize_t need = 16 + (ssize_t)bpr * height;
		if (size < need)
			return NULL;
		BBitmap* bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1),
			(color_space)space);
		if (bitmap->InitCheck() != B_OK || bitmap->BytesPerRow() < bpr) {
			delete bitmap;
			return NULL;
		}
		uint8* dest = (uint8*)bitmap->Bits();
		const uint8* src = bytes + 16;
		for (int32 y = 0; y < height; y++) {
			memcpy(dest + y * bitmap->BytesPerRow(), src + y * bpr, bpr);
		}
		return bitmap;
	}

	status_t _BlobFromBitmap(const BBitmap* bitmap, BMessage& message) const
	{
		if (bitmap == NULL)
			return B_BAD_VALUE;
		int32 width = bitmap->Bounds().IntegerWidth() + 1;
		int32 height = bitmap->Bounds().IntegerHeight() + 1;
		int32 bpr = bitmap->BytesPerRow();
		int32 space = bitmap->ColorSpace();
		size_t size = 16 + (size_t)bpr * height;
		char* blob = new char[size];
		memcpy(blob, &width, 4);
		memcpy(blob + 4, &height, 4);
		memcpy(blob + 8, &bpr, 4);
		memcpy(blob + 12, &space, 4);
		memcpy(blob + 16, bitmap->Bits(), (size_t)bpr * height);
		status_t status = message.AddData("picture", B_RAW_TYPE, blob, size);
		delete[] blob;
		return status;
	}

	void _Save()
	{
		if (fAccount.Length() == 0)
			return;
		BMessage request(kMsgAdminSetProfile);
		request.AddString("name", fAccount.String());
		request.AddString("real", fReal->Text());
		request.AddString("email", fEmail->Text());
		if (fClearPhoto)
			request.AddBool("clear_picture", true);
		else if (fPhoto != NULL)
			_BlobFromBitmap(fPhoto, request);
		BString error;
		if (ask_login(request, error) != B_OK) {
			show_error(error.Length() > 0 ? error.String()
				: "Could not save this account.");
			return;
		}
		_Fill();
	}

	void _Pick()
	{
		if (fPanel == NULL) {
			fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
				B_FILE_NODE, false, new BMessage(kMsgPick));
		}
		fPanel->Show();
	}

	void _PictureRef(BMessage* message)
	{
		entry_ref ref;
		if (message->FindRef("refs", &ref) != B_OK)
			return;
		BBitmap* loaded = BTranslationUtils::GetBitmap(&ref);
		if (loaded == NULL) {
			show_error("That file is not a picture this computer can open.");
			return;
		}
		BBitmap* color = new BBitmap(loaded->Bounds(), B_RGBA32, true);
		if (color->InitCheck() != B_OK || color->Lock() == false) {
			delete color;
			delete loaded;
			show_error("That picture could not be used.");
			return;
		}
		BView* view = new BView(loaded->Bounds(), "scale", 0, 0);
		color->AddChild(view);
		view->DrawBitmap(loaded, BPoint(0, 0));
		view->Sync();
		color->Unlock();
		delete loaded;
		delete fPhoto;
		fPhoto = color;
		fClearPhoto = false;
		fAvatar->SetPerson(fReal->Text()[0] != '\0' ? fReal->Text()
			: fAccount.String(), fPhoto);
	}

	void _ClearPicture()
	{
		delete fPhoto;
		fPhoto = NULL;
		fClearPhoto = true;
		fAvatar->SetPerson(fReal->Text()[0] != '\0' ? fReal->Text()
			: fAccount.String(), NULL);
	}

	void _Password()
	{
		if (fAccount.Length() == 0)
			return;
		bool needCurrent = false;
		struct passwd* pwd = getpwnam(fAccount.String());
		if (pwd != NULL && pwd->pw_uid == getuid())
			needCurrent = true;
		PasswordWindow* window = new PasswordWindow(this, fAccount.String(),
			needCurrent);
		window->Show();
	}

	void _UpdateButtons()
	{
		bool admin = fManaging;
		fAdd->SetEnabled(admin);
		UserItem* item = _Selected();
		if (!admin || item == NULL) {
			fDisable->SetEnabled(false);
			fDelete->SetEnabled(false);
			fDisable->SetLabel("Disable");
			return;
		}
		bool firstHome = _SelectedIsFirstHome();
		fDelete->SetEnabled(!firstHome && item->Uid() != getuid());
		fDisable->SetEnabled(!firstHome);
		fDisable->SetLabel(item->IsDisabled() ? "Enable" : "Disable");
	}

	void _Add()
	{
		if (!_Managing()) {
			show_error("Unlock this with an administrator password.");
			return;
		}
		CreateWindow* window = new CreateWindow(this);
		window->CenterOnScreen();
		window->Show();
	}

	void _DisableOrEnable()
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return;
		if (_SelectedIsFirstHome()) {
			show_error("The original administrator account cannot be "
				"disabled.");
			return;
		}
		BMessage request(kMsgAdminSetDisabled);
		request.AddString("name", item->Name());
		request.AddBool("disable", !item->IsDisabled());
		BString error;
		if (ask_login(request, error) != B_OK)
			show_error(error.String());
		_Fill();
	}

	void _Delete()
	{
		UserItem* item = _Selected();
		if (item == NULL)
			return;
		if (_SelectedIsFirstHome()) {
			show_error("The original administrator account cannot be deleted.");
			return;
		}
		if (item->Uid() == getuid()) {
			show_error("You cannot delete the account that is signed in.");
			return;
		}
		BString text;
		text.SetToFormat("Delete %s?", item->Real()[0] != '\0'
			? item->Real() : item->Name());
		BAlert* alert = new BAlert("Delete", text.String(), "Cancel",
			"Keep files", "Delete files", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return;
		BMessage request(kMsgAdminDelete);
		request.AddString("name", item->Name());
		request.AddBool("delete_files", choice == 2);
		BString error;
		if (ask_login(request, error) != B_OK)
			show_error(error.String());
		_Fill();
	}

	BListView*		fList;
	BGroupView*		fPeople;
	AvatarView*		fAvatar;
	BStringView*	fNameView;
	BStringView*	fRole;
	BTextControl*	fReal;
	BTextControl*	fEmail;
	BButton*		fAdd;
	BButton*		fDisable;
	BButton*		fDelete;
	BFilePanel*		fPanel;
	BBitmap*		fPhoto;
	BString			fAccount;
	bool			fClearPhoto;
	bool			fManaging;
};


class UsersApp : public BApplication {
public:
	UsersApp()
		:
		BApplication(kSignature)
	{
	}

	virtual void ReadyToRun()
	{
		lock_private_homes();
		(new UsersWindow())->Show();
	}
};


int
main()
{
	UsersApp app;
	app.Run();
	return 0;
}
