#!/usr/bin/env python3
"""Ship mDNS/DNS-SD, IPP discovery, and AirPrint (Haiku #1042 #6795 #16318)."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "zeroconf"
AIRPRINT = ROOT / "distro" / "print" / "airprint"


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path} ({tag})")


def copy_sources() -> None:
    dest_net = HAIKU / "src" / "system" / "libnetwork" / "netresolv" / "net"
    dest_hdr = HAIKU / "headers" / "private" / "net"
    dest_srv = HAIKU / "src" / "servers" / "net"
    dest_air = HAIKU / "src" / "add-ons" / "print" / "drivers" / "airprint"
    dest_hdr.mkdir(parents=True, exist_ok=True)
    dest_air.mkdir(parents=True, exist_ok=True)
    for name in ("r5_mdns.c", "r5_mdns.h"):
        shutil.copy2(SRC / name, dest_net / name)
        print(f"copied {name} -> {dest_net}")
    shutil.copy2(SRC / "r5_mdns.h", dest_hdr / "r5_mdns.h")
    print(f"copied r5_mdns.h -> {dest_hdr}")
    shutil.copy2(SRC / "MdnsService.cpp", dest_srv / "MdnsService.cpp")
    print(f"copied MdnsService.cpp -> {dest_srv}")
    for name in (
        "AirPrint.cpp",
        "AirPrint.h",
        "AirPrintCap.cpp",
        "AirPrintCap.h",
        "AirPrintEntry.cpp",
        "AirPrint.rdef",
        "Jamfile",
    ):
        shutil.copy2(AIRPRINT / name, dest_air / name)
        print(f"copied {name} -> {dest_air}")


def patch_netresolv_jam() -> None:
    replace_once(
        HAIKU / "src" / "system" / "libnetwork" / "netresolv" / "Jamfile",
        "\t\t\tgetaddrinfo.c\n",
        "\t\t\tgetaddrinfo.c\n\t\t\tr5_mdns.c\n",
        "Haiku #1042 netresolv jam",
    )


def patch_net_server_jam() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "net" / "Jamfile",
        "\tServices.cpp\n",
        "\tServices.cpp\n\tMdnsService.cpp\n",
        "Haiku #1042 net_server jam",
    )


def patch_print_jams() -> None:
    replace_once(
        HAIKU / "src" / "add-ons" / "print" / "drivers" / "Jamfile",
        "SubInclude HAIKU_TOP src add-ons print drivers preview ;\n",
        "SubInclude HAIKU_TOP src add-ons print drivers preview ;\n"
        "SubInclude HAIKU_TOP src add-ons print drivers airprint ;\n",
        "Haiku #16318 airprint jam",
    )
    replace_once(
        HAIKU / "build" / "jam" / "images" / "definitions" / "regular",
        "\tPreview\n] ;",
        "\tPreview\n\tAirPrint\n] ;",
        "Haiku #16318 ship AirPrint in image",
    )
    replace_once(
        HAIKU / "src" / "add-ons" / "print" / "transports" / "ipp" / "Jamfile",
        "UsePrivateHeaders shared print ;\n",
        "UsePrivateHeaders shared print net ;\n",
        "Haiku #6795 ipp private net headers",
    )


def patch_getaddrinfo() -> None:
    path = HAIKU / "src" / "system" / "libnetwork" / "netresolv" / "net" / "getaddrinfo.c"
    replace_once(
        path,
        '#include "nsswitch.h"\n',
        '#include "nsswitch.h"\n#include "r5_mdns.h"\n',
        "Haiku #1042 getaddrinfo include",
    )
    replace_once(
        path,
        """	result = NULL;

	/*
	 * if the servname does not match socktype/protocol, ignore it.
	 */
	if (get_portmatch(pai, servname, svd) != 0)
		return 0;

	switch (nsdispatch(&result, dtab, NSDB_HOSTS, "getaddrinfo",
	    default_dns_files, hostname, pai, servname)) {
""",
        """	result = NULL;

	/*
	 * if the servname does not match socktype/protocol, ignore it.
	 */
	if (get_portmatch(pai, servname, svd) != 0)
		return 0;

	if (r5_mdns_name_is_local(hostname)) {
		error = r5_mdns_getaddrinfo(hostname, pai, &result);
		if (error == 0 && result != NULL) {
			for (cur = result; cur; cur = cur->ai_next) {
				if (*getport(cur))
					continue;
				GET_PORT(cur, servname, svd);
			}
			*res = result;
			return 0;
		}
		if (result != NULL) {
			freeaddrinfo(result);
			result = NULL;
		}
	}

	switch (nsdispatch(&result, dtab, NSDB_HOSTS, "getaddrinfo",
	    default_dns_files, hostname, pai, servname)) {
""",
        "Haiku #1042 explore_fqdn mDNS",
    )


def patch_net_server() -> None:
    path = HAIKU / "src" / "servers" / "net" / "NetServer.cpp"
    replace_once(
        path,
        '#include "Services.h"\n',
        '#include "Services.h"\n\nextern void r5_start_mdns_service();\n',
        "Haiku #1042 net_server proto",
    )
    replace_once(
        path,
        """	_BringUpInterfaces();
	_StartServices();

	BPrivate::BPathMonitor::StartWatching("/dev/net",
""",
        """	_BringUpInterfaces();
	_StartServices();
	r5_start_mdns_service();

	BPrivate::BPathMonitor::StartWatching("/dev/net",
""",
        "Haiku #1042 start mDNS",
    )


def patch_ipp() -> None:
    ipp = HAIKU / "src" / "add-ons" / "print" / "transports" / "ipp" / "Ipp.cpp"
    replace_once(
        ipp,
        '#include <HashMap.h>\n',
        '#include <HashMap.h>\n#include <r5_mdns.h>\n',
        "Haiku #6795 ipp mdns include",
    )
    replace_once(
        ipp,
        """// List detected printers
extern "C" _EXPORT status_t
list_transport_ports(BMessage* msg)
{
	return gRoster.ListPorts(msg);
}
""",
        """// List detected printers
extern "C" _EXPORT status_t
list_transport_ports(BMessage* msg)
{
	status_t status = gRoster.ListPorts(msg);
	r5_mdns_service svcs[R5_MDNS_MAX_SERVICES];
	const char* types[] = {
		"_ipp._tcp.local",
		"_ipps._tcp.local",
		"_universal._sub._ipp._tcp.local"
	};
	for (int t = 0; t < 3; t++) {
		int n = r5_mdns_browse(types[t], svcs, R5_MDNS_MAX_SERVICES, 800);
		for (int i = 0; i < n; i++) {
			if (svcs[i].uri[0] == '\\0')
				continue;
			msg->AddString("port_id", svcs[i].uri);
			BString name(svcs[i].name);
			if (svcs[i].host[0] != '\\0') {
				name << " [";
				name << svcs[i].host;
				name << "]";
			}
			msg->AddString("port_name", name);
		}
	}
	return status;
}
""",
        "Haiku #6795 ipp dns-sd ports",
    )

    transport = (
        HAIKU / "src" / "add-ons" / "print" / "transports" / "ipp" / "IppTransport.cpp"
    )
    replace_once(
        transport,
        '#include <strings.h>\n',
        '#include <strings.h>\n#include <cstring>\n',
        "Haiku #16318 ipp memcmp",
    )
    replace_once(
        transport,
        """		request->setURI("printer-uri", __url);
		request->setMimeMediaType("document-format", "application/octet-stream");
		request->setNameWithoutLanguage("requesting-user-name", __user);
//		request->setNameWithoutLanguage("job-name", __file);	// optional
		request->setDelimiter(IPP_END_OF_ATTRIBUTES_TAG);

		long fssize = __fs.tellg();
		__fs.seekg(0, ios::beg);
		request->setRawData(__fs, fssize);
""",
        """		request->setURI("printer-uri", __url);
		request->setNameWithoutLanguage("requesting-user-name", __user);
		request->setDelimiter(IPP_END_OF_ATTRIBUTES_TAG);

		long fssize = (long)__fs.tellg();
		__fs.seekg(0, ios::beg);
		char magic[4] = {};
		__fs.read(magic, 4);
		__fs.seekg(0, ios::beg);
		const char* mime = (memcmp(magic, "RaS2", 4) == 0)
			? "image/pwg-raster" : "application/octet-stream";
		request->setMimeMediaType("document-format", mime);
		request->setRawData(__fs, (int)fssize);
""",
        "Haiku #16318 pwg document-format",
    )


def patch_add_printer_dialog() -> None:
    path = HAIKU / "src" / "preferences" / "printers" / "AddPrinterDialog.cpp"
    replace_once(
        path,
        """	if (msg->FindString("path", &path) == B_OK) {
		// transport path selected
		fTransportPathText = path;
""",
        """	if (msg->FindString("path", &path) == B_OK) {
		// transport path selected
		fTransportPathText = path;
		if (path.FindFirst("ipp://") == 0 || path.FindFirst("ipps://") == 0)
			fPrinterText = "AirPrint";
""",
        "Haiku #16318 default AirPrint driver",
    )


def main() -> int:
    if not SRC.is_dir() or not AIRPRINT.is_dir():
        raise SystemExit(f"missing {SRC} or {AIRPRINT}")
    copy_sources()
    patch_netresolv_jam()
    patch_net_server_jam()
    patch_print_jams()
    patch_getaddrinfo()
    patch_net_server()
    patch_ipp()
    patch_add_printer_dialog()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
