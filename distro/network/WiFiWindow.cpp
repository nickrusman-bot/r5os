/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * R5OS: one-screen Wi-Fi join, remember, Deskbar status helpers.
 */


#include "WiFiWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Messenger.h>
#include <NetServer.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <OS.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiWindow"


static const uint32 kMsgRefresh		= 'wref';
static const uint32 kMsgConnect		= 'wcon';
static const uint32 kMsgPasswordOk	= 'pwok';
static const uint32 kMsgPasswordCancel	= 'pwcl';

static WiFiWindow* sWindow = NULL;


static const char*
auth_label(uint32 mode)
{
	switch (mode) {
		case B_NETWORK_AUTHENTICATION_WEP:
			return "WEP";
		case B_NETWORK_AUTHENTICATION_WPA:
			return "WPA";
		case B_NETWORK_AUTHENTICATION_WPA2:
			return "WPA2";
		case B_NETWORK_AUTHENTICATION_EAP:
			return "EAP";
		default:
			return "open";
	}
}


static const char*
auth_setting(uint32 mode)
{
	switch (mode) {
		case B_NETWORK_AUTHENTICATION_WEP:
			return "wep";
		case B_NETWORK_AUTHENTICATION_WPA:
			return "wpa";
		case B_NETWORK_AUTHENTICATION_WPA2:
			return "wpa2";
		default:
			return "none";
	}
}


status_t
r5_wifi_current_ssid(BString& ssid, BString* deviceName)
{
	ssid.Truncate(0);
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		BNetworkDevice device(interface.Name());
		if (!device.IsWireless())
			continue;
		if (deviceName != NULL)
			*deviceName = interface.Name();
		uint32 associated = 0;
		wireless_network network;
		if (device.GetNextAssociatedNetwork(associated, network) == B_OK
			&& network.name[0] != '\0') {
			ssid = network.name;
			return B_OK;
		}
	}
	return B_ERROR;
}


status_t
r5_wifi_join_and_remember(const char* deviceName, const wireless_network& network,
	const char* password)
{
	if (deviceName == NULL || deviceName[0] == '\0' || network.name[0] == '\0')
		return B_BAD_VALUE;

	BNetworkDevice device(deviceName);
	status_t status = device.JoinNetwork(network.name,
		(password != NULL && password[0] != '\0') ? password : NULL);
	if (status != B_OK)
		return status;

	BMessage persist(kMsgAddPersistentNetwork);
	persist.AddString("name", network.name);
	if (password != NULL && password[0] != '\0')
		persist.AddString("password", password);
	persist.AddString("authentication", auth_setting(network.authentication_mode));
	BMessenger server(kNetServerSignature);
	if (server.IsValid())
		server.SendMessage(&persist);
	return B_OK;
}


class PasswordWindow : public BWindow {
public:
	PasswordWindow(const char* ssid, BString* out, sem_id done)
		:
		BWindow(BRect(0, 0, 320, 120), B_TRANSLATE("Wi-Fi password"),
			B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
		fOut(out),
		fDone(done),
		fResult(B_ERROR)
	{
		BString label(B_TRANSLATE("Password for %ssid%"));
		label.ReplaceFirst("%ssid%", ssid);
		fPassword = new BTextControl("password", label, "", NULL);
		if (fPassword->TextView() != NULL)
			fPassword->TextView()->HideTyping(true);

		SetFeel(B_MODAL_APP_WINDOW_FEEL);
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fPassword)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", B_TRANSLATE("Cancel"),
					new BMessage(kMsgPasswordCancel)))
				.Add(new BButton("ok", B_TRANSLATE("Connect"),
					new BMessage(kMsgPasswordOk)))
			.End();
		SetDefaultButton((BButton*)FindView("ok"));
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgPasswordOk:
				if (fOut != NULL)
					*fOut = fPassword->Text();
				fResult = B_OK;
				release_sem(fDone);
				Hide();
				break;
			case kMsgPasswordCancel:
			case B_QUIT_REQUESTED:
				fResult = B_CANCELED;
				release_sem(fDone);
				Hide();
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

	virtual bool QuitRequested()
	{
		fResult = B_CANCELED;
		release_sem(fDone);
		Hide();
		return false;
	}

			status_t			Result() const { return fResult; }

private:
			BTextControl*		fPassword;
			BString*			fOut;
			sem_id				fDone;
			status_t			fResult;
};


status_t
r5_wifi_ask_password(const char* ssid, BString& password)
{
	sem_id done = create_sem(0, "wifi-password");
	if (done < 0)
		return done;

	PasswordWindow* window = new PasswordWindow(ssid, &password, done);
	window->Show();
	while (acquire_sem(done) == B_INTERRUPTED)
		;
	status_t result = window->Result();
	window->Lock();
	window->Quit();
	delete_sem(done);
	return result;
}


class NetworkListItem : public BStringItem {
public:
	NetworkListItem(const wireless_network& network)
		:
		BStringItem(""),
		fNetwork(network)
	{
		BString label;
		label.SetToFormat("%s  (%s, signal %u)", network.name,
			auth_label(network.authentication_mode),
			(unsigned)network.signal_strength);
		SetText(label.String());
	}

	const wireless_network& Network() const { return fNetwork; }

private:
			wireless_network	fNetwork;
};


WiFiWindow::WiFiWindow()
	:
	BWindow(BRect(0, 0, 420, 360), B_TRANSLATE("Wi-Fi"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fList(new BListView("networks")),
	fPassword(new BTextControl("password", B_TRANSLATE("Password"), "",
		NULL)),
	fStatus(new BStringView("status", "")),
	fConnect(new BButton("connect", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnect)))
{
	if (fPassword->TextView() != NULL)
		fPassword->TextView()->HideTyping(true);
	fList->SetInvocationMessage(new BMessage(kMsgConnect));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(new BStringView("hint",
			B_TRANSLATE("Choose a network, enter a password if needed, "
				"and connect. R5OS remembers it for next boot.")))
		.Add(new BScrollView("scroll", fList, 0, false, true))
		.Add(fPassword)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(new BButton("refresh", B_TRANSLATE("Scan"),
				new BMessage(kMsgRefresh)))
			.Add(fConnect)
		.End();

	CenterOnScreen();
	_Refresh();
}


WiFiWindow::~WiFiWindow()
{
	if (sWindow == this)
		sWindow = NULL;
}


void
WiFiWindow::ShowWindow()
{
	if (sWindow == NULL) {
		sWindow = new WiFiWindow();
		sWindow->Show();
		return;
	}
	if (sWindow->Lock()) {
		sWindow->Activate();
		sWindow->_Refresh();
		sWindow->Unlock();
	}
}


bool
WiFiWindow::QuitRequested()
{
	Hide();
	return false;
}


void
WiFiWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefresh:
			_Refresh();
			break;
		case kMsgConnect:
			_ConnectSelected();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
WiFiWindow::_Refresh()
{
	BString ssid;
	r5_wifi_current_ssid(ssid, &fDevice);
	if (ssid.Length() > 0) {
		BString text(B_TRANSLATE("Connected to %ssid%"));
		text.ReplaceFirst("%ssid%", ssid);
		fStatus->SetText(text);
	} else if (fDevice.Length() == 0)
		fStatus->SetText(B_TRANSLATE("No wireless device"));
	else
		fStatus->SetText(B_TRANSLATE("No network"));

	while (fList->CountItems() > 0)
		delete fList->RemoveItem((int32)0);

	if (fDevice.Length() == 0)
		return;

	BNetworkDevice device(fDevice.String());
	device.Scan(false, false);
	uint32 count = 0;
	wireless_network* networks = NULL;
	if (device.GetNetworks(networks, count) != B_OK || networks == NULL)
		return;
	for (uint32 i = 0; i < count; i++) {
		if (networks[i].name[0] == '\0')
			continue;
		NetworkListItem* item = new NetworkListItem(networks[i]);
		fList->AddItem(item);
		if (ssid == networks[i].name)
			fList->Select(fList->CountItems() - 1);
	}
	delete[] networks;
}


void
WiFiWindow::_ConnectSelected()
{
	int32 index = fList->CurrentSelection();
	NetworkListItem* item = dynamic_cast<NetworkListItem*>(
		fList->ItemAt(index));
	if (item == NULL || fDevice.Length() == 0)
		return;

	const wireless_network& network = item->Network();
	BString text(B_TRANSLATE("Connecting to %ssid%…"));
	text.ReplaceFirst("%ssid%", network.name);
	fStatus->SetText(text);

	status_t status = r5_wifi_join_and_remember(fDevice.String(), network,
		fPassword->Text());
	if (status != B_OK) {
		BString error(B_TRANSLATE("Could not join: %error%"));
		error.ReplaceFirst("%error%", strerror(status));
		fStatus->SetText(error);
		return;
	}
	_Refresh();
}
