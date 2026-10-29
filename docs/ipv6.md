# IPv6 on R5OS

Network preferences already has **IPv6 → Mode**: Automatic (SLAAC / DHCPv6), Static, Disabled. Automatic is the default for a new NIC. Disabled stops SLAAC and DHCPv6; link-local `fe80::` can still appear.

## What you should see

On VirtualBox bridged LAN (not QEMU usernet):

1. `fe80::…` link-local (always, from the MAC) — once.
2. A second IPv6 address if the router sends a SLAAC `/64` (A-flag) **or** a DHCPv6 IA_NA lease — once.
3. Syslog lines `slaac`, `dad`, `dhcpv6`. `already assigned` means a second RA was ignored.

A home router often has SLAAC ULA (`fd…`) and **no** DHCPv6 server. Then `dhcpv6: no server` is expected. QEMU usernet is not a LAN: no multicast RA.

VirtualBox bridged LAN (2026-10-28): ULA `fdae:aeb4:a752:9ae4:a00:27ff:fe44:61dd` plus `fe80::`, RS sent, PIO `A=1`, extra RAs `already assigned`, `dhcpv6: no server`.

## Test in the guest (Terminal)

```
grep -E 'slaac|dad|dhcpv6' /var/log/syslog
ifconfig /dev/net/ipro1000/0
route
```

Useful syslog:

| Line | Meaning |
|---|---|
| `slaac …: PIO fd…/64 A=1 L=1` | Router offered SLAAC |
| `slaac …: default route via fe80:…%N` | Off-link IPv6 has a gateway on this NIC |
| `dad …: … unique` | DAD sent an NS and heard no conflict |
| `slaac …: fdae:… via fe80:…` | Second address was assigned |
| `slaac … without SLAAC /64 prefix` | RA had no A-flag `/64`; DHCPv6 may still run |
| `dad …: NS send:` / `socket:` / `bind` | DAD could not run; the address is **not** assigned |
| `RS send failed: Network is unreachable` | Multicast send had no route; kernel now uses the interface `direct_route` |
| `mdns: IPv6 announce` | IPv6 mDNS send to `ff02::fb` failed |
| `dad …: duplicate` | Address not assigned |
| `dhcpv6 …: no server` | No DHCPv6 on this LAN |

Network → select the NIC → **IPv6**. Tooltip on Mode repeats the grep command.

Outbound test (there is no `ping6` command):

```
ping -6 -c 3 ipv6.google.com
```

The source must be the ULA (`fdae:…`), not `fe80::`. `ping -I` needs `IPV6_PKTINFO` (not in Haiku); use `fe80:…%2` instead.

Ping to `ipv6.google.com` will fail until the router offers a public `2xxx:` address. A ULA is only for the LAN. After the alias-route fix, `ping -6 -c 3 fdae:…` (own address) should show replies, and `route` should list that host with `HL`.
