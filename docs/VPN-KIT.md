# VPN Kit

Design, not built. This is how R5 adds VPN connections, so that anyone can write support for a VPN protocol or a VPN service as an add-on. WireGuard is the first protocol. A small example provider shows the account side. Roadmap: Network, item 6 (WireGuard).

## What it is

- C++ classes in BeOS style, in namespace `BVPNKit`, the same way as `BPackageKit` and `BAppearanceKit`. One class per header, named after the class (`<VPNProvider.h>`).
- Library `libr5vpn.so`, source `distro/vpn`, MIT. Any program or add-on may link it. A Haiku program that does not link it is unchanged.
- A system server, `vpn_server` (`application/x-vnd.R5-vpn_server`), that owns the tunnels, the routes, and the DNS while a VPN is up.
- A page **VPN** under Network in Settings, after Connections and Discovery.
- The separate Haiku Network preferences stay installed.

## What Haiku already has

Use these. Do not write a second one (`AGENTS.md`, LibR5).

| Part | Where | Use in R5 |
|---|---|---|
| tun/tap device | `src/add-ons/kernel/network/devices/tunnel` (`tun/N`, `tap/N`) | The tunnel interface. No new kernel driver |
| Network settings add-ons | `headers/os/add-ons/network_settings/NetworkSettingsAddOn.h`, namespace `BNetworkKit` | The pattern for an add-on that brings its own list item and `BView`. There is already a type `B_NETWORK_SETTINGS_TYPE_VPN` |
| VPN add-on stub | `src/add-ons/network_settings/vpn` ("My Awesome VPN") | Shows that Haiku meant VPN to be an add-on. It is empty |
| Key store | `BKeyStore`, `BPasswordKey` (`headers/os/app/KeyStore.h`), `keystore_server` | Where tokens and private keys are stored, with the existing "may this application use this key?" window |
| URL handlers | `application/x-vnd.Be.URL.<scheme>` | A `r5vpn:` link can open the provider add-on after a browser login (later) |
| Messaging | `BMessage`, `BMessenger` | All traffic between the page, the add-ons and `vpn_server` |

### Gap: the key store is not encrypted

`Keyring::_EncryptToFlatBuffer()` has `// TODO: Actually encrypt the flat buffer...`. The keyrings are written in plain text to `~/config/settings/system/keystore/keystore_database`. The only protection is the 0700 home. Uid 0 can read it.

Fix this before the VPN Kit ships secrets: encrypt the keyring with a key from the sign-in password, unlocked by Login at Sign in. That also helps Wi-Fi passwords and mail. It is its own change, in `distro/` as a copy of `keystore_server` with SPDX-Snippet markings. Until then, the VPN page says a token is as safe as the home folder, no more.

## Two kinds of add-ons

| Kind | Base class | Knows | Runs in | Installed in |
|---|---|---|---|---|
| Protocol | `BVPNKit::BVPNProtocol` | How to make a tunnel: WireGuard, later OpenVPN | `vpn_server` (uid 0) | `system/add-ons/vpn/protocols` only |
| Provider | `BVPNKit::BVPNProvider` | An account: login, server list, countries. Produces a tunnel configuration | Its own process, as the signed-in person | `system/add-ons/vpn/providers` and `~/config/non-packaged/add-ons/vpn/providers` |

A provider never touches the tunnel, routes, or DNS. It asks its service for a configuration ("WireGuard, server nl-123") and hands that to `vpn_server` as a `BVPNTunnelConfig`. So a company writes only the account side, and the risky part stays R5 code.

Protocol add-ons are root code. Only protocol add-ons that come with the system are loaded. Third-party code never runs inside `vpn_server`.

## Classes

| Class | Header | What it is |
|---|---|---|
| `BVPNRoster` | `<VPNRoster.h>` | Client side of `vpn_server`, like `BLaunchRoster`. List connections, connect, disconnect, watch state |
| `BVPNConnection` | `<VPNConnection.h>` | One saved connection: name, protocol, provider (optional), auto-connect, state |
| `BVPNTunnelConfig` | `<VPNTunnelConfig.h>` | What a tunnel needs: protocol name, local addresses, peer endpoint, keys (by key-store reference, not by value), DNS servers, routes, MTU. Flattens to a `BMessage` |
| `BVPNProtocol` | `<VPNProtocol.h>` | Base class of a protocol add-on: `Start(const BVPNTunnelConfig&, int tunFD)`, `Stop()`, `Status()`, `ImportFile(BEntry&, BVPNTunnelConfig&)` for `.conf` / `.ovpn` |
| `BVPNProvider` | `<VPNProvider.h>` | Base class of a provider add-on: `Name()`, `Icon()`, `LoginMethods()`, `Login(...)`, `Logout()`, `ServerList(BMessage&)`, `CreateConfig(const char* server, BVPNTunnelConfig&)`, `View()` for its own settings |
| `BVPNLoginView` | `<VPNLoginView.h>` | The standard login card, so every provider looks the same: code login, user name and `BPasswordControl`, later "Sign in with browser" |
| `BVPNServerListView` | `<VPNServerListView.h>` | A standard country and server list a provider can fill |

Add-on entry points, exported C functions as with Deskbar replicants and Haiku add-ons:

```cpp
extern "C" BVPNKit::BVPNProtocol* instantiate_vpn_protocol(image_id image);
extern "C" BVPNKit::BVPNProvider* instantiate_vpn_provider(image_id image);
```

The ABI is fixed from the first release, as in `docs/APPEARANCE-KIT.md`: a private data pointer, `_ReservedVPN<Class>1`–`4`, `uint32 _reserved[4]`. New calls go at the end. A shipped call does not change. The library also gets a section in `docs/LIBR5.md` and pages in R5Book.

## Login

The kit offers three methods. A provider says which ones its service supports (`LoginMethods()`).

1. **Code (`B_VPN_LOGIN_DEVICE_CODE`), preferred.** R5 shows a short code and a QR code. The person signs in on their phone and enters the code. The provider polls its service and gets a token. No browser on R5 is needed, and two-step verification works.
2. **User name and password (`B_VPN_LOGIN_PASSWORD`), fallback.** Fields in `BVPNLoginView`. The password goes straight to the provider's service and is not stored. Only the token is stored.
3. **Browser (`B_VPN_LOGIN_BROWSER`), later.** The system browser opens the service's sign-in page and returns through a `r5vpn:` link. Not before a current browser is in the image (roadmap item 4). WebPositive does not handle many modern sign-in pages.

Whether a given company supports method 1 depends on that company's service. The kit does not assume it.

Secrets:

- A token or a private key is stored with `BKeyStore` in the keyring `VPN`, under the provider's signature. It never appears in a log, in argv, in a settings file, or in What's new.
- `BVPNTunnelConfig` carries a key-store reference. `vpn_server` fetches the key itself when it starts the tunnel.
- A WireGuard private key that R5 creates is made on the machine and never leaves it.

## Who owns a connection

First version: **system-wide**. A VPN changes routes and DNS for everyone, so the VPN page requires an administrator and stays gray until the lock is open, like Display. One unlock covers it. The provider account is the administrator's.

Later, when there is more than one graphical session: a connection may belong to one person and be up only while that person is signed in. Not before fast user switching (`docs/MULTIUSER.md`).

## The VPN page

Settings layout rules apply unchanged (`AGENTS.md`, Settings window; `docs/SETTINGS.md`).

- Title **VPN**, subtitle "Connect through a private network". Icon: the stored network icon with the wrench.
- **Connections** box (bold title): one row per `BVPNConnection` (name, protocol or provider, state) and a switch Connect / Disconnect. Uses `BSidebarItem`-style rows where they fit, otherwise a plain `BListView`.
- **Add…** opens a menu: "Import WireGuard file…", then one item per installed provider. A provider item shows `BVPNLoginView`, then the provider's `View()`.
- **Options** for the selected connection: Connect at startup; Use the VPN's DNS. Block traffic when the VPN drops is shown only when the firewall exists (roadmap item 5); it is not faked.
- A change is applied at once. Revert restores the page as it was when opened. Defaults stays disabled: there is no factory set of connections.
- The Connections page of Network shows a VPN that is up as one more line, as in `docs/design/settings-mockup/network.html`. The Deskbar network tray shows a small VPN mark while one is up.

## vpn_server

- A launch_daemon **service** (uid 0), started on demand by `BVPNRoster`. It does not `Quit()` while a tunnel is up.
- Opens `tun/N`, hands the descriptor to the protocol add-on, sets the address and the routes through `BNetworkInterface` / `BNetworkRoster`, and points the resolver at the VPN's DNS. It restores all of it on disconnect, on a crash of the protocol, and at boot.
- Saved connections: `/boot/system/settings/network/vpn`, a flattened `BMessage`, without secrets.
- Talks only `BMessage`. A provider process is checked by its signature against the installed providers before its configuration is accepted. A configuration that asks for routes other than the default route and its own addresses is refused in the first version.

## WireGuard, the first protocol

- Haiku has no WireGuard. The Linux kernel module is GPL-2.0 and stays out.
- Candidates for a userspace WireGuard on `tun/N`, still to be checked on R5: wireguard-go (MIT, needs Go on Haiku), boringtun (BSD-3-Clause, needs Rust on Haiku), or an R5 C++ implementation with the crypto from OpenSSL 3 (Apache-2.0), which the image already has. Report license, copyright and integration before one is picked (`docs/LICENSING.md`).
- `ImportFile()` reads a standard `wg-quick` `.conf`. The private key goes into the key store and is removed from the copy R5 keeps.

## Example provider

`distro/vpn/example-provider`, "Example VPN". Not a real company. It shows a developer the whole provider side: code login, password login, token in the key store, a server list, and `CreateConfig()` for WireGuard. Its server is a small test service plus a WireGuard peer in WSL, in `scripts/`. It is the example in R5Book.

R5 does not ship add-ons for real VPN companies. Their names, logos and accounts are theirs. They can build their own add-on from this kit.

## Order

1. Key store encryption (gap above).
2. `libr5vpn.so`, `vpn_server`, the WireGuard protocol, Import, and the VPN page.
3. The example provider with code and password login.
4. Browser login, after a current browser is in the image.
5. OpenVPN as the second protocol, as a separate GPL program the add-on drives, so the kit is proven with two protocols.

## Testing

- QEMU usernet passes outgoing UDP, so a WireGuard peer in WSL should be enough for steps 2 and 3. Not tried yet. That is a tunnel test, not a LAN test.
- Check after connect: the route table, the resolver, a ping through the tunnel, and that disconnect and a killed protocol add-on both restore the old state.
- Never claim a test against a real VPN company that did not run.

## Open

- Namespace: `BVPNKit` (own) versus putting the classes in Haiku's `BNetworkKit`. Own namespace chosen, so a future Haiku class cannot collide.
- Whether the VPN page hosts `BNetworkSettingsAddOn` add-ons as well, so the Haiku VPN stub type is reused instead of a parallel list.
- Split tunneling (only some traffic through the VPN): not in the first version.
