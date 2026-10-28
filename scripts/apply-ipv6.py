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
			status = sDatalinkModule->send_routed_data(route, buffer);
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


def main() -> int:
    copy_sources()
    patch_jam()
    patch_looper()
    patch_looper_families()
    patch_net_server()
    patch_prefs()
    patch_kernel_multicast_if()
    patch_datalink_device_route()
    patch_kernel_multicast_route()
    return 0


if __name__ == "__main__":
    sys.exit(main())
