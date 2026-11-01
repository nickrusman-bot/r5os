# R5OS changelog (lokaal)

### Locale broadcast on the build 10023 image

`jam -q @r5-raw` on 1 Nov 2026 at 20:58 Europe/Amsterdam exited 0 and updated 7865 targets. `haiku.hpkg` was created at 20:57 and the raw image at 20:58. Deskbar (20:42), `libtracker.so` (20:42), and `libbe.so` (20:38) are the binaries from the locale compile and are older than that package, so this image contains them. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`.

### Locale changes still did not reach Deskbar

The image packed at 19:34 Europe/Amsterdam contained the country-locale pattern and the settings-file pulse. The user booted that disk and, with Dutch (Netherlands) and the 24-hour choice selected, the Deskbar clock still read “6:54 PM” while the Locale preview read 18:51. Moving Nederlands above English did not change menus. Restarting Tracker for translated folder names did not change icon titles, and VBox Share landed on Trash again.

The preview formats from the preflet’s own roster. Deskbar and Tracker only notice a `B_LOCALE_CHANGED` message, and that message was sent only after the settings file was saved. A failed save left the preview updated and every other program on en_US. `Refresh()` then reads the file and, when the file is missing or old, puts en_US back. The clock pulse did that too.

A locale change is now broadcast even when the save fails, and `~/config/settings` is created when the file is written. `BLocaleRoster::ApplySettings()` applies the message after `Refresh()`, so the message wins over a stale file. The Deskbar clock uses `BTimeFormat`, the same call as the preview, instead of the date-time pattern generator. It no longer reloads the settings file on each pulse. Deskbar and Tracker call `ForceUnloadCatalog()` on the locale message. A language change rebuilds Tracker menus and the Deskbar menu, and icon titles are read again. Trash can become Prullenbak from the Dutch catalog. The Dutch catalog still translates the home folder as “home”. QuickTour and VBox Share have no catalog names. The open Locale window keeps the labels it was opened with. A saved icon position on a mounted volume is ignored, so VBox Share is placed in a free cell instead of on Trash.

`jam -q libbe.so` then `jam -q libtracker.so Deskbar` on 1 Nov 2026 exited 0. `libbe.so`, `libtracker.so`, and Deskbar were relinked. The image section above packs those binaries. About stays build 10023.

### Deskbar still showed AM/PM

The image from 19:05 Europe/Amsterdam still drew “6:07 PM” while the Locale preview showed 18:07. The user had Dutch formatting, the 24-hour choice, and Nederlands as the preferred language. The preview is redrawn from the formatting country. The Deskbar pattern generator used the preferred language’s locale, and it can put AM/PM back into an English pattern. `k` was also rewritten to `K`, which is a 0–11 hour, so 18:00 can draw as 6. The clock rebuilt that pattern only when the locale message reached the view.

The generator now uses the formatting country, unless month and day names are taken from the preferred language. A 24-hour choice rewrites `h`, `K`, and `k` to `H` and drops the day-period marker. Each clock pulse reloads `Locale settings` when that file’s time changes, then rebuilds the pattern. `jam -q libbe.so Deskbar` exited 0 and updated 11 targets, including `FormattingConventions.o`, `DateTimeFormat.o`, `libbe.so`, `TimeView.o`, and Deskbar. `jam -q @r5-raw` on 1 Nov 2026 at 19:34 Europe/Amsterdam exited 0 and updated 1720 targets. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`.

Menus and folder names still come from the catalog loaded at program start. The Locale checkbox for folder names says Deskbar and Tracker need a restart.

### Build 10023 image with the 24-hour clock

`jam -q @r5-raw` on 1 Nov 2026 at 19:05 Europe/Amsterdam exited 0 and updated 2294 targets. `libbe.so` was linked again and packed into the system package. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. About stays build 10023. VirtualBox is started from this image afterwards. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`.

### Deskbar clock kept AM/PM

The Locale examples followed the 24-hour choice. Deskbar did not. Those examples call `GetTimeFormat()`, which runs `CoerceFormatForClock()`. Deskbar stores the ICU pattern from `BDateTimeFormat::SetDateTimeFormat()` as an explicit format, and an explicit format is not coerced later. For an English locale the pattern generator can keep AM/PM after the skeleton asked for 24-hour (`k`). The clock therefore stayed “5:27 PM” while the preflet preview showed 17:27.

`Use24HourClock()` also returned the unset explicit flag once the cache was already filled, so a remembered 24-hour choice could read as off. It now returns that cached or explicit choice. `CoerceFormatForClock()` is public, and `SetDateTimeFormat()` coerces the generated pattern before storing it. Applied by `scripts/apply-ui-bugs.py`.

`jam -q libbe.so` on 1 Nov 2026 at 18:48 Europe/Amsterdam updated 32 targets, including `FormattingConventions.o`, `DateTimeFormat.o`, and the `libbe.so` link, and exited 0. That library was not on the build 10023 disk that was running then. VirtualBox was not restarted for that compile. The image section above packs this library.

Menus, the Locale window’s own labels, and folders already open stay in the language that program loaded at start. The translation catalog is loaded once. A program started after the language change loads the new catalog. Folder names also stay English while “Translate application and folder names” is off. Tracker does not rebuild those names on the locale message.

### Build 10023 daily

What's new for the four Interface Kit controls, and for the well-known folder names and icon labels, is build 10023. `jam -q @r5-raw` on 1 Nov 2026 at 18:14 Europe/Amsterdam exited 0 and updated 1727 targets. `libbe.so` and `libtracker.so` were relinked. About was relinked and contains Build 10023. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. Not booted in this record. The first VirtualBox start reused the build 10022 disk: the guest had been writing that disk, so its timestamp was newer than the image. `scripts/run-vbox-r5.ps1` now stores the raw image time in `generated/r5-os.built` and does not trust the timestamp of a disk that was running. A second start converted this image and powered the VM on at 18:18 Europe/Amsterdam. The desktop was not seen. The first boot of this disk can sit in `package_daemon`.

### Text control, slider, scrollbar, and option popup

Four Interface Kit bugs, applied by `scripts/apply-ui-bugs.py`. They are on the build 10023 image. The object compile below was not a boot. VirtualBox was still on build 10022 then.

`BTextControl::SetText()` always went through the private text view, and that view sent the modification message for every insert and delete. Programmatic `SetText()` now suppresses that message. Typing still sends it (#11735).

`BSlider` sent its message only when the thumb moved. A click on the bar or the thumb now sends the message even when the value stays the same. A click in the padding still does nothing. The modification message is still only sent when the value changes (#13680).

Scrollbar arrows stayed enabled when the thumb already covered the whole range. They stay off when the minimum is below the maximum and the proportion is at least 0 and below 1. Proportion 0 is still the non-proportional mode, so those arrows keep working (#6873).

`BOptionPopUp::SetValue()` stored a value that was not one of the options, so the control value and the marked item disagreed. An unknown value is now ignored. A menu that is not created yet still stores the value (#18648).

`jam -q '<libbe!x86_64>interface_kit.o'` on 1 Nov 2026 at 18:06 Europe/Amsterdam updated `OptionPopUp.o`, `ListView.o`, and `interface_kit.o` and exited 0. The text-input objects were linked into `interface_kit.o` in an earlier run the same evening, after a duplicated declaration in the header was removed. That run updated `TextControl.o`, `TextInput.o`, `ListView.o`, and `interface_kit.o` and exited 0. `ScrollBar.o` and `Slider.o` were already compiled. `libbe.so` was not relinked. Not booted.

### Well-known folder names and icon labels

Two Tracker bugs, applied by `scripts/apply-tracker-desktop.py`. They are on the build 10023 image. The `libtracker.so` compile below was not a boot.

`system`, `home`, `apps`, `preferences`, `develop`, and `config` have catalog strings, but Tracker only showed them when the folder carried a `SYS:NAME` attribute. If that attribute is missing, the name now comes from the same catalog context (#15527). Other folders are unchanged (#10200 stays open).

A name fitted to the list column stayed truncated after switching to icon view. Icon view refits when the stored width is still the column width (#15444).

`jam -q libtracker.so` on 1 Nov 2026 at 17:37 Europe/Amsterdam updated 3 targets (`Model.o`, `WidgetAttributeText.o`, and the library link) and exited 0. Not booted. VirtualBox was left on build 10022.

### Leaf menu scroll arrows

The leaf menu on build 10021 drew an up arrow and a down arrow while the items still fit, with empty desktop below the menu. `BMenu::_CalcFrame` insets the screen by 4px so a menu border stays on screen (#13431). That same rectangle decided scrolling. The leaf menu starts at the top of the screen, which is above the inset, so scrolling turned on and `AttachScrollers()` added the arrows.

Scrolling now uses the real screen. The 4px margin is still used when placing a menu that would hang off an edge. `jam -q '<libbe!x86_64>interface_kit.o'` on 1 Nov 2026 at 17:22 Europe/Amsterdam updated 2 targets (`Menu.o` and `interface_kit.o`) and exited 0. `jam -q @r5-raw` at 17:25 Europe/Amsterdam exited 0 and updated 1725 targets. `libbe.so` was relinked. About is build 10022. The package check printed `mesa_packages_ok` and no Mesa 22. The 10021 session was still running when this image was built. The first restart compared the disk time after power-off, so VirtualBox booted that old disk again. `scripts/run-vbox-r5.ps1` now keeps the time from before power-off. A forced convert wrote the 10022 image and the VM started at 17:27 Europe/Amsterdam. The desktop was not seen from here. The first boot of this disk can sit in `package_daemon`. Do not Reset.

### Build 10021 daily

What's new for the list, Find, and clock fixes is build 10021. `jam -q @r5-raw` on 1 Nov 2026 at 17:11 Europe/Amsterdam exited 0 and updated 1256 targets. About was relinked after What's new was copied. The package check printed `mesa_packages_ok`: libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, the LLVM 21.1.8 runtime, and the Vulkan loader. No Mesa 22 package. The pin script did not run for this jam because the launch copy was not executable. The image still used the Mesa 25 repository files already on disk. Not booted in this record. VirtualBox is started from this image afterwards.

### Tracker keys, Find title, and Deskbar replicant label

Three more desktop fixes, applied by `scripts/apply-tracker-desktop.py`. Not on the daily image. Not booted.

Ctrl+Up and Ctrl+Down in a Tracker window follow Home and End. In list view that selects the first or last item. In icon view it scrolls to the top or bottom. Command+Up is still Open parent (#9974).

Find used to keep the first query file name, so a second search left the result window on the old title. The file is renamed from the current name, formula, or attribute line before the result opens again (#4154). A name typed by the user is left as it is. `UserSpecifiedName()` still returns nothing, so that path is unused.

The Deskbar menu item is “Show replicants handle” (#7644).

`jam -q libtracker.so` on 1 Nov 2026 at 15:58 Europe/Amsterdam updated 3 targets (`FindPanel.o`, `PoseView.o`, and the library link). `jam -q Deskbar` updated 2 targets (`DeskbarMenu.o` and the Deskbar link). Both printed `updated` and the jam process exited 0. Not booted.

### Tracker sort, location field, and Deskbar clock

Three more Tracker and Deskbar bugs, still applied by `scripts/apply-tracker-desktop.py` (`replace_once` on the existing tree). No new overlay script. Not on the daily image. Not booted.

Shift-click on a list column header now cycles the secondary sort: ascending, descending, then off. A plain click clears the secondary sort. The header triangle for that column follows its own direction. The reversed secondary order is kept for the open window and is not written into the folder view-state attribute, which has only one reverse flag (#14900).

Command+A in the single-window location field selects the path. A window filter runs before the Window menu shortcut, and only when that text view has focus, so Select all in the menu still selects the files (#16968).

A vertical Deskbar clock that is wider than the bar no longer slides the string off the left edge. Draw shortens it with an ellipsis: the weekday stays when it is shown, otherwise the clock digits stay (#14846). The earlier preferred-width margin did not stop a cut first letter.

Open With drops Tracker add-ons and no longer compares the running Tracker against an uninitialized `app_info` (#12859). That was already in `libtracker.so` from the earlier compile. This jam rebuilt that library, so the same binary has #12859 as well.

`jam -q libtracker.so` on 1 Nov 2026 at 15:22 Europe/Amsterdam updated 36 targets and exited 0. `jam -q Deskbar` updated 2 targets (`TimeView.o` and the Deskbar link) and exited 0. Not booted. The VirtualBox session that is already running was not restarted.

### Desktop icon no longer freezes Tracker

VBox Share is a directory on the Desktop with another filesystem mounted on it. Placement gave up by putting a late icon on the first grid cell, which was already taken, so the share sat on top of another icon. Dragging it then saved the icon position with `WriteAttr` on that mount. That call blocks, the Desktop window thread stops, and folders no longer open.

`scripts/apply-tracker-desktop.py` now keeps scanning for a free cell instead of dropping the icon on the first one, and stops after 400 cells. Saving positions skips a mount point (`stat` device differs from the parent) and clears the save flag, so Tracker does not open that filesystem to write the attribute. The icon still moves until the next boot. `jam -q libtracker.so` updated 35 targets and exited 0. `jam -q @r5-raw` on 1 Nov 2026 at 15:14 Europe/Amsterdam exited 0 and updated 1256 targets. The package check printed `mesa_packages_ok` and no Mesa 22. Not booted. The VirtualBox session that is already running is still the previous disk.

### Tracker and Deskbar batch

Thirty open Tracker and Deskbar bugs, applied by `scripts/tracker-deskbar-more.diff` from `scripts/apply-tracker-desktop.py`. No new overlay script. The patch is against the r5 Haiku tree after the existing overlays. It does not replace those overlays.

Tracker: the copy status buffer terminated a long name with the character `0` (#5338) and the window width ignored the font (#16185). A non-finite finish estimate became 13 Dec 1901 (#11176). Escape clears the selection when it is not clearing a cut or type-ahead (#9216). List rows use the font's ascent, descent, and leading (#16853). A stat change with no field mask still re-sorts (#20261). Scrolling a just-shown file updates the scroll range first (#16643). Single-window Open parent selects the folder you left (#11029). Find starts in the open folder (#19546). The Add-ons menu is rebuilt when the menu bar opens (#11495). The item count no longer uses the scrollbar's minimum width (#16726). Find's most-used type still fills the attribute row (#11402). Open With no longer writes into the MIME type string (#8551) and restores its frame after layout, in 12-point units (#19520, #8772). A narrow rating column and a date `parsedate` cannot read no longer crash or store year 1901 (#19661, #19663). The replace question uses `BDateTimeFormat` (#12335). The volume space bar invalidates the whole pose (#19573). Get Info refreshes the icon after identify (#8544), keeps the path tip on the current workspace (#8543), can be resized (#19433), and says when a name cannot be changed (#9383).

Deskbar: the leaf menu says Shut down and Restart, matching the question (#18667). An empty Recent menu gets a disabled item instead of a huge frame (#7328). Auto-raise is applied again when the workspace changes (#16359). A vertical Deskbar's minimum height includes one icon row (#15975). The lower scroll arrow is moved back to the visible edge (#15910). Menus stay 4px inside the screen (#13431). Escape always quits menu tracking (#16144).

`jam -q libtracker.so` and `jam -q '<libbe!x86_64>interface_kit.o'` compiled the Tracker objects and `Menu.o`, then linked `libbe.so` (the unknown target name `deskbar` made that command exit 1 after those links). `jam -q Deskbar` then updated 18 targets and exited 0. `jam -q @r5-raw` on 1 Nov 2026 at 12:02 Europe/Amsterdam exited 0 and updated 1724 targets. The image is `generated.x86_64/r5-os.image`, 2 GiB. About was relinked after `r5_whatsnew.txt` was copied, and `r5_os_version.h` says build 10019. The package check printed `mesa_packages_ok` (libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel`, Lavapipe, LLVM 21 runtime, Vulkan loader) and no Mesa 22 package. Not booted. Not tested on a LAN. The VirtualBox session started earlier is still the previous disk.

### Deskbar closes the other menu bar before opening one

No commit on `upstream/master` (fetched 1 Nov 2026) names #20099. The leaf menu and the running-apps list are two `BMenuBar`s in one window. Each one installed the window’s menu semaphore and left the other menu tracking.

`scripts/apply-deskbar-bugs.py` now stops the other menu bar before `StartMenuBar` tracks, and waits outside the window lock so that tracking thread can exit. When a menu finishes, it clears the window semaphore only if that semaphore is still its own. A popup does the same, so an older popup thread does not wipe a menu bar that started later.

`jam -q '<libbe!x86_64>interface_kit.o'` compiled `MenuBar.o`, `PopUpMenu.o`, and `Window.o` (47 targets updated, exit 0). Not booted. The other open Deskbar tickets (crashes, layout, enhancements) are not in this change.
### Large fonts no longer clip box and separator labels

No commit on `upstream/master` (fetched 1 Nov 2026) names #11625, #18518, or #18813. `scripts/apply-ui-bugs.py` changes the label metrics.

A `BBox` label used to shift its baseline up by one sixth of the line height, which drew the top of the glyphs above y=0. The label box is now the full line, including leading, and the baseline sits on the ascent (#11625, #18518). A `BSeparatorView` section title uses that same line height and is centered in the label band when the view is taller (#18813).

The same script used to insert another `#include <Region.h>` into `ListView.cpp` on every apply, because the new text still contained the old `#include <Window.h>` line. A second apply now leaves a single include.

`jam -q '<libbe!x86_64>interface_kit.o'` compiled `Box.o`, `SeparatorView.o`, and `ListView.o` (47 targets updated, exit 0). Not booted.
### Control colors for flat buttons, scrollbar arrows, and plain text

Haiku’s June 2025 control-color rework (`91bed92`, “use B_CONTROL_BACKGROUND_COLOR”) is already in this tree. Nothing newer on `upstream/master` (fetched 1 Nov 2026) names #19629, #19630, #20418, or #20372. Gerrit 11871 for #20372 was abandoned. The leftovers are applied from `scripts/apply-darkmode.py`.

Scrollbar arrow buttons use `B_CONTROL_BACKGROUND_COLOR` and `B_CONTROL_TEXT_COLOR`. The trough stays the panel color (#19630). A flat `BButton` takes its parent’s low color, so hover does not jump to the control gray (#19629). `SetViewColor()` still clears the UI color; that face is now the button color, with black or white text when the control text would have no contrast (#20418). High and low color changes inside an overridden `Draw` stay ignored, which is what LibLayout in Wonderbrush relies on. A non-stylable `BTextView` updates its single run when the document text color changes (#20372). Styled runs are unchanged: a run does not record which UI color it came from.

`jam -q '<libbe!x86_64>interface_kit.o'` from `generated.x86_64` updated 47 targets and exited 0, including `Button.o`, `ScrollBar.o`, and `TextView.o`. Not booted. R5OS sign-in, lock, and Users buttons do not call `SetViewColor()` on the button, so they keep the system control color.

### GLTeapot draws on the VirtualBox daily

2026-11-01 about 9:21 Europe/Amsterdam. The guest clock read 8:21 because the VM uses UTC. The user opened GLTeapot on the daily VirtualBox session. The window showed a red teapot and 387 FPS. That disk is the Mesa 25 / libglvnd image. No renderer string was captured on that boot. The 5625U was not the machine.

The same session reached ACPI S5 (`ACPI: Entering S5 power state` in `VBox.log`, about 88 seconds after power on) and VirtualBox stayed in `POWERING_OFF` with no `POWERED_OFF`. The window froze on “Shutting down...”. The NIC is bridged to the MediaTek MT7921. The process was ended from the host. That hang is not fixed here.

`docs/GRAPHICS.md` is the GPU plan for this image. `docs/LOOK.md` is the window-chrome plan. `docs/KERNEL.md` is the CPU audit. Those files had only existed on the local `cursor/graphics-plan` checkout, whose `distro/BUILD` was still 10013. The source edits in that checkout already match `main`.

### OpenGL apps compile against libglvnd, not Mesa 22

`scripts/pin-mesa-packages.sh` runs before jam. It replaces the HaikuPorts pin `mesa-22.0.5-3`, `mesa_devel-22.0.5-3`, and `mesa_swpipe-22.0.5-3` with libglvnd 1.7.0, Mesa 25.3.6, `mesa_devel` 25.3.6, Lavapipe, the LLVM 21 runtime, and the Vulkan loader. `mesa_devel` carries the Mesa 25 headers and libglvnd's `libGL.so` (the real file, not a symlink into a Mesa 22 base package). Haiku's `GLView.h` and `GLRenderer.h` from the old devel package are copied unchanged.

The image solver no longer sees Mesa 22. `scripts/pin-mesa-packages.sh` builds `download/r5-haikuports.repo` from the HaikuPorts catalog with those four Mesa 22 packages removed, and `download/r5-opengl.repo` for the R5OS packages (a different vendor, so they cannot share one catalog). `distro/UserBuildConfig` points the image at those files and installs the packages. A solver check with `glu` and `haiku.hpkg` asked for `libglvnd-1.7.0-1`, not Mesa 22. `jam -q @r5-raw` then exited 0 and updated 1230 targets. It extracted `mesa-25.3.6-1` and relinked GLTeapot. `develop/lib/libGL.so.1.0.0` in that tree is 954288 bytes, the libglvnd file. The image check printed `mesa_packages_ok`: libglvnd, Mesa 25, `mesa_devel`, Lavapipe, the LLVM 21 runtime, and the Vulkan loader, and no Mesa 22 package. That jam itself was not booted. The VirtualBox sitting is the section above. No new build number.

### About on the Mesa image still showed Build 10017

The version line in About was already Build 10018. What's new was still the Build 10017 license text. `scripts/jam-r5-raw.sh` wrote `r5_os_version.h` and did not copy `distro/WHATSNEW`. The What's new resource is rebuilt when `AboutSystem.rdef` is newer than the last resource compile, not when only the text file changes. The script now copies What's new and touches that rdef before jam. No new build number: this is the Build 10018 section that was already in `distro/WHATSNEW`.

The daily `haiku.hpkg` was rebuilt (`jam -q haiku.hpkg`, exit 0) and replaced on `r5-os.image`. The AboutSystem inside it contains `R5OS 2026.1-dev Build 10018 - OpenGL`. Mesa 25 and libglvnd stayed on that image. VirtualBox was pointed at the updated disk. The About window was not opened from here.

### Mesa 25 replaces Mesa 22 on the daily image

Build 10018. The jam pin still downloads Mesa 22.0.5 so OpenGL apps can compile. `scripts/install-mesa-packages.sh`, run at the end of `scripts/jam-r5-raw.sh`, then removes `mesa-22.0.5-3`, `mesa_devel-22.0.5-3`, and `mesa_swpipe-22.0.5-3` from `r5-os.image` and adds the hpkgs in `distro/packages/x86_64`: libglvnd 1.7.0 (`libGL.so.1`), Mesa 25.3.6 (`libEGL_mesa.so.0` plus `50_mesa.json` with `./libEGL_mesa.so.0`), Lavapipe, a private LLVM 21.1.8 runtime that provides only `lib:libllvm_21` (so it does not clash with `llvm12_libs` on `liblto` / `libremarks`), and the HaikuPorts Vulkan loader 1.4.367.

QEMU test image before this jam, TCG, one CPU, GTK, no KVM (this user is not in group `kvm`). `/boot/home/mesa-test.txt`: `renderer=llvmpipe (LLVM 21.1.8, 128 bits)`, `vendor=Mesa`, `version=4.5 (Compatibility Profile) Mesa 25.3.6`. The probe locked, swapped, unlocked, and exited. An earlier boot of that same image showed GLTeapot with a drawn teapot, and Get Info on `libGL.so.1` showed package 1.7.0-1. The regular image was still Mesa 22 at that point. Lavapipe was not the library that probe loaded. The 5625U was not used.

### Licenses tab uses What's new headings and Haiku's component list

Build 10017. Section titles on the Licenses tab are green and bold, like What's new. Lines that are only a web address are links. After that summary, the tab lists the same components Credits already lists (libprint and the rest), with the same license-file and site links. Credits itself is unchanged. The first compile failed because this Haiku `BString` has no `Contains`; the heading test uses `FindFirst(".")`. AMD64 jam then finished with exit 0 (1105 targets). The tab was not opened from here.

### About has a Licenses tab

Build 10016. About's Credits tab no longer carries the VirtualBox GPL paragraph. That text, plus the R5OS, Haiku, and AirPlay license notes, is a Licenses tab. Credits still names people and projects, including a short VirtualBox line. The tab words are `distro/about/licenses.txt`. AMD64 jam of the SPDX headers (pull request 92) finished with `jam_exit=0` and updated 1310 targets. The Licenses tab itself has not been opened on a booted screen.

### SPDX headers on R5OS-owned source

Files that R5OS wrote as a whole now carry `SPDX-FileCopyrightText: 2026 R5OS Contributors` and `SPDX-License-Identifier: GPL-3.0-or-later`. That covers the new Login pieces (lock screen, session helper, tray), Users & Accounts, the IPv6 and Wi-Fi code, AirPrint, the COM1 clipboard, mDNS, and the AirPlay Deskbar program. Four IPv6 headers had said MIT under a 2026 R5OS copyright. That grant is now GPL-3.0-or-later.

Haiku files copied under `distro/` keep their original copyright and license. Edits inside those files were not retagged. The AirPlay RAOP server stays MIT because the flow follows Shairport. ALAC stays MIT (David Hammerton). tiny-AES-c is tagged Unlicense, and the text is `LICENSES/Unlicense.txt`. `scripts/` still has no SPDX headers. No image was rebuilt for this.

### The Desktop share is named VBox Share

2026-10-31 about 14:25 Europe/Amsterdam. Build **10015**, GUI VM, UART `generated/vbox-daily.log`. VirtualBox rejects a space in a share name, so the host mapping stays `r5share`. Sign-in mounts it at `/boot/home/Desktop/VBox Share`. Serial shows `mounted r5share /boot/home/Desktop/VBox Share`, `vboxsf: mount(r5share)`, and `publish_vnode(): 0`. The guest opened the existing `from-host.txt`. There is no `VbglR0SfCreate: -104` and no panic line. The Desktop icon was not watched from here.

`-104` is `VERR_INVALID_NAME`. The driver was sending `.` and `..` to the host. Those names now stay in the driver: `.` is the directory itself, and `..` is its parent. At the share root the VFS walks out to the Desktop.

A relink of `vboxsf` first produced a read-only `PT_LOAD` and a GNU hash. The kernel did not load that file (`General system error`, and `Bad data` when the segment base was `0x400000`). The module that loads uses `-z noseparate-code -z norelro --hash-style=sysv`: SysV hash, `.rodata` in the executable segment, same shape as the module from the 13:14 boot. `generated/r5-os.vdi` was not replaced. QEMU was not used. Unicode names, a large new file, unmount, and a host disconnect were not tried.

### Daily image follows the VirtualBox window and mounts host shares

2026-10-31 13:14 Europe/Amsterdam. Build **10015**. `HAIKU_IMAGE_SIZE` is 2048. `jam -q @r5-raw` exited 0 and wrote `generated.x86_64/r5-os.image` at 2147483648 bytes. `bfs_shell` reports 1.2G free of 2.0G (622564 blocks of 2048), volume name R5OS. `generated/r5-os.vdi` was not replaced.

Sign in starts `VBoxService`, `/system/non-packaged/bin/r5-vbox-shares`, and `VBoxTray` only when `/dev/misc/vboxguest` exists. The script asks `VBoxControl` for every host share and runs `mount -t vboxsf -p <name>` on `<home>/Desktop/<name>`. QEMU and real hardware do not have that device, so the same sign-in returns without mounting. This image was not booted in QEMU. The user launch file no longer starts a second `VBoxService` or `VBoxTray`.

The GUI VM was powered off and its disk swapped again (`generated/r5-os-vboxid.vdi`, UUID `3eab20a2-9599-4ac2-8353-0f1a436ce727`). The previous 2 GB disk is `generated/r5-os-vboxid-before-pkgfs.vdi`. Host log `VBox.log` opened 2026-10-31T12:10:18Z (13:10 Europe/Amsterdam). At about four minutes it reports graphics capability `0x4` and `Display::i_handleDisplayResize` from 1024×768 to 1160×775. Serial `generated/vbox-daily.log` prints `r5-vbox-shares: mounted r5share /boot/home/Desktop/r5share`, `vboxsf: mount(r5share)`, and `publish_vnode(): 0`. The guest then opened `from-host.txt` and `from-guest.txt` (the files already in `generated/vbox-share`). Lookup of `..` inside the share returns -104. No panic line. The picture was not watched from here, and pointer clicks after the mode change were not tried. Unicode names, a new large file, unmount, and a host disconnect were not tried. Files inside a share stay uid 0; the mount point is the signed-in Desktop, which is mode 0700 before the mount. Another account was not used.

Two earlier 2 GB boots missed the share. The first script was saved with CR LF, so Haiku `sh` did not run it. The second script was installed with `AddFilesToHaikuImage system bin`. packagefs covers `/boot/system`, so `/system/bin/r5-vbox-shares` was not there. The session log says `r5-vbox-shares: No Error (127)` about 14 ms after launch. `VBoxTray` still started on that boot, which is why the window size followed while the share stayed unmounted. The script is now `system non-packaged bin`, same place as `VBoxTray`. Writes to `/dev/dprintf` from the script did not appear on the serial log. The `vboxsf` lines did.

### Daily image installs the VirtualBox guest additions

2026-10-31 11:49 Europe/Amsterdam. The daily raw image `generated.x86_64/r5-os.image` was jammed again (`jam -q @r5-raw`, exit 0) after build **10014**. `bfs_shell` on that image lists the guest files and the source. Free space afterwards is 368.0 MB of 1.2 GB. `generated/r5-os.vdi` was not replaced.

At 12:00 the throwaway VM was powered off and its disk was swapped for a fixed VDI of that image (`generated/r5-os-vboxid.vdi`, same UUID as before). The previous throwaway disk is `generated/r5-os-vboxid-before-daily.vdi`. Around 12:04 the user said this boot works. The window resize did not follow. The host share `r5share` was still defined on that VM and was not mounted; this image has no automount job. No panic line was in `generated/vbox-daily.log` through the USB driver. First-boot processing of 129 packages is in that log. Tracker on a share was not tried.

Installed under `/boot/system/non-packaged`:

- `add-ons/kernel/generic/vboxguest` (274024 bytes) and `add-ons/kernel/drivers/dev/misc/vboxguest` (20976 bytes, the device stub built as `vboxdev`)
- `add-ons/kernel/file_systems/vboxsf` (55256), `add-ons/kernel/drivers/dev/graphics/vboxvideo` (34424), `add-ons/accelerants/vboxvideo.accelerant` (43632)
- `bin/VBoxService`, `bin/VBoxTray`, `bin/VBoxControl`

`data/licenses/virtualbox/` has the unmodified GNU GPL version 3 (`COPYING`, 35147 bytes), `VirtualBox-7.2.22.tar.bz2` (257289481 bytes, sha256 `440a2b3eeb46e7e992961b70ef24f43e95b3c99fa2ab30aac20b963ef9e91ad7`), `virtualbox-7.2.22-r5os.patch`, the build scripts, and `SOURCE.txt`. The same GPL text is also the packaged `GNU GPL v3` file. About R5OS states that this build includes GNU GPL version 3 code and names that directory. The What's new text for build 10014 is in the About binary. The user launch file in `haiku.hpkg` starts `VBoxService` and `VBoxTray`. It does not mount a share.

The mouse filter is not installed. VirtualBox clipboard mode and drag-and-drop stay off in the product script. The clipboard R5OS starts is still the serial one. The binaries and the tarball are not in git. `scripts/apply-vbox-guest.py` stops if the local build or the tarball is missing.

A first jam in this same run deleted the previous `r5-os.image`. Jam was started without `PWD`, so the image script names stayed relative and dash could not source them. The second jam was a login shell, `PWD` was set, and it wrote the image above. The one shell copy remains the throwaway result from earlier today. Tracker was not opened on a share. QEMU was not used for this image.

### Throwaway shared folder reads and writes one small file

2026-10-31 10:54 Europe/Amsterdam, headless, throwaway `generated/r5-os-vboxid.vdi`, UART `generated/vbox-sf6.log`. The machine mapping `r5share` is `generated/vbox-share`. A launch job ran `mount -t vboxsf -p r5share /boot/home/vbox-share` after boot. Haiku's `mount` passes the share name with `-p`; a bare extra argument is the device, and this file system rejects a device. Serial shows `vboxsf: connected`, `vboxsf: inited successfully`, and `publish_vnode(): 0`, then an open of `/from-host.txt`. Guest property `R5Sf` is `host=R5OS-SF-HOST` at 09:54:23Z. That matches the 12-byte host file `from-host.txt`. The host directory then also had `from-guest.txt`, 11 bytes: `from-guest` plus the newline from `echo`. No panic line in that log. Tracker was not opened on the share. Unicode names, a large file, unmount, and a host disconnect were not tried. Not the daily image. The binaries are not in git.

Two earlier headless boots never reached `mount`. The kernel loads every file system module while it is still scanning drivers. `init_module` called the host then, before `drivers/dev/misc/vboxguest` existed, and `generated/vbox-sf3.log` stops at `file_systems/vboxsf: B_MODULE_INIT`. The connect now waits for the first mount. That mount then stopped after `vboxsf: guest module loaded` (`generated/vbox-sf5.log`). `VbglR0SfInit` takes `RTSemMutexRequest`, and Haiku `RTSemMutexCreate` created the semaphore with count 0, so the first acquire waited forever. The count now starts at 1. A release compared `OwnerId` with `-1` and did not clear it; it now assigns `-1`. A recursive acquire incremented the thread id; it now increments the recursion count. The mutex code is linked into the `vboxguest` module. `vboxsf` calls it through that module. Drag and drop was not started. It is GitHub issue 89. The build fixes against the Oracle 7.2.22 tarball are `distro/vbox-guest/virtualbox-7.2.22-r5os.patch`. The tarball and the binaries are not in git. Build steps: `distro/vbox-guest/README.md`.

### Throwaway guest mode follows a host size hint

2026-10-31 10:01 Europe/Amsterdam, GUI, throwaway `generated/r5-os-vboxid.vdi`, UART `generated/vbox-resize.log`. This boot stopped at Sign in. Enter with an empty password opened the desktop. Deskbar listed Tracker only. `VBoxTray` was not in that list. The app resource `BEOS:APP_FLAGS` is now in the `VBoxTray` binary (`B_BACKGROUND_APP`); the earlier binary had the resource file but it was not attached.

`VBoxDisplayService` runs in that same process, next to the clipboard. It is not a Deskbar replicant. After the desktop was up, `setvideomodehint 1280 800 32` changed the guest framebuffer from 1024×768 to 1280×800. The picture stayed the normal desktop (icons, Deskbar, the Haiku logo). No panic line in the serial log. At 10:06 Europe/Amsterdam the user showed the VirtualBox window itself: the desktop filled that window, Deskbar still listed only Tracker, and the guest clock read 9:06 AM. They said the resize works. Pointer clicks after the change were not tried. Not the daily image. The binary is not in git.

### Throwaway Screen prefs names the VirtualBox display

2026-10-30 22:59 Europe/Amsterdam, GUI, throwaway `generated/r5-os-vboxid.vdi`, UART `generated/vbox-video3.log`. Screen says `Virtual display (VirtualBox Graphics Adapter)`, 1024×768, 24 bits/pixel. The desktop is the normal Tracker picture, not a black screen. Serial shows `VBoxVideo: found device graphics/vd_80EE_BEEF_000200` and `loaded driver .../drivers/dev/graphics/vboxvideo`. The packaged `vesa` driver still loads; Screen is not using it. No panic line in that log. `VBoxDisplayService` is not running, so a window resize was not tried. The 60 Hz line in Screen is the placeholder timing in the driver, not a measured rate. Only the current mode is published. Not the daily image. The binaries are not in git.

The first copy of this driver was installed under `drivers/graphics/` (no `dev/`). The 20:42 GUI boot (`generated/vbox-video.log`) loaded only `.../dev/graphics/vesa` and had no `VBoxVideo:` line. After the move into `drivers/dev/graphics`, the 20:46 boot loaded the driver and mapped the framebuffer (`assuming memory type` for `0xe0000000`), and Screen still said `VESA driver (Generic VESA)`. The accelerant had been linked as a guest executable with hidden visibility, so `get_accelerant_hook` was not in the dynamic symbol table and `app_server` fell back to VESA. The hook is now default-visibility, and the link keeps it (`-Wl,--export-dynamic -Wl,-u,get_accelerant_hook`). That binary is 43632 bytes at `non-packaged/add-ons/accelerants/vboxvideo.accelerant`. Deskbar still lists `VBoxTray` on this boot.

### Throwaway additions boot in QEMU with no guest device

2026-10-30 20:21 Europe/Amsterdam. QEMU `pc`, TCG, `qemu64`, 2048 MB, 2 CPUs, std VGA, serial `generated/qemu-nodevice.log`. The disk was `generated/r5-os-vboxid-emptyout.raw`, a copy of the throwaway that already has `vboxguest`, `VBoxService`, and `VBoxTray`. KVM in this shell returned permission denied, so this was TCG. PCI in the log is `8086:1237`, `8086:7000`, `1234:1111` (display), `1af4:1000` (virtio-net), and `8086:2668` (HDA). No `80ee:cafe` and no `80ee:beef`. `vesa_init()` completed. `package_daemon` registered `/boot`. No panic line and no `vboxguest` load line. The guest tray log, mtime 20:22, ends with `tray start` and `init -102` (`VERR_FILE_NOT_FOUND`). That is `VbglR3Init` not finding `/dev/misc/vboxguest`, after the tray job had already waited for `app_server`. `vbox-service.log` stayed at 20:05: the service opens that file only after init succeeds. The screen was not watched, so Tracker and Deskbar were not seen. QEMU was killed from the host after the log had stopped growing; the guest did not shut itself down. The VirtualBox VM was not the disk under test.

### StyledEdit text reached Windows Notepad

2026-10-30 20:13 Europe/Amsterdam, GUI, throwaway `generated/r5-os-vboxid.vdi`. The user copied text in StyledEdit and pasted it into Windows Notepad. Deskbar listed `VBoxTray` next to Tracker. That entry is the running clipboard process, not a replicant. `VBoxTray.rdef` now sets `B_BACKGROUND_APP` so Deskbar skips it. The binary on this disk does not have that flag yet. Not the daily image.

### Throwaway VBoxControl no longer panics, and a 32 KB host paste is on disk

2026-10-30 19:55 Europe/Amsterdam, headless, `generated/r5-os-vboxid.vdi`, UART `generated/vbox-panicfix.log`. The 19:12 panic was `_mutex_lock()` with interrupts disabled inside `RTSemEventMultiCreateEx`, called from `vgdrvWaitAlloc` during an HGCM disconnect. Two Haiku IPRT mistakes in the VirtualBox tree outside git caused it. `RTSpinlockAcquire` stored the saved interrupt state before `acquire_spinlock`. The other CPU, including the ISR where `disable_interrupts()` returns 0, could overwrite that field, and release then left interrupts disabled for the next `malloc`. The state is now saved in a local, stored only after the lock is owned, and copied out before release. `RTSemEventMultiCreateEx` and `RTSemMutexCreate` also treated `create_sem` failure (`sem_id < B_OK`) as success, so a real semaphore was freed and the call returned `VERR_TOO_MANY_SEMAPHORES`. An interruptible HGCM wait then returned `VERR_INTERRUPTED` immediately. The driver sent `HGCMCancel2`, and the tray’s `MSG_CANCEL` completed a host message that was not in process. That is the `HGCMThread::MsgComplete` assert (`0x80000003`) about 50 ms after `New Clipboard API enabled`. The success test is now `sem_id >= B_OK`, matching the event and fast-mutex files.

This boot ran the `VBoxControl` probe. The guest set `R5Clip=ready` at 19:55:58. There is no panic line in the UART log and the kernel debugger did not open. The host cleared the clipboard with `Clipboard.Clear()` (`Set-Clipboard -Value ""` throws here). The guest file `/boot/home/vbox-clip-empty-in.txt` is 0 bytes, and the guest property recorded `empty-in bytes=0`. The host then placed 32768 bytes (`R5OS-LARGE-0000` through `R5OS-LARGE-2047`). After ACPI S5 (`VBox.log` `ACPI: Entering S5 power state` at 00:03:58, `showvminfo` `poweroff`) the image still had `/boot/home/vbox-clip-large-in.txt` at 32769 bytes: those 2048 lines, plus one trailing newline from `clipboard -p`. The same `VBox.log` has `New Clipboard API enabled` and no `Assertion Failed`. Headless, so no Windows dialog. The tray log still shows eight `wait -39` lines and then `stop -2909` (`VERR_HGCM_TOO_MANY_CLIENT_CALLS`). A second `tray start` in that log received `msg 3 formats 1`, which is the large paste. Not the daily image. The modules are not in git.

A later headless start the same evening (20:02 Europe/Amsterdam) put `R5OS-MARKER` on the Windows clipboard first. The guest recorded `empty-in bytes=12` (the marker plus `clipboard -p`’s newline). `clipboard -r` then left the Windows clipboard as `R5OS-MARKER` (length 11) at 20:03:50. The host service ignores an empty format report on purpose, so a guest clear does not wipe Windows. A rebuild that reports format 0 on a local clear was booted at 20:06; at 20:07:11 the host clipboard was still `R5OS-MARKER`. That direction is not shown. No `Assertion Failed` in that boot’s log either. `showvminfo` afterwards was `poweroff`.

### Throwaway desktop, mouse, resize, and guest shutdown

2026-10-30 19:16 Europe/Amsterdam, GUI, `generated/r5-os-vboxid.vdi`, `VBoxControl` probe replaced with `exit 0`. The user had already seen boot icons, sign-in, Tracker, and Deskbar on the previous start of this disk. On this start, movement, clicks, and scrolling worked. The pointer reached the edge of the guest screen and then moved on in Windows. The guest desktop did not follow a VirtualBox window resize, and shutdown from the guest produced no Windows error. `showvminfo` then said `powered off` since 18:16:33Z. `VBoxDisplayService` was not running, so the picture stays the VESA framebuffer. No second mouse was added. Not the daily image. The `VBoxControl` panic from 19:12 is not fixed.

### VBoxControl panics the throwaway after the desktop is up

2026-10-30 19:12 Europe/Amsterdam, GUI boot of `generated/r5-os-vboxid.vdi`. The user saw the boot icons, the sign-in screen, a successful sign-in, Tracker, and Deskbar. Then the kernel debugger opened. Thread `VBoxControl` on CPU 1. Panic: `_mutex_lock()` called with interrupts disabled. The stack runs from the syscall through `vgdrvHaikuIOCtl`, `VbglR3CommonIOCtl`, `vbglR3HGCMInternalDisconnect`, `vgdrvWaitAlloc`, and `RTSemEventCreateEx` into `RTMemAllocEx`. The probe that calls `VBoxControl` was replaced with `exit 0` on the next image so the desktop can be looked at. `VBoxTray` is still started. The panic is not fixed. Not the daily image.

### Throwaway guest can place 32 KB of text on the Windows clipboard

Headless boot of `generated/r5-os-vboxid.vdi`, 2026-10-30 18:49 Europe/Amsterdam, UART `generated/vbox-clip5.log`, this VM’s clipboard mode still bidirectional. The guest built 2048 lines `R5OS-LARGE-0000` through `R5OS-LARGE-2047` (32768 bytes) with `clipboard -i`. `Get-Clipboard -Raw` on the host returned that length, that first line, and that last line. `VBox.log` for that start (opened 17:49:34Z) still has the `HGCMThread::MsgComplete` assert at 00:00:27 from `VBoxSharedClipboard.DLL`. Headless did not show the Windows `0x80000003` dialog, and the transfer above happened after the assert. The log then records power off to `OFF` at 00:03:36. `showvminfo` afterwards said `aborted`. The probe’s paste file was not on the disk; it never called `sync`. `Set-Clipboard -Value ""` throws in this PowerShell, and the host clipboard was already empty, so an empty transfer is not shown. The host’s 32768-byte paste was only acknowledged (`R5Clip=large-in`). Product clipboard mode is still off. COM1 is still the shipped path. Not the daily image.

### Throwaway clipboard text works in both directions

`VBoxService` 7.2.22 has no Haiku clipboard service. `VBoxTray` normally installs a Deskbar replicant and exits; that replicant also starts the display service. For this test `ReadyToRun` keeps the process up, starts only `VBoxShClService`, and does not load the replicant. The first bidirectional boot on `generated/r5-os-vboxid.vdi` (serial `generated/vbox-clip2.log`) logged `connect 0` and then no text. The host log shows `VERR_CANCELLED` while reporting formats to the guest and `VERR_TIMEOUT` while reading from the guest. `VbglR3ClipboardGetHostMsgOld` returned `VERR_INTERRUPTED` (-39) and the service thread treated that as fatal. A host read was also posted to the looper and the thread waited again immediately. The thread now cancels and retries an interrupted wait, and it handles the host message before the next wait. That change is in the VirtualBox tree outside git.

The next boot (host clipboard log has no timeout line) wrote `/boot/home/vbox-clip-paste.txt` with `R5OS-CLIP-IN` between `PASTE-BEGIN` and `PASTE-END`. `clipboard -c R5OS-CLIP-OUT` then left that string on the Windows clipboard. The tray log shows `msg 2 formats 1` and `write 0`. UART was `generated/vbox-clip3.log`. Empty clipboard and a large paste were not tried. Clipboard mode on `scripts/run-vbox-r5.ps1` is still disabled. COM1 is still the shipped path. The binaries are not in git and not on the daily image. `VBox-GPL-3.0.txt` on this throwaway is `LICENSES/GPL-3.0-or-later.txt`.

Shutdown of that boot opened the debugger. Team `VBoxTray`, thread segment violation in `__dynamic_cast`, called from `BApplication::_WindowQuitLoop` during `Quit`. `VBoxTray` is built `-fno-rtti`. libbe `dynamic_cast`s every looper to `BWindow` on quit, and this `BApplication` had no typeinfo. The original `ReadyToRun` called `exit(0)`, so it never reached that walk. `VBoxTray_CXXFLAGS` now adds `-frtti` (the template still passes `-fno-rtti`; `-frtti` is last). The following boot entered ACPI S5 (`VBox.log`: `ACPI: Entering S5 power state`) about 45 seconds after start. The serial log has no segment violation. Earlier in that same boot, at about 30 seconds, `VirtualBoxVM.exe` raised `0x80000003` (breakpoint) on the `ShClipboard` thread. `VBox.log` shows `HGCMThread::MsgComplete` asserting `(pMsg->m_fu32Flags & HGCM_MSG_F_IN_PROCESS) != 0` at `HGCMThread.cpp:578`, from `VBoxSharedClipboard.DLL` after `Unknown guest function`, 20 ms after `New Clipboard API enabled`. The Windows dialog says the exception is at `0x00007FFF19CEB61E`, which is that log's `ExceptionAddress`. VirtualBox then reported the machine state as `aborted`.

### Guest version properties show up on the throwaway

`vminfo` writes were `VERR_ACCESS_DENIED` because Haiku user-memory locks always asked for a writable mapping (`B_READ_DEVICE`) and, when the caller passed no team, looked the address up in the kernel team. Read-only guest strings then fault with `B_PERMISSION_DENIED`, which IPRT prints as `VERR_ACCESS_DENIED`. `memobj-r0drv-haiku.c` in the VirtualBox tree outside git now locks `B_CURRENT_TEAM` for that case, asks for a writable mapping only when the access includes write, and repeats that team and those flags on unlock. That file is not in this git tree.

The first boots of that module panicked. Serial `generated/vbox-vminfo.log` (two starts) and `generated/vbox-vminfo2.log` (cold start) stop in `interrupts.cpp`: interrupts were enabled after the handlers for vector 20. The routing table maps PCI `0:4` (`80ee:cafe`) to GSI 20. On the cold start the driver line was printed and the panicked thread was `VBoxService`, already waiting. No `/VirtualBox/GuestAdd/Version` was written. `vgdrvHaikuISR` now calls `disable_interrupts()` before it returns.

The next start (about 16:35 Europe/Amsterdam, serial `generated/vbox-vminfo3.log`) loaded `drivers/dev/misc/vboxguest`, did not panic, and reached the first-login script. `VBoxManage guestproperty enumerate` shows `/VirtualBox/GuestAdd/Version` `7.2.22`, `Revision` `175245`, `VersionExt` `7.2.22`, and `/VirtualBox/GuestInfo/OS/Product` `Haiku` with release `R1~beta6+development` and version `r5-2026.1-dev Oct 26 2026 16:02`. `LoggedInUsers` was `0` at that sample. At 16:38 Europe/Amsterdam the screen showed ProcessController on the desktop: Tracker, Deskbar, Login, `vbox-probe.sh`, and `VBoxService` (`-f -v -v`). Clipboard mode stayed disabled. The modules and `VBoxService` are not in git and not on the daily image.

### VBoxService stays up on the throwaway and sets the guest clock

`VBoxService` 7.2.22 was already on `generated/r5-os-vboxid.vdi`. The first launch that wrote a log (about 15:40 Europe/Amsterdam) opened `/dev/misc/vboxguest` and exited immediately. Every ioctl came back as `VERR_INVALID_POINTER`. Haiku's `ioctl()` reads a `size_t` after the buffer pointer; the VirtualBox userspace call did not pass one. `vbglR3DoIOCtlRaw` in the VirtualBox tree outside git now passes the request size on Haiku. That change is not in this git tree.

The next boot (about 15:47 Europe/Amsterdam, serial `generated/vbox-svc3.log`) reached the first-login script with no panic. `/boot/home/vbox-service.log` says `All services started`. `timesync` set the guest clock from the host. `vminfo` connected and then every guest-property write returned `VERR_ACCESS_DENIED`, including `/VirtualBox/GuestAdd/Version`. `VBoxManage guestproperty enumerate` showed only host-written properties. The driver open used `VMMDEV_REQUESTOR_USERMODE` alone. It now also sets `VMMDEV_REQUESTOR_USR_ROOT` when `geteuid()` is 0. The boot after that rebuild (about 15:52 Europe/Amsterdam, serial `generated/vbox-svc4.log`) again reached the first-login script. The service log shows `timesync time set` at 14:52:07, 14:52:17 and 14:52:27 UTC and the same `VERR_ACCESS_DENIED` on property writes. The screen was not watched. Memory ballooning was disabled. Clipboard mode stayed disabled. The modules and `VBoxService` are not in git and not on the daily image.

### VirtualBox guest driver claims 80ee:cafe on a throwaway disk

The first throwaway that reached userspace (`generated/vbox-vboxsvc.log`, 2026-10-30) loaded `VBoxService` and then refused `drivers/dev/misc/vboxguest`: `could not resolve symbol 'VBoxGuest_RTLogLoggerEx'`. Haiku resolves driver imports before `init_hardware`, and that symbol existed only in the `generic/vboxguest` module, which is not loaded yet. A no-op definition was added in the VirtualBox 7.2.22 tree outside git (`VBoxGuest-haiku-stubs.c`) and `vboxdev` was relinked. The same link also rebuilt `vboxsf`, `vboxvideo`, the mouse filter, and the accelerant. Those were not installed. The stub does not print, so driver `LogRel` lines stay silent.

The replacement disk `generated/r5-os-vboxid.vdi` (UUID `3eab20a2-9599-4ac2-8353-0f1a436ce727`) was started about 15:27 Europe/Amsterdam with clipboard mode disabled and UART1 writing `generated/vbox-identify.log`. Serial shows PCI `80ee:beef` and `80ee:cafe`, then `loaded driver .../misc/vboxguest`. That message is printed only when `init_hardware` succeeds, and that function returns the `vboxguest` module init, which returns success only after the PCI attach. No `Symbol not found`, no `RTR0Init failed`, no panic. The log reaches the first-login script and then stays quiet while the VM keeps running. The screen was not watched. The version word was not printed. `VBoxService` has no line in this log. The modules are not in git and not on `r5-os.vdi` or `r5-os-loginfix.vdi`. The VM was left on this throwaway.

### Sign in lists root, and Tracker crashes on the desktop shelf

The GUI boot of `generated/r5-os-rocketfix.vdi` reached Sign in. The list showed `root` and `user`. That disk's `/etc/passwd` gives `root` home `/boot/home`, which is the same test Sign in uses for a person. The system account `root` is no longer listed. On sign-in its home is moved to `/boot/system/var/root` and the shadow password is locked (`!`). An administrator that is still uid 0 and named `user` stays on the list.

Signing in as `user` opened the debugger: Tracker thread, segment violation in `BShelf::_InitData`, called from `BDeskWindow::Init`. The desktop pose view was never created, and the shelf still called `AddFilter` on that null view. This disk already had `user` at uid 1001, so migration did not `chown` `/boot/home`. Login then sets that tree to mode `0700` while it is still owned by uid 0, and Tracker (uid 1001) cannot open `/boot/home/Desktop`. Migration now gives that tree to the account when the home directory is still owned by uid 0. If the pose view is still missing, Tracker logs the status to `/dev/dprintf` and returns instead of entering the debugger.

Jam AMD64 2026-10-30 14:55 Europe/Amsterdam: image built, no jam error line. The fresh image was then given the same test accounts as the rocket disk (`root` home `/boot/home`, `user` uid 1001, empty password, session file `user`). GUI start of `generated/r5-os-loginfix.vdi` the same afternoon: serial shows `package_daemon` registering `/boot` and `vesa: vesa_init() completed successfully!`, then `Running first login script ...`. No `Segment violation`, no `Tracker: desktop` line, no `vesa_uninit`. At 15:01 Europe/Amsterdam the screen showed Tracker on `/boot/home` (Desktop, Documents, and the other home folders), Deskbar, and About R5OS 2026.1-dev Build 10013. About reported Walter, two processors (AMD Ryzen 5 5625U), and 2048 MiB. The guest clock was 2:01 PM UTC. COM1 was the log file. The daily `r5-os.vdi` was not attached.

### Second boot stuck on the rocket icon

After the first sign-in, `/boot/system/settings/r5-desktop-session` contains the account name. On the next boot the registrar (still uid 0, still inside `ReadyToRun()`) built the MIME database and called `getpwnam()` to find that account's home. `getpwnam()` sends the question back to the registrar, which is not reading messages yet. The registrar never finishes startup, `package_daemon` never logs, and the splash stays on the rocket (`BOOT_SPLASH_STAGE_7_RUN_BOOT_SCRIPT`). CPU goes idle. Serial on that boot stops at the same slab line where the first boot printed `REG: Failed to open shadow passwd`.

`mime_user_settings_path()` now reads `/etc/passwd` itself. The first-login script `default_deskbar_items.sh` was CRLF, so its shebang was `/bin/sh\r` and the runtime loader could not open it (`needed by <NULL>`). `apply-login.py` writes that script with LF line endings. That script failure is what the first boot printed just before `vesa_uninit()` and a reset; it is not what keeps the next boot on the rocket.

Jam AMD64 2026-10-30 14:11 Europe/Amsterdam: `jam_exit=0`. The new image was given a `r5-desktop-session` of `user` and a passwd line `user` uid 1001, which is the state that deadlocked the previous disk. Cold start of `generated/r5-os-rocketfix.vdi` the same afternoon: serial shows `smp: booting 1 other cpu`, `package_daemon` registering `/boot`, and `vesa: vesa_init() completed successfully!`. The log then goes quiet at the audio driver and the guest CPU is idle. No `vesa_uninit`, no second boot loader, no `/bin/sh` error. The screen was not watched from here. COM1 is still the log file. The daily `r5-os.vdi` was not attached.

### Licensing policy

`docs/LICENSING.md` is the multi-license policy. New R5OS-owned files default to GPL-3.0-or-later when R5OS can license the whole file. Haiku headers stay as they are. GPL-2.0-only is not relicensed to GPL-3.0 and is not combined with GPL-3.0 in one work. `THIRD_PARTY_NOTICES.md` lists what has actually been identified. `LICENSES/` has the unmodified GPL-3.0 and MIT texts. The Haiku tree has not been audited file by file. About credits, REUSE lint, and a release SBOM are not built. VirtualBox 7.2.22 Haiku additions are GPL-3.0-only and are not in git or the image. Userspace may ship later as separate GPL-3.0 programs. Kernel modules stay out pending review of how far GPL-3.0 reaches into the kernel. `README.md` points at the policy, the notices, and `LICENSES/`.

### Boot loader no longer waits forever for a missing CPU

The BIOS boot loader’s `smp_boot_other_cpus()` waited, with no timeout, until the next CPU’s trampoline cleared a stack word. On this VirtualBox VM that wait often never finished, so the kernel never started and the splash icons stayed gray. Delivery-status waits had the same problem. Each CPU now gets three attempts. A delivery bit that stays set is reported and the startup sequence continues. If a CPU still does not answer within about a second, the loader sets `num_cpus` to the CPUs that did start and enters the kernel. Later CPUs are not started, so the kernel does not wait on a hole in the CPU list. The waits use a bounded `pause` loop. They do not call `spin()`, because that BIOS wait can fail to return. The source copy is `distro/boot/bios_ia32/smp.cpp`, installed by `scripts/apply-kernel-bugs.py`.

Jam AMD64 2026-10-30 13:16 Europe/Amsterdam: `jam_exit=0`. Cold start of that image (`generated/r5-os-smpfix.vdi`, not the daily VDI) at 13:18 Europe/Amsterdam: serial shows `smp: booting 1 other cpu`, then the kernel, `CPU 1`, and `scheduler_init: found 2 logical cpus`. The same boot reached the first-login script and then the boot loader started again (`vesa_uninit`). A second pass in that same run again entered the kernel with CPU 1. No `did not start` line, so the give-up path was not hit. The screen was not watched. The VM was put back on `generated/r5-os.vdi`, which still has the old loader. COM1 is the pipe again.

### Lock screen (build 10013)

Deskbar → Lock screen covers the session. Tracker and Deskbar stay running. Login does not call `StartSession` and does not clear the session file. Unlock checks the password of the name in the session file. Log out on that screen is the existing log-out path (apps quit, Sign in returns). An open preference grant is cleared, so those windows lock again while the screen is locked.

A keyboard shortcut was tried and then dropped. Ctrl+Shift+L is taken by Windows before VirtualBox sees it. Ctrl+Alt+L did not arrive either, including from the VirtualBox soft keyboard. The `r5_lock` input filter is no longer built. The shortcut is [#72](https://github.com/nickrusman-bot/r5os/issues/72). A menu shortcut was not set: Deskbar shortcuts always add the command key.

Jam AMD64 2026-10-30 09:33 Europe/Amsterdam: `jam_exit=0` (Login, Deskbar, `r5_lock`, image).

VBox 2026-10-30 09:37 Europe/Amsterdam (guest 08:34, UTC): the cover, password, Unlock, Log out, Restart, and Shut down were on screen. The panel had no title tab. Ctrl+Shift+L did nothing. Control turns the `bytes` field into a control character, so the filter never saw the letter L. It now uses `raw_char`. The panel is a titled window, same look as Sign in, on the blue cover.

Jam AMD64 2026-10-30 09:43 Europe/Amsterdam: `jam_exit=0` (Login, `r5_lock`, image).

VBox 2026-10-30 09:45 Europe/Amsterdam (guest 08:44, UTC): the title tab is right. Ctrl+Shift+L still did nothing. On Windows that chord switches the keyboard layout before VirtualBox sees it. The filter now also accepts Ctrl+Alt+L (Alt is the Haiku command key). Ctrl+Shift+L stays for a machine that is not behind the Windows shortcut.

Jam AMD64 2026-10-30 09:54 Europe/Amsterdam: `jam_exit=0` (the `r5_lock` filter was rebuilt on its own, then the image).

VBox 2026-10-30 09:58 Europe/Amsterdam: Ctrl+Alt+L still did nothing, including from the VirtualBox soft keyboard. The chord is out of the source. Lock screen stays the leaf menu item. `apply-login.py` no longer builds `r5_lock`. Not jammed or booted after that drop. The image from 09:54 Europe/Amsterdam still contains the filter; it does not lock the screen because the chord never arrives.

### Machine settings and account checks (build 10012)

Printers, Repositories, and VirtualMemory use the same bottom-right lock as Network. Printer queues resolve to `/boot/system/settings/printers` for every account (`B_USER_PRINTERS_DIRECTORY`). An existing queue folder under `/boot/home/config/settings/printers` is copied once when the system folder is empty. Swap settings are written to `/boot/system/settings/kernel/drivers/virtual_memory`. The kernel reads that file and, if it is missing, the old per-account file. The repository list file moves to system settings. `package-repositories` is group `admin`, and world-writable only while a grant is open, same as network settings. Window positions for Printers and VirtualMemory stay in the signed-in account's settings.

A new account requires a password. Login refuses to delete the name in the session file (Login itself is uid 0, so `getuid()` did not mean the person at the screen). If `rm` of `/boot/home/users/<name>` fails, the reply says the account is gone and the files are not. Add, Disable, and Delete already send `error` 0 on success; that fix is in this image together with the checks above.

Jam AMD64 2026-10-29 23:48 Europe/Amsterdam: `jam_exit=0` (Login, Users, Printers, Repositories, VirtualMemory, kernel, libroot, image).

VBox 2026-10-30 Europe/Amsterdam: build 10012 confirmed on the running image.

Bijhouden in deze overlay-repo. Geen Haiku-hrev. Nieuwste eerst.

## 2026-10-29 (Europe/Amsterdam)

### Your account (build 10011)

Users & Accounts is a card: round picture (initials until you choose one), name, username, email, and Change password. A standard account sees only that card. An administrator, or Unlock, also sees the people list and Add / Disable / Delete. Sign in still lists everyone who can sign in.

Name and password go through Login (uid 0). The signed-in person must type the current password. An administrator setting someone else's password does not. Email and the picture live in that account's `config/settings/r5-account` (mode 0600). Username stays fixed.

Jam AMD64 2026-10-29 22:42 Europe/Amsterdam: `jam_exit=0` (Users, Login, image).

VBox 22:44 Europe/Amsterdam (guest 9:44 PM, UTC): as `user`, the card and Change password opened. The card background was white (`B_DOCUMENT_BACKGROUND_COLOR`) and the labels did not share one column.

The card now uses the normal panel color. Name, username, email, and account sit on one grid. Choose picture and Remove picture share one width under the avatar. Change password and Save sit on the same row. The people row shows the name and the account on two lines. Same build number (10011).

Jam AMD64 2026-10-29 22:51 Europe/Amsterdam: `jam_exit=0` (Users, image).

VBox 22:56 Europe/Amsterdam: the panel color is right, and the people list (name, then account) looks right. As `nicky` the window title was "Your account" but the people list was still there, and the lock blocked editing that account's own name, email, picture, and password. As `user`, Name and Email grew into tall boxes; the label sat in the middle of the row and the text sat at the top. `IsHidden()` before the window is shown follows the window, so the list was never actually hidden. A grid row beside the avatar took the spare height.

The people column is now a layout item, hidden after the window is shown, unless this account may manage people. The signed-in person's card stays editable while Locked. Name and Email stay one line, labels share the right edge, and the first row starts level with the top of the list. Rules for the next window are in `docs/STYLE.md`.

Jam AMD64 2026-10-29 23:09 Europe/Amsterdam: `jam_exit=0` (Users, image). Not yet seen in VBox.

VBox 23:12 Europe/Amsterdam: as `nicky`, Your account no longer showed the people list. Change password closed as if it had worked, and the password stayed the old one. Login's message switch never included profile get/set (`r5ah`, `r5af`), so the request was dropped. The reply had no `error` field and Users treated that as success. The same hole covered Save of name, email, and picture. Login now handles those messages, and a reply without `error` is a failure.

Jam AMD64 2026-10-29 23:19 Europe/Amsterdam: `jam_exit=0` (Login, Users, image). Not yet seen in VBox.

VBox 23:25 Europe/Amsterdam (guest 9:58 PM, UTC): as `user`, Users & Accounts showed the people list, a one-line Name and Email, and `nicky` as a standard account. As `nicky`, the own card worked, including a password change. Add account created the person and then reported "Sign in did not answer." Add, Disable, and Delete returned no `error` field on success, and the stricter reply check treated that as failure.

Success on those three requests now includes `error` 0. Login relinked 2026-10-29 23:28 Europe/Amsterdam. The image was not rebuilt, so the running VBox still shows the false alert.

### Log out name and Sign in after Restart (build 10010)

The Deskbar leaf item is `Log out <name>` for the signed-in account.

After the first Sign in, uid 0 is locked `root` with home `/boot/system`. `autologin` still `StartSession`s that uid-0 name (Login must stay uid 0). `/boot/system` is packagefs, so the user `launch_daemon` could not take the screen and Restart/Reset stayed on the boot splash. The session daemon now uses `/boot/system/var/root` when uid 0 has no writable home. Still not `StartSession` of the signed-in person.

Jam AMD64 2026-10-29 22:20 Europe/Amsterdam: `jam_exit=0` (launch_daemon, Deskbar, Login, image).

VBox 22:26 Europe/Amsterdam: after the first Sign in, Restart reached Sign in, not the splash. Log out, then Sign in as someone else. The preference lock was closed again after Log out.

### Locked machine settings (build 10009)

Network, Screen, Time, and Users & Accounts stay visible. An administrator (group `admin`, or the `/boot/home` account) changes them immediately. A standard account sees the same window with a lock. Unlock sends that administrator's name and password to Login, which is still uid 0. The preference app itself stays the signed-in user. The grant lasts 5 minutes and is dropped on Log out. It applies to that preference app only.

Time's clock and timezone go through Login, because the kernel allows those calls only for uid 0. Screen mode is refused in app_server unless the client is an administrator or holds a grant (`/boot/system/settings/r5-machine-unlock`). Network settings files are group `admin` mode `0664`. During a standard account's grant the directory is world-writable, because that account is not in `admin` and only one person is at the screen. Personal preflets are not locked.

Jam AMD64 2026-10-29 21:44 Europe/Amsterdam: `jam_exit=0` (Login, Users, Time, Screen, Network, app_server, image).

VBox `-ForceConvert` 21:45 Europe/Amsterdam: the VM powered on. Fresh disk, so the first desktop waits on `package_daemon`.

VBox 21:49 Europe/Amsterdam (guest clock 8:49 PM, UTC): Network showed Locked and Unlock at the top left. Add, Apply, and the other controls stayed active. The lock now sits on the bottom right of Network, Screen, Time, and Users. While it is locked, buttons, checkboxes, menus, and text fields in that window are disabled. Scrolling still works.

Jam AMD64 2026-10-29 22:02 Europe/Amsterdam: `jam_exit=0` (Users, Time, Screen, Network, image).

VBox 22:04 Europe/Amsterdam (guest clock 9:04 PM, UTC): signed in as standard account `nick` (uid 1002, `/boot/home/users/nick`). Network: lock at the bottom right, Revert / Disable / Renegotiate gray. Users: after Unlock, “Unlocked for a few minutes” and Lock at the bottom right, Add on. Network stayed Locked while Users was unlocked — the grant is per preference window.

VBox 22:06 Europe/Amsterdam (guest clock 9:06 PM, UTC): Screen and Time, still as `nick`, Locked at the bottom right. Screen: Revert and Apply gray, workspace +/− gray. Time: Revert gray. The date and analog clock still update.

### Sign in as that person (build 10008)

The session file is the account whose password just worked, not always the first administrator. `r5-desktop` already drops to that name before Tracker and Deskbar. A standard account’s home is `/boot/home/users/<name>` (`create_home` on sign-in). `user` still gets `/boot/home`. Log out deletes the session file.

This does **not** call `BLaunchRoster::StartSession`. That spawns another `launch_daemon <name>`, which reads `data/launch/user` and starts a second `app_server`. Removing autologin instead left the boot splash. Autologin stays. Desktop jobs stay off `initial_volumes_mounted`. One shared screen.

Trash is `$h/trash` for the signed-in account. Preferences are not hidden by role. Not a second graphical session.

Jam AMD64 2026-10-29 19:53 Europe/Amsterdam: `jam_exit=0` (Login, `r5-desktop`, What’s new, image). VBox `-ForceConvert` started.

VBox 19:55 Europe/Amsterdam (guest clock 6:55 PM, UTC): Sign in as `nicky` reached a blue desktop. Terminal `id` was `uid=1002(nicky) gid=100(users) groups=100(users)`. `ls /boot/home/Desktop` was permission denied. `ls` in `nicky`’s folder showed Desktop, Documents, and the rest. `echo $HOME` was still `/boot/home`, and the shell reported `sh: /boot/home/config/settings/profile: Permission denied`. The desktop showed the volume icon only: no Trash, Home, Quick Tour, or User Guide. Sign in as `user` afterwards: `uid=1001(user)`, `$HOME` `/boot/home`, `ls /boot/home/users/nicky` permission denied. A Deskbar crash dialog sat over Sign in (`/system/Deskbar`).

`/bin/sh` sources `SetupEnvironment`, which forced `HOME=/boot/home` for every account. Trash is `find_directory(B_TRASH_DIRECTORY)`, which was `/boot/trash` mode `0700` for the administrator, so Tracker drew no Trash pose. Home, Quick Tour, and User Guide are symlinks that the image only puts in `/boot/home/Desktop`. Those three are now created in each account’s Desktop. Trash is `$h/trash` (the old `/boot/trash` moves to `/boot/home/trash` once). Deskbar no longer calls through a missing settings file on quit.

Jam AMD64 2026-10-29 20:14 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 20:16 Europe/Amsterdam (guest clock 7:16 PM, UTC): Sign in as `nicky`. Terminal had no profile error. `echo $HOME` was `/boot/home/users/nicky`. `ls /boot/home/Desktop` was permission denied. Tracker titled `nicky` listed Desktop, Documents, Downloads, Music, Pictures, Videos, and config. The desktop showed R5OS, Home, Quick Tour, User Guide, and Trash. Deskbar listed Tracker and Terminal.

VBox 20:17 Europe/Amsterdam (guest clock 7:17 PM, UTC): Sign in as `user`. `id` was `uid=1001(user) gid=100(users) groups=100(users),103(admin)`. `ls /boot/home/users/nicky` was permission denied. That desktop also showed Home, Quick Tour, User Guide, and Trash. No Deskbar crash dialog on this pass.

Deskbar leaf-menu folders were yellow for `nicky` and blue-grey for `user`. Tracker reads `~/config/settings/mime_db` first. `user` had that database because registrar (uid 0) writes into `/boot/home`. `nicky` had none and used the system yellow folder icon. Sign in now copies `/boot/system/data/mime_db` into that account’s settings if missing, and puts the system directory icon on `application/x-vnd.be-directory` so every account gets the yellow folders. Registrar follows the session file instead of always `/boot/home`.

Jam AMD64 2026-10-29 20:45 Europe/Amsterdam: `jam_exit=0`. VBox `-ForceConvert` started.

VBox 20:47 Europe/Amsterdam (guest clock 7:47 PM, UTC): `nicky`’s leaf menu (Applications, Demos, Desktop applets, Preferences) was the yellow folder. `user`’s same four items were the blue-grey folder. `nicky`’s `config/settings/mime_db` was present (7:48 PM). Copying the directory type was not enough. Those menu rows are merged folders, type `application/x-vnd.haiku-virtual-directory`, whose icon is blue. A standard account often cannot create `/tmp/tracker/virtual-directories` (mode 0755, owned by whoever opened the menu first), so Tracker keeps the real system folder and its yellow icon. The administrator can, and then sees the blue virtual-directory icon. That type now uses the same yellow icon as `application/x-vnd.be-directory`. Sign-in copies both types into the account database again.

Jam AMD64 2026-10-29 21:13 Europe/Amsterdam: `jam_exit=0` (8116 targets). Optional translator headers were missing again; the image still built. VBox `-ForceConvert` started 21:16 Europe/Amsterdam.

VBox 21:16 Europe/Amsterdam (guest clock 8:16 PM, UTC): the leaf menu showed Applications, Demos, Desktop applets, and Preferences with the yellow folder. This was the fresh disk, so that desktop is the first account (`user`). `nicky` is not on a fresh image until Users & Accounts creates the account again.

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
