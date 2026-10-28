# R5OS

Eigen distro op basis van [Haiku](https://www.haiku-os.org/), genoemd naar BeOS R5 als historische inspiratie.

Dit is **niet** de officiële Haiku-distributie en **niet** BeOS. Zie `distro/DISCLAIMER.txt`.

## Indeling

| Pad | Rol |
|---|---|
| `distro/` | Overlay: Jam-profiel, disclaimer, later branding en extra apps |
| `docs/ROADMAP.md` | Productrichting (geen agent-regels; die staan in `AGENTS.md`) |
| `tracker/` | Alle open Haiku-tickets + of R5OS ze al gefixt heeft |
| `scripts/` | Clone, overlay, image, ticket-sync |
| `haiku/` | R5OS-fork van de OS-bron (`r5`-branch; niet in *deze* git) |
| `buildtools/` | Haiku buildtools (na setup, fetch-only) |

`haiku/` is een **eigen fork**. Bugfixes mergen we in `r5` en pushen we alleen naar jullie git. We pushen niet naar Haiku: hun beleid weigert AI-gegenereerde of AI-aangepaste bijdragen.

## Eerste build (WSL Ubuntu)

Haiku bouwt niet native op Windows. In WSL:

```bash
sudo apt update
sudo apt install git nasm autoconf automake texinfo flex bison gawk \
  build-essential unzip wget zip xorriso mtools python3

cd /mnt/c/Users/nickr/Projects/R5OS
chmod +x scripts/*.sh
./scripts/setup-wsl.sh
./scripts/build-image.sh r5-raw
```

Daarna de image in QEMU starten, bijvoorbeeld:

```bash
# WSL: scripts/run-qemu-r5.sh (TCG of KVM, IDE-schijf, virtio-net, USB-tablet, HDA)
qemu-system-x86_64 -machine pc,accel=kvm -m 2048 -smp 2 \
  -drive file=haiku/generated.x86_64/r5-os.image,format=raw,if=ide \
  -vga std -usb -device usb-tablet \
  -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
  -device intel-hda -device hda-duplex
```

Profielen: `r5-raw` (QEMU/schijf), `r5-anyboot` (ISO/USB), `r5-vmware`.

`--distro-compatibility compatible` blijft aan tot we bewust de systeem-API wijzigen.

## Architecturen

R5OS is **64-bit**: **AMD64** (nu, referentiemachine 5625U), daarna **ARM64**, optioneel **RISC-V 64**. Geen focus op 32-bit of overige Haiku-ports. Zie `distro/TODO.md`.

Clone **niet** op `/mnt/c/...` als je in WSL bouwt: NTFS is case-insensitive en traag. Overlay mag op Windows staan; `haiku/` en `buildtools/` het liefst op het Linux-bestandssysteem (`~/Projects/R5OS`).

## Git: R5OS is de bron van waarheid

```text
origin     <jullie haiku-fork>                 push/pull
upstream   https://review.haiku-os.org/haiku   alleen fetch
branch     r5
```

1. Lege repo aanmaken voor de fork (GitHub, Codeberg of Origin).
2. `export R5OS_HAIKU_ORIGIN=git@github.com:<jij>/r5os-haiku.git`
3. `./scripts/setup-wsl.sh` en daarna `git -C haiku push -u origin r5`
4. Fix: `./scripts/new-bugfix.sh 12345 korte-slug` → commit → mergen in `r5` → `./scripts/publish-bugfix.sh` → `python3 scripts/tracker-set.py 12345 fixed --commit <sha>`
5. Officiële Haiku-commits ophalen (optioneel): `./scripts/sync-upstream.sh`

## Open Haiku-bugs bijhouden

Haiku’s Trac is de lijst van open bugs; **`tracker/`** is ons werkboek. Per ticket: `open`, `in-progress`, `fixed`, `wontfix`, of `not-applicable`. Importeren overschrijft nooit een R5-status.

Trac blokkeert automatische downloads. Eenmalig (en daarna af en toe) in de browser CSV opslaan:

1. [Open tickets (CSV-query)](https://dev.haiku-os.org/query?status=new&status=assigned&status=reopened&col=id&col=summary&col=status&col=type&col=priority&col=component&col=milestone&order=id&max=10000)
2. Onderaan **Comma-delimited Text** downloaden
3. `./scripts/tracker-sync.sh ~/Downloads/query.csv`

Daarna is `tracker/STATUS.md` het overzicht: hoeveel open, bezig, gefixt. We sturen geen patches naar Haiku; hun ticketnummer is alleen het id in onze fork.

## Merk

Gebruik niet de merknamen of logo’s van Haiku of BeOS op de image of website. Eigen naam: **R5OS**. AboutSystem moet de disclaimer uit `distro/DISCLAIMER.txt` tonen.

## Versie

Publieke releases: **R5OS 2026.1**, **2026.2**, … (jaar, daarna +1 per release; nieuw jaar begint weer bij `.1`).

Tussen builds: suffix `-dev`. Huidige boom: `distro/VERSION` (nu `2026.1-dev`). Jam gebruikt `HAIKU_REVISION=r5-<VERSION>`.

Open distro-taken (o.a. Haiku-verwijzingen uit de UI): `distro/TODO.md`.
