/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef R5_WIFI_WINDOW_H
#define R5_WIFI_WINDOW_H


#include <String.h>
#include <Window.h>

#include <NetworkDevice.h>


class BButton;
class BListView;
class BStringView;
class BTextControl;


class WiFiWindow : public BWindow {
public:
								WiFiWindow();
	virtual						~WiFiWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	static	void				ShowWindow();

private:
			void				_Refresh();
			void				_ConnectSelected();

			BListView*			fList;
			BTextControl*		fPassword;
			BStringView*		fStatus;
			BButton*			fConnect;
			BString				fDevice;
};


status_t	r5_wifi_current_ssid(BString& ssid, BString* deviceName = NULL);
status_t	r5_wifi_join_and_remember(const char* device,
					const wireless_network& network, const char* password);
status_t	r5_wifi_ask_password(const char* ssid, BString& password);


#endif
