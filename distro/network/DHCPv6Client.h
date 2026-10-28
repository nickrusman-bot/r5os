/*
 * Copyright 2026, R5OS.
 * Distributed under the terms of the MIT License.
 */
#ifndef DHCPV6_CLIENT_H
#define DHCPV6_CLIENT_H


#include "AutoconfigClient.h"

#include <netinet/in.h>


class DHCPv6Client : public AutoconfigClient {
public:
								DHCPv6Client(BMessenger target,
									const char* device);
	virtual						~DHCPv6Client();

	virtual	status_t			Start();

private:
	static	status_t			_Thread(void* data);
			status_t			_Run();
			status_t			_EnsureLinkLocal();
			int					_OpenSocket(uint32& ifIndex);
			ssize_t				_BuildSolicit(uint8* out, size_t outSize,
									bool informationOnly);
			status_t			_Send(int socket, uint32 ifIndex,
									const uint8* packet, size_t length);
			status_t			_HandleReply(const uint8* packet, ssize_t bytes,
									uint32 ifIndex);

			thread_id			fThread;
			int					fSocket;
			uint8				fMAC[6];
			uint32				fTransaction;
			uint8				fServerId[128];
			uint16				fServerIdLength;
			bool				fHaveMAC;
};


#endif	// DHCPV6_CLIENT_H
