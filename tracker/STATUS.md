# R5OS ticket status

Gegenereerd door `scripts/tracker-report.py`. Niet met de hand bewerken.

| R5-status | Aantal |
|---|---|
| open | 3831 |
| in-progress | 8 |
| fixed | 97 |
| wontfix | 0 |
| not-applicable | 0 |
| **totaal in tracker** | **3936** |
| open bij Haiku (laatste import) | 3936 |

Nog open in R5OS: **2488** bugs, **1291** enhancements, rest overig (van 3839 open/bezig).

## Type

| Type | Aantal |
|---|---|
| bug | 2488 |
| enhancement | 1291 |
| task | 60 |

## Priority

| Priority | Aantal |
|---|---|
| normal | 3545 |
| low | 258 |
| high | 34 |
| critical | 1 |
| blocker | 1 |

## Milestone

| Milestone | Aantal |
|---|---|
| Unscheduled | 3049 |
| R1 | 420 |
| R1.1 | 299 |
| — | 29 |
| R1/beta7 | 26 |
| R2 | 16 |

## Component (top 20)

| Component | Aantal |
|---|---|
| Applications/Tracker | 183 |
| Servers/app_server | 161 |
| Kits/Interface Kit | 157 |
| System/Kernel | 153 |
| Applications/WebPositive | 132 |
| Kits/Web Kit | 126 |
| Applications/MediaPlayer | 95 |
| - General | 87 |
| User Interface | 85 |
| Drivers/Graphics/radeon_hd | 77 |
| Applications/HaikuDepot | 68 |
| Build System | 64 |
| Applications/Command Line Tools | 53 |
| Applications/Debugger | 45 |
| System/POSIX | 43 |
| Applications/Deskbar | 43 |
| Kits/Package Kit | 40 |
| Documentation | 38 |
| System | 37 |
| Applications/Icon-O-Matic | 37 |

## Bezig

| Ticket | Haiku | Component | Samenvatting | Branch |
|---|---|---|---|---|
| [#1042](https://dev.haiku-os.org/ticket/1042) | assigned | Network & Internet | implement ZeroConf/Bonjour support | cursor/airplay-receiver |
| [#6795](https://dev.haiku-os.org/ticket/6795) | assigned | Printing | Mechanism to detect and identify network printer and offer setup wizard | cursor/zeroconf-1042 |
| [#8316](https://dev.haiku-os.org/ticket/8316) | assigned | Network & Internet/IPv6 | Haiku needs IPv6 link scope Auto Configuration | cursor/ipv6-finish |
| [#8317](https://dev.haiku-os.org/ticket/8317) | assigned | Network & Internet/IPv6 | Haiku needs IPv6 global scope Auto Configuration | cursor/ipv6-finish |
| [#8319](https://dev.haiku-os.org/ticket/8319) | assigned | Network & Internet/IPv6 | Haiku needs IPv6 Duplicate Address Detection | cursor/ipv6-finish |
| [#8940](https://dev.haiku-os.org/ticket/8940) | new | Preferences | optional user login (screen) similar to BeLogin but homegrown supported | cursor/session-user |
| [#16318](https://dev.haiku-os.org/ticket/16318) | new | Printing | IPP-Everywhere and AirPrint - Print support the XXIst century way | cursor/zeroconf-1042 |
| [#18970](https://dev.haiku-os.org/ticket/18970) | assigned | User Interface | Various dark mode problems (easy) |  |

## Gefixt in R5OS

| Ticket | Haiku | Component | Samenvatting | Branch |
|---|---|---|---|---|
| [#1006](https://dev.haiku-os.org/ticket/1006) | assigned | Preferences/Backgrounds | inconsistent behavior of Revert button in Backgrounds preflet (easy) |  |
| [#2136](https://dev.haiku-os.org/ticket/2136) | reopened | Applications/Deskbar | Deskbar doesn't have the 'show the desktop' feature |  |
| [#2372](https://dev.haiku-os.org/ticket/2372) | new | Applications/Tracker | [Tracker] unneeded offset in icon mode |  |
| [#2542](https://dev.haiku-os.org/ticket/2542) | new | User Interface | force quit for stubborn apps |  |
| [#2701](https://dev.haiku-os.org/ticket/2701) | new | User Interface | Changing mouse pointer |  |
| [#2864](https://dev.haiku-os.org/ticket/2864) | assigned | Applications/Tracker | after mounting, disk icons overlap on desktop |  |
| [#3076](https://dev.haiku-os.org/ticket/3076) | assigned | Applications/Deskbar | [Deskbar] disabled menu items after dropping some file on a leaf |  |
| [#3171](https://dev.haiku-os.org/ticket/3171) | assigned | Applications/Tracker | Better prompting in Tracker file operations |  |
| [#3291](https://dev.haiku-os.org/ticket/3291) | assigned | Applications/Tracker | Deskbar overlaps icons |  |
| [#3505](https://dev.haiku-os.org/ticket/3505) | assigned | Servers/app_server | Reposition windows on resolution change if they lie completely outside of the bounds of the new resolution |  |
| [#3677](https://dev.haiku-os.org/ticket/3677) | new | Preferences/Appearance | Preference panels with tabs should remember the selected tab |  |
| [#3732](https://dev.haiku-os.org/ticket/3732) | assigned | Applications/Tracker | Renaming files: selection without suffix |  |
| [#3813](https://dev.haiku-os.org/ticket/3813) | new | Applications/Screenshot | Screenshot enhancements |  |
| [#4089](https://dev.haiku-os.org/ticket/4089) | assigned | Servers/app_server | Per window tab/border color (easy) |  |
| [#4216](https://dev.haiku-os.org/ticket/4216) | assigned | Applications/Tracker | Simplify the Move to / Copy to / Create Link context menus |  |
| [#4276](https://dev.haiku-os.org/ticket/4276) | assigned | Applications/Deskbar | [Deskbar] hide application expander if there are no windows |  |
| [#4626](https://dev.haiku-os.org/ticket/4626) | new | Applications/Deskbar | Context menu for Deskbar application entries |  |
| [#4793](https://dev.haiku-os.org/ticket/4793) | new | Applications/Tracker | Selectable/Copyable text in file Info Panels |  |
| [#5000](https://dev.haiku-os.org/ticket/5000) | assigned | Applications/Deskbar | Additional Deskbar click behaviour |  |
| [#5338](https://dev.haiku-os.org/ticket/5338) | assigned | Applications/Tracker | Long name not fit in Tracker status window |  |
| [#5930](https://dev.haiku-os.org/ticket/5930) | assigned | Applications/Tracker | Add "Favorites" to standard Tracker Menu |  |
| [#7328](https://dev.haiku-os.org/ticket/7328) | assigned | Applications/Deskbar | Deskbar - Empty Recent applications expands outside of screen |  |
| [#8246](https://dev.haiku-os.org/ticket/8246) | assigned | Applications/Tracker | New folders are created on top of each other on the desktop |  |
| [#8543](https://dev.haiku-os.org/ticket/8543) | assigned | Applications/Tracker | [Tracker] path tooltip in Info Window appears on all workspaces |  |
| [#8544](https://dev.haiku-os.org/ticket/8544) | reopened | Applications/Tracker | [Tracker] InfoWindow doesn't update file icon after identification |  |
| [#8551](https://dev.haiku-os.org/ticket/8551) | assigned | Applications/Tracker | "Open with" shows uppercase words in Relation column |  |
| [#8772](https://dev.haiku-os.org/ticket/8772) | new | Applications/Tracker | [Tracker] columns move on second OpenWithWindow invocation |  |
| [#9216](https://dev.haiku-os.org/ticket/9216) | assigned | Applications/Tracker | Clearing selection not possible with keyboard only |  |
| [#9383](https://dev.haiku-os.org/ticket/9383) | assigned | Applications/Tracker | [Tracker] doesn't show a warning when renaming system folder in InfoWindow |  |
| [#10208](https://dev.haiku-os.org/ticket/10208) | new | Partitioning Systems | Drivesetup creating partition size errors |  |
| [#11029](https://dev.haiku-os.org/ticket/11029) | assigned | Applications/Tracker | [Tracker] selection is lost when opening parent directory |  |
| [#11138](https://dev.haiku-os.org/ticket/11138) | assigned | Kits/Interface Kit | Alert _text field archived but never instantiated (easy) |  |
| [#11176](https://dev.haiku-os.org/ticket/11176) | reopened | Applications/Tracker | [Tracker] status window displays Dec 13, 1901 as finish time |  |
| [#11397](https://dev.haiku-os.org/ticket/11397) | assigned | Applications/Deskbar | Deskbar seems that doesn't follow color scheme based on "Appearance" colors |  |
| [#11402](https://dev.haiku-os.org/ticket/11402) | assigned | Applications/Tracker | Tracker's find panel doesn't update the attribute list when changing file type. |  |
| [#11495](https://dev.haiku-os.org/ticket/11495) | assigned | Applications/Tracker | The content of submenu "Add-ons" doesn't update automatically |  |
| [#11625](https://dev.haiku-os.org/ticket/11625) | assigned | Kits/Interface Kit | BBox with layout cuts title at big font sizes |  |
| [#12335](https://dev.haiku-os.org/ticket/12335) | assigned | Applications/Tracker | Replace dialog doesn't use preferred date/time format |  |
| [#12884](https://dev.haiku-os.org/ticket/12884) | new | User Interface | Deskbar forgets settings |  |
| [#13431](https://dev.haiku-os.org/ticket/13431) | assigned | Applications/Deskbar | Deskbar menu screen edge margin |  |
| [#13754](https://dev.haiku-os.org/ticket/13754) | new | Servers/input_server | Mouse + keyboard input non-functional after install |  |
| [#14846](https://dev.haiku-os.org/ticket/14846) | new | Applications/Deskbar | Deskbar display of weekday clipped |  |
| [#14991](https://dev.haiku-os.org/ticket/14991) | new | User Interface | Several apps don't obey changing system colors on the fly |  |
| [#15541](https://dev.haiku-os.org/ticket/15541) | new | User Interface | Text colour in Tracker (and a few more places) is not changeable |  |
| [#15690](https://dev.haiku-os.org/ticket/15690) | new | Applications/SoftwareUpdater | [SoftwareUpdater] layout issue (easy) |  |
| [#15792](https://dev.haiku-os.org/ticket/15792) | assigned | Applications/Deskbar | [Deskbar] mini-mode confused state |  |
| [#15843](https://dev.haiku-os.org/ticket/15843) | assigned | Applications/Deskbar | [Deskbar] left dotted grip in mini-mode |  |
| [#15910](https://dev.haiku-os.org/ticket/15910) | new | Applications/Deskbar | Deskbar: bottom scroll arrow is not displayed sometimes |  |
| [#15975](https://dev.haiku-os.org/ticket/15975) | new | Applications/Deskbar | Sane minimum size for Deskbar |  |
| [#16116](https://dev.haiku-os.org/ticket/16116) | new | User Interface | Change standard icon size for Desktop to 48x48 |  |
| [#16144](https://dev.haiku-os.org/ticket/16144) | assigned | Applications/Deskbar | ESC doesn't always close Deskbar menu |  |
| [#16185](https://dev.haiku-os.org/ticket/16185) | new | Applications/Tracker | [Tracker] Status window is too small with large fonts |  |
| [#16245](https://dev.haiku-os.org/ticket/16245) | new | Servers/registrar | [Registrar] Add an Interface for Haiku to Detect and Handle Unresponsive Apps |  |
| [#16359](https://dev.haiku-os.org/ticket/16359) | new | Applications/Deskbar | Deskbar auto-raise status should be the same for every virtual desktop |  |
| [#16458](https://dev.haiku-os.org/ticket/16458) | new | Applications/Deskbar | Deskbar - Opens in background |  |
| [#16471](https://dev.haiku-os.org/ticket/16471) | new | User Interface | Selected desktop icon is not distinguishable enough with black background |  |
| [#16573](https://dev.haiku-os.org/ticket/16573) | assigned | Kits/Package Kit | Un-activated packages after install |  |
| [#16618](https://dev.haiku-os.org/ticket/16618) | new | User Interface | HaikuControlLook: DrawMenuBarBackground Disabled Flag dont work |  |
| [#16643](https://dev.haiku-os.org/ticket/16643) | new | Applications/Tracker | Tracker: when requesting to show file in a folder with a lot of files, list is not scrolled to selected file |  |
| [#16678](https://dev.haiku-os.org/ticket/16678) | assigned | Applications/Tracker | Tracker: use same text color for header |  |
| [#16726](https://dev.haiku-os.org/ticket/16726) | new | Applications/Tracker | Item count view in Tracker windows are now resizing |  |
| [#16838](https://dev.haiku-os.org/ticket/16838) | new | User Interface | Misaligned entries in BListVIew / Save / Open dialogs |  |
| [#16853](https://dev.haiku-os.org/ticket/16853) | new | Applications/Tracker | Garbled modified date text on list view |  |
| [#17377](https://dev.haiku-os.org/ticket/17377) | new | Drivers/Graphics/radeon_hd | radeon_hd crashes in connector_probe() |  |
| [#17664](https://dev.haiku-os.org/ticket/17664) | new | Drivers/Graphics/radeon_hd | radeon_hd "Cedar": app_server crashes writing to 0MB tiny framebuffer |  |
| [#17901](https://dev.haiku-os.org/ticket/17901) | new | Applications/Tracker | [Tracker] count view drawing glitches |  |
| [#18057](https://dev.haiku-os.org/ticket/18057) | new | User Interface | Misaligned scrollbars and resize knob |  |
| [#18060](https://dev.haiku-os.org/ticket/18060) | new | Applications/Tracker | [Tracker] Double clicking on Size column header hides all info |  |
| [#18300](https://dev.haiku-os.org/ticket/18300) | new | User Interface | Application menus at screen border |  |
| [#18411](https://dev.haiku-os.org/ticket/18411) | new | Applications/Deskbar | PowerStatus replicant is cut off |  |
| [#18514](https://dev.haiku-os.org/ticket/18514) | new | Applications/Tracker | bug: [Tracker] Icon view does not get laid out nicely |  |
| [#18518](https://dev.haiku-os.org/ticket/18518) | new | Kits/Interface Kit | Font ascender height is cut off |  |
| [#18643](https://dev.haiku-os.org/ticket/18643) | new | Applications/Deskbar | Deskbar: use B_MENU_SELECTED_BACKGROUND_COLOR in running apps list |  |
| [#18667](https://dev.haiku-os.org/ticket/18667) | assigned | Applications/Deskbar | Deskbar shutdown menu vs. dialog wording is inconsistent |  |
| [#18813](https://dev.haiku-os.org/ticket/18813) | new | Kits/Interface Kit | Section title font X-height cut off |  |
| [#19070](https://dev.haiku-os.org/ticket/19070) | new | Applications/Tracker | [Tracker] UI: Fixed black/gray status colours when copying |  |
| [#19084](https://dev.haiku-os.org/ticket/19084) | new | Applications/Tracker | render glitch when resizing table columns in tracker |  |
| [#19433](https://dev.haiku-os.org/ticket/19433) | new | Applications/Tracker | Tracker "Get Info" window is not really resizable |  |
| [#19520](https://dev.haiku-os.org/ticket/19520) | new | Applications/Tracker | Open with window does not remember the size and position. |  |
| [#19546](https://dev.haiku-os.org/ticket/19546) | reopened | Applications/Tracker | Using Find from Tracker should set folder |  |
| … | | | *17 meer in r5-status.tsv* | |

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
| [#1036](https://dev.haiku-os.org/ticket/1036) | new | Applications/Tracker | Tracker Doesn't Like Hard Links |  |
| [#1041](https://dev.haiku-os.org/ticket/1041) | new | File Systems/UDF | UDF attributes support |  |
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
| [#2138](https://dev.haiku-os.org/ticket/2138) | new | Applications/Tracker | [Tracker] ability to preview the content of files |  |
| [#2177](https://dev.haiku-os.org/ticket/2177) | assigned | System/Boot Loader/BIOS | boot screen video mode incorrect/unsupported, but desktop okay |  |
| [#2198](https://dev.haiku-os.org/ticket/2198) | new | Kits/Interface Kit | Add BTabView::SetSelectionMessage() like BListView does |  |
| [#2474](https://dev.haiku-os.org/ticket/2474) | assigned | Drivers | Serial port driver needed for Oxford Semi UART 16950 |  |
| [#2490](https://dev.haiku-os.org/ticket/2490) | new | File Systems/BFS | File version as on VMS (DEC VAX) |  |
| [#2499](https://dev.haiku-os.org/ticket/2499) | assigned | Kits/Interface Kit | [Keymap] shortcuts don't work with some keymaps |  |
| [#2505](https://dev.haiku-os.org/ticket/2505) | reopened | Network & Internet/Bluetooth | bluetooth_server provokes vm_page_fault in h2generic |  |
| … | | | *3751 meer in r5-status.tsv* | |

