# R5OS changelog (lokaal)

Bijhouden in deze overlay-repo. Geen Haiku-hrev. Nieuwste eerst.

## 2026-10-29 (Europe/Amsterdam)

### Log out (build 10005)

Deskbar leaf menu **Log out** (confirm). Login stays running after Sign in (hidden). Logout quits session apps, `StopTarget("desktop")` so Tracker/Deskbar are disabled instead of relaunched, then Sign in is shown again. Next Sign in `SetEnabled` on desktop jobs then `Target("desktop")`. Same uid/home as before. Not registrar `ShutDown`.

VBox 2026-10-29 13:46 Europe/Amsterdam: Login appeared in the Deskbar running-apps list (`B_BACKGROUND_APP` was missing). Log out crashed Login: `PrepareForSignIn` called `BTextControl::SetText` from the app looper (`Looper must be locked`). net_server also crashed; logout now skips `B_BACKGROUND_APP` and `/servers/` teams and locks the Sign in windows before Show/SetText.

VBox 2026-10-29 13:54 Europe/Amsterdam: Log out no longer crashed, but the screen was only the Login desktop wallpaper (blue, no Sign in card). Showing the hidden modal again left it behind/unlaid-out; `Activate()` on the desktop-feel window made that worse. Log out now rebuilds the Sign in window the same way as first boot.

VBox 2026-10-29 14:01 Europe/Amsterdam: Log out returned to Sign in. Login is not a Deskbar team. Same session after the next Sign in. Isolation is still not done.

### Sign-in screen (build 10004)

Haiku Login is the boot path instead of auto-starting Tracker/Deskbar. Native widgets: person in the middle (list hidden when there is only one), password under the name, Sign in, clock plus Restart/Shut down at the bottom. After a valid password, `BLaunchRoster().Target("desktop")` starts the existing user session. `StartSession` for another account is not used yet (second app_server). Empty password is allowed when shadow is empty. Disabled accounts (`!`) are omitted.

VBox 2026-10-29 12:54 Europe/Amsterdam: live image after Restart was a white empty screen (PrintScreen still opened Screenshot). Login used `kDesktopWindowLook` plus layout, so the card did not draw. It is a titled modal window again, with the Haiku desktop window behind it. "Try R5OS" now starts `login`, not `desktop`.

VBox 2026-10-29 13:01 Europe/Amsterdam: Sign in appeared as a yellow tab over a white empty client. The window had no layout, so `LoginView()` (nameless `BView`, frame 0) never got a size. The window now owns the layout and the card has no expanding glue.

VBox 2026-10-29 13:07 Europe/Amsterdam: the form showed `R5OS` / `user`. Sign in printed `Name not found`: password was accepted, then `BLaunchRoster().Start("desktop")` looked up a job named desktop (`B_NAME_NOT_FOUND`). FirstBootPrompt uses `Target("desktop")`; Login now does too. Welcome copy: “Welcome to R5OS” / “Please login with your credentials”.

VBox 2026-10-29 13:16 Europe/Amsterdam: Sign in reached the desktop. Restart powered the VM off: `reboot` stayed false on `kRebootAction`. Restart now calls `ShutDown(true, …)`. The gecos line `R5OS` above `user` is removed; Welcome is enough.

VBox 2026-10-29 13:22 Europe/Amsterdam: Restart works.

The login name `user` had disappeared with the gecos line. It is shown again as `pw_name` (not display name R5OS). Jam 13:26 Europe/Amsterdam; VBox `-ForceConvert` 13:27.

VBox 2026-10-29 13:27 Europe/Amsterdam: live Try, one account — Welcome, hint, **user**, Password, Sign in, Restart, Shut down. 13:28: with `nicky` added, the list shows `user` and `nicky`; the selected login stays above Password. Restart was already confirmed. Logout and isolation are still not done.

### Multi-user foundations (build 10002)

R5OS is still a single shared desktop (`user`, `/boot/home`, often UID 0). Haiku already has `passwd`/`shadow`, `useradd`, and `src/apps/login` (`StartSession`); none of that is the boot path. First step: document the model (`docs/MULTIUSER.md`), put **Users & Accounts** in Preferences as a **read-only** `getpwent` list, bump **build 10002**, and stop hardcoding `10001` in `wsl-install-branding.sh`. Extra homes will be `/boot/home/users/<name>`; the first account keeps `/boot/home`. Login, isolation, and account create/delete are not in this build.

VBox 2026-10-29: the preflet listed `user` and **sshd** (home under `/packages`). Only accounts whose home is `/boot/home` or `/boot/home/…` are shown. About showed Build 10002 but What’s new started at 10001: Jam did not rebuild `AboutSystem.rdef` when `r5_whatsnew.txt` changed. Copy now touches the rdef and the Jamfile depends on the notes file.

### Users & Accounts add/disable/delete (build 10003)

Add / Disable / Enable / Delete in the preflet. Haiku `useradd` starts UIDs at 1000 and skips taken ids (sshd is 1000, so the first extra person is usually 1001). New homes are `/boot/home/users/<name>` with Desktop/Documents/…. Administrators go in group `admin`. UID 0 `user` cannot be deleted or disabled; the last enabled administrator cannot be removed. Empty password uses `passwd -d` (not on argv). Non-empty uses `crypt` + registrar. Disable prefixes `!` on the shadow hash so Enable can restore it. Still one shared desktop. Login as the new person is out of scope.

VBox 2026-10-29, Europe/Amsterdam (five tests from `docs/MULTIUSER.md`):

1. Open Users & Accounts — `user` uid 0 `/boot/home` administrator (R5OS); `sshd` hidden.
2. Add a standard user — `nicky` uid 1001 `/boot/home/users/nicky`. Also `nicky2`/`nicky3` uid 1002/1003 administrators; Tracker showed the three folders.
3. Disable a non-admin — not captured in the screenshots.
4. Delete a non-admin — list left as `user` and `nicky3`.
5. UID 0 refuse delete/disable — Disable/Delete stay inactive unless a removable account is selected; `user` was not removed.

The intro line first made the window wider than the screen (`BStringView` + `B_AUTO_UPDATE_SIZE_LIMITS`). It now wraps. Add-account labels are still uneven. The preflet icon is the Haiku Login/Tracker people vector.

### Shutdown hung after IPv6 SLAAC

Guest shutdown stuck again, same class of hang as before DAD used `select()`. SLAAC now listens for extra RAs for the whole session, so `~SLAACClient` did `wait_for_thread` forever if `shutdown()` did not wake the raw ICMPv6 `select()`, or if an RA with RDNSS did `SendMessage(..., reply)` while net_server was already quitting. Teardown now shuts the socket, waits at most 2s, then `kill_thread`. Resolver updates are fire-and-forget. DHCPv6 uses the same stop path. Not VBox-tested yet.

VBox still hung after that: `r5_vbox_clip` blocked in `WaitForInput` on COM1 with `B_INFINITE_TIMEOUT`, then `~ClipApp` did `kill_thread` + unbounded `wait_for_thread` (a kernel wait often does not die). Close the port, poll 250ms, never wait forever. AirPlay tray was a **service** that kept pulsing `BDeskbar::AddItem`; that is a user **job** that `Quit()`s after install (PowerStatus). net_server workers no longer `wait_for_thread` after `kill_thread`.

### IPv6 default route and dual-stack DNS

SLAAC put a ULA on the NIC but the default route used the router’s `fe80::` **without** `sin6_scope_id`, so apps could not send off-link IPv6. The route now copies the RA source sockaddr and fills the interface index. Syslog: `default route via fe80:…%N` and `dad … unique`.

VBox syslog 2026-10-29: DAD unique and ULA assigned, but no `default route` line. An RA with prefix A=1/L=1 can still have Router Lifetime 0 (not a default router per RFC 4861). If that RA also offered a SLAAC `/64`, R5OS still uses the advertiser as default router and logs `RA lifetime 0`.

Several LAN hosts re-advertise the same ULA `/64` with lifetime 0. Each one used to replace the IPv6 default route. Only the first such advertiser (or a later RA with lifetime > 0) keeps the gateway.

`ping -6 ipv6.google.com` resolved AAAA (`2a00:1450:…`) but sent from `fe80::` (the first IPv6 address on the NIC). Google will not answer a link-local source. Outgoing IPv6 now prefers the ULA/GUA when the destination is off-link (RFC 6724).

Ping to the machine’s own ULA also lost all packets: `B_SOCKET_ADD_ALIAS` never installed the IPv6 `RTF_LOCAL` host route (IPv4 DHCP uses `SIOCSIFADDR` and does). Packets to `fdae:…` followed `::/0` off-link. Alias add now calls `AddDefaultRoutes`.

IPv6 route lookup masked the destination into a `sockaddr` (16 bytes). `ipv6_mask_address` writes a `sockaddr_in6` (28 bytes), so a `/64` never matched and everything used `::/0`. Lookup now uses `sockaddr_storage`.

After that, `route` showed the ULA with `HL`, but `ping` still used `fe80` as source: an IPv6 default route’s all-zero mask had `first_mask_bit` 0, same as a host route, so `::/0` matched first. All-zero IPv6 masks now rank as 128 (least specific), like IPv4.

VBox 2026-10-29 after that jam: `::/0` was last, but `ping -6` to the guest ULA still printed `fe80` and lost every packet. UDP `connect` binds the source to `route->interface_address` (`::/0` is still the fe80 alias). Self-ping then followed the gateway instead of `RTF_LOCAL`. Connect now uses the dest if it is local, otherwise a ULA/GUA; sending a packet to a local IPv6 address loops back; `SIOCADDRT` binds `::/0` to the ULA when one exists.

Next VBox: source was the ULA, still 0 replies. ICMPv6 echo copied IPv4 but checksummed the request instead of the reply and fell through without `break`. `ping -6` also discarded replies that lacked `IPV6_PKTINFO`/`HOPLIMIT` cmsg. Echo reply matches IPv4; ping counts replies without those cmsgs. Two `::/0` lines were the fe80 default plus a ULA default; remove now matches any default on the same NIC, and SLAAC adds `::/0` after the ULA.

VBox bridged 2026-10-29 11:07 Europe/Amsterdam: `ping -6 -c 3` to the guest ULA (`fdae:aeb4:a752:9ae4:a00:27ff:fe44:61dd`) — 3/3 received, 0% loss, source and dest both ULA. Internet IPv6 (Google) was not retested; ULA is still not a GUA.

RDNSS from an RA no longer replaces `resolv.conf` (that wiped IPv4 DHCP nameservers). Existing nameservers are kept and new ones are appended. Extra RAs still refresh the route and DNS.

Host DAD packet helpers: `scripts/test_ipv6_dad.cpp`. Outbound `ping`/`wget` over IPv6 was not run in this change. DHCPv6 IA_NA still needs a LAN with a DHCPv6 server. Wi-Fi + IPv6 on the Envy was not tested.

## 2026-10-28 (Europe/Amsterdam)

### About R5OS: build number and What's new

About shows `R5OS 2026.1-dev Build 10001`. A What's new tab sits next to Credits. Text comes from `distro/WHATSNEW` (no dates; heading `R5OS <version> Build <n> - Title`). Bump `distro/BUILD` when adding a section, not on every jam. Credits still auto-scroll; What's new does not.

The R5OS wordmark is drawn in the same box as Walter (`170x110`). The PNG stays 420px wide for the installer; About was showing it 1:1 and looked oversized.

### IPv6 SLAAC in net_server

On link-up, net_server assigns IPv6 link-local, SLAAC (RS/RA `/64`, default route, RDNSS), DAD before using an address, and DHCPv6 (IA_NA + DNS, or Information-Request). VirtualBox bridged LAN got a ULA via SLAAC.

DAD Neighbor Solicit used to fail with “Network is unreachable” and then **block** the second IPv6 address, so Network only showed `fe80::`. DAD stays RFC 4862 (NS from `::`, no SLLA) and fail-closed: send/socket errors do not assign the address. Only a real duplicate NA/NS also refuses it.

Network → NIC → IPv6 already has Mode Automatic / Static / Disabled. Automatic is labeled SLAAC/DHCPv6; Disabled is honored per family. Test: `docs/ipv6.md` (`grep -E 'slaac|dad|dhcpv6' /var/log/syslog`). A RA without A-flag `/64` cannot invent a ULA; DHCPv6 may still run.

IPv6 aliases were listed twice: IPv4 DHCP reconfigure started a second AutoconfigLooper, and `FindAddress` missed existing IPv6 slots. One looper per NIC; add address only if the `in6_addr` is not already present.

DAD follows RFC 4862: NS from `::` with no Source Link-Layer option. Haiku `IPV6_MULTICAST_IF` takes an interface `in6_addr`, not an ifindex, and still cannot send multicast without a unicast route. Socket/bind/setsockopt/send failures block assigning the address. `select()` is used instead of a blocking `recvfrom` so shutdown can finish. Host tests: `scripts/test_ipv6_dad.cpp`.

SLAAC must not DAD the link-local before it exists (`IPV6_MULTICAST_IF` needs an address on the NIC). That aborted SLAAC, so no ULA, ~10s DHCPv6 wait, and shutdown could stick in `select`. Link-local is added without DAD (NetServer already does); DAD stays on the SLAAC/DHCPv6 address. Destructor `shutdown()`s the socket so the thread can exit.

Haiku `IPV6_MULTICAST_IF` looked up the interface with an uninitialized `sockaddr_in6` (`sa_family` 0 → `EADDRNOTAVAIL`). After setting `sin6_family`/`sin6_len`, RS/DAD/DHCPv6 still logged `Network is unreachable`: IPv6 `get_route` has no unicast route for multicast. Kernel IPv6 multicast uses the interface `direct_route` (`get_device_route`) from `IPV6_MULTICAST_IF`, `SO_BINDTODEVICE`, or `sin6_scope_id`, then `ipv6_send_routed_data` (IPv6 header + checksum). Calling datalink `send_routed_data` directly skipped that and broke RS/DAD again (`ENETUNREACH`). Userspace SLAAC/DAD/DHCPv6 still bind the socket to the NIC. DAD stays fail-closed. Shutdown no longer hangs (`select` + socket `shutdown`).

A new VirtualBox VDI is a virgin disk: `package_daemon` first-boot on IDE often takes several minutes (QEMU virtio of an already-booted image is ~seconds). Resetting that boot leaves IPv6/packages half-done. Wait for the desktop. Syslog `ata 0-0 error: invalid target lun 2` … `lun 7` and `target device not present` is VirtualBox probing the empty IDE slave/secondary; a **Reset is ~5s with the same ATA lines**, so that probe is not the multi-minute first boot.

mDNS announces `_raop._tcp` / `_airplay._tcp` on IPv6 `ff02::fb` per NIC (`sin6_scope_id`). That does not make iPhone Control Center show an AirPlay 2 speaker (pairing/PTP still absent).

VirtualBox copies the raw image onto NTFS and uses a fixed VDI. The VDI is reused unless the raw image is newer (`-ForceConvert` rebuilds). Converting on every start made a virgin disk each time, so `package_daemon` ran first-boot processing and the guest looked hung until Reset. A **new** VDI first boot can take several minutes; a guest Restart is fast because packages are already processed. Do not Reset during first-boot.

VBox bridged LAN (2026-10-28, Europe/Amsterdam): after `ipv6_send_routed_data`, syslog `fdae:… via fe80:…`, PIO `A=1`, extra RA `already assigned`, `dhcpv6: no server`, Network shows `fe80::` and `fdae:aeb4:a752:9ae4:a00:27ff:fe44:61dd`. Not a DHCPv6-server LAN. No `mdns: IPv6 announce` error in the snippet. QEMU usernet was not used. DAD NS/NA was not packet-captured.

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
