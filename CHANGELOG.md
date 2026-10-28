# R5OS changelog (lokaal)

Bijhouden in deze overlay-repo. Geen Haiku-hrev. Nieuwste eerst.

## 2026-10-28 (Europe/Amsterdam)

### IPv6 SLAAC in net_server

On link-up, net_server assigns IPv6 link-local, SLAAC (RS/RA `/64`, default route, RDNSS), DAD before using an address, and DHCPv6 (IA_NA + DNS, or Information-Request). VirtualBox bridged LAN got a ULA via SLAAC.

DAD Neighbor Solicit used to fail with “Network is unreachable” on a bound-unspecified socket and then **block** the second IPv6 address, so Network only showed `fe80::`. DAD now binds link-local, retries, and continues without DAD if NS still cannot be sent. Only a real duplicate NA refuses the address.

Network → NIC → IPv6 already has Mode Automatic / Static / Disabled. Automatic is labeled SLAAC/DHCPv6; Disabled is honored per family. Test: `docs/ipv6.md` (`grep -E 'slaac|dad|dhcpv6' /var/log/syslog`). A RA without A-flag `/64` cannot invent a ULA; DHCPv6 may still run.

IPv6 aliases were listed twice: IPv4 DHCP reconfigure started a second AutoconfigLooper, and `FindAddress` missed existing IPv6 slots. One looper per NIC; add address only if the `in6_addr` is not already present.

DAD follows RFC 4862: NS from `::` with no Source Link-Layer option. Haiku `IPV6_MULTICAST_IF` takes an interface `in6_addr`, not an ifindex. Socket/bind/setsockopt/send failures block assigning the address. `select()` is used instead of a blocking `recvfrom` so shutdown can finish. Host tests: `scripts/test_ipv6_dad.cpp`.

SLAAC must not DAD the link-local before it exists (`IPV6_MULTICAST_IF` needs an address on the NIC). That aborted SLAAC, so no ULA, ~10s DHCPv6 wait, and shutdown could stick in `select`. Link-local is added without DAD (NetServer already does); DAD stays on the SLAAC/DHCPv6 address. Destructor `shutdown()`s the socket so the thread can exit.

Haiku `IPV6_MULTICAST_IF` looked up the interface with an uninitialized `sockaddr_in6` (`sa_family` 0 → `EADDRNOTAVAIL`), so DAD/RS/DHCPv6 logged “Can't assign requested address” / “Network is unreachable” and never applied the ULA. Overlay sets `sin6_family`/`sin6_len`. VirtualBox copies the raw image onto NTFS and uses a fixed VDI so the first power-on does not hang on `\\wsl.localhost`.

### VirtualBox text clipboard (no Guest Additions)

Host/guest copy-paste of Unicode text over COM1 (`\\.\pipe\R5OS-clip`). Guest `r5_vbox_clip` opens `pc_serial1` and quits if the port never appears. Drag-and-drop is still off: that is a Guest Additions protocol. Needs a jammed image with the overlay.

### VirtualBox bridged LAN (no Guest Additions)

`scripts/run-vbox-r5.ps1` boots the raw image as IDE + Intel PRO/1000 on a bridged NIC. No vboxsf, no auto-resize. USB stick: Rufus DD of the same image (`docs/vbox.md`).

### AGENTS.md vs roadmap

`AGENTS.md` is the short rule file (git, overlays, Deskbar/launch, QEMU limits). Product backlog (Hub, Store, remaining network items, arches) lives in `docs/ROADMAP.md` so agents do not implement named products during a focused fix.

### Wi-Fi: one screen, remember, Deskbar tray

NetworkStatus opens a Wi-Fi window (scan, password, connect) and the same from the tray menu. Successful joins are stored as persistent networks. After link loss (sleep), autoconfig may auto-join again (`fJoiningNetwork` is reset). NetworkStatus `--deskbar` starts with the desktop session. Tooltip shows SSID, “Connecting…”, or “No network”.

### AirPlay replicant Instantiate was stripped

Log: `instantiate_deskbar_item` OK, `AddItem` OK (id 999), maar `HasItem=0` en geen `AttachedToWindow`. Deskbar archiveert de view en maakt hem opnieuw via `Class::Instantiate`. Die functie stond inline in de class en zat niet in de binary (`nm` toonde alleen `instantiate_deskbar_item`). Out-of-line `Instantiate` + juiste `class`/`add_on` in het archive.

### AirPlay tray-debug

`airplay_deskbar` als launch **job** zonder `on`-event startte waarschijnlijk nooit — vandaar geen proces. Terug als **service** (blijft draaien). `airplay_server` probeert zelf `AddItem` op `/system/servers/airplay_deskbar`. Elke stap naar `/boot/home/airplay_deskbar.log`.

### AirPlay Deskbar-replicant (PowerStatus-pad)

Echte tray-replicant: `airplay_deskbar` exporteert `instantiate_deskbar_item`, belt `BDeskbar::AddItem` op de eigen binary, en stopt daarna. launch_daemon start hem als **job** (niet als service), anders komt hij na `Quit()` meteen weer op.

Waarom het eerder misging (niet de API):

1. `AddItem` op `airplay_server` — Deskbar laadt die binary in z’n eigen team (`load_add_on`). OpenSSL + Media Kit + globale player/sender horen daar niet.
2. Helper als **service** die na install `Quit()` deed — services worden herstart; vandaar het flikkeren in ProcessController.
3. Helper die bleef draaien — PowerStatus sluit af; de replicant blijft in Deskbar achter.
4. Zwevend venster als noodoplossing — zichtbaar, maar niet in de tray-shelf.

### AirPlay 1 ontvangen / zenden

`airplay_server` (achtergrond, `/system/servers`): RAOP/AirPlay 1, mDNS `_raop._tcp` / `_airplay._tcp`. AirPlay 2 alleen advertentie + `/info`. Sender: mixer-tap naar RAOP-speakers (QEMU-usernet ziet geen LAN-multicast).
