# Settings

One Settings window for R5OS. Existing preference programs stay separate and keep their own files.

## Host

`Settings` is a single-launch preference (`application/x-vnd.R5OS-Settings`). The window is a sidebar, a search field, and one content deck (`BCardLayout`). Changing page does not create a new window.

This frame is fixed (`AGENTS.md`). The list starts under the tab and runs down to Search. The title and the subtitle sit beside the list, and that page's icon sits at the right of the title block. The page content is under the subtitle. Search stays under the list. The bottom row is Back, Home, Revert, and Save. Back and Home call `r5_button_set_icon()` (`R5_ICON_BACK`, `R5_ICON_HOME`) so they match the height of Revert and Save. Those two icons do not get the preferences wrench. The window opens at 1100 by 610. The list scrollbar is there only when the items are taller than the list. A category card lights up under the pointer and looks pressed while the mouse button is held down.

A new page adds its icon signature in `module_icon()` in `SettingsWindow.cpp`. Home uses the icon of the Preferences folder. Information uses the About icon. Every one of those page icons, and the icons on the home cards, has the Haiku preferences wrench drawn on top. The wrench is the unmodified Haiku file `overlay-prefs.hvif`. It belongs to this Settings application and its modules. About R5 is not a settings module and does not get the wrench.

Information is a page under System. It shows the computer and the R5OS version in two boxes. The labels are left-aligned. Copy sits under the rows of its box. There is no device id to show. Storage is the boot volume. The build date is the Europe/Amsterdam time of the image jam.

The sidebar and the home cards are filled from the module list. Category names for `personalization`, `system`, `devices`, `network`, and `accounts` live in one table in `SettingsWindow.cpp`. A module with another category id is still shown. A category page uses the same cards as Home and does not repeat the category summary under the title. Back and Home sit with Revert and Save.

Manrope Regular and Bold are the default plain and bold fonts when no font settings are saved. The fixed font stays Noto Sans Mono. The files are installed in `data/fonts/ttfonts`. Settings still selects Manrope for its own window, and falls back to the system font if that family is missing. The page title is the bold face, eight points above the bold system size.

Wallpaper is the Backgrounds window, rewritten as this page. It writes `be:bgndimginfo` on the Desktop folder, or on a folder, then asks Tracker to restore the picture. Save can keep the other workspaces, replace every workspace, or change a folder. The desktop color is `BScreen::SetDesktopColor`. The separate Backgrounds program stays on the disk and is not a page in this window.

Search compares the module name, description, keywords, and category label.

Back walks the pages already opened. A page with unsaved edits asks before it is left.

Page changes are immediate. There is no compositor, so the window does not animate a transition (`docs/LOOK.md`).

## Modules

The contract is `distro/settings/SettingsModule.h`, version 1. `distro/settings/ModuleAPI.md` is the developer note.

`SettingsManager` loads add-ons from the `Settings` directories under the system and user add-ons paths, including `non-packaged`. It checks `settings_api_version()` before `instantiate_settings_module()`. A wrong version is not loaded. Images stay loaded until the modules are deleted, then `unload_add_on` runs.

Appearance (`settings_appearance`) is the first native module. It stores `large_sample` in `~/config/settings/r5-settings/appearance`. It does not write the Haiku Appearance file. Colors and fonts open `application/x-vnd.Haiku-Appearance`.

## Legacy programs

`LegacyPages.cpp` is a table of programs that already exist: Appearance, Deskbar, Screen, Sounds, Keymap, Input, Bluetooth, Printers, Network, and Users & Accounts. Each row is a page with an Open button. `BRoster::Launch` starts that program. Nothing is embedded, because those programs are windows, not views we can reuse. Backgrounds stays installed and is not in this table.

Adding a row there is how an old preference shows up before it is rewritten. A new native page does not need a row and does not need a change to the window.

## Storage and the administrator lock

New R5OS values are `BMessage` files under `~/config/settings/r5-settings/<module id>`. The id is checked so one module cannot name another's file. Haiku files such as `Appearance`, `Keyboard`, and `Network` are not moved.

`SettingsHost::AdminChangeAllowed()` calls `r5_admin_change_allowed()` from Users & Accounts. The window refuses Save when the module says `RequiresAdmin()` and that call is false. The module repeats the check in `SaveSettings()` before a privileged write.

## Not in this foundation

Power, storage, sharing, sign-in options, permissions, installed applications, and default applications have no page yet. Wi-Fi stays inside the existing Network preferences. Catalogs are not wired, so the strings are English.
