#!/usr/bin/env python3
"""Wi-Fi: one-screen join, remember networks, Deskbar tray (R5OS)."""
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
    dest = HAIKU / "src" / "apps" / "networkstatus"
    for name in ("WiFiWindow.cpp", "WiFiWindow.h"):
        shutil.copy2(SRC / name, dest / name)
        print(f"copied {name} -> {dest}")


def patch_jam() -> None:
    replace_once(
        HAIKU / "src" / "apps" / "networkstatus" / "Jamfile",
        """Application NetworkStatus :
	NetworkStatusWindow.cpp
	NetworkStatusView.cpp
	NetworkStatus.cpp
	RadioView.cpp
	WirelessNetworkMenuItem.cpp
""",
        """Application NetworkStatus :
	NetworkStatusWindow.cpp
	NetworkStatusView.cpp
	NetworkStatus.cpp
	RadioView.cpp
	WirelessNetworkMenuItem.cpp
	WiFiWindow.cpp
""",
        "WiFiWindow jam",
    )


def patch_autoconfig() -> None:
    replace_once(
        HAIKU / "src" / "servers" / "net" / "AutoconfigLooper.cpp",
        """			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
			}
""",
        """			} else if ((media & IFM_ACTIVE) == 0) {
				_RemoveClient();
				fJoiningNetwork = false;
			}
""",
        "rejoin after link loss",
    )


def patch_status_view() -> None:
    view = HAIKU / "src" / "apps" / "networkstatus" / "NetworkStatusView.cpp"
    replace_once(
        view,
        '#include "WirelessNetworkMenuItem.h"\n',
        '#include "WirelessNetworkMenuItem.h"\n'
        '#include "WiFiWindow.h"\n',
        "include WiFiWindow",
    )
    replace_once(
        view,
        "const uint32 kMsgJoinNetwork = 'join';\n",
        "const uint32 kMsgJoinNetwork = 'join';\n"
        "const uint32 kMsgOpenWifi = 'wifi';\n",
        "kMsgOpenWifi",
    )
    replace_once(
        view,
        """		case kMsgOpenNetworkPreferences:
			_OpenNetworksPreferences();
			break;

		case kMsgJoinNetwork:
		{
			const char* deviceName;
			const char* name;
			BNetworkAddress address;
			if (message->FindString("device", &deviceName) == B_OK
				&& message->FindString("name", &name) == B_OK
				&& message->FindFlat("address", &address) == B_OK) {
				BNetworkDevice device(deviceName);
				status_t status = device.JoinNetwork(address);
				if (status != B_OK) {
					BString text
						= B_TRANSLATE("Could not join wireless network:\\n");
					text << strerror(status);
					BAlert* alert = new BAlert(name, text.String(),
						B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
						B_STOP_ALERT);
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go(NULL);
				}
			}
			break;
		}
""",
        """		case kMsgOpenNetworkPreferences:
			_OpenNetworksPreferences();
			break;

		case kMsgOpenWifi:
			WiFiWindow::ShowWindow();
			break;

		case kMsgJoinNetwork:
		{
			const char* deviceName;
			const char* name;
			if (message->FindString("device", &deviceName) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BNetworkDevice device(deviceName);
				wireless_network network;
				if (device.GetNetwork(name, network) != B_OK) {
					strlcpy(network.name, name, sizeof(network.name));
					network.authentication_mode
						= B_NETWORK_AUTHENTICATION_WPA2;
				}
				BString password;
				if (network.authentication_mode
						!= B_NETWORK_AUTHENTICATION_NONE) {
					if (r5_wifi_ask_password(name, password) != B_OK)
						break;
				}
				status_t status = r5_wifi_join_and_remember(deviceName,
					network, password.String());
				if (status != B_OK) {
					BString text
						= B_TRANSLATE("Could not join wireless network:\\n");
					text << strerror(status);
					BAlert* alert = new BAlert(name, text.String(),
						B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
						B_STOP_ALERT);
					alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
					alert->Go(NULL);
				}
			}
			break;
		}
""",
        "join with password and remember",
    )
    replace_once(
        view,
        """	if (!wifiInterface.IsEmpty()) {
		std::set<BNetworkAddress> associated;
""",
        """	menu->AddItem(new BMenuItem(B_TRANSLATE("Wi-Fi" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenWifi)));

	BString currentSsid;
	if (r5_wifi_current_ssid(currentSsid) == B_OK && currentSsid.Length() > 0) {
		BString connected(B_TRANSLATE("Connected to %ssid%"));
		connected.ReplaceFirst("%ssid%", currentSsid);
		BMenuItem* current = new BMenuItem(connected.String(), NULL);
		current->SetEnabled(false);
		current->SetMarked(true);
		menu->AddItem(current);
	} else {
		BMenuItem* none = new BMenuItem(B_TRANSLATE("No network"), NULL);
		none->SetEnabled(false);
		menu->AddItem(none);
	}
	menu->AddSeparatorItem();

	if (!wifiInterface.IsEmpty()) {
		std::set<BNetworkAddress> associated;
""",
        "menu ssid and wifi window",
    )
    replace_once(
        view,
        """			fInterfaceStatuses[interface.Name()] = status;
		}
	}
""",
        """			fInterfaceStatuses[interface.Name()] = status;
		}
	}

	BString ssid;
	BString tip;
	if (r5_wifi_current_ssid(ssid) == B_OK && ssid.Length() > 0)
		tip = ssid;
	else {
		int32 best = kStatusUnknown;
		for (std::map<BString, int32>::const_iterator it
			= fInterfaceStatuses.begin(); it != fInterfaceStatuses.end();
			++it) {
			if (it->second > best)
				best = it->second;
		}
		if (best == kStatusConnecting)
			tip = B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
		else
			tip = B_TRANSLATE("No network");
	}
	SetToolTip(tip.String());
""",
        "tooltip SSID",
    )


def patch_launch() -> None:
    replace_once(
        HAIKU / "data" / "launch" / "user",
        """	service x-vnd.Be-TSKB {
		launch /system/Deskbar
		on initial_volumes_mounted
	}
""",
        """	service x-vnd.Be-TSKB {
		launch /system/Deskbar
		on initial_volumes_mounted
	}

	job x-vnd.R5OS-NetworkStatus {
		launch /system/apps/NetworkStatus --deskbar
		requires x-vnd.Be-TSKB
		on initial_volumes_mounted
		legacy
		no_safemode
	}
""",
        "NetworkStatus deskbar job",
    )


def main() -> int:
    copy_sources()
    patch_jam()
    patch_autoconfig()
    patch_status_view()
    patch_launch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
