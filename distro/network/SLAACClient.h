/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SLAAC_CLIENT_H
#define SLAAC_CLIENT_H


#include "AutoconfigClient.h"

#include <NetworkInterface.h>
#include <OS.h>
#include <netinet/in.h>


class SLAACClient : public AutoconfigClient {
public:
								SLAACClient(BMessenger target,
									const char* device);
	virtual						~SLAACClient();

	virtual	status_t			Start();

private:
	static	status_t			_Thread(void* data);
			status_t			_Run();
			status_t			_EnsureLinkLocal(BNetworkInterface& interface);
			status_t			_BindAndJoin(int socket, uint32 ifIndex);
			status_t			_SendSolicit(int socket, uint32 ifIndex);
			status_t			_ApplyAdvert(const uint8* buffer, ssize_t bytes,
									const sockaddr_in6& from, uint32 ifIndex);
			status_t			_ApplyDefaultRoute(const sockaddr_in6& from,
									uint32 ifIndex);
			void				_ApplyResolver(BMessage& resolver,
									int32 nameservers);
			void				_BuildSlaacAddress(const in6_addr& prefix,
									in6_addr& out) const;
			BString				_ToString(const in6_addr& address) const;

			thread_id			fThread;
			int					fSocket;
			uint8				fMAC[6];
			bool				fHaveMAC;
			bool				fHaveDefaultRoute;
			sockaddr_in6		fDefaultGateway;
};


#endif	// SLAAC_CLIENT_H
