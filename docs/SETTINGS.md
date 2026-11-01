# Settings

One Settings window for R5OS. Existing preference programs stay separate and keep their own files.

## Host

`Settings` is a single-launch preference (`application/x-vnd.R5OS-Settings`). The window is a sidebar, a search field, and one content deck (`BCardLayout`). Changing page does not create a new window.

The sidebar and the home cards are filled from the module list. Category names for `personalization`, `system`, `devices`, `network`, and `accounts` live in one table in `SettingsWindow.cpp`. A module with another category id is still shown. A category page uses the same cards as Home and does not repeat the category summary under the title. Home sits with Back, Revert, and Save.

Settings loads Manrope Regular and Bold from `/boot/system/data/settings/fonts` for its own views. If a file is missing, that face stays the system font. The desktop font is not changed. The page title is the bold face, eight points above the bold system size.

Search compares the module name, description, keywords, and category label.

Back walks the pages already opened. A page with unsaved edits asks before it is left.

Page changes are immediate. There is no compositor, so the window does not animate a transition (`docs/LOOK.md`).

## Modules

The contract is `distro/settings/SettingsModule.h`, version 1. `distro/settings/ModuleAPI.md` is the developer note.

`SettingsManager` loads add-ons from the `Settings` directories under the system and user add-ons paths, including `non-packaged`. It checks `settings_api_version()` before `instantiate_settings_module()`. A wrong version is not loaded. Images stay loaded until the modules are deleted, then `unload_add_on` runs.

Appearance (`settings_appearance`) is the first native module. It stores `large_sample` in `~/config/settings/r5-settings/appearance`. It does not write the Haiku Appearance file. Colors and fonts open `application/x-vnd.Haiku-Appearance`.

## Legacy programs

`LegacyPages.cpp` is a table of programs that already exist: Appearance, Deskbar, Backgrounds, Screen, Sounds, Keymap, Input, Bluetooth, Printers, Network, and Users & Accounts. Each row is a page with an Open button. `BRoster::Launch` starts that program. Nothing is embedded, because those programs are windows, not views we can reuse.

Adding a row there is how an old preference shows up before it is rewritten. A new native page does not need a row and does not need a change to the window.

## Storage and the administrator lock

New R5OS values are `BMessage` files under `~/config/settings/r5-settings/<module id>`. The id is checked so one module cannot name another's file. Haiku files such as `Appearance`, `Keyboard`, and `Network` are not moved.

`SettingsHost::AdminChangeAllowed()` calls `r5_admin_change_allowed()` from Users & Accounts. The window refuses Save when the module says `RequiresAdmin()` and that call is false. The module repeats the check in `SaveSettings()` before a privileged write.

## Not in this foundation

Power, storage, sharing, sign-in options, permissions, installed applications, and default applications have no page yet. Wi-Fi stays inside the existing Network preferences. Catalogs are not wired, so the strings are English.
