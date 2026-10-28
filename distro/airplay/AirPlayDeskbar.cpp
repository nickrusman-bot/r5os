#include "AirPlayMessages.h"

#include <Archivable.h>
#include <Deskbar.h>
#include <Entry.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <View.h>
#include <stdio.h>

#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <r5_mdns.h>


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

	static AirPlayReplicant* Instantiate(BMessage* archive)
	{
		if (!validate_instantiation(archive, "AirPlayReplicant"))
			return NULL;
		return new AirPlayReplicant(archive);
	}

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t err = BView::Archive(archive, deep);
		if (err == B_OK)
			err = archive->AddString("add_on", kAirPlaySignature);
		if (err == B_OK)
			err = archive->AddString("class", "AirPlayReplicant");
		return err;
	}

	virtual void AttachedToWindow()
	{
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
			snprintf(label, sizeof(label), "Play to %s",
				at != NULL ? at + 1 : svcs[i].name);
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


extern "C" _EXPORT BView*
instantiate_deskbar_item(float /*maxWidth*/, float maxHeight)
{
	return new AirPlayReplicant(BRect(0, 0, maxHeight - 1, maxHeight - 1));
}


status_t
r5_install_airplay_deskbar()
{
	BDeskbar deskbar;
	if (!deskbar.IsRunning())
		return B_ERROR;
	if (deskbar.HasItem(kAirPlayDeskbarItem))
		return B_OK;

	app_info info;
	status_t err = be_roster->GetAppInfo(kAirPlaySignature, &info);
	if (err != B_OK) {
		fprintf(stderr, "airplay: GetAppInfo: %s\n", strerror(err));
		return err;
	}

	int32 id = -1;
	err = deskbar.AddItem(&info.ref, &id);
	if (err != B_OK) {
		AirPlayReplicant view(BRect(0, 0, 15, 15));
		err = deskbar.AddItem(&view, &id);
	}
	fprintf(stderr, "airplay: Deskbar AddItem -> %s (id %ld)\n",
		strerror(err), (long)id);
	return err;
}
