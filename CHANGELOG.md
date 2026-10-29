# R5OS changelog (lokaal)

Bijhouden in deze overlay-repo. Geen Haiku-hrev. Nieuwste eerst.

## 2026-10-29 (Europe/Amsterdam)

### Sign in as that person (build 10008)

The session file is the account whose password just worked, not always the first administrator. `r5-desktop` already drops to that name before Tracker and Deskbar. A standard account’s home is `/boot/home/users/<name>` (`create_home` on sign-in). `user` still gets `/boot/home`. Log out deletes the session file.

This does **not** call `BLaunchRoster::StartSession`. That spawns another `launch_daemon <name>`, which reads `data/launch/user` and starts a second `app_server`. Removing autologin instead left the boot splash. Autologin stays. Desktop jobs stay off `initial_volumes_mounted`. One shared screen.

`/boot/trash` is chown’d only for the `/boot/home` account. A standard user’s Trash may not work. Preferences are not hidden by role. Not a second graphical session.

Jam AMD64 2026-10-29 19:53 Europe/Amsterdam: `jam_exit=0` (Login, `r5-desktop`, What’s new, image). VBox `-ForceConvert` started.

VBox 19:55 Europe/Amsterdam (guest clock 6:55 PM, UTC): Sign in as `nicky` reached a blue desktop. Terminal `id` was `uid=1002(nicky) gid=100(users) groups=100(users)`. `ls /boot/home/Desktop` was permission denied. `ls` in `nicky`’s folder showed Desktop, Documents, and the rest. `echo $HOME` was still `/boot/home`, and the shell reported `sh: /boot/home/config/settings/profile: Permission denied`. The desktop showed the volume icon only: no Trash, Home, Quick Tour, or User Guide. Sign in as `user` afterwards: `uid=1001(user)`, `$HOME` `/boot/home`, `ls /boot/home/users/nicky` permission denied. A Deskbar crash dialog sat over Sign in (`/system/Deskbar`).

`/bin/sh` sources `SetupEnvironment`, which forced `HOME=/boot/home` for every account. Trash is `find_directory(B_TRASH_DIRECTORY)`, which was `/boot/trash` mode `0700` for the administrator, so Tracker drew no Trash pose. Home, Quick Tour, and User Guide are symlinks that the image only puts in `/boot/home/Desktop`. Those three are now created in each account’s Desktop. Trash is `$h/trash` (the old `/boot/trash` moves to `/boot/home/trash` once). Deskbar no longer calls through a missing settings file on quit.

Jam AMD64 2026-10-29 20:14 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Not confirmed on this image yet.

### First administrator off uid 0 (build 10007)

Login (still uid 0) rewrites `/etc/passwd` so the `/boot/home` account is the first free uid ≥ 1000 (sshd keeps 1000), primary group `users`, member of `admin`. It `chown`s `/boot/home` except `/boot/home/users/<other>`. Desktop jobs start through `/system/apps/r5-desktop`, which drops to that uid before exec. Not `StartSession`. Sign in as a standard user still opens this administrator desktop.

Users & Accounts talks to Login for add/disable/delete (preflet euid is no longer 0). The `/boot/home` account cannot be deleted or disabled. Last-admin check stays.

Jam AMD64 2026-10-29 15:58 Europe/Amsterdam: `jam_exit=0` (Login, Users, `r5-desktop`, image).

VBox first boot: Sign in showed **General system error**. Login had rewritten `/etc/passwd` on disk; `getpwnam` still talks to the registrar, which had not changed uid, so migrate returned `B_ERROR`. That rewrite also left **no uid 0 account**. Restart then stuck on the black bootsplash. Registrar cannot change an existing uid (`B_BAD_VALUE`); useradd uses `B_REG_UPDATE_USER` + `add user`. Migrate now deletes `user` and adds it back at a free uid ≥ 1000, and adds a locked `root` uid 0 so boot still has a root account. Session file is `/tmp/r5-desktop-session`. Migrate runs only after a valid password, not when Sign in appears.

Jam AMD64 2026-10-29 16:20 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started a fresh VDI.

VBox after that Sign in: password accepted, then a **empty blue screen** (app_server, no Tracker/Deskbar). Target("desktop") returned OK but the wrapped jobs did not come up. Login now launches Tracker and Deskbar itself through `r5-desktop` (no libbe; session file under `/boot/system/settings`) and waits for Tracker before hiding Sign in. launch_daemon does not start those two as uid 0.

Jam AMD64 2026-10-29 16:29 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox Sign in still empty blue, a bit longer wait. Tracker registered (`IsRunning`) but its windows were on a **second app_server desktop** for uid 1001; the screen stays on uid 0 (Login). `AS_GET_DESKTOP` now reuses the existing screen until `StartSession`. Login starts Tracker/Deskbar with `load_image` (not `BRoster::Launch`, which would reuse one r5-desktop team) and quits the Sign in wallpaper window.

Jam AMD64 2026-10-29 16:36 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Shared app_server desktop after Sign in not confirmed yet.

VBox after that Sign in: desktop icons appeared (R5OS, home, Quick Tour, User Guide, Trash) at 15:38 Europe/Amsterdam. Deskbar listed **four `r5-desktop` teams**. The mouse cursor sat as a hand and did not move. Haiku `exec` keeps the team name of the helper, so Tracker, Deskbar, NetworkStatus and the clip/AirPlay tray all showed as `r5-desktop`. `r5-desktop` now `load_image`s the real binary and exits. launch_daemon jobs are not wrapped with it (a service whose team exits is restarted forever). Login still starts Tracker, Deskbar, NetworkStatus, `airplay_deskbar`, and `r5_vbox_clip` through the helper after Target("desktop"), and stops those launch jobs so Target does not also spawn uid-0 copies.

Jam AMD64 2026-10-29 16:49 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 16:51 Europe/Amsterdam (guest clock 3:51 PM, UTC): desktop icons, mouse, Deskbar names **Tracker / AboutSystem / Terminal** (not four `r5-desktop`). Terminal `id` was **`uid=0(root)`** `gid=0(root)` `groups=0(root),101(party)`. Migrate did add `root` at uid 0, but Tracker came from launch_daemon as uid 0. `GetJobs("desktop")` returns `B_NAME_NOT_FOUND` without a session daemon (`StartSession` is not used), so Login never disabled Be-TRAK/Be-TSKB. Target then starts Tracker as root; the helper’s second Tracker is a single-instance no-op. Login now stops those jobs by hardcoded name and waits until uid-0 Tracker is gone before `r5-desktop` starts the session copy.

Jam AMD64 2026-10-29 16:59 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:01 Europe/Amsterdam: Terminal `id` still `uid=0(root)`. `/system/var/log/syslog` had no `r5-desktop` / Login lines: the helper only syslog()s on error, exits in milliseconds, and does not call `openlog`. Target("desktop") was still starting Tracker as uid 0 *before* the helper, so the root copy won. Login now starts Tracker/Deskbar through `r5-desktop` first, refuses a uid-0 Tracker team, then Target. Session trace goes to `/tmp/r5-session.log` and `/system/var/log/r5-session.log`.

Jam AMD64 2026-10-29 17:07 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:14 Europe/Amsterdam (guest clock 4:09 PM, UTC): Terminal `id` is **`uid=1001(user)`** `gid=100(users)` `groups=100(users),103(admin)`. Deskbar shows Tracker and Terminal (not `r5-desktop`). Mouse works. Session log is `/tmp/r5-session.log` (lowercase), not next to syslog; `/system/var/log/` is packagefs and that copy often cannot be created. Isolation of another account’s home was not tested on this first-boot VDI (`nicky` is not an image account).

VBox 17:16: `/tmp/r5-session.log` shows Tracker team 256 and Deskbar team 258 at uid 1001, then NetworkStatus/AirPlay/clip, `sign-in done: No error`. Deskbar was missing ProcessController, PowerStatus, and the volume replicant: those are installed by `first-login` as uid 0 (`default_deskbar_items.sh`), and `BDeskbar::AddItem` does not land in a uid-1001 Deskbar. Login now starts ProcessController `-deskbar`, PowerStatus `--deskbar`, and `desklink --add-volume` through `r5-desktop` after Deskbar is up.

Jam AMD64 2026-10-29 17:21 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:24 Europe/Amsterdam: tray replicants looked right. `/boot/home` listed `config`, `Desktop`, `mail`, `users` (correct first-home layout; Documents/Pictures are not created for that account). Trash opened as `/boot/trash` (Haiku’s trash path). Log out did not return to Sign in: `StopTarget("desktop")` ran while Tracker/Deskbar were not launch_daemon teams, then **launch_daemon user** crashed. ProcessController showed **Your setting file could not be saved (Initialization failed)** on that teardown. Debugger attached to that uid-0 daemon and had no windows on the uid-1001 Deskbar. Logout now quits Tracker/Deskbar first, shows Sign in, then StopTarget. `/boot/trash` is chown’d to the session user so Tracker can treat it as trash.

Jam AMD64 2026-10-29 17:30 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 17:34 Europe/Amsterdam: Trash icon is correct. Log out reached Sign in slowly, showed **Damned! / Initialization failed**, then the next Sign in left the controls disabled (gray). ProcessController `-deskbar` still runs `ReadyToRun`, which opens a window; quitting that window saves `ProcessController Prefs` and the alert is `B_NO_INIT` when `config/settings` cannot be created. Logout then called `StopTarget("desktop")`, which took down the user launch_daemon, so the next `Target("desktop")` never replied and Sign in stayed disabled. Logout now only disables those jobs and quits Tracker/Deskbar (no `StopTarget`). Replicants are installed by `r5-tray` (`AddItem` only). `/boot/home` gets Documents, Downloads, Music, Pictures, Videos, and `config/settings` for the administrator too (the image only shipped Desktop/config/mail; extra accounts already had the full set).

Jam AMD64 2026-10-29 17:41 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Log out then Sign in again not confirmed yet.

VBox 17:43 Europe/Amsterdam (guest clock 4:43 PM, UTC): Documents, Downloads, Music, Pictures, and Videos are in `/boot/home`. Trash icon is correct. **launch_daemon user** still crashed (`autologin` → `StartSession("user")` while that account was uid 0; the daemon stays uid 0). Choosing Debug put **Debugger** in the Deskbar with **No windows**. Log out after confirm did nothing: `Stop`/`SetEnabled` wait for a reply from that daemon, and a team stopped in the debugger never replies, so Sign in was never shown.

Log out no longer talks to the launch daemon. It quits Tracker, Deskbar, and the session tray apps, then shows Sign in (`kill_team` if quit is ignored). Sign in does not call `Target("desktop")`. Jobs inside `target desktop` no longer use `on initial_volumes_mounted`, so that uid-0 daemon does not start Tracker beside `r5-desktop`. The Debugger team window is shown as soon as it exists (`Hide()` before the first `Show()` left the show level at 1, which Deskbar skips). `user-bootscript`, `dstcheck`, and `first-login` in that target do not run. Not a second account. Not StartSession.

Jam AMD64 2026-10-29 18:10 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 18:14 Europe/Amsterdam (guest clock 5:14 PM, UTC): **Log out** returned to Sign in. No ProcessController **Damned** / Initialization failed. No **launch_daemon user** crash dialog. **Restart** came back to the desktop. Debugger opened a **Teams** window (kernel_team, launch_daemon, Login, Tracker, Deskbar, Debugger; Deskbar listed Debugger with that window, not “No windows”). Team list also has **launch_daemon root** (autologin `StartSession` after migrate names uid 0 `root`). Attaching from a crash dialog was not in this shot (there was no crash). Extra-home isolation still untested.

VBox after that Sign in: `ls /boot/home/users/nick` as the administrator is **Permission denied**. Isolation of that extra home holds on this VDI (uid 1001 cannot list another person’s folder).

After Deskbar is up, Login starts `dstcheck`, `~/config/settings/boot/UserBootscript` if that file exists, and first-login `PostInstallScript` if `~/config/settings/first_login` exists. `default_deskbar_items.sh` does not launch ProcessController `-deskbar` (that was Damned). Volume and tray replicants stay `r5-tray` / `desklink`.

Jam AMD64 2026-10-29 18:26 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started. Boot stayed on the **R5OS splash**: removing `x-vnd.Haiku-autologin` meant no user `launch_daemon`, so `app_server` and Login (in `data/launch/user`) never started. Autologin is back. It still `StartSession`s `getpwuid(0)` (`user` on first boot, `root` after migrate). Desktop jobs stay off `initial_volumes_mounted` so that daemon does not start Tracker. Not StartSession as the product path.

Jam AMD64 2026-10-29 18:33 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 18:34 Europe/Amsterdam (guest clock 5:34 PM, UTC): boot left the splash, Sign in reached the desktop. Debugger **Teams** lists `launch_daemon`, **`launch_daemon user`**, `app_server`, Login, Tracker, Deskbar, Debugger (no crash dialog, not “No windows”). `launch_daemon user` is autologin `StartSession` of uid 0 while that account was still named `user`. Not `launch_daemon root` on this first-boot VDI. Isolation and second Sign in were not rechecked on this disk.

### First administrator off uid 0 (plan)

Documented in `docs/MULTIUSER.md` and `AGENTS.md`. Product people: administrator `user` at `/boot/home`, optional standard users under `/boot/home/users/<name>`. Test names such as `nicky` are VBox fixtures, not image accounts. Next code: unused uid ≥ 1000 (not sshd 1000), `chown` of the first home, desktop jobs as that uid, Users & Accounts via an admin helper. No new overlays. Not `StartSession`. Not implemented yet.

### Joysticks on the image

Deskbar → Preferences includes Joysticks (`SYSTEM_PREFERENCES`). Settings dir is `find_directory(B_USER_SETTINGS_DIRECTORY)/joysticks`. Without a game port the preflet should still open.

VBox 2026-10-29 15:26 Europe/Amsterdam: Joysticks was in Preferences. As `user`, `id` was uid 0 and `$HOME` was `/boot/home`. `su nicky` showed uid 1001, `HOME=/boot/home/users/nicky`, and that home listed `config Desktop Documents Downloads Music Pictures Videos`. `su nicky -c 'ls /boot/home/Desktop'` — permission denied. Sign in is still one session.

### Per-user settings audit

Appearance, Backgrounds, Deskbar, Tracker, and app_server desktop settings already call `find_directory(B_USER_* )`. For uid 0 that still maps to `/boot/home` (`get_user_home_path`). Extra accounts would get their own `config/settings` only after a real session switch. The AirPlay deskbar log no longer writes `/boot/home/airplay_deskbar.log`. Not a second desktop.

Bundled apps (second pass): Mail, Terminal, Screenshot, ShowImage, Clock, DeskCalc, HaikuDepot, People, ProcessController, Workspaces and most other image apps already use `find_directory`. MidiPlayer, Joysticks, and PoorMan now live as native copies under `distro/user-paths` (apply copies files; not a string patch). WebPositive stays the image `webpositive.hpkg`. Preflet split accepted: personal vs machine (Network, Screen, Time). No second desktop.

Jam AMD64 2026-10-29 15:01 Europe/Amsterdam: `jam_exit=0` with those MidiPlayer/Joysticks/PoorMan sources in the tree (`haiku.hpkg` rebuilt). VBox 15:12: MidiPlayer wrote `/boot/home/config/settings/MidiPlayerSettings` (0-byte file with attributes; Tracker cannot open it as a document). PoorMan `public_html` existed. Not a second desktop.

### Private home folders (build 10006)

`/boot/home` is `0711` so other accounts can reach `/boot/home/users/<name>` but not list the first account’s files. Names directly under `/boot/home` except `users` are `0700` (directories) or `0600` (files). Each extra home is `0700`. Login applies this before Sign in; creating an account does too.

The first account `user` is still uid 0. Mode bits do not stop that account from reading other homes. Sign in is still one shared session.

VBox 2026-10-29 14:30 Europe/Amsterdam: `ls -ld` showed `/boot/home` `drwx--x--x`, Desktop `drwx------`, `users` `drwxr-xr-x`. `su nicky -c 'ls /boot/home/Desktop'` — permission denied. `su nicky -c 'ls /boot/home/users/nicky'` listed that home. As uid 0 `user`, listing nicky’s home still worked.

### Log out (build 10005)

Deskbar leaf menu **Log out** (confirm). Login stays running after Sign in (hidden). Logout quits session apps, `StopTarget("desktop")` so Tracker/Deskbar are disabled instead of relaunched, then Sign in is shown again. Next Sign in `SetEnabled` on desktop jobs then `Target("desktop")`. Same uid/home as before. Not registrar `ShutDown`.

VBox 2026-10-29 13:46 Europe/Amsterdam: Login appeared in the Deskbar running-apps list (`B_BACKGROUND_APP` was missing). Log out crashed Login: `PrepareForSignIn` called `BTextControl::SetText` from the app looper (`Looper must be locked`). net_server also crashed; logout now skips `B_BACKGROUND_APP` and `/servers/` teams and locks the Sign in windows before Show/SetText.

VBox 2026-10-29 13:54 Europe/Amsterdam: Log out no longer crashed, but the screen was only the Login desktop wallpaper (blue, no Sign in card). Showing the hidden modal again left it behind/unlaid-out; `Activate()` on the desktop-feel window made that worse. Log out now rebuilds the Sign in window the same way as first boot.

VBox 2026-10-29 14:01 Europe/Amsterdam: Log out returned to Sign in. Login is not a Deskbar team. Same session after the next Sign in. Isolation is still not done.

VBox 2026-10-29 14:09 Europe/Amsterdam: after opening Installer from Applications, Log out showed FirstBootPrompt (Try R5OS). Installer `Quit()` relaunches that prompt when Deskbar is not running (live cancel-install path). Log out stops Deskbar first. Installer now skips that launch if Sign in is running.

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
