#!/usr/bin/env python3
"""Ship IPv4 mDNS hostname.local advertise + resolve (Haiku #1042)."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "zeroconf"


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
    dest_srv = HAIKU / "src" / "servers" / "net"
    for name in ("r5_mdns.c", "r5_mdns.h"):
        shutil.copy2(SRC / name, dest_net / name)
        print(f"copied {name} -> {dest_net}")
    shutil.copy2(SRC / "MdnsService.cpp", dest_srv / "MdnsService.cpp")
    print(f"copied MdnsService.cpp -> {dest_srv}")


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


def main() -> int:
    if not SRC.is_dir():
        raise SystemExit(f"missing {SRC}")
    copy_sources()
    patch_netresolv_jam()
    patch_net_server_jam()
    patch_getaddrinfo()
    patch_net_server()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
