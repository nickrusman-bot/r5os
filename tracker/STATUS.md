# R5 OS ticket status

Gegenereerd na import van `query.csv`. Niet met de hand bewerken.

| R5-status | Aantal |
|---|---|
| open | 3936 |
| in-progress | 0 |
| fixed | 0 |
| wontfix | 0 |
| not-applicable | 0 |
| **totaal in tracker** | **3936** |
| open bij Haiku (laatste import) | 3936 |

Haiku-type in deze snapshot: **2569** bugs, **1307** enhancements, rest overig.

## Type

| Type | Aantal |
|---|---|
| bug | 2569 |
| enhancement | 1307 |
| task | 60 |

## Priority

| Priority | Aantal |
|---|---|
| normal | 3631 |
| low | 260 |
| high | 42 |
| blocker | 2 |
| critical | 1 |

## Milestone

| Milestone | Aantal |
|---|---|
| Unscheduled | 3119 |
| R1 | 434 |
| R1.1 | 310 |
|  | 29 |
| R1/beta7 | 28 |
| R2 | 16 |

## Component (top 20)

| Component | Aantal |
|---|---|
| Applications/Tracker | 223 |
| Kits/Interface Kit | 167 |
| Servers/app_server | 163 |
| System/Kernel | 154 |
| Applications/WebPositive | 132 |
| Kits/Web Kit | 126 |
| User Interface | 96 |
| Applications/MediaPlayer | 95 |
| - General | 88 |
| Drivers/Graphics/radeon_hd | 79 |
| Applications/HaikuDepot | 68 |
| Applications/Deskbar | 65 |
| Build System | 64 |
| Applications/Command Line Tools | 53 |
| Applications/Debugger | 45 |
| System/POSIX | 43 |
| Kits/Package Kit | 41 |
| Documentation | 38 |
| Applications/Terminal | 37 |
| System | 37 |

## Bezig

_geen_

## Gefixt in R5 OS

_geen_

## Nog open (eerste 80)

| Ticket | Haiku | Component | Samenvatting | Branch |
|---|---|---|---|---|
| [#344](https://dev.haiku-os.org/ticket/344) | assigned | - General | Unify file/folder names in ~/config/settings |  |
| [#570](https://dev.haiku-os.org/ticket/570) | assigned | - General | spybmessage wont load NetPositive |  |
| [#605](https://dev.haiku-os.org/ticket/605) | assigned | Kits/Interface Kit | [VLC] resizing control window doesn't resize the timeview |  |
| [#804](https://dev.haiku-os.org/ticket/804) | new | Network & Internet | "ifconfig" should support :x notion |  |
| [#808](https://dev.haiku-os.org/ticket/808) | new | Network & Internet/Stack | Get userland stack to run again |  |
| [#813](https://dev.haiku-os.org/ticket/813) | new | Network & Internet/Stack | Manage routes via a radix tree |  |
| [#840](https://dev.haiku-os.org/ticket/840) | new | Kits/libtextencoding.so | Translation kit should be reviewed for utf-8 string vulnerabilities |  |
| [#868](https://dev.haiku-os.org/ticket/868) | new | Network & Internet/Stack | create general point-to-point connection kernel module API |  |
| [#869](https://dev.haiku-os.org/ticket/869) | new | Network & Internet/PPP | [IPCP] get ConnectOnDemand working |  |
| [#870](https://dev.haiku-os.org/ticket/870) | new | Network & Internet/PPP | finish PPP server mode support |  |
| [#872](https://dev.haiku-os.org/ticket/872) | new | Network & Internet/PPP | [PPPReportManager] replace notification mechanism with KMessage |  |
| [#873](https://dev.haiku-os.org/ticket/873) | new | Network & Internet/PPP | automatically create an interface instance for every configuration file |  |
| [#922](https://dev.haiku-os.org/ticket/922) | new | Network & Internet/PPP | add CHAP authentication support |  |
| [#923](https://dev.haiku-os.org/ticket/923) | assigned | Network & Internet/PPP | add dial-up (phone line) modem support |  |
| [#976](https://dev.haiku-os.org/ticket/976) | assigned | Kits/libtextencoding.so | convert_from_utf8() broken |  |
| [#1006](https://dev.haiku-os.org/ticket/1006) | assigned | Preferences/Backgrounds | inconsistent behavior of Revert button in Backgrounds preflet (easy) |  |
| [#1036](https://dev.haiku-os.org/ticket/1036) | new | Applications/Tracker | Tracker Doesn't Like Hard Links |  |
| [#1041](https://dev.haiku-os.org/ticket/1041) | new | File Systems/UDF | UDF attributes support |  |
| [#1042](https://dev.haiku-os.org/ticket/1042) | assigned | Network & Internet | implement ZeroConf/Bonjour support |  |
| [#1048](https://dev.haiku-os.org/ticket/1048) | assigned | System | add support for PowerPC |  |
| [#1059](https://dev.haiku-os.org/ticket/1059) | new | Network & Internet/PPP | [IPCP] update DNS server list |  |
| [#1070](https://dev.haiku-os.org/ticket/1070) | new | System/Kernel | Implement a precache algorithm for the file system caches |  |
| [#1071](https://dev.haiku-os.org/ticket/1071) | new | System/Kernel | Identify and fix performance bottlenecks in the kernel and I/O subsystems |  |
| [#1074](https://dev.haiku-os.org/ticket/1074) | assigned | Servers/app_server | Identify and fix performance bottlenecks in our app_server |  |
| [#1082](https://dev.haiku-os.org/ticket/1082) | assigned | System/runtime_loader | Runtime Loader Fails for Programs on File Systems not Using the File Cache |  |
| [#1085](https://dev.haiku-os.org/ticket/1085) | new | Drivers/Disk | Add Mount Rainier support to the CD driver |  |
| [#1099](https://dev.haiku-os.org/ticket/1099) | assigned | Applications/ShowImage | Lossless rotation of JPEG images in ShowImage |  |
| [#1102](https://dev.haiku-os.org/ticket/1102) | new | System/Kernel | Enable the kernel to run as a userland process on top of itself |  |
| [#1106](https://dev.haiku-os.org/ticket/1106) | assigned | Drivers/Disk/USB | Port USB mass storage driver to new SCSI module API |  |
| [#1133](https://dev.haiku-os.org/ticket/1133) | assigned | Kits/Interface Kit | BPicture R5 compatiblity |  |
| [#1171](https://dev.haiku-os.org/ticket/1171) | assigned | Drivers/Graphics/VIA | Add support for VIA/S3 Unichrome PRO IGP |  |
| [#1182](https://dev.haiku-os.org/ticket/1182) | assigned | Kits/OpenGL Kit | BGLScreen is not implemented |  |
| [#1242](https://dev.haiku-os.org/ticket/1242) | new | File Systems/BFS | Inode::WriteAttribute() does not work correctly for small_data items |  |
| [#1244](https://dev.haiku-os.org/ticket/1244) | new | Add-Ons/Translators | create an .eps image translator |  |
| [#1259](https://dev.haiku-os.org/ticket/1259) | new | Build System | Fix gcc 2.95.3 Type Info Functions |  |
| [#1326](https://dev.haiku-os.org/ticket/1326) | assigned | Applications/Mail | Undo/Redo menu items are not properly enabled/disabled in Mail app |  |
| [#1370](https://dev.haiku-os.org/ticket/1370) | new | Servers/app_server | Alpha Transparency in PNGs missing for desktop backgrounds |  |
| [#1373](https://dev.haiku-os.org/ticket/1373) | assigned | Applications/Command Line Tools | Better parsing of team name in /bin/top |  |
| [#1402](https://dev.haiku-os.org/ticket/1402) | assigned | Servers/app_server | Implement message logging for app_server <-> libbe communication |  |
| [#1449](https://dev.haiku-os.org/ticket/1449) | reopened | Servers/input_server | implement keymap switching application |  |
| [#1467](https://dev.haiku-os.org/ticket/1467) | assigned | - General | Can't move windows around with changed clickfunctions by UniversalScroller |  |
| [#1501](https://dev.haiku-os.org/ticket/1501) | new | Servers/app_server | Failed Assertion in RegionSupport.cpp |  |
| [#1504](https://dev.haiku-os.org/ticket/1504) | new | Drivers/USB | Enable USB -> RS-232 Serial port adapter on startup to allow for serial debugging |  |
| [#1576](https://dev.haiku-os.org/ticket/1576) | assigned | - General | Some headers should be rewritten |  |
| [#1588](https://dev.haiku-os.org/ticket/1588) | new | Drivers/Graphics/nVidia | Geforce 8xxx support in Nvidia driver |  |
| [#1621](https://dev.haiku-os.org/ticket/1621) | assigned | Kits/Interface Kit/BMenu | Rewrite menu tracking using Mouse hooks |  |
| [#1760](https://dev.haiku-os.org/ticket/1760) | new | Applications/Cortex | Cortex does invasive changes |  |
| [#1786](https://dev.haiku-os.org/ticket/1786) | assigned | Applications/MediaPlayer | MIDI capability should be integrated into MediaPlayer and MidiPlayer dropped |  |
| [#1810](https://dev.haiku-os.org/ticket/1810) | new | Network & Internet/PPP | [Network] implement pptp/l2tp support |  |
| [#1815](https://dev.haiku-os.org/ticket/1815) | new | Drivers/Disk | Haiku takes very long to boot with new ata busmanager |  |
| [#1834](https://dev.haiku-os.org/ticket/1834) | new | Applications/DriveSetup | Minimum size should be calculated for extended partitions |  |
| [#1837](https://dev.haiku-os.org/ticket/1837) | new | Servers/app_server | Implement Begin- and EndRectTracking() in the app_server |  |
| [#1842](https://dev.haiku-os.org/ticket/1842) | assigned | Servers/app_server | Black box around mouse cursor when holding it in front of GLTeapot |  |
| [#1852](https://dev.haiku-os.org/ticket/1852) | assigned | User Interface | Menubar navigation bugs |  |
| [#1863](https://dev.haiku-os.org/ticket/1863) | new | Drivers | Extend the FreeBSD compatibility layer beyond networking |  |
| [#1872](https://dev.haiku-os.org/ticket/1872) | new | Applications/LaunchBox | Sluggishness in LaunchBox pad preparation? |  |
| [#1876](https://dev.haiku-os.org/ticket/1876) | assigned | Add-Ons/Media/Mixer | Media preferences, clicking on some checkboxes switches back to first tab |  |
| [#1902](https://dev.haiku-os.org/ticket/1902) | assigned | Servers/media_server | [Media_Server] play StartUp sound on sucessful start/restart |  |
| [#1903](https://dev.haiku-os.org/ticket/1903) | assigned | Kits/Network Kit | ifconfig: media selection option is available but not supported. |  |
| [#1904](https://dev.haiku-os.org/ticket/1904) | new | Drivers/Graphics/Matrox | Hardware cursor not working on Matrox G200 |  |
| [#1907](https://dev.haiku-os.org/ticket/1907) | new | System/libroot.so | Replace GNU C library and utilities with BSD-licensed equivalents |  |
| [#1916](https://dev.haiku-os.org/ticket/1916) | new | Network & Internet/Bluetooth | [Bluetooth] Provide RSSI for discovered Bluetooth device |  |
| [#1930](https://dev.haiku-os.org/ticket/1930) | assigned | User Interface | Modifying menu behavior for different mouse clicks |  |
| [#1989](https://dev.haiku-os.org/ticket/1989) | assigned | System/POSIX | Complete termios.h definitions |  |
| [#1993](https://dev.haiku-os.org/ticket/1993) | new | System/Kernel | Implement using HPET timers and other mechanisms than TSC for time keeping |  |
| [#1994](https://dev.haiku-os.org/ticket/1994) | new | Network & Internet/TCP | TCP does not support urgent (or inline) data |  |
| [#2044](https://dev.haiku-os.org/ticket/2044) | assigned | System/POSIX | Many sysconf definitions are missing. |  |
| [#2045](https://dev.haiku-os.org/ticket/2045) | new | Applications/Tracker | NetPositive Bookmarks not consistent |  |
| [#2055](https://dev.haiku-os.org/ticket/2055) | reopened | Add-Ons/Media/Mixer | Eric's Ulimate Solitare, and Axia fail to run |  |
| [#2103](https://dev.haiku-os.org/ticket/2103) | new | Servers/app_server | BShisen: changing tile set is not working as expected |  |
| [#2120](https://dev.haiku-os.org/ticket/2120) | assigned | Documentation | Build system Documentation/Reference? |  |
| [#2123](https://dev.haiku-os.org/ticket/2123) | new | User Interface | Using the "Super" key. |  |
| [#2126](https://dev.haiku-os.org/ticket/2126) | assigned | Servers/registrar | Cancel Shutdown does not work as expected |  |
| [#2131](https://dev.haiku-os.org/ticket/2131) | assigned | Drivers/Graphics | AMD Geode Video controller needs a driver |  |
| [#2132](https://dev.haiku-os.org/ticket/2132) | assigned | Drivers/Audio | AMD Geode Audio controller needs a driver |  |
| [#2136](https://dev.haiku-os.org/ticket/2136) | reopened | Applications/Deskbar | Deskbar doesn't have the 'show the desktop' feature |  |
| [#2138](https://dev.haiku-os.org/ticket/2138) | new | Applications/Tracker | [Tracker] ability to preview the content of files |  |
| [#2177](https://dev.haiku-os.org/ticket/2177) | assigned | System/Boot Loader/BIOS | boot screen video mode incorrect/unsupported, but desktop okay |  |
| [#2198](https://dev.haiku-os.org/ticket/2198) | new | Kits/Interface Kit | Add BTabView::SetSelectionMessage() like BListView does |  |
| [#2372](https://dev.haiku-os.org/ticket/2372) | new | Applications/Tracker | [Tracker] unneeded offset in icon mode |  |
| … | | | *3856 meer in r5-status.tsv* | |

