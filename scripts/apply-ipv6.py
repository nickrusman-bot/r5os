#!/usr/bin/env python3
"""IPv6 SLAAC in net_server (Haiku #8316/#8317). Idempotent."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "network"


def replace_once(
    path: pathlib.Path, old: str, new: str, tag: str, optional: bool = False
) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        if optional:
            print(f"skip {path.name} ({tag})")
            return
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_sources() -> None:
    dest = HAIKU / "src" / "servers" / "net"
    for name in (
        "SLAACClient.cpp",
        "SLAACClient.h",
        "IPv6Dad.cpp",
        "IPv6Dad.h",
        "IPv6DadPacket.h",
        "DHCPv6Client.cpp",
        "DHCPv6Client.h",
    ):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")


def patch_jam() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "net" / "Jamfile",
        "	DHCPClient.cpp\n"
        "	Services.cpp\n",
        "	DHCPClient.cpp\n"
        "	SLAACClient.cpp\n"
        "	IPv6Dad.cpp\n"
        "	DHCPv6Client.cpp\n"
        "	Services.cpp\n",
        "SLAACClient jam",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "Jamfile",
        "	SLAACClient.cpp\n"
        "	Services.cpp\n",
        "	SLAACClient.cpp\n"
        "	IPv6Dad.cpp\n"
        "	DHCPv6Client.cpp\n"
        "	Services.cpp\n",
        "DAD DHCPv6 jam",
        optional=True,
    )


def patch_looper() -> None:
    header = HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.h"
    replace_once(
        header,
        "class AutoconfigClient;\n",
        "class AutoconfigClient;\n"
        "class SLAACClient;\n"
        "class DHCPv6Client;\n",
        "SLAACClient forward",
        optional=True,
    )
    replace_once(
        header,
        "class SLAACClient;\n"
        "class DHCPv6Client;\n"
        "class SLAACClient;\n",
        "class SLAACClient;\n"
        "class DHCPv6Client;\n",
        "dedupe forward",
        optional=True,
    )
    replace_once(
        header,
        "class SLAACClient;\n",
        "class SLAACClient;\n"
        "class DHCPv6Client;\n",
        "DHCPv6Client forward",
        optional=True,
    )
    replace_once(
        header,
        "			void				_RemoveClient();\n"
        "			void				_ConfigureIPv4();\n",
        "			void				_RemoveClient();\n"
        "			void				_RemoveSLAAC();\n"
        "			void				_RemoveDHCPv6();\n"
        "			void				_ConfigureIPv4();\n"
        "			void				_ConfigureIPv6();\n",
        "looper ipv6 methods",
        optional=True,
    )
    replace_once(
        header,
        "			void				_RemoveSLAAC();\n",
        "			void				_RemoveSLAAC();\n"
        "			void				_RemoveDHCPv6();\n",
        "looper DHCPv6 method",
        optional=True,
    )
    replace_once(
        header,
        "			AutoconfigClient*	fCurrentClient;\n",
        "			AutoconfigClient*	fCurrentClient;\n"
        "			SLAACClient*		fSLAACClient;\n"
        "			DHCPv6Client*		fDHCPv6Client;\n",
        "fSLAACClient member",
        optional=True,
    )
    replace_once(
        header,
        "			SLAACClient*		fSLAACClient;\n",
        "			SLAACClient*		fSLAACClient;\n"
        "			DHCPv6Client*		fDHCPv6Client;\n",
        "fDHCPv6Client member",
        optional=True,
    )

    looper = HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.cpp"
    replace_once(
        looper,
        '#include "DHCPClient.h"\n'
        '#include "NetServer.h"\n',
        '#include "DHCPClient.h"\n'
        '#include "SLAACClient.h"\n'
        '#include "DHCPv6Client.h"\n'
        '#include "NetServer.h"\n',
        "include SLAACClient",
        optional=True,
    )
    replace_once(
        looper,
        '#include "SLAACClient.h"\n'
        '#include "NetServer.h"\n',
        '#include "SLAACClient.h"\n'
        '#include "DHCPv6Client.h"\n'
        '#include "NetServer.h"\n',
        "include DHCPv6Client",
        optional=True,
    )
    replace_once(
        looper,
        "	fCurrentClient(NULL),\n"
        "	fLastMediaStatus(0),\n"
        "	fJoiningNetwork(false)\n",
        "	fCurrentClient(NULL),\n"
        "	fSLAACClient(NULL),\n"
        "	fDHCPv6Client(NULL),\n"
        "	fLastMediaStatus(0),\n"
        "	fJoiningNetwork(false)\n",
        "init fSLAACClient",
        optional=True,
    )
    replace_once(
        looper,
        "	fSLAACClient(NULL),\n"
        "	fLastMediaStatus(0),\n",
        "	fSLAACClient(NULL),\n"
        "	fDHCPv6Client(NULL),\n"
        "	fLastMediaStatus(0),\n",
        "init fDHCPv6Client",
        optional=True,
    )
    replace_once(
        looper,
        """AutoconfigLooper::~AutoconfigLooper()
{
	_RemoveClient();
}
""",
        """AutoconfigLooper::~AutoconfigLooper()
{
	_RemoveClient();
	_RemoveSLAAC();
}
""",
        "dtor remove SLAAC",
        optional=True,
    )
    replace_once(
        looper,
        """	_RemoveSLAAC();
}
""",
        """	_RemoveSLAAC();
	_RemoveDHCPv6();
}
""",
        "dtor DHCPv6",
        optional=True,
    )
    replace_once(
        looper,
        """void
AutoconfigLooper::_RemoveSLAAC()
{
	if (fSLAACClient == NULL)
		return;

	delete fSLAACClient;
	fSLAACClient = NULL;
}
""",
        """void
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
""",
        "add _RemoveDHCPv6",
        optional=True,
    )
    replace_once(
        looper,
        """void
AutoconfigLooper::_RemoveClient()
{
	if (fCurrentClient == NULL)
		return;

	delete fCurrentClient;
	fCurrentClient = NULL;
}
""",
        """void
AutoconfigLooper::_RemoveClient()
{
	if (fCurrentClient == NULL)
		return;

	delete fCurrentClient;
	fCurrentClient = NULL;
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
}
""",
        "SLAAC methods",
        optional=True,
    )
    replace_once(
        looper,
        """	fSLAACClient->Start();
}
""",
        """	fSLAACClient->Start();

	if (fDHCPv6Client == NULL) {
		fDHCPv6Client = new DHCPv6Client(fTarget, fDevice.String());
		AddHandler(fDHCPv6Client);
	}
	fDHCPv6Client->Start();
}
""",
        "start DHCPv6",
        optional=True,
    )
    replace_once(
        looper,
        "		_ConfigureIPv4();\n"
        "		//_ConfigureIPv6();	// TODO: router advertisement and dhcpv6\n",
        "		_ConfigureIPv4();\n"
        "		_ConfigureIPv6();\n",
        "ready IPv6",
        optional=True,
    )
    replace_once(
        looper,
        "				_ConfigureIPv4();\n"
        "				//_ConfigureIPv6();	// TODO: router advertisement and dhcpv6\n",
        "				_ConfigureIPv4();\n"
        "				_ConfigureIPv6();\n",
        "link-up IPv6",
        optional=True,
    )
    replace_once(
        looper,
        """			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
				fJoiningNetwork = false;
			}
""",
        """			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
				_RemoveSLAAC();
				_RemoveDHCPv6();
				fJoiningNetwork = false;
			}
""",
        "link-down SLAAC",
        optional=True,
    )
    replace_once(
        looper,
        """				_RemoveClient();
				_RemoveSLAAC();
				fJoiningNetwork = false;
""",
        """				_RemoveClient();
				_RemoveSLAAC();
				_RemoveDHCPv6();
				fJoiningNetwork = false;
""",
        "link-down DHCPv6",
        optional=True,
    )


def patch_net_server() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """	// TODO: our IPv6 stack is still fairly fragile. We need more v6 work
	// (including IPv6 address scope flags before we start attaching link
	// local addresses by default.
	//_ConfigureIPv6LinkLocal(name);
""",
        """	_ConfigureIPv6LinkLocal(name);
""",
        "enable IPv6 link-local",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """	bool startAutoConfig = false;
""",
        """	bool startAutoConfig = false;
	bool autoIPv4 = false;
	bool autoIPv6 = false;
	bool sawIPv6 = false;
""",
        "family auto flags",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """		if (addressSettings.IsAutoConfigure())
			startAutoConfig = true;
""",
        """		if (addressSettings.Family() == AF_INET6)
			sawIPv6 = true;
		if (addressSettings.IsAutoConfigure()) {
			startAutoConfig = true;
			if (addressSettings.Family() == AF_INET6)
				autoIPv6 = true;
			else
				autoIPv4 = true;
		}
""",
        "per-family auto_config",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """	if (startAutoConfig) {
		// start auto configuration
		AutoconfigLooper* looper = new AutoconfigLooper(this, name);
		looper->Run();
""",
        """	if (!sawIPv6)
		autoIPv6 = true;
	if (startAutoConfig || autoIPv6) {
		// start auto configuration
		if (_LooperForDevice(name) == NULL) {
			AutoconfigLooper* looper = new AutoconfigLooper(this, name,
				autoIPv4, autoIPv6);
			looper->Run();
			fDeviceMap[name] = looper;
		}
""",
        "start looper families",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """	if (startAutoConfig || autoIPv6) {
		// start auto configuration
		AutoconfigLooper* looper = new AutoconfigLooper(this, name, autoIPv4,
			autoIPv6);
		looper->Run();

		fDeviceMap[name] = looper;
""",
        """	if (startAutoConfig || autoIPv6) {
		// start auto configuration
		if (_LooperForDevice(name) == NULL) {
			AutoconfigLooper* looper = new AutoconfigLooper(this, name,
				autoIPv4, autoIPv6);
			looper->Run();
			fDeviceMap[name] = looper;
		}
""",
        "one looper per device",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        '#include "NetServer.h"\n',
        '#include "NetServer.h"\n'
        '#include "IPv6Dad.h"\n',
        "include IPv6Dad",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """	if (interface.FindAddress(localLinkAddress) >= 0) {
		// uhoh... already has a local link address
""",
        """	if (r5_ipv6_has_address(name, addressRaw)) {
		// uhoh... already has a local link address
""",
        "link-local has_address",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """	address.AddString("family", "inet");
	address.AddBool("auto_config", true);
	interface.AddMessage("address", &address);
""",
        """	address.AddString("family", "inet");
	address.AddBool("auto_config", true);
	interface.AddMessage("address", &address);
	BMessage address6;
	address6.AddString("family", "inet6");
	address6.AddBool("auto_config", true);
	interface.AddMessage("address", &address6);
""",
        "default inet6 auto",
        optional=True,
    )
    replace_once(
        HAIKU / "src" / "servers" / "net" / "NetServer.cpp",
        """status_t
NetServer::_ConfigureResolver(BMessage& resolverConfiguration)
{
	// Store resolver settings in resolv.conf file, while maintaining any
	// user specified settings already present.

	BPath path;
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append("network/resolv.conf") != B_OK)
		return B_ERROR;

	FILE* file = fopen(path.Path(), "r+");
	// open existing resolv.conf if possible
	if (file == NULL) {
		// no existing resolv.conf, create a new one
		file = fopen(path.Path(), "w");
		if (file == NULL) {
			fprintf(stderr, "Could not open resolv.conf: %s\\n",
				strerror(errno));
			return errno;
		}
	} else {
		// An existing resolv.conf was found, parse it for user settings
		const char* staticDNS = "# Static DNS Only";
		size_t sizeStaticDNS = strlen(staticDNS);
		const char* dynamicDNS = "# Dynamic DNS entries";
		size_t sizeDynamicDNS = strlen(dynamicDNS);
		char resolveConfBuffer[80];
		size_t sizeResolveConfBuffer = sizeof(resolveConfBuffer);

		while (fgets(resolveConfBuffer, sizeResolveConfBuffer, file)) {
			if (strncmp(resolveConfBuffer, staticDNS, sizeStaticDNS) == 0) {
				// If DNS is set to static only, don't modify
				fclose(file);
				return B_OK;
			} else if (strncmp(resolveConfBuffer, dynamicDNS, sizeDynamicDNS)
					== 0) {
				// Overwrite existing dynamic entries
				break;
			}
		}

		if (feof(file) != 0) {
			// No static entries found, close and re-open as new file
			fclose(file);
			file = fopen(path.Path(), "w");
			if (file == NULL) {
				fprintf(stderr, "Could not open resolv.conf: %s\\n",
					strerror(errno));
				return errno;
			}
		}
	}

	fprintf(file, "# Added automatically by DHCP\\n");

	const char* nameserver;
	for (int32 i = 0; resolverConfiguration.FindString("nameserver", i,
			&nameserver) == B_OK; i++) {
		fprintf(file, "nameserver %s\\n", nameserver);
	}
""",
        """status_t
NetServer::_ConfigureResolver(BMessage& resolverConfiguration)
{
	// Union IPv4 DHCP and IPv6 RDNSS nameservers. A rewrite that only
	// listed RDNSS wiped the IPv4 servers and broke dual-stack DNS.

	BPath path;
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append("network/resolv.conf") != B_OK)
		return B_ERROR;

	char servers[8][INET6_ADDRSTRLEN];
	int32 nServers = 0;
	bool staticOnly = false;

	FILE* existing = fopen(path.Path(), "r");
	if (existing != NULL) {
		char line[128];
		while (fgets(line, sizeof(line), existing) != NULL) {
			if (strncmp(line, "# Static DNS Only", 17) == 0) {
				staticOnly = true;
				break;
			}
			if (strncmp(line, "nameserver ", 11) == 0) {
				char* start = line + 11;
				size_t length = strlen(start);
				while (length > 0 && (start[length - 1] == '\\n'
						|| start[length - 1] == '\\r')) {
					start[--length] = '\\0';
				}
				bool have = false;
				for (int32 i = 0; i < nServers; i++) {
					if (strcmp(servers[i], start) == 0)
						have = true;
				}
				if (!have && nServers < 8 && start[0] != '\\0') {
					strlcpy(servers[nServers], start, sizeof(servers[0]));
					nServers++;
				}
			}
		}
		fclose(existing);
	}
	if (staticOnly)
		return B_OK;

	const char* nameserver;
	for (int32 i = 0; resolverConfiguration.FindString("nameserver", i,
			&nameserver) == B_OK; i++) {
		if (nameserver == NULL || nameserver[0] == '\\0')
			continue;
		bool have = false;
		for (int32 j = 0; j < nServers; j++) {
			if (strcmp(servers[j], nameserver) == 0)
				have = true;
		}
		if (!have && nServers < 8) {
			strlcpy(servers[nServers], nameserver, sizeof(servers[0]));
			nServers++;
		}
	}

	FILE* file = fopen(path.Path(), "w");
	if (file == NULL) {
		fprintf(stderr, "Could not open resolv.conf: %s\\n", strerror(errno));
		return errno;
	}

	fprintf(file, "# Added automatically by DHCP\\n");
	for (int32 i = 0; i < nServers; i++)
		fprintf(file, "nameserver %s\\n", servers[i]);
""",
        "merge IPv4/IPv6 nameservers",
        optional=True,
    )


def patch_looper_families() -> None:
    header = HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.h"
    replace_once(
        header,
        """								AutoconfigLooper(BMessenger target,
									const char* device);
""",
        """								AutoconfigLooper(BMessenger target,
									const char* device,
									bool autoIPv4 = true,
									bool autoIPv6 = true);
""",
        "ctor families",
        optional=True,
    )
    replace_once(
        header,
        """			bool				fJoiningNetwork;
""",
        """			bool				fJoiningNetwork;
			bool				fAutoIPv4;
			bool				fAutoIPv6;
""",
        "family members",
        optional=True,
    )

    looper = HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.cpp"
    replace_once(
        looper,
        """AutoconfigLooper::AutoconfigLooper(BMessenger target, const char* device)
	: BLooper(device),
	fTarget(target),
	fDevice(device),
	fCurrentClient(NULL),
	fSLAACClient(NULL),
	fDHCPv6Client(NULL),
	fLastMediaStatus(0),
	fJoiningNetwork(false)
{
""",
        """AutoconfigLooper::AutoconfigLooper(BMessenger target, const char* device,
	bool autoIPv4, bool autoIPv6)
	: BLooper(device),
	fTarget(target),
	fDevice(device),
	fCurrentClient(NULL),
	fSLAACClient(NULL),
	fDHCPv6Client(NULL),
	fLastMediaStatus(0),
	fJoiningNetwork(false),
	fAutoIPv4(autoIPv4),
	fAutoIPv6(autoIPv6)
{
""",
        "ctor init families",
        optional=True,
    )
    replace_once(
        looper,
        """	if (interface.HasLink()) {
		_ConfigureIPv4();
		_ConfigureIPv6();
""",
        """	if (interface.HasLink()) {
		if (fAutoIPv4)
			_ConfigureIPv4();
		if (fAutoIPv6)
			_ConfigureIPv6();
""",
        "ready family gate",
        optional=True,
    )
    replace_once(
        looper,
        """				_ConfigureIPv4();
				_ConfigureIPv6();
""",
        """				if (fAutoIPv4)
					_ConfigureIPv4();
				if (fAutoIPv6)
					_ConfigureIPv6();
""",
        "link-up family gate",
        optional=True,
    )


def patch_prefs() -> None:
    view = HAIKU / "src" / "preferences" / "network" / "InterfaceAddressView.cpp"
    replace_once(
        view,
        """	if (fFamily == AF_INET6) {
		// Automatic can be DHCPv6 or Router Advertisements
		fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Automatic"),
			new BMessage(kModeAuto)));
	}
""",
        """	if (fFamily == AF_INET6) {
		// Automatic can be DHCPv6 or Router Advertisements
		fModePopUpMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Automatic (SLAAC / DHCPv6)"),
			new BMessage(kModeAuto)));
	}
""",
        "ipv6 mode label",
        optional=True,
    )
    replace_once(
        view,
        """	fModeField->SetToolTip(
		B_TRANSLATE("The method for obtaining an IP address"));
""",
        """	fModeField->SetToolTip(fFamily == AF_INET6
		? B_TRANSLATE("Automatic uses router advertisements (SLAAC) and "
			"DHCPv6. Test in Terminal: grep -E 'slaac|dad|dhcpv6' "
			"/var/log/syslog")
		: B_TRANSLATE("The method for obtaining an IP address"));
""",
        "ipv6 mode tooltip",
        optional=True,
    )


def patch_kernel_multicast_if() -> None:
    path = HAIKU / "src" / "add-ons" / "kernel" / "network" / "protocols" / "ipv6" / "ipv6.cpp"
    replace_once(
        path,
        """			struct sockaddr_in6* address = new (std::nothrow) sockaddr_in6;
			if (address == NULL)
				return B_NO_MEMORY;

			if (user_memcpy(&address->sin6_addr, value, sizeof(in6_addr))
""",
        """			struct sockaddr_in6* address = new (std::nothrow) sockaddr_in6;
			if (address == NULL)
				return B_NO_MEMORY;

			memset(address, 0, sizeof(*address));
			address->sin6_len = sizeof(*address);
			address->sin6_family = AF_INET6;

			if (user_memcpy(&address->sin6_addr, value, sizeof(in6_addr))
""",
        "IPV6_MULTICAST_IF family",
        optional=True,
    )


def patch_datalink_device_route() -> None:
    header = HAIKU / "headers" / "private" / "net" / "net_datalink.h"
    replace_once(
        header,
        """	status_t		(*update_route_info)(net_domain* domain,
						net_route_info* info);
};
""",
        """	status_t		(*update_route_info)(net_domain* domain,
						net_route_info* info);
	status_t		(*get_device_route)(net_domain* domain, uint32 index,
						net_route** _route);
};
""",
        "datalink get_device_route",
        optional=True,
    )
    datalink = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "stack" / "datalink.cpp"
    )
    replace_once(
        datalink,
        """	register_route_info,
	unregister_route_info,
	update_route_info
};
""",
        """	register_route_info,
	unregister_route_info,
	update_route_info,
	get_device_route
};
""",
        "datalink get_device_route export",
        optional=True,
    )


def patch_alias_local_routes() -> None:
    path = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "stack"
        / "interfaces.cpp"
    )
    replace_once(
        path,
        """	if (status == B_OK)
		notify_interface_changed(interface);
	else
		delete address;

	return status;
}


status_t
""",
        """	if (status == B_OK) {
		// SIOCAIFADDR / ADD_ALIAS used to skip this; IPv6 SLAAC then had
		// no RTF_LOCAL host route, so ping to the ULA went off-link.
		address->AddDefaultRoutes(SIOCSIFADDR);
		notify_interface_changed(interface);
	} else
		delete address;

	return status;
}


status_t
""",
        "alias local IPv6 routes",
        optional=True,
    )


def patch_ipv6_route_lookup() -> None:
    path = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "stack" / "routes.cpp"
    )
    replace_once(
        path,
        """		if (route->mask) {
			sockaddr maskedAddress;
			domain->address_module->mask_address(address, route->mask,
				&maskedAddress);
			if (!domain->address_module->equal_addresses(&maskedAddress,
					route->destination))
				continue;
""",
        """		if (route->mask) {
			sockaddr_storage maskedAddress;
			domain->address_module->mask_address(address, route->mask,
				(sockaddr*)&maskedAddress);
			if (!domain->address_module->equal_addresses(
					(sockaddr*)&maskedAddress, route->destination))
				continue;
""",
        "IPv6 route lookup mask buffer",
        optional=True,
    )


def patch_ipv6_default_route_specificity() -> None:
    path = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "protocols"
        / "ipv6" / "ipv6_address.cpp"
    )
    replace_once(
        path,
        """static int32
ipv6_first_mask_bit(const sockaddr *_mask)
{
	if (_mask == NULL)
		return 0;

	const uint8 *pmask = ((const sockaddr_in6 *)_mask)->sin6_addr.s6_addr;
	for (uint8 i = 0; i < sizeof(in6_addr); ++i) {
""",
        """static int32
ipv6_first_mask_bit(const sockaddr *_mask)
{
	if (_mask == NULL)
		return 0;

	const uint8 *pmask = ((const sockaddr_in6 *)_mask)->sin6_addr.s6_addr;
	bool allZero = true;
	for (uint8 i = 0; i < sizeof(in6_addr); ++i) {
		if (pmask[i] != 0) {
			allZero = false;
			break;
		}
	}
	if (allZero)
		return 128;

	for (uint8 i = 0; i < sizeof(in6_addr); ++i) {
""",
        "IPv6 default mask least specific",
        optional=True,
    )


def patch_kernel_multicast_route() -> None:
    path = HAIKU / "src" / "add-ons" / "kernel" / "network" / "protocols" / "ipv6" / "ipv6.cpp"
    replace_once(
        path,
        """	sockaddr_in6* destination = (sockaddr_in6*)buffer->destination;

	// handle IPV6_MULTICAST_IF
	if (IN6_IS_ADDR_MULTICAST(&destination->sin6_addr)
		&& protocol->multicast_address != NULL) {
		net_interface_address* address = sDatalinkModule->get_interface_address(
			protocol->multicast_address);
		if (address == NULL || (address->interface->flags & IFF_UP) == 0) {
			sDatalinkModule->put_interface_address(address);
			return EADDRNOTAVAIL;
		}

		sDatalinkModule->put_interface_address(buffer->interface_address);
		buffer->interface_address = address;
			// the buffer takes over ownership of the address

		net_route* route = sDatalinkModule->get_route(sDomain, address->local);
		if (route == NULL)
			return ENETUNREACH;

		return sDatalinkModule->send_routed_data(route, buffer);
	}

	return sDatalinkModule->send_data(protocol, sDomain, buffer);
}
""",
        """	sockaddr_in6* destination = (sockaddr_in6*)buffer->destination;

	if (IN6_IS_ADDR_MULTICAST(&destination->sin6_addr) && protocol != NULL) {
		uint32 index = 0;
		if (protocol->multicast_address != NULL) {
			net_interface_address* address
				= sDatalinkModule->get_interface_address(
					protocol->multicast_address);
			if (address == NULL
				|| (address->interface->flags & IFF_UP) == 0) {
				sDatalinkModule->put_interface_address(address);
				return EADDRNOTAVAIL;
			}

			index = address->interface->index;
			sDatalinkModule->put_interface_address(buffer->interface_address);
			buffer->interface_address = address;
		} else if (protocol->socket != NULL
			&& protocol->socket->bound_to_device != 0) {
			index = protocol->socket->bound_to_device;
		} else if (destination->sin6_scope_id != 0)
			index = destination->sin6_scope_id;

		if (index != 0) {
			net_route* route = NULL;
			status_t status = sDatalinkModule->get_device_route(sDomain,
				index, &route);
			if (status != B_OK)
				return status;
			status = ipv6_send_routed_data(protocol, route, buffer);
			sDatalinkModule->put_route(sDomain, route);
			return status;
		}
	}

	return sDatalinkModule->send_data(protocol, sDomain, buffer);
}
""",
        "IPv6 multicast device route",
        optional=True,
    )
    replace_once(
        path,
        """			status = sDatalinkModule->send_routed_data(route, buffer);
			sDatalinkModule->put_route(sDomain, route);
			return status;
""",
        """			status = ipv6_send_routed_data(protocol, route, buffer);
			sDatalinkModule->put_route(sDomain, route);
			return status;
""",
        "IPv6 multicast via ipv6_send_routed_data",
        optional=True,
    )
    replace_once(
        path,
        """status_t
ipv6_send_routed_data(net_protocol* _protocol, struct net_route* route,
	net_buffer* buffer)
{
""",
        """static void
ipv6_prefer_nonlocal_source(net_interface* interface, sockaddr_in6& source,
	const sockaddr_in6& destination)
{
	// Default IPv6 routes are bound to the first address (link-local).
	// Off-link destinations must not use fe80 as the source (RFC 6724).
	if (!IN6_IS_ADDR_LINKLOCAL(&source.sin6_addr))
		return;
	if (IN6_IS_ADDR_LINKLOCAL(&destination.sin6_addr)
		|| IN6_IS_ADDR_MC_LINKLOCAL(&destination.sin6_addr)
		|| IN6_IS_ADDR_MC_NODELOCAL(&destination.sin6_addr))
		return;

	net_interface_address* address = NULL;
	while (sDatalinkModule->get_next_interface_address(interface, &address)) {
		if (address->local == NULL || address->local->sa_family != AF_INET6)
			continue;
		const sockaddr_in6* local
			= (const sockaddr_in6*)address->local;
		if (IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
			|| IN6_IS_ADDR_UNSPECIFIED(&local->sin6_addr)
			|| IN6_IS_ADDR_LOOPBACK(&local->sin6_addr)
			|| IN6_IS_ADDR_MULTICAST(&local->sin6_addr))
			continue;
		source.sin6_addr = local->sin6_addr;
		sDatalinkModule->put_interface_address(address);
		return;
	}
}


status_t
ipv6_send_routed_data(net_protocol* _protocol, struct net_route* route,
	net_buffer* buffer)
{
""",
        "IPv6 RFC 6724 source pick",
        optional=True,
    )
    replace_once(
        path,
        """	sockaddr_in6& source = *(sockaddr_in6*)buffer->source;
	sockaddr_in6& destination = *(sockaddr_in6*)buffer->destination;

	buffer->msg_flags &= ~(MSG_BCAST | MSG_MCAST);
""",
        """	sockaddr_in6& source = *(sockaddr_in6*)buffer->source;
	sockaddr_in6& destination = *(sockaddr_in6*)buffer->destination;

	ipv6_prefer_nonlocal_source(interface, source, destination);

	buffer->msg_flags &= ~(MSG_BCAST | MSG_MCAST);
""",
        "IPv6 use ULA source off-link",
        optional=True,
    )
    replace_once(
        path,
        """	ipv6_prefer_nonlocal_source(interface, source, destination);

	buffer->msg_flags &= ~(MSG_BCAST | MSG_MCAST);
""",
        """	if (sDatalinkModule->is_local_address(sDomain, (sockaddr*)&destination,
			NULL, NULL))
		source.sin6_addr = destination.sin6_addr;
	else
		ipv6_prefer_nonlocal_source(interface, source, destination);

	buffer->msg_flags &= ~(MSG_BCAST | MSG_MCAST);
""",
        "IPv6 self dest as source",
        optional=True,
    )


def patch_ipv6_find_route_default_last() -> None:
    path = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "stack" / "routes.cpp"
    )
    replace_once(
        path,
        """#include <net/if_dl.h>
#include <net/route.h>
""",
        """#include <net/if_dl.h>
#include <net/route.h>
#include <netinet6/in6.h>
""",
        "routes.cpp in6.h",
        optional=True,
    )
    replace_once(
        path,
        """	RouteList::Iterator iterator = domain->routes.GetIterator();
	net_route_private* candidate = NULL;

	TRACE("test address %s for routes...\\n",
		AddressString(domain, address).Data());

	// TODO: alternate equal default routes

	while (iterator.HasNext()) {
		net_route_private* route = iterator.Next();

		if (route->mask) {
			sockaddr_storage maskedAddress;
			domain->address_module->mask_address(address, route->mask,
				(sockaddr*)&maskedAddress);
			if (!domain->address_module->equal_addresses(
					(sockaddr*)&maskedAddress, route->destination))
				continue;
		} else if (!domain->address_module->equal_addresses(address,
				route->destination))
			continue;

		// neglect routes that point to devices that have no link
		if ((route->interface_address->interface->device->flags & IFF_LINK)
				== 0) {
			if (candidate == NULL) {
				TRACE("  found candidate: %s, flags %lx\\n", AddressString(
					domain, route->destination).Data(), route->flags);
				candidate = route;
			}
			continue;
		}

		TRACE("  found route: %s, flags %lx\\n",
			AddressString(domain, route->destination).Data(), route->flags);

		return route;
	}

	return candidate;
""",
        """	RouteList::Iterator iterator = domain->routes.GetIterator();
	net_route_private* candidate = NULL;
	net_route_private* defaultRoute = NULL;

	TRACE("test address %s for routes...\\n",
		AddressString(domain, address).Data());

	// TODO: alternate equal default routes

	while (iterator.HasNext()) {
		net_route_private* route = iterator.Next();

		bool matched = false;
		if (route->mask) {
			sockaddr_storage maskedAddress;
			if (domain->address_module->mask_address(address, route->mask,
					(sockaddr*)&maskedAddress) == B_OK
				&& domain->address_module->equal_addresses(
					(sockaddr*)&maskedAddress, route->destination))
				matched = true;
			else if (domain->address_module->equal_masked_addresses(address,
					route->destination, route->mask))
				matched = true;
		} else if (domain->address_module->equal_addresses(address,
				route->destination))
			matched = true;

		if (!matched)
			continue;

		if ((route->flags & RTF_DEFAULT) != 0) {
			if (defaultRoute == NULL)
				defaultRoute = route;
			continue;
		}

		// neglect routes that point to devices that have no link
		if ((route->interface_address->interface->device->flags & IFF_LINK)
				== 0) {
			if (candidate == NULL) {
				TRACE("  found candidate: %s, flags %lx\\n", AddressString(
					domain, route->destination).Data(), route->flags);
				candidate = route;
			}
			continue;
		}

		TRACE("  found route: %s, flags %lx\\n",
			AddressString(domain, route->destination).Data(), route->flags);

		return route;
	}

	if (candidate != NULL)
		return candidate;
	if (defaultRoute != NULL
		&& (defaultRoute->interface_address->interface->device->flags
			& IFF_LINK) == 0) {
		if (candidate == NULL)
			return defaultRoute;
	} else if (defaultRoute != NULL)
		return defaultRoute;
	return candidate;
""",
        "IPv6 find_route default last",
        optional=True,
    )
    replace_once(
        path,
        """			InterfaceAddress* address
				= interface->FirstForFamily(domain->family);

			route.mtu = entry.mtu;
""",
        """			InterfaceAddress* address
				= interface->FirstForFamily(domain->family);
			if (domain->family == AF_INET6 && address != NULL) {
				size_t count = interface->CountAddresses();
				for (size_t i = 0; i < count; i++) {
					InterfaceAddress* candidate = interface->AddressAt(i);
					if (candidate == NULL)
						continue;
					if (candidate->local != NULL
						&& candidate->local->sa_family == AF_INET6) {
						const sockaddr_in6* local
							= (const sockaddr_in6*)candidate->local;
						if (!IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
							&& local->sin6_addr.s6_addr[0] != 0) {
							address->ReleaseReference();
							address = candidate;
							break;
						}
					}
					candidate->ReleaseReference();
				}
			}

			route.mtu = entry.mtu;
""",
        "IPv6 default route on ULA",
        optional=True,
    )
    replace_once(
        path,
        """						if (!IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
							&& !IN6_IS_ADDR_LOOPBACK(&local->sin6_addr)
							&& !IN6_IS_ADDR_UNSPECIFIED(&local->sin6_addr)
							&& !IN6_IS_ADDR_MULTICAST(&local->sin6_addr)) {
""",
        """						if (!IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
							&& local->sin6_addr.s6_addr[0] != 0) {
""",
        "IPv6 default route no in6addr",
        optional=True,
    )
    replace_once(
        path,
        """	if (route->interface_address != NULL
		&& route->interface_address->local != NULL) {
		status = domain->address_module->update_to(source,
			route->interface_address->local);
	}

	if (status != B_OK)
		put_route_internal(domain, route);
	else
		*_route = route;
""",
        """	if (route->interface_address != NULL
		&& route->interface_address->local != NULL) {
		status = domain->address_module->update_to(source,
			route->interface_address->local);
	}

	InterfaceAddress* localDest = get_interface_address(buffer->destination);
	if (status == B_OK && localDest != NULL && localDest->local != NULL) {
		uint16 port = domain->address_module->get_port(source);
		status = domain->address_module->set_to(source, localDest->local);
		if (status == B_OK)
			domain->address_module->set_port(source, port);
		localDest->ReleaseReference();
	} else if (localDest != NULL)
		localDest->ReleaseReference();

	if (status != B_OK)
		put_route_internal(domain, route);
	else
		*_route = route;
""",
        "local dest as IPv6 source",
        optional=True,
    )


def patch_ipv6_local_loopback() -> None:
    path = HAIKU / "src" / "add-ons" / "kernel" / "network" / "stack" / "datalink.cpp"
    replace_once(
        path,
        """static status_t
datalink_send_routed_data(struct net_route* route, net_buffer* buffer)
{
""",
        """static bool datalink_is_local_address(net_domain* domain,
	const struct sockaddr* address, net_interface_address** _interfaceAddress,
	uint32* _matchedType);


static status_t
datalink_send_routed_data(struct net_route* route, net_buffer* buffer)
{
""",
        "forward datalink_is_local_address",
        optional=True,
    )
    replace_once(
        path,
        """	if ((route->flags & RTF_LOCAL) != 0) {
		TRACE("  local route\\n");
""",
        """	if ((route->flags & RTF_LOCAL) != 0
		|| datalink_is_local_address(address->domain, buffer->destination,
			NULL, NULL)) {
		TRACE("  local route\\n");
""",
        "loopback local IPv6 dest",
        optional=True,
    )


def patch_udp_ipv6_source() -> None:
    path = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "protocols"
        / "udp" / "udp.cpp"
    )
    replace_once(
        path,
        """#include <netinet/in.h>
#include <netinet/ip.h>
""",
        """#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet6/in6.h>
""",
        "udp.cpp in6.h",
        optional=True,
    )
    replace_once(
        path,
        """		if (routeToDestination) {
			// stay bound to current local port, if any.
			uint16 port = endpoint->LocalAddress().Port();
			status = endpoint->LocalAddress().SetTo(
				routeToDestination->interface_address->local);
			endpoint->LocalAddress().SetPort(port);
			gDatalinkModule->put_route(fDomain, routeToDestination);
			if (status < B_OK)
				return status;
		}
""",
        """		if (routeToDestination) {
			// stay bound to current local port, if any.
			uint16 port = endpoint->LocalAddress().Port();
			status = endpoint->LocalAddress().SetTo(
				routeToDestination->interface_address->local);
			endpoint->LocalAddress().SetPort(port);
			if (status == B_OK
				&& gDatalinkModule->is_local_address(fDomain, address,
					NULL, NULL)) {
				endpoint->LocalAddress().SetTo(address);
				endpoint->LocalAddress().SetPort(port);
			} else if (status == B_OK && address->sa_family == AF_INET6) {
				const sockaddr_in6* dest6 = (const sockaddr_in6*)address;
				const sockaddr_in6* src6
					= (const sockaddr_in6*)*endpoint->LocalAddress();
				if (src6 != NULL && IN6_IS_ADDR_LINKLOCAL(&src6->sin6_addr)
					&& !IN6_IS_ADDR_LINKLOCAL(&dest6->sin6_addr)
					&& !IN6_IS_ADDR_MC_LINKLOCAL(&dest6->sin6_addr)
					&& routeToDestination->interface_address != NULL) {
					net_interface* interface
						= routeToDestination->interface_address->interface;
					net_interface_address* cursor = NULL;
					while (gDatalinkModule->get_next_interface_address(
							interface, &cursor)) {
						if (cursor->local == NULL
							|| cursor->local->sa_family != AF_INET6)
							continue;
						const sockaddr_in6* local
							= (const sockaddr_in6*)cursor->local;
						if (IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
							|| IN6_IS_ADDR_MULTICAST(&local->sin6_addr)
							|| local->sin6_addr.s6_addr[0] == 0)
							continue;
						endpoint->LocalAddress().SetTo(cursor->local);
						endpoint->LocalAddress().SetPort(port);
						gDatalinkModule->put_interface_address(cursor);
						break;
					}
				}
			}
			gDatalinkModule->put_route(fDomain, routeToDestination);
			if (status < B_OK)
				return status;
		}
""",
        "UDP IPv6 ULA source",
        optional=True,
    )
    replace_once(
        path,
        """						if (IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
							|| IN6_IS_ADDR_LOOPBACK(&local->sin6_addr)
							|| IN6_IS_ADDR_UNSPECIFIED(&local->sin6_addr)
							|| IN6_IS_ADDR_MULTICAST(&local->sin6_addr))
							continue;
""",
        """						if (IN6_IS_ADDR_LINKLOCAL(&local->sin6_addr)
							|| IN6_IS_ADDR_MULTICAST(&local->sin6_addr)
							|| local->sin6_addr.s6_addr[0] == 0)
							continue;
""",
        "UDP source no in6addr",
        optional=True,
    )


def patch_ipv6_copy_sa_len() -> None:
    path = (
        HAIKU / "src" / "add-ons" / "kernel" / "network" / "protocols"
        / "ipv6" / "ipv6_address.cpp"
    )
    replace_once(
        path,
        """		*to = (sockaddr *)malloc(sizeof(sockaddr_in6));
		if (*to == NULL)
			return B_NO_MEMORY;

		memcpy(*to, from, sizeof(sockaddr_in6));

		if (mask != NULL)
			ipv6_mask_adress_inplace(*to, mask);
""",
        """		*to = (sockaddr *)malloc(sizeof(sockaddr_in6));
		if (*to == NULL)
			return B_NO_MEMORY;

		memset(*to, 0, sizeof(sockaddr_in6));
		size_t copyLen = from->sa_len;
		if (copyLen > sizeof(sockaddr_in6))
			copyLen = sizeof(sockaddr_in6);
		memcpy(*to, from, copyLen);
		(*to)->sa_len = sizeof(sockaddr_in6);
		(*to)->sa_family = AF_INET6;

		if (mask != NULL)
			ipv6_mask_adress_inplace(*to, mask);
""",
        "IPv6 copy canonical sa_len",
        optional=True,
    )


def main() -> int:
    copy_sources()
    patch_jam()
    patch_looper()
    patch_looper_families()
    patch_net_server()
    patch_prefs()
    patch_kernel_multicast_if()
    patch_datalink_device_route()
    patch_alias_local_routes()
    patch_ipv6_route_lookup()
    patch_ipv6_default_route_specificity()
    patch_kernel_multicast_route()
    patch_ipv6_find_route_default_last()
    patch_ipv6_local_loopback()
    patch_udp_ipv6_source()
    patch_ipv6_copy_sa_len()
    return 0


if __name__ == "__main__":
    sys.exit(main())
