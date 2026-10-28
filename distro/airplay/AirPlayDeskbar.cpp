#include "AirPlayMessages.h"

#include <Application.h>
#include <Deskbar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <View.h>
#include <Window.h>

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
		BView(frame, kAirPlayDeskbarItem, B_FOLLOW_ALL, B_WILL_DRAW)
	{
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


class AirPlayTrayWindow : public BWindow {
public:
	AirPlayTrayWindow()
		:
		BWindow(BRect(0, 0, 15, 15), "AirPlay", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL,
			B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
				| B_NOT_MINIMIZABLE | B_AVOID_FOCUS
				| B_WILL_ACCEPT_FIRST_CLICK | B_ASYNCHRONOUS_CONTROLS)
	{
		AddChild(new AirPlayReplicant(Bounds()));
	}

	void DockToDeskbar()
	{
		BRect db;
		if (get_deskbar_frame(&db) != B_OK || !db.IsValid()
			|| db.Width() < 8 || db.Height() < 8) {
			if (!IsHidden())
				Hide();
			return;
		}

		float size = db.Height();
		if (size < 16)
			size = 16;
		float x;
		if (db.left > 64)
			x = db.left - size;
		else
			x = db.right + 1;
		float y = db.top;
		MoveTo(x, y);
		ResizeTo(size - 1, size - 1);
		if (IsHidden())
			Show();
	}

	virtual bool QuitRequested()
	{
		return false;
	}
};


class AirPlayTrayApp : public BApplication {
public:
	AirPlayTrayApp()
		:
		BApplication(kAirPlayTraySignature),
		fWindow(NULL)
	{
	}

	virtual void ReadyToRun()
	{
		fWindow = new AirPlayTrayWindow();
		SetPulseRate(500000);
		_Dock();
	}

	virtual void Pulse()
	{
		_Dock();
	}

private:
			void				_Dock()
			{
				if (fWindow == NULL || !fWindow->Lock())
					return;
				fWindow->DockToDeskbar();
				fWindow->Unlock();
			}

			AirPlayTrayWindow*	fWindow;
};


int
main()
{
	AirPlayTrayApp app;
	app.Run();
	return 0;
}
