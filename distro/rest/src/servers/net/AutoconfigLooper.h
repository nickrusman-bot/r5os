/*
 * Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/*
 * R5OS modifications in this file are GPL-3.0-or-later. They are wrapped in
 * SPDX-Snippet markers. The copyright and license notice above still cover
 * the original work. A build that ships both follows GPL-3.0-or-later, and
 * that notice stays on the original portions.
 */

#ifndef AUTOCONFIG_LOOPER_H
#define AUTOCONFIG_LOOPER_H


#include <Looper.h>
#include <Messenger.h>
#include <String.h>
#include <netinet6/in6.h>

class AutoconfigClient;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
class SLAACClient;
class DHCPv6Client;
// SPDX-SnippetEnd

class AutoconfigLooper : public BLooper {
public:
								AutoconfigLooper(BMessenger target,
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
									const char* device,
									bool autoIPv4 = true,
									bool autoIPv6 = true);
// SPDX-SnippetEnd
	virtual						~AutoconfigLooper();

	virtual	void				MessageReceived(BMessage* message);

			BMessenger			Target() const { return fTarget; }

private:
			void				_RemoveClient();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			void				_RemoveSLAAC();
			void				_RemoveDHCPv6();
// SPDX-SnippetEnd
			void				_ConfigureIPv4();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			void				_ConfigureIPv6();
// SPDX-SnippetEnd
			void				_ConfigureIPv4Failed();
			void				_ReadyToRun();
			void				_NetworkMonitorNotification(BMessage* message);

			BMessenger			fTarget;
			BString				fDevice;
			AutoconfigClient*	fCurrentClient;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			SLAACClient*		fSLAACClient;
			DHCPv6Client*		fDHCPv6Client;
// SPDX-SnippetEnd
			int32				fLastMediaStatus;
			bool				fJoiningNetwork;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
			bool				fAutoIPv4;
			bool				fAutoIPv6;
// SPDX-SnippetEnd
};

#endif	// AUTOCONFIG_LOOPER_H
