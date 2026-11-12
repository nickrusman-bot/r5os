/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Device Manager. Distributed under the terms of the MIT License.
 */
#ifndef DM_MESSAGES_H
#define DM_MESSAGES_H


#include <SupportDefs.h>


static const char* const kAppSignature = "application/x-vnd.R5-DeviceManager";

enum {
	kMsgProperties			= 'dmpr',	// "cookie" (uint32), optional
	kMsgToggleDriver		= 'dmtd',	// "cookie" (uint32), optional
	kMsgTurnOnPath			= 'dmtp',	// "path" (string)
	kMsgScan				= 'dmsc',
	kMsgViewByType			= 'dmvt',
	kMsgViewByConnection	= 'dmvc',
	kMsgShowHidden			= 'dmsh',
	kMsgExport				= 'dmex',
	kMsgTurnedOffWindow		= 'dmto',
	kMsgSelectionChanged	= 'dmsl',
	kMsgDevChanged			= 'dmdc',
	kMsgReloadNow			= 'dmrl',
	kMsgDeviceUpdate		= 'dmdu',	// to a Properties window
	kMsgDeviceGone			= 'dmdg',	// to a Properties window
	kMsgPropertyChosen		= 'dmpc',
	kMsgDriverDetails		= 'dmdd',
	kMsgPropertiesClosed	= 'dmpx',	// "cookie" (uint32)
	kMsgTurnedOffRefresh	= 'dmtr',
	kMsgTurnOnSelected		= 'dmts'
};


#endif
