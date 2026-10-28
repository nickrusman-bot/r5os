#!/usr/bin/env python3
"""IPv6 SLAAC in net_server (Haiku #8316/#8317). Idempotent."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "network"


def replace_once(path: pathlib.Path, old: str, new: str, tag: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"already {path.name} ({tag})")
        return
    if old not in text:
        raise SystemExit(f"pattern not found in {path} ({tag})")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"patched {path.name} ({tag})")


def copy_sources() -> None:
    dest = HAIKU / "src" / "servers" / "net"
    for name in ("SLAACClient.cpp", "SLAACClient.h"):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")


def patch_jam() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "net" / "Jamfile",
        "	DHCPClient.cpp\n",
        "	DHCPClient.cpp\n"
        "	SLAACClient.cpp\n",
        "SLAACClient jam",
    )


def patch_looper() -> None:
    header = HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.h"
    replace_once(
        header,
        "class AutoconfigClient;\n",
        "class AutoconfigClient;\n"
        "class SLAACClient;\n",
        "SLAACClient forward",
    )
    replace_once(
        header,
        "			void				_RemoveClient();\n"
        "			void				_ConfigureIPv4();\n",
        "			void				_RemoveClient();\n"
        "			void				_RemoveSLAAC();\n"
        "			void				_ConfigureIPv4();\n"
        "			void				_ConfigureIPv6();\n",
        "looper ipv6 methods",
    )
    replace_once(
        header,
        "			AutoconfigClient*	fCurrentClient;\n",
        "			AutoconfigClient*	fCurrentClient;\n"
        "			SLAACClient*		fSLAACClient;\n",
        "fSLAACClient member",
    )

    looper = HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.cpp"
    replace_once(
        looper,
        '#include "DHCPClient.h"\n'
        '#include "NetServer.h"\n',
        '#include "DHCPClient.h"\n'
        '#include "SLAACClient.h"\n'
        '#include "NetServer.h"\n',
        "include SLAACClient",
    )
    replace_once(
        looper,
        "	fCurrentClient(NULL),\n"
        "	fLastMediaStatus(0),\n"
        "	fJoiningNetwork(false)\n",
        "	fCurrentClient(NULL),\n"
        "	fSLAACClient(NULL),\n"
        "	fLastMediaStatus(0),\n"
        "	fJoiningNetwork(false)\n",
        "init fSLAACClient",
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
AutoconfigLooper::_ConfigureIPv6()
{
	if (fSLAACClient == NULL) {
		fSLAACClient = new SLAACClient(fTarget, fDevice.String());
		AddHandler(fSLAACClient);
	}

	fSLAACClient->Start();
}
""",
        "SLAAC methods",
    )
    replace_once(
        looper,
        "		_ConfigureIPv4();\n"
        "		//_ConfigureIPv6();	// TODO: router advertisement and dhcpv6\n",
        "		_ConfigureIPv4();\n"
        "		_ConfigureIPv6();\n",
        "ready IPv6",
    )
    replace_once(
        looper,
        "				_ConfigureIPv4();\n"
        "				//_ConfigureIPv6();	// TODO: router advertisement and dhcpv6\n",
        "				_ConfigureIPv4();\n"
        "				_ConfigureIPv6();\n",
        "link-up IPv6",
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
				fJoiningNetwork = false;
			}
""",
        "link-down SLAAC",
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
    )


def main() -> int:
    copy_sources()
    patch_jam()
    patch_looper()
    patch_net_server()
    return 0


if __name__ == "__main__":
    sys.exit(main())
