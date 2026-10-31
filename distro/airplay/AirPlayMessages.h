/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_AIRPLAY_MESSAGES_H
#define R5_AIRPLAY_MESSAGES_H

#include <SupportDefs.h>

enum {
	kMsgAirPlayPlayTo		= 'apto',
	kMsgAirPlayStopSend		= 'apst',
	kMsgAirPlayInstallTray	= 'apdb'
};

#define kAirPlaySignature		"application/x-vnd.R5OS-airplay"
#define kAirPlayTraySignature	"application/x-vnd.R5OS-airplay-tray"
#define kAirPlayDeskbarItem		"AirPlay"

status_t r5_install_airplay_deskbar();

#endif
