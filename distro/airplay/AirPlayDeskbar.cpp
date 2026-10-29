#include "AirPlayLog.h"
#include "AirPlayMessages.h"

#include <Application.h>
#include <Archivable.h>
#include <Deskbar.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <View.h>

#include <image.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <r5_mdns.h>


static status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char*)our_image >= (char*)image.text
			&& (char*)our_image <= (char*)image.text + image.text_size)
			return B_OK;
	}
	return B_ERROR;
}


class AirPlayReplicant : public BView {
public:
	AirPlayReplicant(BRect frame)
		:
		BView(frame, kAirPlayDeskbarItem, B_FOLLOW_NONE, B_WILL_DRAW)
	{
	}

	AirPlayReplicant(BMessage* archive)
		:
		BView(archive)
	{
	}

	static BArchivable* Instantiate(BMessage* archive);

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t err = BView::Archive(archive, deep);
		if (err != B_OK)
			return err;
		archive->RemoveName("class");
		err = archive->AddString("class", "AirPlayReplicant");
		if (err == B_OK) {
			archive->RemoveName("add_on");
			err = archive->AddString("add_on", kAirPlayTraySignature);
		}
		return err;
	}

	virtual void AttachedToWindow()
	{
		r5_airplay_log("replicant", "AttachedToWindow frame %.0fx%.0f",
			Bounds().Width() + 1, Bounds().Height() + 1);
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		BView::AttachedToWindow();
	}

	virtual void Draw(BRect)
	{
		BRect r = Bounds();
		SetHighColor(0, 140, 210);
		FillEllipse(r.InsetByCopy(1, 1));
		SetHighColor(255, 255, 255);
		BFont font(be_bold_font);
		float size = r.Height() - 6;
		if (size < 9)
			size = 9;
		font.SetSize(size);
		SetFont(&font);
		const char* label = "A";
		float w = StringWidth(label);
		font_height fh;
		GetFontHeight(&fh);
		DrawString(label, BPoint((r.Width() - w) / 2,
			(r.Height() + fh.ascent - fh.descent) / 2));
	}

	virtual void MouseDown(BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
		menu->AddItem(new BMenuItem("This computer (receive)", NULL));
		menu->ItemAt(0)->SetMarked(true);
		menu->ItemAt(0)->SetEnabled(false);
		menu->AddSeparatorItem();

		r5_mdns_service svcs[R5_MDNS_MAX_SERVICES];
		int n = r5_mdns_browse("_raop._tcp.local", svcs, R5_MDNS_MAX_SERVICES,
			900);
		char host[128];
		if (gethostname(host, sizeof(host)) != 0)
			strlcpy(host, "r5os", sizeof(host));

		int found = 0;
		for (int i = 0; i < n; i++) {
			if (svcs[i].port == 0 || svcs[i].addr.s_addr == 0)
				continue;
			if (strcasestr(svcs[i].name, host) != NULL)
				continue;
			char label[96];
			const char* at = strchr(svcs[i].name, '@');
			const char* name = at != NULL ? at + 1 : svcs[i].name;
			snprintf(label, sizeof(label), "Play to %.80s", name);
			BMessage* msg = new BMessage(kMsgAirPlayPlayTo);
			msg->AddString("host", inet_ntoa(svcs[i].addr));
			msg->AddInt32("port", svcs[i].port);
			msg->AddInt32("ipv4", (int32)ntohl(svcs[i].addr.s_addr));
			menu->AddItem(new BMenuItem(label, msg));
			found++;
		}
		if (found == 0)
			menu->AddItem(new BMenuItem("No AirPlay speakers found", NULL));

		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Stop sending",
			new BMessage(kMsgAirPlayStopSend)));

		menu->SetTargetForItems(BMessenger(kAirPlaySignature));
		ConvertToScreen(&where);
		menu->Go(where, true, true, true);
	}
};


_EXPORT BArchivable*
AirPlayReplicant::Instantiate(BMessage* archive)
{
	r5_airplay_log("replicant", "Instantiate");
	if (!validate_instantiation(archive, "AirPlayReplicant"))
		return NULL;
	return new AirPlayReplicant(archive);
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float /*maxWidth*/, float maxHeight)
{
	r5_airplay_log("replicant", "instantiate_deskbar_item height %.0f",
		maxHeight);
	(void)&AirPlayReplicant::Instantiate;
	return new AirPlayReplicant(BRect(0, 0, maxHeight - 1, maxHeight - 1));
}


status_t
r5_install_airplay_deskbar()
{
	BDeskbar deskbar;
	bool running = deskbar.IsRunning();
	bool has = running && deskbar.HasItem(kAirPlayDeskbarItem);
	r5_airplay_log("install", "deskbar running=%d has=%d count=%ld",
		running, has, running ? (long)deskbar.CountItems() : -1L);
	if (!running)
		return B_ERROR;
	if (has)
		return B_OK;

	BPath path;
	status_t err = find_directory(B_SYSTEM_SERVERS_DIRECTORY, &path);
	if (err == B_OK)
		err = path.Append("airplay_deskbar");

	BEntry entry;
	if (err == B_OK)
		err = entry.SetTo(path.Path());
	if (err != B_OK || !entry.Exists()) {
		image_info info;
		err = our_image(info);
		if (err == B_OK) {
			path.SetTo(info.name);
			err = entry.SetTo(path.Path());
		}
	}

	r5_airplay_log("install", "addon path %s exists=%d err=%s",
		path.Path() != NULL ? path.Path() : "(null)",
		entry.Exists(), strerror(err));
	if (err != B_OK)
		return err;

	entry_ref ref;
	err = entry.GetRef(&ref);
	if (err != B_OK) {
		r5_airplay_log("install", "GetRef %s", strerror(err));
		return err;
	}

	int32 id = -1;
	err = deskbar.AddItem(&ref, &id);
	r5_airplay_log("install", "AddItem -> %s (id %ld) has=%d",
		strerror(err), (long)id, deskbar.HasItem(kAirPlayDeskbarItem));
	return err;
}


class AirPlayTrayApp : public BApplication {
public:
	AirPlayTrayApp()
		:
		BApplication(kAirPlayTraySignature),
		fTries(0)
	{
	}

	virtual void ReadyToRun()
	{
		r5_airplay_log("tray", "ReadyToRun");
		SetPulseRate(1000000);
		_TryInstall();
	}

	virtual void Pulse()
	{
		_TryInstall();
	}

private:
			void				_TryInstall()
			{
				if (r5_install_airplay_deskbar() == B_OK || ++fTries >= 30)
					Quit();
			}

			int32				fTries;
};


int
main()
{
	r5_airplay_log("tray", "main");
	AirPlayTrayApp app;
	app.Run();
	r5_airplay_log("tray", "main exit");
	return 0;
}
