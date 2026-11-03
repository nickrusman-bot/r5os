/*
 * Copyright 2006-2012, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Alexander von Gluck, kallisti5@unixzen.com
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The copyright and license notice above still cover
 * the original work. A build that ships both follows GPL-3.0-or-later, and
 * that notice stays on the original portions.
 */



#include "AutoconfigLooper.h"

#include <errno.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <NetworkInterface.h>
#include <NetworkNotifications.h>

#include "DHCPClient.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SLAACClient.h"
#include "DHCPv6Client.h"
// SPDX-SnippetEnd
#include "NetServer.h"


static const uint32 kMsgReadyToRun = 'rdyr';


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
AutoconfigLooper::AutoconfigLooper(BMessenger target, const char* device,
	bool autoIPv4, bool autoIPv6)
// SPDX-SnippetEnd
	: BLooper(device),
	fTarget(target),
	fDevice(device),
	fCurrentClient(NULL),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	fSLAACClient(NULL),
	fDHCPv6Client(NULL),
// SPDX-SnippetEnd
	fLastMediaStatus(0),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	fJoiningNetwork(false),
	fAutoIPv4(autoIPv4),
	fAutoIPv6(autoIPv6)
// SPDX-SnippetEnd
{
	BMessage ready(kMsgReadyToRun);
	PostMessage(&ready);
}


AutoconfigLooper::~AutoconfigLooper()
{
	_RemoveClient();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
	_RemoveSLAAC();
	_RemoveDHCPv6();
// SPDX-SnippetEnd
}


void
AutoconfigLooper::_RemoveClient()
{
	if (fCurrentClient == NULL)
		return;

	delete fCurrentClient;
	fCurrentClient = NULL;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
}


void
AutoconfigLooper::_RemoveSLAAC()
{
	if (fSLAACClient == NULL)
		return;

	delete fSLAACClient;
	fSLAACClient = NULL;
}


void
AutoconfigLooper::_RemoveDHCPv6()
{
	if (fDHCPv6Client == NULL)
		return;

	delete fDHCPv6Client;
	fDHCPv6Client = NULL;
}


void
AutoconfigLooper::_ConfigureIPv6()
{
	if (fSLAACClient == NULL) {
		fSLAACClient = new SLAACClient(fTarget, fDevice.String());
		AddHandler(fSLAACClient);
	}

	fSLAACClient->Start();

	if (fDHCPv6Client == NULL) {
		fDHCPv6Client = new DHCPv6Client(fTarget, fDevice.String());
		AddHandler(fDHCPv6Client);
	}
	fDHCPv6Client->Start();
// SPDX-SnippetEnd
}


void
AutoconfigLooper::_ConfigureIPv4()
{
	// start with DHCP

	if (fCurrentClient == NULL) {
		fCurrentClient = new DHCPClient(fTarget, fDevice.String());
		AddHandler(fCurrentClient);
	}

	// set IFF_CONFIGURING flag on interface

	BNetworkInterface interface(fDevice.String());
	int32 flags = interface.Flags() & ~IFF_AUTO_CONFIGURED;
	interface.SetFlags(flags | IFF_CONFIGURING);

	if (fCurrentClient->Start() == B_OK)
		return;

	_ConfigureIPv4Failed();
}


void
AutoconfigLooper::_ConfigureIPv4Failed()
{
	_RemoveClient();

	puts("DHCP failed miserably!");

	// DHCP obviously didn't work out, take some default values for now
	// TODO: have a look at zeroconf
	// TODO: this could also be done add-on based

	BNetworkInterface interface(fDevice.String());
	if ((interface.Flags() & IFF_CONFIGURING) == 0) {
		// Someone else configured the interface in the mean time
		return;
	}

	BMessage message(kMsgConfigureInterface);
	message.AddString("device", fDevice.String());
	message.AddBool("auto_configured", true);

	BNetworkAddress link;
	uint8 last = 56;
	if (interface.GetHardwareAddress(link) == B_OK) {
		// choose IP address depending on the MAC address, if available
		uint8* mac = link.LinkLevelAddress();
		last = mac[0] ^ mac[1] ^ mac[2] ^ mac[3] ^ mac[4] ^ mac[5];
		if (last > 253)
			last = 253;
		else if (last == 0)
			last = 1;
	}

	// IANA defined the default autoconfig network (for when a DHCP request
	// fails for some reason) as being 169.254.0.0/255.255.0.0. We are only
	// generating the last octet but we could also use the 2 last octets if
	// wanted.
	char string[64];
	snprintf(string, sizeof(string), "169.254.0.%u", last);

	BMessage address;
	address.AddInt32("family", AF_INET);
	address.AddString("address", string);
	message.AddMessage("address", &address);

	fTarget.SendMessage(&message);
}


void
AutoconfigLooper::_ReadyToRun()
{
	start_watching_network(
		B_WATCH_NETWORK_LINK_CHANGES | B_WATCH_NETWORK_WLAN_CHANGES, this);

	BNetworkInterface interface(fDevice.String());
	if (interface.HasLink()) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
		if (fAutoIPv4)
			_ConfigureIPv4();
		if (fAutoIPv6)
			_ConfigureIPv6();
// SPDX-SnippetEnd

		// Also make sure we don't spuriously try to configure again from
		// a link changed notification that might race us.
		fLastMediaStatus |= IFM_ACTIVE;
	}
}


void
AutoconfigLooper::_NetworkMonitorNotification(BMessage* message)
{
	int32 opcode;
	BString device;
	if (message->FindString("device", &device) != B_OK) {
		if (message->FindString("interface", &device) != B_OK)
			return;

		// TODO: Clean this mess up. Wireless devices currently use their
		// "device_name" in the interface field. First of all the
		// joins/leaves/scans should be device, not interface specific, so
		// the field should be changed. Then the device_name as seen by the
		// driver is missing the "/dev" part, as it is a relative path within
		// "/dev". On the other hand the net stack uses names that include
		// "/dev" as it uses them to open the fds, hence a full absolute path.
		// Note that the wpa_supplicant does the same workaround as we do here
		// to build an interface name, so that has to be changed as well when
		// this is fixed.
		device.Prepend("/dev/");
	}

	if (device != fDevice || message->FindInt32("opcode", &opcode) != B_OK)
		return;

	switch (opcode) {
		case B_NETWORK_DEVICE_LINK_CHANGED:
		{
			int32 media;
			if (message->FindInt32("media", &media) != B_OK)
				break;

			if ((fLastMediaStatus & IFM_ACTIVE) == 0 && (media & IFM_ACTIVE) != 0) {
				// Reconfigure the interface when we have a link again
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
				if (fAutoIPv4)
					_ConfigureIPv4();
				if (fAutoIPv6)
					_ConfigureIPv6();
// SPDX-SnippetEnd
			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
				_RemoveSLAAC();
				_RemoveDHCPv6();
				fJoiningNetwork = false;
// SPDX-SnippetEnd
			}

			fLastMediaStatus = media;
			break;
		}

		case B_NETWORK_WLAN_SCANNED:
		{
			if (fJoiningNetwork || (fLastMediaStatus & IFM_ACTIVE) != 0) {
				// We already have a link or are already joining.
				break;
			}

			fJoiningNetwork = true;
				// TODO: For now we never reset this flag. We can only do that
				// after infrastructure has been added to discern a scan reason.
				// If we would always auto join we would possibly interfere
				// with active scans in the process of connecting to an AP
				// either for the initial connection, or after connection loss
				// to re-establish the link.

			BMessage message(kMsgAutoJoinNetwork);
			message.AddString("device", fDevice);
			fTarget.SendMessage(&message);
			break;
		}
	}
}


void
AutoconfigLooper::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgReadyToRun:
			_ReadyToRun();
			break;

		case kMsgAutoConfigureFailed:
			_ConfigureIPv4Failed();
			break;

		case B_NETWORK_MONITOR:
			_NetworkMonitorNotification(message);
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}

